#include "light_admin_dial.h"
#include "light_config_dial.h"
#include "light_client_dial.h"
#include "admin_server_dial.h"
#include "admin_auth_dial.h"
#include "cJSON.h"
#include "valve_logic.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static atomic_flag admission=ATOMIC_FLAG_INIT;
static esp_err_t reply(httpd_req_t *r,int code,const char *body) {
    const char *status=code==200?"200 OK":code==409?"409 Conflict":code==422?"422 Unprocessable Entity":code==401?"401 Unauthorized":code==403?"403 Forbidden":"503 Service Unavailable";
    httpd_resp_set_status(r,status);
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_sendstr(r,body);
}
static bool body_read(httpd_req_t *req,char *out,size_t cap) {
    char content[64];
    if (req->content_len==0||req->content_len>=cap||httpd_req_get_hdr_value_str(req,"Content-Type",content,sizeof content)!=ESP_OK||(strcmp(content,"application/json")&&strcmp(content,"application/json; charset=utf-8")))return false;
    size_t used=0;
    while (used<req->content_len) {
        int n=httpd_req_recv(req,out+used,req->content_len-used);
        if (n<=0)return false;
        used+=n;
    }
    out[used]=0;
    return !memchr(out,0,used)&&valve_json_object_valid(out,used)&&!strstr(out,"\\u0000");
}
static bool config_write(const char *body,light_config_t *c) {
    cJSON *j=cJSON_Parse(body);
    bool ok=cJSON_IsObject(j)&&cJSON_GetArraySize(j)==3;
    const char *keys[]= {
        "url","display_token","settings_token"
    };
    char *dest[]= {
        c->url,c->display,c->settings
    };
    size_t caps[]= {
        sizeof c->url,sizeof c->display,sizeof c->settings
    };
    unsigned seen=0;
    for (cJSON *v=j?j->child:NULL;v;v=v->next) {
        unsigned k=0;
        for (;k<3;k++)if (!strcmp(v->string,keys[k]))break;
        if (k==3||(seen&(1u<<k))) {
            ok=false;
            break;
        }
        seen|=1u<<k;
    }
    for (unsigned i=0;ok&&i<3;i++) {
        cJSON *v=cJSON_GetObjectItemCaseSensitive(j,keys[i]);
        ok=cJSON_IsString(v)&&strlen(v->valuestring)<caps[i];
        if (ok)strcpy(dest[i],v->valuestring);
    }
    cJSON_Delete(j);
    return ok&&seen==7&&light_config_valid(c);
}
typedef struct {
    httpd_req_t *req;
    char session[65],kind[16],body[512];
    unsigned offset;
    SemaphoreHandle_t done;
    atomic_bool cancelled;
    atomic_uint refs;
    light_reply_t result;
} admin_job_t;
static void release(admin_job_t *j) {
    if (atomic_fetch_sub(&j->refs,1)==1) {
        vSemaphoreDelete(j->done);
        memset(j,0,sizeof *j);
        free(j);
    }
}
static bool guard(void *ctx) {
    admin_job_t *j=ctx;
    return !atomic_load(&j->cancelled)&&admin_auth_session_valid(j->session,false);
}
static void complete(const light_reply_t *r,void *ctx) {
    admin_job_t *j=ctx;
    if (r)j->result=*r;
    else j->result.http_status=503;
    xSemaphoreGive(j->done);
    release(j);
}
static void async_worker(void *ctx) {
    admin_job_t *j=ctx;
    bool submitted=light_client_request_guarded(j->kind,j->body[0]?j->body:NULL,j->offset,complete,j,guard);
    if (!submitted)release(j);
    bool done=submitted&&xSemaphoreTake(j->done,pdMS_TO_TICKS(6000))==pdTRUE;
    if (!done)atomic_store(&j->cancelled,true);
    if (admin_http_authorize(j->req,false)) {
        int code=done?j->result.http_status:503;
        reply(j->req,code,code==200?j->result.body:"{\"error\":\"unconfirmed\"}");
    }
    httpd_req_async_handler_complete(j->req);
    release(j);
    atomic_flag_clear(&admission);
    vTaskDelete(NULL);
}
esp_err_t light_admin_handler(httpd_req_t *req) {
    bool write=req->method!=HTTP_GET;
    if (!admin_http_authorize(req,write))return ESP_OK;
    const char *kind=req->uri+strlen("/admin/api/light/");
    char body[512]= {
        0
    };
    if (!strncmp(kind,"config",6)) {
        light_config_t *c=calloc(1,sizeof *c);
        if (!c)return reply(req,503,"{\"error\":\"unavailable\"}");
        bool ok=true;
        if (write) {
            char *private_body=calloc(1,4097);
            ok=private_body&&body_read(req,private_body,4097)&&config_write(private_body,c)&&light_config_save(c);
            if (private_body) {
                memset(private_body,0,4097);
                free(private_body);
            }
        }
        else (void)light_config_load(c);
        /* No token, token prefix, or hash is ever included in the response. */
        cJSON *view=cJSON_CreateObject();
        cJSON_AddStringToObject(view,"url",c->url);
        cJSON_AddBoolToObject(view,"configured",light_config_valid(c));
        cJSON_AddNumberToObject(view,"generation",light_config_generation());
        char *text=cJSON_PrintUnformatted(view);
        cJSON_Delete(view);
        memset(c,0,sizeof *c);
        free(c);
        esp_err_t e=reply(req,ok?200:422,text?text:"{}");
        free(text);
        return e;
    }
    if (write&&(!body_read(req,body,512)||!light_write_valid(kind,body,strlen(body))))return reply(req,422,"{\"error\":\"invalid\"}");
    unsigned offset=0;
    if (!strncmp(kind,"history",7)) {
        kind="history";
        const char *query=strchr(req->uri,'?');
        if (query) {
            if (strncmp(query,"?offset=",8))return reply(req,422,"{}");
            const char *p=query+8;
            if (!*p)return reply(req,422,"{}");
            while (*p) {
                if (*p<'0'||*p>'9')return reply(req,422,"{}");
                offset=offset*10+(*p++-'0');
                if (offset>1000000)return reply(req,422,"{}");
            }
        }
    }
    if (atomic_flag_test_and_set(&admission))return reply(req,503,"{\"error\":\"busy\"}");
    admin_job_t *j=calloc(1,sizeof *j);
    if (!j) {
        atomic_flag_clear(&admission);
        return reply(req,503,"{}");
    }
    char cookie[512];
    bool cookie_ok=httpd_req_get_hdr_value_str(req,"Cookie",cookie,sizeof cookie)==ESP_OK;
    char *start=cookie_ok?strstr(cookie,"dial_session="):NULL;
    if (!start) {
        free(j);
        atomic_flag_clear(&admission);
        return reply(req,401,"{}");
    }
    memcpy(j->session,start+13,64);
    j->session[64]=0;
    j->offset=offset;
    strncpy(j->kind,kind,sizeof j->kind-1);
    strcpy(j->body,body);
    j->done=xSemaphoreCreateBinary();
    atomic_init(&j->refs,2);
    if (!j->done) {
        free(j);
        atomic_flag_clear(&admission);
        return reply(req,503,"{}");
    }
    if (httpd_req_async_handler_begin(req,&j->req)!=ESP_OK) {
        vSemaphoreDelete(j->done);
        free(j);
        atomic_flag_clear(&admission);
        return reply(req,503,"{}");
    }
    if (xTaskCreate(async_worker,"light_admin",4096,j,4,NULL)!=pdPASS) {
        reply(j->req,503,"{}");
        httpd_req_async_handler_complete(j->req);
        vSemaphoreDelete(j->done);
        free(j);
        atomic_flag_clear(&admission);
    }
    return ESP_OK;
}

#include "light_admin_dial.h"
#include "light_config_dial.h"
#include "light_client_dial.h"
#include "admin_server_dial.h"
#include "admin_auth_dial.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "valve_logic.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
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
    unsigned offset,generation;
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
    return !atomic_load(&j->cancelled)&&j->generation==light_config_generation()&&admin_auth_session_valid(j->session,false);
}
static void complete(const light_reply_t *r,void *ctx) {
    admin_job_t *j=ctx;
    if (r)j->result=*r;
    else j->result.http_status=503;
    xSemaphoreGive(j->done);
    release(j);
}
static void process_job(void *ctx) {
    admin_job_t *j=ctx;
    bool submitted=light_client_request_guarded_for_config(j->kind,j->body[0]?j->body:NULL,j->offset,j->generation,complete,j,guard);
    if (!submitted)release(j);
    bool done=submitted&&xSemaphoreTake(j->done,pdMS_TO_TICKS(6000))==pdTRUE;
    if (!done)atomic_store(&j->cancelled,true);
    if (admin_http_authorize(j->req,false)) {
        int code=j->generation!=light_config_generation()?409:done?j->result.http_status:503;
        /* Upstream role credentials are separate from the local PIN session. */
        if(code!=200&&code!=409&&code!=422)code=503;
        reply(j->req,code,code==200?j->result.body:"{\"error\":\"unconfirmed\"}");
    }
    httpd_req_async_handler_complete(j->req);
    release(j);
    atomic_flag_clear(&admission);
}
/* Reserve the single admin worker at link time. BLE/Wi-Fi heap pressure must
 * not require a fresh internal-RAM stack for every authenticated request. */
static DRAM_ATTR StackType_t worker_stack[4096 / sizeof(StackType_t)] __attribute__((aligned(portBYTE_ALIGNMENT)));
static DRAM_ATTR StaticTask_t worker_tcb;
static DRAM_ATTR StaticQueue_t worker_queue_control;
static DRAM_ATTR uint8_t worker_queue_storage[sizeof(admin_job_t *)];
static QueueHandle_t worker_queue;
static TaskHandle_t worker_task;
static void worker(void *unused) {
    (void)unused;
    admin_job_t *job;
    for (;;)if(xQueueReceive(worker_queue,&job,portMAX_DELAY)==pdTRUE)process_job(job);
}
/* Caller holds admission, serializing initialization as well as requests. */
static bool worker_start(void) {
    if(worker_task)return true;
    if(!worker_queue)worker_queue=xQueueCreateStatic(1,sizeof(admin_job_t *),worker_queue_storage,&worker_queue_control);
    if(!worker_queue)return false;
    worker_task=xTaskCreateStatic(worker,"light_admin",sizeof worker_stack,NULL,4,worker_stack,&worker_tcb);
    return worker_task!=NULL;
}
/* Local header binds the original browser observation to one credential/host
 * identity. It is never forwarded to the strict upstream JSON API. */
static bool generation_read(httpd_req_t *req,unsigned *out) {
    char text[16];
    if(httpd_req_get_hdr_value_str(req,"X-Light-Config-Generation",text,sizeof text)!=ESP_OK||!text[0])return false;
    unsigned value=0;
    for(const char *p=text;*p;p++) {
        if(*p<'0'||*p>'9'||value>(UINT32_MAX-(unsigned)(*p-'0'))/10)return false;
        value=value*10+(unsigned)(*p-'0');
    }
    *out=value;
    return true;
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
    unsigned intent_generation;
    if(!generation_read(req,&intent_generation))return reply(req,422,"{\"error\":\"binding_required\"}");
    if((intent_generation&1)||intent_generation!=light_config_generation())return reply(req,409,"{\"error\":\"configuration_changed\"}");
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
    if(!worker_start()) {
        atomic_flag_clear(&admission);
        return reply(req,503,"{}");
    }
    /* The response buffer exceeds 8KB. Keep it out of the internal heap
     * needed for the short-lived admin task stack and Bluetooth. */
    admin_job_t *j=heap_caps_calloc(1,sizeof *j,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
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
    j->generation=intent_generation;
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
    if (xQueueSend(worker_queue,&j,0)!=pdTRUE) {
        reply(j->req,503,"{}");
        httpd_req_async_handler_complete(j->req);
        vSemaphoreDelete(j->done);
        free(j);
        atomic_flag_clear(&admission);
    }
    return ESP_OK;
}

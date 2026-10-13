#define _POSIX_C_SOURCE 200809L
#include "light_client_dial.h"
#include "light_config_dial.h"
#include "valve_logic.h"
#include "cJSON.h"
#define JSMN_STATIC
#include "third_party/jsmn.h"
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#if !defined(LIGHT_CLIENT_HOST_TEST) || defined(LIGHT_CLIENT_HTTP_HOST_TEST)
#include "esp_http_client.h"
#endif
#ifndef LIGHT_CLIENT_HOST_TEST
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#endif
static bool id_valid(const char *p) {
    if (!p||!*p||strlen(p)>128)return false;
    for (;*p;p++)if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("_.:-",*p)))return false;
    return true;
}
/* Retain integer spelling; reject coercion, duplicate/unknown keys, encoded
* names and nested scalar DTOs before cJSON converts numbers to doubles. */
static bool encoded_nul(const char *text,size_t n) {
    for (size_t i=0;i+6<=n;i++)if (!memcmp(text+i,"\\u0000",6))return true;
    return false;
}
static cJSON *flat(const char *text,size_t n,const char **keys,unsigned count,unsigned integers) {
    if (!text||!n||n>4096||memchr(text,0,n)||!valve_json_object_valid(text,n)||encoded_nul(text,n))return NULL;
    jsmntok_t t[32];
    jsmn_parser p;
    jsmn_init(&p);
    int total=jsmn_parse(&p,text,n,t,32);
    if (total!=(int)(1+2*count)||t[0].type!=JSMN_OBJECT)return NULL;
    unsigned seen=0;
    for (int i=1;i<total;i+=2) {
        unsigned k=0;
        for (;k<count;k++)if ((size_t)(t[i].end-t[i].start)==strlen(keys[k])&&!memcmp(text+t[i].start,keys[k],strlen(keys[k])))break;
        if (k==count||(seen&(1u<<k))||t[i].type!=JSMN_STRING||t[i+1].type==JSMN_OBJECT||t[i+1].type==JSMN_ARRAY)return NULL;
        seen|=1u<<k;
        if (integers&(1u<<k)) {
            uint64_t v=0;
            if (t[i+1].type!=JSMN_PRIMITIVE)return NULL;
            for (int a=t[i+1].start;a<t[i+1].end;a++) {
                unsigned char c=text[a];
                if (c<'0'||c>'9'||v>(LIGHT_REVISION_MAX-(c-'0'))/10)return NULL;
                v=v*10+(c-'0');
            }
        }
    }
    return cJSON_ParseWithLengthOpts(text,n,NULL,false);
}
static bool text_field(cJSON *j,const char *key,char *out,size_t cap) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if (!cJSON_IsString(v)||strlen(v->valuestring)>=cap)return false;
    strcpy(out,v->valuestring);
    return true;
}
static bool utc_time(const char *v) {
    size_t n=strlen(v);
    if (n<20||n>40||v[4]!='-'||v[7]!='-'||v[10]!='T'||v[13]!=':'||v[16]!=':')return false;
    unsigned fields[6]= {
        0
    };
    const unsigned start[]= {
        0,5,8,11,14,17
    }
    ,length[]= {
        4,2,2,2,2,2
    };
    for (unsigned f=0;f<6;f++)for (unsigned i=0;i<length[f];i++) {
        char c=v[start[f]+i];
        if (c<'0'||c>'9')return false;
        fields[f]=fields[f]*10+(unsigned)(c-'0');
    }
    unsigned year=fields[0],month=fields[1],day=fields[2];
    static const unsigned days[]= {
        31,28,31,30,31,30,31,31,30,31,30,31
    };
    if (!year||!month||month>12||!day||fields[3]>23||fields[4]>59||fields[5]>59)return false;
    unsigned maximum=days[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));
    if (day>maximum)return false;
    const char *suffix=v+19;
    if (*suffix=='.') {
        suffix++;
        const char *begin=suffix;
        while (*suffix>='0'&&*suffix<='9')suffix++;
        if (suffix==begin)return false;
    }
    return !strcmp(suffix,"Z")||!strcmp(suffix,"+00:00");
}
bool light_status_parse(const char *text,size_t n,light_status_t *out) {
    const char *keys[]= {
        "capability","revision","mode","desired_on","gpio_on","health","reason","remaining_seconds","manual_remaining_seconds","observed_at"
    };
    cJSON *j=flat(text,n,keys,10,(1u<<1)|(1u<<7)|(1u<<8));
    if (!j||!out) {
        cJSON_Delete(j);
        return false;
    }
    light_status_t s= {
        0
    };
    char cap[32],mode[16];
    cJSON *desired=cJSON_GetObjectItemCaseSensitive(j,"desired_on"),*gpio=cJSON_GetObjectItemCaseSensitive(j,"gpio_on");
    double remaining=cJSON_GetObjectItemCaseSensitive(j,"remaining_seconds")->valuedouble,manual=cJSON_GetObjectItemCaseSensitive(j,"manual_remaining_seconds")->valuedouble;
    bool ok=text_field(j,"capability",cap,sizeof cap)&&!strcmp(cap,"independent_light_v1")&&text_field(j,"mode",mode,sizeof mode)&&(!strcmp(mode,"AUTO")||!strcmp(mode,"MANUAL_ON")||!strcmp(mode,"MANUAL_OFF"))&&cJSON_IsBool(desired)&&(cJSON_IsBool(gpio)||cJSON_IsNull(gpio))&&text_field(j,"health",s.health,sizeof s.health)&&(!strcmp(s.health,"OK")||!strcmp(s.health,"FAULT")||!strcmp(s.health,"UNAVAILABLE"))&&text_field(j,"reason",s.reason,sizeof s.reason)&&text_field(j,"observed_at",s.observed_at,sizeof s.observed_at)&&utc_time(s.observed_at)&&remaining<=7200&&manual<=7200;
    size_t time_len=strlen(s.observed_at);
    ok=ok&&(s.observed_at[time_len?time_len-1:0]=='Z'||(time_len>=6&&!strcmp(s.observed_at+time_len-6,"+00:00")));
    if (ok) {
        s.revision=(uint64_t)cJSON_GetObjectItemCaseSensitive(j,"revision")->valuedouble;
        s.mode=!strcmp(mode,"AUTO")?LIGHT_AUTO:!strcmp(mode,"MANUAL_ON")?LIGHT_MANUAL_ON:LIGHT_MANUAL_OFF;
        s.desired_on=cJSON_IsTrue(desired);
        s.gpio_on=cJSON_IsNull(gpio)?-1:cJSON_IsTrue(gpio);
        s.remaining_seconds=(unsigned)remaining;
        s.manual_remaining_seconds=(unsigned)manual;
        *out=s;
    }
    cJSON_Delete(j);
    return ok;
}
bool light_status_fresh(const light_status_t *s,uint64_t received,uint64_t now) {
    return s&&now>=received&&now-received<LIGHT_STALE_MS;
}
static const char *actions[]= {
    "timed_on","off","manual_on","manual_off","auto"
};
bool light_action_json(const char *id,uint64_t revision,light_action_t action,char *out,size_t cap) {
    if (!id_valid(id)||revision>LIGHT_REVISION_MAX||(unsigned)action>=5||!out)return false;
    int n=snprintf(out,cap,"{\"request_id\":\"%s\",\"expected_revision\":%llu,\"action\":\"%s\"}",id,(unsigned long long)revision,actions[action]);
    return n>0&&(size_t)n<cap;
}
bool light_settings_parse(const char *text,size_t n,uint64_t *revision,unsigned *hold,unsigned *manual) {
    const char *keys[]= {
        "revision","hold_seconds","manual_seconds"
    };
    cJSON *j=flat(text,n,keys,3,7);
    if (!j)return false;
    double h=cJSON_GetObjectItemCaseSensitive(j,"hold_seconds")->valuedouble,m=cJSON_GetObjectItemCaseSensitive(j,"manual_seconds")->valuedouble;
    bool ok=h>=60&&h<=7200&&m>=60&&m<=7200&&fmod(h,60)==0&&fmod(m,60)==0;
    if (ok) {
        *revision=(uint64_t)cJSON_GetObjectItemCaseSensitive(j,"revision")->valuedouble;
        *hold=h;
        *manual=m;
    }
    cJSON_Delete(j);
    return ok;
}
bool light_write_valid(const char *kind,const char *text,size_t n) {
    const char *ak[]= {
        "request_id","expected_revision","action"
    }
    ,*sk[]= {
        "request_id","expected_revision","hold_seconds","manual_seconds"
    };
    bool setting=!strcmp(kind,"settings");
    cJSON *j=flat(text,n,setting?sk:ak,setting?4:3,setting?14:2);
    if (!j)return false;
    cJSON *id=cJSON_GetObjectItemCaseSensitive(j,"request_id");
    bool ok=cJSON_IsString(id)&&id_valid(id->valuestring);
    if (setting) {
        double h=cJSON_GetObjectItemCaseSensitive(j,"hold_seconds")->valuedouble,m=cJSON_GetObjectItemCaseSensitive(j,"manual_seconds")->valuedouble;
        ok=ok&&h>=60&&h<=7200&&m>=60&&m<=7200&&fmod(h,60)==0&&fmod(m,60)==0;
    }
    else {
        cJSON *a=cJSON_GetObjectItemCaseSensitive(j,"action");
        bool match=false;
        for (unsigned i=0;i<5;i++)if (cJSON_IsString(a)&&!strcmp(a->valuestring,actions[i]))match=true;
        ok=ok&&match;
    }
    cJSON_Delete(j);
    return ok;
}
static bool unique(cJSON *obj) {
    if (!cJSON_IsObject(obj))return false;
    for (cJSON *a=obj->child;a;a=a->next)for (cJSON *b=a->next;b;b=b->next)if (!strcmp(a->string,b->string))return false;
    return true;
}
bool light_history_parse(const char *text,size_t n,unsigned offset) {
    if (n>LIGHT_BODY_MAX||memchr(text,0,n)||!valve_json_object_valid(text,n)||encoded_nul(text,n))return false;
    cJSON *j=cJSON_ParseWithLengthOpts(text,n,NULL,false);
    bool ok=unique(j)&&cJSON_GetArraySize(j)==3;
    cJSON *items=cJSON_GetObjectItemCaseSensitive(j,"items"),*limit=cJSON_GetObjectItemCaseSensitive(j,"limit"),*off=cJSON_GetObjectItemCaseSensitive(j,"offset");
    ok=ok&&cJSON_IsArray(items)&&cJSON_GetArraySize(items)<=20&&cJSON_IsNumber(limit)&&limit->valuedouble==20&&cJSON_IsNumber(off)&&off->valuedouble==offset;
    for (cJSON *v=items?items->child:NULL;ok&&v;v=v->next) {
        ok=unique(v)&&cJSON_GetArraySize(v)==6;
        const char *strings[]= {
            "at","source","mode","reason"
        };
        for (unsigned i=0;ok&&i<4;i++) {
            cJSON *f=cJSON_GetObjectItemCaseSensitive(v,strings[i]);
            ok=cJSON_IsString(f)&&strlen(f->valuestring)<=128;
        }
        cJSON *id=cJSON_GetObjectItemCaseSensitive(v,"id"),*d=cJSON_GetObjectItemCaseSensitive(v,"desired_on");
        ok=ok&&cJSON_IsNumber(id)&&id->valuedouble>=0&&id->valuedouble<=LIGHT_REVISION_MAX&&floor(id->valuedouble)==id->valuedouble&&cJSON_IsBool(d);
    }
    cJSON_Delete(j);
    return ok;
}
static atomic_uint session;
static atomic_bool connected;
static atomic_flag admission=ATOMIC_FLAG_INIT;
typedef struct {
    char kind[16],body[512];
    unsigned offset,session,generation;
    uint64_t at;
    light_callback_t callback;
    void *context;
    bool (*guard)(void *);
} job_t;
#ifdef LIGHT_CLIENT_HOST_TEST
static uint64_t host_now;
static light_transport_t host_transport;
static void *host_context;
static job_t pending;
static bool has_pending;
void light_client_test_clock(uint64_t n) {
    host_now=n;
}
uint64_t light_client_now(void) {
    return host_now;
}
void light_client_test_transport(light_transport_t t,void *c) {
    host_transport=t;
    host_context=c;
}
#else
static QueueHandle_t queue;
uint64_t light_client_now(void) {
    return (uint64_t)(esp_timer_get_time()/1000);
}
#endif
unsigned light_client_session(void) {
    return atomic_load(&session);
}
void light_client_link(bool c) {
    atomic_store(&connected,c);
    atomic_fetch_add(&session,1);
}
static bool current(const job_t *j) {
    uint64_t n=light_client_now();
    return (!j->guard||j->guard(j->context))&&
    atomic_load(&connected)&&
    j->session==light_client_session()&&
    j->generation==light_config_generation()&&
    !(j->generation&1)&&
    n>=j->at&&
    n-j->at<LIGHT_JOB_MS;
}
#if !defined(LIGHT_CLIENT_HOST_TEST) || defined(LIGHT_CLIENT_HTTP_HOST_TEST)
static bool budget(esp_http_client_handle_t h,const job_t *j) {
    if (!current(j))return false;
    uint64_t left=LIGHT_JOB_MS-(light_client_now()-j->at);
    return esp_http_client_set_timeout_ms(h,(int)(left<2000?left:2000))==ESP_OK;
}
static int network(const char *method,const char *url,const char *token,const char *body,char *out,size_t cap,size_t *length,const job_t *j) {
    esp_http_client_config_t cfg= {
        .url=url,.method=!strcmp(method,"POST")?HTTP_METHOD_POST:!strcmp(method,"PUT")?HTTP_METHOD_PUT:HTTP_METHOD_GET,.timeout_ms=2000,.disable_auto_redirect=true
    };
    esp_http_client_handle_t h=esp_http_client_init(&cfg);
    if (!h)return 503;
    int result=503;
    char auth[LIGHT_TOKEN_LEN+8];
    snprintf(auth,sizeof auth,"Bearer %s",token);
    size_t size=body?strlen(body):0;
    if (esp_http_client_set_header(h,"Authorization",auth)!=ESP_OK||esp_http_client_set_header(h,"Content-Type","application/json")!=ESP_OK||!budget(h,j)||esp_http_client_open(h,(int)size)!=ESP_OK)goto done;
    if (!budget(h,j)||(size&&esp_http_client_write(h,body,(int)size)!=(int)size)||!budget(h,j)||esp_http_client_fetch_headers(h)<0)goto done;
    result=esp_http_client_get_status_code(h);
    if (result!=200)goto done;
    size_t used=0;
    while (used<cap) {
        if (!budget(h,j)) {
            result=503;
            break;
        }
        int n=esp_http_client_read(h,out+used,(int)(cap-used));
        if (n<0) {
            result=503;
            break;
        }
        if (!n) {
            if (!esp_http_client_is_complete_data_received(h))result=503;
            else *length=used;
            break;
        }
        used+=(size_t)n;
        if (used==cap)result=503;
    }
    done:memset(auth,0,sizeof auth);
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return result;
}
#endif
static void perform(job_t *j) {
    light_reply_t *r=calloc(1,sizeof *r);
    if (!r) {
        if (j->callback)j->callback(NULL,j->context);
        atomic_flag_clear(&admission);
        return;
    }
    r->http_status=503;
    r->session=j->session;
    r->generation=j->generation;
    light_config_t cfg= {
        0
    };
    char url[LIGHT_URL_LEN+96];
    if (!current(j)||!light_config_load(&cfg)||!current(j))goto done;
    snprintf(url,sizeof url,"%s/api/v1/lights/%s%s",cfg.url,j->kind,!strcmp(j->kind,"history")?"?limit=20&offset=":"");
    if (!strcmp(j->kind,"history"))snprintf(url,sizeof url,"%s/api/v1/lights/history?limit=20&offset=%u",cfg.url,j->offset);
    const char *method=j->body[0]?(!strcmp(j->kind,"settings")?"PUT":"POST"):"GET";
    const char *token=(!strcmp(j->kind,"settings")||!strcmp(j->kind,"history"))?cfg.settings:cfg.display;
#if defined(LIGHT_CLIENT_HOST_TEST) && !defined(LIGHT_CLIENT_HTTP_HOST_TEST)
    if (host_transport)r->http_status=host_transport(method,url,token,j->body[0]?j->body:NULL,r->body,LIGHT_BODY_MAX,&r->length,host_context);
#else
    r->http_status=network(method,url,token,j->body[0]?j->body:NULL,r->body,LIGHT_BODY_MAX,&r->length,j);
#endif
    if (!current(j)||r->length>LIGHT_BODY_MAX) {
        r->http_status=503;
        r->length=0;
    }
    r->body[r->length]=0;
    if (r->http_status==200) {
        light_status_t s;
        uint64_t rev;
        unsigned h,m;
        bool valid=!strcmp(j->kind,"settings")?light_settings_parse(r->body,r->length,&rev,&h,&m):!strcmp(j->kind,"history")?light_history_parse(r->body,r->length,j->offset):light_status_parse(r->body,r->length,&s);
        if (!valid) {
            r->http_status=503;
            r->length=0;
        }
    }
    done:memset(&cfg,0,sizeof cfg);
    if (r->http_status!=200) {
        r->length=0;
        r->body[0]=0;
    }
    r->received_ms=light_client_now();
    if (j->callback)j->callback(r,j->context);
    free(r);
    atomic_flag_clear(&admission);
}
#ifdef LIGHT_CLIENT_HOST_TEST
bool light_client_start(void) {
    return true;
}
void light_client_test_run(void) {
    if (has_pending) {
        has_pending=false;
        perform(&pending);
    }
}
#else
static void worker(void *unused) {
    (void)unused;
    job_t j;
    for (;;)if (xQueueReceive(queue,&j,portMAX_DELAY)==pdTRUE)perform(&j);
}
bool light_client_start(void) {
    if (queue)return true;
    queue=xQueueCreate(1,sizeof(job_t));
    if (!queue)return false;
    if (xTaskCreate(worker,"light_http",8192,NULL,4,NULL)!=pdPASS) {
        vQueueDelete(queue);
        queue=NULL;
        return false;
    }
    return true;
}
#endif
static bool request(const char *kind,const char *body,unsigned offset,unsigned generation,light_callback_t cb,void *ctx,bool (*guard)(void *)) {
    if (!kind||(strcmp(kind,"status")&&strcmp(kind,"actions")&&strcmp(kind,"settings")&&strcmp(kind,"history"))||offset>1000000||generation!=light_config_generation()||(generation&1)||!atomic_load(&connected)||(body&&(!light_write_valid(kind,body,strlen(body))||strlen(body)>=512))||atomic_flag_test_and_set(&admission))return false;
    job_t j= {
        .offset=offset,.session=light_client_session(),.generation=generation,.at=light_client_now(),.callback=cb,.context=ctx,.guard=guard
    };
    strcpy(j.kind,kind);
    if (body)strcpy(j.body,body);
#ifdef LIGHT_CLIENT_HOST_TEST
    pending=j;
    has_pending=true;
    return true;
#else
    if (queue&&xQueueSend(queue,&j,0)==pdTRUE)return true;
    atomic_flag_clear(&admission);
    return false;
#endif
}
bool light_client_request(const char *kind,const char *body,unsigned offset,light_callback_t cb,void *ctx) {
    return request(kind,body,offset,light_config_generation(),cb,ctx,NULL);
}
bool light_client_action(light_action_t action,uint64_t revision,unsigned generation,light_callback_t cb,void *ctx) {
    char id[64],body[256];
    static atomic_uint serial;
#ifdef LIGHT_CLIENT_HOST_TEST
    unsigned random=123;
#else
    unsigned random=esp_random();
#endif
    snprintf(id,sizeof id,"dial:%08x:%u:%u",random,light_client_session(),atomic_fetch_add(&serial,1));
    if (!light_action_json(id,revision,action,body,sizeof body))return false;
    return request("actions",body,0,generation,cb,ctx,NULL);
}
bool light_client_request_guarded(const char *kind,const char *body,unsigned offset,light_callback_t cb,void *ctx,bool (*guard)(void *)) {
    return request(kind,body,offset,light_config_generation(),cb,ctx,guard);
}

bool light_client_request_guarded_for_config(const char *kind,const char *body,unsigned offset,unsigned generation,light_callback_t cb,void *ctx,bool (*guard)(void *)) {
    return request(kind,body,offset,generation,cb,ctx,guard);
}

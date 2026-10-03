#include "weather_admin_dial.h"
#include "valve_config_dial.h"
#include "valve_client_dial.h"
#include "valve_logic.h"
#include "cJSON.h"
#define JSMN_STATIC
#include "third_party/jsmn.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#ifndef WEATHER_ADMIN_HOST_TEST
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif
#define WEATHER_REQUEST_MS UINT64_C(5000)
typedef struct { uint64_t origin_ms,expires_ms; const atomic_bool *cancelled; } settings_deadline_t;
static uint64_t settings_now_ms(void) {
#ifdef WEATHER_ADMIN_HOST_TEST
    return 0; /* Synchronous transport unit tests have no queued deadline. */
#else
    return (uint64_t)(esp_timer_get_time()/1000);
#endif
}
static bool settings_expired(const settings_deadline_t *deadline) {
    if(!deadline)return false;
    uint64_t now=settings_now_ms();
    return (deadline->cancelled && atomic_load(deadline->cancelled)) ||
        now<deadline->origin_ms || now>=deadline->expires_ms;
}
static atomic_flag admission=ATOMIC_FLAG_INIT;
static atomic_uint mutation_epoch;
uint32_t weather_admin_generation(void) { return atomic_load(&mutation_epoch); }
static bool timezone_valid(const char *value) {
    size_t length=0;
    while (length<WEATHER_TIMEZONE_LEN && value[length]) {
        char c=value[length++];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-'||c=='+')) return false;
    }
    return length>0 && length<WEATHER_TIMEZONE_LEN;
}
static bool settings_valid(bool enabled,bool coordinates,double latitude,double longitude,const char *timezone) {
    return timezone_valid(timezone) && (!enabled || coordinates) &&
        (!coordinates || (isfinite(latitude)&&isfinite(longitude)&&latitude>=-90&&latitude<=90&&longitude>=-180&&longitude<=180));
}
/* Use the production JSON grammar validator before either parser; reject nested
 * DTO values before cJSON recursion, and retain integer spelling before conversion. */
static bool parse_settings(const char *text,size_t length,bool write,weather_settings_view_t *out) {
    if (!text || length==0 || length>WEATHER_BODY_MAX || memchr(text,0,length) || !valve_json_object_valid(text,length)) return false;
    jsmntok_t tokens[16]; jsmn_parser parser; jsmn_init(&parser);
    int count=jsmn_parse(&parser,text,length,tokens,16);
    if (count!=11 || tokens[0].type!=JSMN_OBJECT) return false;
    for (int i=2;i<count;i+=2) {
        if (tokens[i].type==JSMN_OBJECT || tokens[i].type==JSMN_ARRAY) return false;
        if (tokens[i].type==JSMN_STRING && memchr(text+tokens[i].start,'\\',(size_t)(tokens[i].end-tokens[i].start))) return false;
    }
    const char *revision_key=write?"expected_revision":"revision";
    const char *keys[]={revision_key,"enabled","latitude","longitude","timezone"};
    unsigned raw_seen=0;
    for(int i=1;i<count;i+=2) {
        if(tokens[i].type!=JSMN_STRING)return false;
        unsigned bit=0;
        for(unsigned key=0;key<5;++key) {
            size_t key_length=strlen(keys[key]);
            if((size_t)(tokens[i].end-tokens[i].start)==key_length &&
                !memcmp(text+tokens[i].start,keys[key],key_length))bit=1u<<key;
        }
        if(!bit || (raw_seen&bit))return false;
        raw_seen|=bit;
    }
    if(raw_seen!=31)return false;
    uint64_t revision=0; bool found=false;
    for (int i=1;i<count;i+=2) {
        if (tokens[i].type!=JSMN_STRING) return false;
        if ((size_t)(tokens[i].end-tokens[i].start)!=strlen(revision_key) ||
            memcmp(text+tokens[i].start,revision_key,strlen(revision_key))) continue;
        if (found || tokens[i+1].type!=JSMN_PRIMITIVE) return false;
        found=true;
        for(int n=tokens[i+1].start;n<tokens[i+1].end;++n) {
            unsigned char c=(unsigned char)text[n];
            if(c<'0'||c>'9'||revision>(WEATHER_REVISION_MAX-(c-'0'))/10) return false;
            revision=revision*10+(c-'0');
        }
    }
    if (!found) return false;
    cJSON *json=cJSON_ParseWithLengthOpts(text,length,NULL,false);
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); return false; }
    unsigned seen=0; bool ok=true;
    for (cJSON *item=json->child;item;item=item->next) {
        unsigned bit=!strcmp(item->string,revision_key)?1:!strcmp(item->string,"enabled")?2:
            !strcmp(item->string,"latitude")?4:!strcmp(item->string,"longitude")?8:!strcmp(item->string,"timezone")?16:0;
        if (!bit || (seen&bit)) ok=false;
        seen|=bit;
    }
    cJSON *enabled=cJSON_GetObjectItemCaseSensitive(json,"enabled");
    cJSON *latitude=cJSON_GetObjectItemCaseSensitive(json,"latitude");
    cJSON *longitude=cJSON_GetObjectItemCaseSensitive(json,"longitude");
    cJSON *timezone=cJSON_GetObjectItemCaseSensitive(json,"timezone");
    bool coordinates=cJSON_IsNumber(latitude)&&cJSON_IsNumber(longitude);
    ok=ok && seen==31 && cJSON_IsBool(enabled) && cJSON_IsString(timezone) &&
        (coordinates||(cJSON_IsNull(latitude)&&cJSON_IsNull(longitude)));
    if (ok) {
        memset(out,0,sizeof *out); out->revision=revision; out->enabled=cJSON_IsTrue(enabled);
        out->coordinates_available=coordinates;
        out->latitude=coordinates?latitude->valuedouble:0; out->longitude=coordinates?longitude->valuedouble:0;
        if(strlen(timezone->valuestring)>=sizeof out->timezone) ok=false;
        else strcpy(out->timezone,timezone->valuestring);
        ok=ok && settings_valid(out->enabled,coordinates,out->latitude,out->longitude,out->timezone);
    }
    cJSON_Delete(json); return ok;
}
bool weather_settings_write_parse(const char *json,size_t length,weather_settings_write_t *out) {
    weather_settings_view_t view;
    if (!out || !parse_settings(json,length,true,&view)) return false;
    *out=(weather_settings_write_t){.expected_revision=view.revision,.enabled=view.enabled,
        .coordinates_available=view.coordinates_available,.latitude=view.latitude,.longitude=view.longitude};
    strcpy(out->timezone,view.timezone); return true;
}
static bool settings_json(uint64_t revision,bool enabled,bool coordinates,double latitude,double longitude,
    const char *timezone,bool write,char *out,size_t capacity) {
    if (revision>WEATHER_REVISION_MAX || !settings_valid(enabled,coordinates,latitude,longitude,timezone)) return false;
    char lat[32]="null",lon[32]="null";
    if(coordinates) { snprintf(lat,sizeof lat,"%.17g",latitude); snprintf(lon,sizeof lon,"%.17g",longitude); }
    int n=snprintf(out,capacity,"{\"%s\":%llu,\"enabled\":%s,\"latitude\":%s,\"longitude\":%s,\"timezone\":\"%s\"}",
        write?"expected_revision":"revision",(unsigned long long)revision,enabled?"true":"false",lat,lon,timezone);
    return n>0 && (size_t)n<capacity;
}
bool weather_settings_view_json(const weather_settings_view_t *view,char *out,size_t capacity) {
    return view && out && settings_json(view->revision,view->enabled,view->coordinates_available,
        view->latitude,view->longitude,view->timezone,false,out,capacity);
}
#ifdef WEATHER_ADMIN_HOST_TEST
static weather_admin_transport_fn transport;
static void *transport_context;
void weather_admin_test_transport(weather_admin_transport_fn fn,void *context) { transport=fn;transport_context=context; }
#else
static bool settings_transport_budget(esp_http_client_handle_t client,const settings_deadline_t *deadline) {
    if(!deadline)return true;
    if(settings_expired(deadline))return false;
    uint64_t remaining=deadline->expires_ms-settings_now_ms();
    if(!remaining || remaining>WEATHER_REQUEST_MS)return false;
    return esp_http_client_set_timeout_ms(client,(int)(remaining<3000?remaining:3000))==ESP_OK;
}
static int transport(const char *method,const char *url,const char *header,const char *token,
    const char *body,size_t body_length,int *status,char *out,size_t capacity,size_t *length,void *context) {
    const settings_deadline_t *deadline=context;
    esp_http_client_config_t config={.url=url,.method=!strcmp(method,"PUT")?HTTP_METHOD_PUT:HTTP_METHOD_GET,.timeout_ms=3000,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client)return -1;
    int result=-1;
    if(esp_http_client_set_header(client,header,token)!=ESP_OK ||
       esp_http_client_set_header(client,"Accept","application/json")!=ESP_OK ||
       (body_length && esp_http_client_set_header(client,"Content-Type","application/json")!=ESP_OK))goto done;
    if(!settings_transport_budget(client,deadline)) { result=-3;goto done; }
    esp_err_t err=esp_http_client_open(client,(int)body_length);
    if(err!=ESP_OK) { result=err==ESP_ERR_TIMEOUT?-3:-1;goto done; }
    if(!settings_transport_budget(client,deadline)) { result=-3;goto done; }
    if(body_length && esp_http_client_write(client,body,(int)body_length)!=(int)body_length) { result=-3;goto done; }
    if(!settings_transport_budget(client,deadline)) { result=-3;goto done; }
    if(esp_http_client_fetch_headers(client)<0) { result=-3;goto done; }
    *status=esp_http_client_get_status_code(client);
    if(*status!=200) { result=0;goto done; }
    size_t used=0;
    while(used<capacity) {
        if(!settings_transport_budget(client,deadline)) { result=-3;goto done; }
        int n=esp_http_client_read(client,out+used,capacity-used);
        if(n<0) { result=-3;goto done; }
        if(n==0) { *length=used;result=0;goto done; }
        used+=(size_t)n;
    }
    result=-2;
done:
    esp_http_client_close(client);esp_http_client_cleanup(client);return result;
}
#endif

/* Publish an immutable settings read before optional status I/O. A stalled
 * observation must not turn a confirmed saved preference into a failed read. */
typedef struct {weather_settings_view_t value;unsigned generation,session;atomic_bool ready;} settings_snapshot_t;
static bool bounded_text(const cJSON *value,size_t maximum) {
    return cJSON_IsString(value)&&strlen(value->valuestring)<=maximum;
}
static bool observation_parse(const char *body,size_t length,char *out,size_t capacity) {
    valve_status_t status;
    if(!valve_status_parse(body,length,&status)||status.weather_assistance_version!=1||status.operation==VALVE_OPERATION_UNKNOWN)return false;
    /* Reject encoded NULs and duplicate names before selecting public fields. */
    for(size_t n=0;n+5<length;n++)if(!memcmp(body+n,"\\u0000",6))return false;
    cJSON *root=cJSON_ParseWithLengthOpts(body,length,NULL,false),*filtered=NULL;
    cJSON *weather=cJSON_GetObjectItemCaseSensitive(root,"weather");
    /* An array child has no object key. Reject the container before walking
     * named members; malformed optional data must not crash settings reads. */
    if(!cJSON_IsObject(weather)){cJSON_Delete(root);return false;}
    cJSON *operation=cJSON_GetObjectItemCaseSensitive(root,"operation");
    const char *names[]={"enabled","available","eligible","reason","dates","minima_c","last_successful_check"};
    bool ok=cJSON_IsObject(weather)&&bounded_text(operation,32);
    for(cJSON *item=root?root->child:NULL;item;item=item->next)
        for(cJSON *next=item->next;next;next=next->next)if(!strcmp(item->string,next->string))ok=false;
    unsigned seen=0;
    for(cJSON *item=weather?weather->child:NULL;item;item=item->next){
        if(!item->string){ok=false;break;}
        unsigned bit=0;for(unsigned i=0;i<7;i++)if(!strcmp(item->string,names[i]))bit=1u<<i;
        if(!bit||(seen&bit))ok=false;
        seen|=bit;
    }
    cJSON *enabled=cJSON_GetObjectItemCaseSensitive(weather,"enabled"),*available=cJSON_GetObjectItemCaseSensitive(weather,"available"),*eligible=cJSON_GetObjectItemCaseSensitive(weather,"eligible");
    cJSON *reason=cJSON_GetObjectItemCaseSensitive(weather,"reason"),*dates=cJSON_GetObjectItemCaseSensitive(weather,"dates"),*minima=cJSON_GetObjectItemCaseSensitive(weather,"minima_c"),*last=cJSON_GetObjectItemCaseSensitive(weather,"last_successful_check");
    int days=cJSON_GetArraySize(dates);
    ok=ok&&seen==127&&cJSON_IsBool(enabled)&&cJSON_IsBool(available)&&cJSON_IsBool(eligible)&&bounded_text(reason,96)&&cJSON_IsArray(dates)&&cJSON_IsArray(minima)&&(days==0||days==5)&&cJSON_GetArraySize(minima)==days&&(cJSON_IsNull(last)||bounded_text(last,40));
    if(cJSON_IsTrue(eligible))ok=ok&&cJSON_IsTrue(available)&&days==5&&cJSON_IsString(last);
    for(int i=0;ok&&i<days;i++){
        cJSON *date=cJSON_GetArrayItem(dates,i),*minimum=cJSON_GetArrayItem(minima,i);
        ok=bounded_text(date,10)&&strlen(date->valuestring)==10&&cJSON_IsNumber(minimum)&&isfinite(minimum->valuedouble)&&minimum->valuedouble>=-100&&minimum->valuedouble<=100;
    }
    if(ok){
        filtered=cJSON_CreateObject();ok=filtered&&cJSON_AddItemToObject(filtered,"operation",cJSON_Duplicate(operation,true));
        for(unsigned i=0;ok&&i<7;i++)ok=cJSON_AddItemToObject(filtered,names[i],cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(weather,names[i]),true));
        ok=ok&&cJSON_PrintPreallocated(filtered,out,(int)capacity,false);
    }
    cJSON_Delete(filtered);cJSON_Delete(root);return ok;
}
bool weather_admin_view_json(const weather_settings_view_t *view,char *out,size_t capacity) {
    char settings[384];if(!weather_settings_view_json(view,settings,sizeof settings))return false;
    size_t length=strlen(settings);settings[length-1]=0;
    int n=snprintf(out,capacity,"%s,\"observation\":%s}",settings,view->observation[0]?view->observation:"null");
    return n>0&&(size_t)n<capacity;
}
static void read_observation(weather_settings_view_t *out,const settings_deadline_t *deadline,unsigned generation,unsigned session) {
    char base[VALVE_CONFIG_URL_LEN]={0},token[VALVE_CONFIG_TOKEN_LEN]={0},url[VALVE_CONFIG_URL_LEN+64];
    char *response=NULL;size_t length=0;int status=0;
    if(settings_expired(deadline)||generation!=valve_config_generation()||!valve_config_load(base,sizeof base,token,sizeof token))goto done;
    response=malloc(WEATHER_BODY_MAX);if(!response)goto done;
    snprintf(url,sizeof url,"%s/api/v1/display/status",base);
    int rc=transport("GET",url,"X-Display-Token",token,NULL,0,&status,response,WEATHER_BODY_MAX,&length,
#ifdef WEATHER_ADMIN_HOST_TEST
        transport_context
#else
        (void *)deadline
#endif
    );
    if(rc||status!=200||length>WEATHER_BODY_MAX||settings_expired(deadline)||generation!=valve_config_generation()||session!=valve_client_current_session())goto done;
    if(!observation_parse(response,length,out->observation,sizeof out->observation))out->observation[0]=0;
done:
    memset(token,0,sizeof token);free(response);
}
static void perform(const weather_settings_write_t *write,weather_settings_view_t *out,const settings_deadline_t *deadline,settings_snapshot_t *snapshot) {
    memset(out,0,sizeof *out);out->result=WEATHER_ADMIN_UNAVAILABLE;
    if(settings_expired(deadline)) {out->result=WEATHER_ADMIN_TIMEOUT;return;}
    unsigned generation=valve_config_generation(),session=valve_client_current_session();
    char base[VALVE_CONFIG_URL_LEN]={0},token[VALVE_CONFIG_TOKEN_LEN]={0};
    char url[VALVE_CONFIG_URL_LEN+64],body[384],response[512];size_t length=0;int status=0;
    if ((generation&1u) || !valve_config_weather_load(base,sizeof base,token,sizeof token) ||
        generation!=valve_config_generation())goto done;
    if (write && !settings_json(write->expected_revision,write->enabled,write->coordinates_available,
        write->latitude,write->longitude,write->timezone,true,body,sizeof body)) { out->result=WEATHER_ADMIN_INVALID;goto done; }
    snprintf(url,sizeof url,"%s/api/v1/display/weather-settings",base);
#ifdef WEATHER_ADMIN_HOST_TEST
    if(!transport)goto done;
#endif
    if(write)atomic_fetch_add(&mutation_epoch,1);
    int rc=transport(write?"PUT":"GET",url,"X-Weather-Settings-Token",token,write?body:NULL,
        write?strlen(body):0,&status,response,sizeof response,&length,
#ifdef WEATHER_ADMIN_HOST_TEST
        transport_context
#else
        (void *)deadline
#endif
    );
    if(settings_expired(deadline)) {out->result=WEATHER_ADMIN_TIMEOUT;goto done;}
    if(generation!=valve_config_generation()||session!=valve_client_current_session())goto done;
    if(rc==-3) {out->result=WEATHER_ADMIN_TIMEOUT;goto done;}
    if(rc!=0 || length>sizeof response)goto done;
    if(status==409) {out->result=WEATHER_ADMIN_CONFLICT;goto done;}
    if(status==401) {out->result=WEATHER_ADMIN_UNAUTHORIZED;goto done;}
    if(status==422 || status==413) {out->result=WEATHER_ADMIN_INVALID;goto done;}
    if(status!=200)goto done;
    weather_settings_view_t parsed;
    if(!parse_settings(response,length,false,&parsed)) {out->result=WEATHER_ADMIN_INVALID;goto done;}
    if(write && (write->expected_revision==WEATHER_REVISION_MAX || parsed.revision!=write->expected_revision+1 ||
        parsed.enabled!=write->enabled || parsed.coordinates_available!=write->coordinates_available ||
        (parsed.coordinates_available && (parsed.latitude!=write->latitude||parsed.longitude!=write->longitude)) ||
        strcmp(parsed.timezone,write->timezone))) {out->result=WEATHER_ADMIN_INVALID;goto done;}
    if(settings_expired(deadline)){out->result=WEATHER_ADMIN_TIMEOUT;goto done;}
    *out=parsed;out->result=WEATHER_ADMIN_OK;
    if(!write){
        if(snapshot){snapshot->value=*out;snapshot->generation=generation;snapshot->session=session;atomic_store(&snapshot->ready,true);}
        read_observation(out,deadline,generation,session);
        if(generation!=valve_config_generation()||session!=valve_client_current_session()){
            memset(out,0,sizeof *out);out->result=WEATHER_ADMIN_UNAVAILABLE;
        }
    }
done:
    memset(token,0,sizeof token);memset(body,0,sizeof body);
}
#ifndef WEATHER_ADMIN_HOST_TEST
/* Caller and worker own separate references. A stalled DNS/socket operation can
 * outlive the finite HTTP wait without retaining stack pointers or server state. */
static QueueHandle_t jobs;
typedef struct {
    bool write;
    weather_settings_write_t request;
    weather_settings_view_t result;
    settings_snapshot_t confirmed;
    SemaphoreHandle_t done;
    atomic_uint references;
    atomic_bool cancelled;
    settings_deadline_t deadline;
} job_t;
static void job_release(job_t *job) {
    if(atomic_fetch_sub(&job->references,1)==1) {
        vSemaphoreDelete(job->done);free(job);
    }
}
static void worker(void *context) {
    (void)context;job_t *job;
    for(;;)if(xQueueReceive(jobs,&job,portMAX_DELAY)==pdTRUE) {
        perform(job->write?&job->request:NULL,&job->result,&job->deadline,&job->confirmed);
        if(settings_expired(&job->deadline)) {
            memset(&job->result,0,sizeof job->result);job->result.result=WEATHER_ADMIN_TIMEOUT;
        }
        xSemaphoreGive(job->done);
        atomic_flag_clear(&admission);
        job_release(job);
    }
}
bool weather_admin_init(void) {
    if(jobs)return true;
    jobs=xQueueCreate(1,sizeof(job_t *));
    if(!jobs || xTaskCreate(worker,"weather_settings",8192,NULL,3,NULL)!=pdPASS) {
        if(jobs)vQueueDelete(jobs);
        jobs=NULL;return false;
    }
    return true;
}
#else
bool weather_admin_init(void) { return true; }
#endif
static bool execute(const weather_settings_write_t *write,weather_settings_view_t *out) {
    if(!out)return false;
    memset(out,0,sizeof *out);out->result=WEATHER_ADMIN_UNAVAILABLE;
    if(atomic_flag_test_and_set(&admission))return false;
#ifdef WEATHER_ADMIN_HOST_TEST
    perform(write,out,NULL,NULL);atomic_flag_clear(&admission);
#else
    uint64_t origin=settings_now_ms();
    job_t *job=jobs && origin<=UINT64_MAX-WEATHER_REQUEST_MS ? calloc(1,sizeof *job):NULL;
    if(!job) {atomic_flag_clear(&admission);return false;}
    job->write=write!=NULL;if(write)job->request=*write;
    job->done=xSemaphoreCreateBinary();
    atomic_init(&job->references,2);atomic_init(&job->cancelled,false);atomic_init(&job->confirmed.ready,false);
    job->deadline=(settings_deadline_t){.origin_ms=origin,.expires_ms=origin+WEATHER_REQUEST_MS,.cancelled=&job->cancelled};
    if(!job->done || xQueueSend(jobs,&job,0)!=pdTRUE) {
        if(job->done)vSemaphoreDelete(job->done);
        free(job);atomic_flag_clear(&admission);return false;
    }
    uint64_t now=settings_now_ms();
    uint64_t remaining=now>=origin && now<job->deadline.expires_ms?job->deadline.expires_ms-now:0;
    TickType_t wait=pdMS_TO_TICKS(remaining);
    bool completed=remaining && xSemaphoreTake(job->done,wait)==pdTRUE;
    if(completed && !settings_expired(&job->deadline))*out=job->result;
    else {
        atomic_store(&job->cancelled,true);out->result=WEATHER_ADMIN_TIMEOUT;
        if(!write&&atomic_load(&job->confirmed.ready)&&job->confirmed.generation==valve_config_generation()&&job->confirmed.session==valve_client_current_session())*out=job->confirmed.value;
    }
    /* Admission remains occupied until the worker finishes, bounding orphaned
     * work at one job even if DNS never returns. Other admin routes stay free. */
    job_release(job);
#endif
    return out->result==WEATHER_ADMIN_OK;
}

bool weather_admin_get(weather_settings_view_t *out) { return execute(NULL,out); }
bool weather_admin_update(const weather_settings_write_t *request,weather_settings_view_t *out) {
    return request && execute(request,out);
}

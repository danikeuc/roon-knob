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
#ifndef WEATHER_ADMIN_HOST_TEST
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif
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
static int transport(const char *method,const char *url,const char *header,const char *token,
    const char *body,size_t body_length,int *status,char *out,size_t capacity,size_t *length,void *context) {
    (void)context;
    esp_http_client_config_t config={.url=url,.method=!strcmp(method,"PUT")?HTTP_METHOD_PUT:HTTP_METHOD_GET,.timeout_ms=3000,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client)return -1;
    int result=-1;
    if(esp_http_client_set_header(client,header,token)!=ESP_OK ||
       esp_http_client_set_header(client,"Accept","application/json")!=ESP_OK ||
       (body_length && esp_http_client_set_header(client,"Content-Type","application/json")!=ESP_OK))goto done;
    esp_err_t err=esp_http_client_open(client,(int)body_length);
    if(err!=ESP_OK) { result=err==ESP_ERR_TIMEOUT?-3:-1;goto done; }
    if(body_length && esp_http_client_write(client,body,(int)body_length)!=(int)body_length) { result=-3;goto done; }
    if(esp_http_client_fetch_headers(client)<0) { result=-3;goto done; }
    *status=esp_http_client_get_status_code(client);
    if(*status!=200) { result=0;goto done; }
    size_t used=0;
    while(used<capacity) {
        int n=esp_http_client_read(client,out+used,capacity-used);
        if(n<0) { result=-3;goto done; }
        if(n==0) { *length=used;result=0;goto done; }
        used+=(size_t)n;
    }
    result=-2;
done:
    esp_http_client_close(client);esp_http_client_cleanup(client);return result;
}
static void *transport_context;
#endif
static void perform(const weather_settings_write_t *write,weather_settings_view_t *out) {
    memset(out,0,sizeof *out);out->result=WEATHER_ADMIN_UNAVAILABLE;
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
        write?strlen(body):0,&status,response,sizeof response,&length,transport_context);
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
    *out=parsed;out->result=WEATHER_ADMIN_OK;
done:
    memset(token,0,sizeof token);memset(body,0,sizeof body);
}
#ifndef WEATHER_ADMIN_HOST_TEST
/* Dedicated settings worker. This task has no relationship to playback/artwork
 * workers. HTTP callers retain their owned immutable request until completion. */
static QueueHandle_t jobs;
static SemaphoreHandle_t admission;
typedef struct { bool write; weather_settings_write_t request; weather_settings_view_t result; SemaphoreHandle_t done; } job_t;
static void worker(void *context) {
    (void)context;job_t *job;
    for(;;)if(xQueueReceive(jobs,&job,portMAX_DELAY)==pdTRUE) {
        perform(job->write?&job->request:NULL,&job->result);xSemaphoreGive(job->done);
    }
}
bool weather_admin_init(void) {
    if(jobs)return true;
    admission=xSemaphoreCreateMutex();jobs=xQueueCreate(1,sizeof(job_t *));
    if(!admission||!jobs || xTaskCreate(worker,"weather_settings",8192,NULL,3,NULL)!=pdPASS) {
        if(jobs)vQueueDelete(jobs);
        if(admission)vSemaphoreDelete(admission);
        jobs=NULL;admission=NULL;return false;
    }
    return true;
}
#else
bool weather_admin_init(void) { return true; }
static atomic_flag admission=ATOMIC_FLAG_INIT;
#endif
static bool execute(const weather_settings_write_t *write,weather_settings_view_t *out) {
    if(!out)return false;
    memset(out,0,sizeof *out);out->result=WEATHER_ADMIN_UNAVAILABLE;
#ifdef WEATHER_ADMIN_HOST_TEST
    if(atomic_flag_test_and_set(&admission))return false;
    perform(write,out);atomic_flag_clear(&admission);
#else
    if(!jobs || xSemaphoreTake(admission,0)!=pdTRUE)return false;
    job_t job={.write=write!=NULL};if(write)job.request=*write;
    job.done=xSemaphoreCreateBinary();job_t *pointer=&job;
    if(job.done && xQueueSend(jobs,&pointer,0)==pdTRUE) {
        /* Transport is bounded. Never abandon a queued stack-owned request. */
        xSemaphoreTake(job.done,portMAX_DELAY);*out=job.result;
    }
    if(job.done)vSemaphoreDelete(job.done);
    xSemaphoreGive(admission);
#endif
    return out->result==WEATHER_ADMIN_OK;
}
bool weather_admin_get(weather_settings_view_t *out) { return execute(NULL,out); }
bool weather_admin_update(const weather_settings_write_t *request,weather_settings_view_t *out) {
    return request && execute(request,out);
}

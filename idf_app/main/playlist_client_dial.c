#include "playlist_client_dial.h"
#include "playlist_admin_dial.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#ifndef PLAYLIST_HOST_TEST
#include "controller_config.h"
#include "bridge_client.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif
#define ADMIN_MS UINT64_C(5000)
static atomic_flag admission=ATOMIC_FLAG_INIT,result_lock=ATOMIC_FLAG_INIT;
static atomic_uint ui_generation=1,session_generation=1;
static atomic_bool online;
static playlist_client_result_t result_slot;
static bool has_result;
static struct { bool active;char id[37],zone[129];uint64_t start,poll;uint32_t generation; } pending;
#ifdef PLAYLIST_HOST_TEST
static uint64_t (*clock_hook)(void);
static bool (*snapshot_hook)(playlist_endpoint_t *);
static int (*transport_hook)(const playlist_http_request_t *,int *,char *,size_t,size_t *);
void playlist_client_test_hooks(uint64_t (*clock)(void),bool (*snapshot)(playlist_endpoint_t *),int (*transport)(const playlist_http_request_t *,int *,char *,size_t,size_t *)){clock_hook=clock;snapshot_hook=snapshot;transport_hook=transport;}
#endif
static uint64_t now_ms(void) {
#ifdef PLAYLIST_HOST_TEST
 return clock_hook?clock_hook():0;
#else
 return (uint64_t)(esp_timer_get_time()/1000);
#endif
}
static bool snapshot(playlist_endpoint_t *out) {
#ifdef PLAYLIST_HOST_TEST
 return snapshot_hook&&snapshot_hook(out);
#else
 controller_config_snapshot_t cfg;controller_config_endpoint_token_t endpoint;
 if(!controller_config_snapshot(&cfg)||!controller_config_capture_endpoint_token(&endpoint))return false;
 memset(out,0,sizeof *out);
 if(strlen(endpoint.bridge_base)>=sizeof out->base||!endpoint.bridge_base[0]||!bridge_client_get_current_zone_id(out->zone,sizeof out->zone))return false;
 strcpy(out->base,endpoint.bridge_base);out->generation=endpoint.generation;out->session=atomic_load(&session_generation);out->credential_generation=playlist_private_generation();return !(out->credential_generation&1u);
#endif
}
typedef enum { JOB_FAVORITES,JOB_PLAY,JOB_RECEIPT,JOB_ADMIN } job_kind_t;
typedef struct job {
 job_kind_t kind;playlist_endpoint_t endpoint;uint64_t origin,deadline,revision,core_epoch;
 uint32_t generation;char favorite_id[129],request_id[37],intent_id[129],path[256],method[5];char *body,*reply;size_t capacity,length;int status;
 atomic_bool cancelled;atomic_uint references;
#ifndef PLAYLIST_HOST_TEST
 SemaphoreHandle_t done;
#endif
} job_t;
#ifndef PLAYLIST_HOST_TEST
static QueueHandle_t jobs;
#else
static job_t *queued;
#endif
static void release(job_t *job){if(atomic_fetch_sub(&job->references,1)==1){
#ifndef PLAYLIST_HOST_TEST
 if(job->done)vSemaphoreDelete(job->done);
#endif
 free(job->body);free(job->reply);memset(job->endpoint.token,0,sizeof job->endpoint.token);free(job);}}
static bool valid(void *context) {
 job_t *j=context;uint64_t now=now_ms();playlist_endpoint_t current;
 if(atomic_load(&j->cancelled)||!atomic_load(&online)||now<j->origin||now>=j->deadline||!snapshot(&current))return false;
 return (j->kind==JOB_ADMIN||j->generation==atomic_load(&ui_generation))&&current.generation==j->endpoint.generation&&current.session==j->endpoint.session&&current.credential_generation==j->endpoint.credential_generation&&!strcmp(current.base,j->endpoint.base)&&!strcmp(current.zone,j->endpoint.zone);
}
static void publish(job_t *j,playlist_client_status_t status,const playlist_favorites_t *favorites){
 if(j->generation!=atomic_load(&ui_generation))return;
 while(atomic_flag_test_and_set(&result_lock)){}
 result_slot=(playlist_client_result_t){.status=status,.generation=j->generation};
 if(favorites){result_slot.favorites=*favorites;}
 strcpy(result_slot.request_id,j->request_id);strcpy(result_slot.zone,j->endpoint.zone);has_result=true;atomic_flag_clear(&result_lock);
}
#ifndef PLAYLIST_HOST_TEST
static bool budget(esp_http_client_handle_t client,const playlist_http_request_t *r){if(!r->valid(r->context))return false;uint64_t now=now_ms();if(now>=r->deadline_ms)return false;uint64_t left=r->deadline_ms-now;return esp_http_client_set_timeout_ms(client,(int)(left<3000?left:3000))==ESP_OK;}
static int http(const playlist_http_request_t *r,int *status,char *out,size_t capacity,size_t *length){
 esp_http_client_config_t cfg={.url=r->url,.timeout_ms=3000,.disable_auto_redirect=true,.method=!strcmp(r->method,"POST")?HTTP_METHOD_POST:!strcmp(r->method,"PUT")?HTTP_METHOD_PUT:HTTP_METHOD_GET};
 esp_http_client_handle_t c=esp_http_client_init(&cfg);if(!c)return -1;char bearer[136];snprintf(bearer,sizeof bearer,"Bearer %s",r->token);int rc=-3;size_t n=r->body?strlen(r->body):0;
 if(esp_http_client_set_header(c,"Authorization",bearer)!=ESP_OK||esp_http_client_set_header(c,"Accept","application/json")!=ESP_OK||(n&&esp_http_client_set_header(c,"Content-Type","application/json")!=ESP_OK))goto done;
 if(!budget(c,r)||esp_http_client_open(c,(int)n)!=ESP_OK)goto done;
 /* DNS/open can outlive its timeout. Revalidate immediately before body bytes. */
 if(!budget(c,r))goto done;
 if(n&&esp_http_client_write(c,r->body,(int)n)!=(int)n)goto done;
 if(!budget(c,r)||esp_http_client_fetch_headers(c)<0)goto done;
 *status=esp_http_client_get_status_code(c);if(*status!=200){rc=0;goto done;}
 size_t used=0;
 while(used<capacity){if(!budget(c,r))goto done;int got=esp_http_client_read(c,out+used,(int)(capacity-used));if(got<0)goto done;if(!got){if(!esp_http_client_is_complete_data_received(c))goto done;out[used]=0;*length=used;rc=0;goto done;}used+=(size_t)got;}
 rc=-2;
done:memset(bearer,0,sizeof bearer);esp_http_client_close(c);esp_http_client_cleanup(c);return rc;
}
#endif
static int exchange(job_t *job,const char *method,const char *path,const char *body,char *out,size_t cap,size_t *length) {
 if(!valid(job))return -3;
 char url[400];int n=snprintf(url,sizeof url,"%s%s%s",job->endpoint.base,(strlen(job->endpoint.base)>=5&&!strcmp(job->endpoint.base+strlen(job->endpoint.base)-5,"/roon"))?"":"/roon",path);if(n<0||(size_t)n>=sizeof url)return -1;
 playlist_http_request_t request={.method=method,.path=path,.body=body,.url=url,.token=job->endpoint.token,.deadline_ms=job->deadline,.valid=valid,.context=job};int status=0;
#ifdef PLAYLIST_HOST_TEST
 int rc=transport_hook?transport_hook(&request,&status,out,cap,length):-1;
#else
 int rc=http(&request,&status,out,cap,length);
#endif
 return rc?rc:status;
}
static bool capabilities(job_t *job){char body[1025];size_t len=0;return exchange(job,"GET","/dial/v1/capabilities",NULL,body,1024,&len)==200&&playlist_capabilities_valid(body,len);}
static void uuid(char out[37]) {
#ifdef PLAYLIST_HOST_TEST
 strcpy(out,"00000000-0000-4000-8000-000000000001");
#else
 unsigned char id[16];esp_fill_random(id,sizeof id);id[6]=(id[6]&15)|64;id[8]=(id[8]&63)|128;
 snprintf(out,37,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",id[0],id[1],id[2],id[3],id[4],id[5],id[6],id[7],id[8],id[9],id[10],id[11],id[12],id[13],id[14],id[15]);
#endif
}
static char *allocate(size_t n){
#ifdef PLAYLIST_HOST_TEST
 return malloc(n);
#else
 return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
}
static void perform(job_t *j) {
 j->status=503;
#ifndef PLAYLIST_HOST_TEST
 if(!playlist_private_load(j->endpoint.base,j->endpoint.token,sizeof j->endpoint.token))goto error;
#endif
 if(!valid(j))goto error;
 if(j->kind==JOB_RECEIPT){char body[2049],path[128];size_t len=0;int status=0;snprintf(path,sizeof path,"/dial/v1/requests/%s",j->request_id);int rc=exchange(j,"GET",path,NULL,body,2048,&len);if(rc==200&&playlist_receipt_parse(body,len,j->request_id,j->endpoint.zone,&status)){publish(j,(playlist_client_status_t)status,NULL);return;}publish(j,PLAYLIST_CLIENT_UNKNOWN,NULL);return;}
 if(!capabilities(j))goto error;
 if(j->kind==JOB_FAVORITES){char *body=allocate(PLAYLIST_FAVORITES_MAX+1);size_t length=0;playlist_favorites_t f;bool ok=body&&exchange(j,"GET","/dial/v1/favorites",NULL,body,PLAYLIST_FAVORITES_MAX,&length)==200&&valid(j)&&playlist_favorites_parse(body,length,&f)&&!strcmp(f.zone_id,j->endpoint.zone);free(body);if(ok){publish(j,PLAYLIST_CLIENT_FAVORITES,&f);return;}goto error;}
 if(j->kind==JOB_ADMIN){j->reply=allocate(j->capacity+1);if(!j->reply)goto error;int rc=exchange(j,j->method,j->path,j->body,j->reply,j->capacity,&j->length);j->status=rc>0?rc:503;if(!valid(j))j->status=503;if(j->status==200){bool catalog=strstr(j->path,"playlists")!=NULL;playlist_favorites_t f;if(!(catalog?playlist_catalog_valid(j->reply,j->length):(playlist_favorites_parse(j->reply,j->length,&f)&&!strcmp(f.zone_id,j->endpoint.zone))))j->status=503;}return;}
 if(j->kind==JOB_PLAY){char response[2049];size_t len=0;uint64_t expires;
  cJSON *body=cJSON_CreateObject();if(!body)goto error;cJSON_AddStringToObject(body,"favorite_id",j->favorite_id);cJSON_AddNumberToObject(body,"expected_revision",(double)j->revision);cJSON_AddNumberToObject(body,"core_epoch",(double)j->core_epoch);char *text=cJSON_PrintUnformatted(body);cJSON_Delete(body);if(!text)goto error;
  int rc=exchange(j,"POST","/dial/v1/play-intents",text,response,2048,&len);free(text);
  if(rc!=200||!valid(j)||!playlist_intent_parse(response,len,j->intent_id,sizeof j->intent_id,&expires))goto error;
  uint64_t prepared=now_ms();if(prepared>UINT64_MAX-expires)goto error;if(prepared+expires<j->deadline)j->deadline=prepared+expires;
  if(!valid(j))goto error;
  body=cJSON_CreateObject();if(!body)goto error;cJSON_AddStringToObject(body,"request_id",j->request_id);cJSON_AddStringToObject(body,"intent_id",j->intent_id);text=cJSON_PrintUnformatted(body);cJSON_Delete(body);if(!text)goto error;
  rc=exchange(j,"POST","/dial/v1/play",text,response,2048,&len);free(text);
  /* Once a play body might have been delivered, only observation is permitted. */
  if(j->generation!=atomic_load(&ui_generation))return;
  int status;if(rc==200&&playlist_receipt_parse(response,len,j->request_id,j->endpoint.zone,&status))publish(j,(playlist_client_status_t)status,NULL);
  else if(rc==409||rc==401||rc==422||rc==413)publish(j,PLAYLIST_CLIENT_ERROR,NULL);
  else publish(j,PLAYLIST_CLIENT_ACCEPTED,NULL);
  return;
 }
error:if(j->kind!=JOB_ADMIN)publish(j,PLAYLIST_CLIENT_ERROR,NULL);
}
#ifndef PLAYLIST_HOST_TEST
static void worker(void *unused){(void)unused;job_t *j;for(;;)if(xQueueReceive(jobs,&j,portMAX_DELAY)==pdTRUE){perform(j);if(j->done)xSemaphoreGive(j->done);atomic_flag_clear(&admission);release(j);}}
#endif
bool playlist_client_init(void){
#ifndef PLAYLIST_HOST_TEST
 if(jobs)return true;
 jobs=xQueueCreate(1,sizeof(job_t *));if(!jobs||xTaskCreate(worker,"playlist",12288,NULL,3,NULL)!=pdPASS){if(jobs)vQueueDelete(jobs);jobs=NULL;return false;}
#endif
 return true;
}
void playlist_client_connected(bool connected){atomic_store(&online,connected);atomic_fetch_add(&session_generation,1);atomic_fetch_add(&ui_generation,1);}
void playlist_client_cancel(void){atomic_fetch_add(&ui_generation,1);pending.active=false;while(atomic_flag_test_and_set(&result_lock)){}has_result=false;atomic_flag_clear(&result_lock);}
static job_t *new_job(job_kind_t kind,uint64_t age){
 if(!atomic_load(&online)||atomic_flag_test_and_set(&admission))return NULL;
 uint64_t now=now_ms();job_t *j=now<=UINT64_MAX-age?calloc(1,sizeof *j):NULL;
 if(!j||!snapshot(&j->endpoint)){free(j);atomic_flag_clear(&admission);return NULL;}
 j->kind=kind;j->origin=now;j->deadline=now+age;j->generation=atomic_load(&ui_generation);atomic_init(&j->references,1);atomic_init(&j->cancelled,false);return j;
}
static bool enqueue(job_t *j){
#ifdef PLAYLIST_HOST_TEST
 queued=j;return true;
#else
 if(jobs&&xQueueSend(jobs,&j,0)==pdTRUE)return true;
 atomic_flag_clear(&admission);release(j);return false;
#endif
}
bool playlist_client_request_favorites(void){job_t *j=new_job(JOB_FAVORITES,ADMIN_MS);return j&&enqueue(j);}
bool playlist_client_request_play(const char *id,uint64_t revision,uint64_t epoch){
 if(!id||!*id||strlen(id)>128||revision>PLAYLIST_NUMBER_MAX||epoch>PLAYLIST_NUMBER_MAX)return false;
 job_t *j=new_job(JOB_PLAY,PLAYLIST_UNSENT_MS);if(!j)return false;strcpy(j->favorite_id,id);j->revision=revision;j->core_epoch=epoch;uuid(j->request_id);return enqueue(j);
}
bool playlist_client_take(playlist_client_result_t *out){if(!out||atomic_flag_test_and_set(&result_lock))return false;bool yes=has_result&&result_slot.generation==atomic_load(&ui_generation);if(yes)*out=result_slot;has_result=false;atomic_flag_clear(&result_lock);if(yes&&out->status==PLAYLIST_CLIENT_ACCEPTED){if(!pending.active){pending.start=now_ms();pending.poll=pending.start;pending.generation=out->generation;strcpy(pending.id,out->request_id);strcpy(pending.zone,out->zone);}pending.active=true;}else if(yes)pending.active=false;return yes;}
void playlist_client_process(void){
 if(!pending.active)return;
 uint64_t now=now_ms();
 if(pending.generation!=atomic_load(&ui_generation)){pending.active=false;return;}
 if(now<pending.start||now-pending.start>=15000){job_t j={.generation=pending.generation};strcpy(j.request_id,pending.id);strcpy(j.endpoint.zone,pending.zone);publish(&j,PLAYLIST_CLIENT_UNKNOWN,NULL);pending.active=false;return;}
 if(now-pending.poll>=1000){job_t *j=new_job(JOB_RECEIPT,ADMIN_MS);if(j){strcpy(j->request_id,pending.id);if(strcmp(j->endpoint.zone,pending.zone)){atomic_flag_clear(&admission);release(j);playlist_client_cancel();return;}pending.poll=now;enqueue(j);}}
}
#ifdef PLAYLIST_HOST_TEST
void playlist_client_test_run(void){job_t *j=queued;if(!j)return;queued=NULL;perform(j);atomic_flag_clear(&admission);release(j);}
#endif
int playlist_client_admin(const char *method,const char *path,const char *body,size_t cap,char **out,size_t *length){
 if(!out||!length||!method||!path||strlen(path)>=256||strlen(method)>=5||cap>PLAYLIST_CATALOG_MAX)return 422;
 *out=NULL;*length=0;
 job_t *j=new_job(JOB_ADMIN,ADMIN_MS);if(!j)return 503;strcpy(j->method,method);strcpy(j->path,path);j->capacity=cap;
 if(body){size_t n=strlen(body);j->body=malloc(n+1);if(!j->body){atomic_flag_clear(&admission);release(j);return 503;}memcpy(j->body,body,n+1);}
 atomic_store(&j->references,2);
#ifdef PLAYLIST_HOST_TEST
 perform(j);atomic_flag_clear(&admission);bool complete=true;
#else
 j->done=xSemaphoreCreateBinary();if(!j->done){atomic_flag_clear(&admission);release(j);release(j);return 503;}
 if(!enqueue(j)){release(j);return 503;}
 uint64_t now=now_ms(),left=now>=j->origin&&now<j->deadline?j->deadline-now:0;
 bool complete=left&&xSemaphoreTake(j->done,pdMS_TO_TICKS(left))==pdTRUE&&valid(j);
#endif
 int status=complete?j->status:503;
 if(status==200){*out=j->reply;*length=j->length;j->reply=NULL;}
 if(!complete)atomic_store(&j->cancelled,true);
#ifdef PLAYLIST_HOST_TEST
 release(j);
#endif
 release(j);return status;
}

#define _POSIX_C_SOURCE 200809L
#include "runtime_fixture.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "controller_config.h"
#include "nvs.h"
#include "playlist_client_dial.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
atomic_uint fixture_body_writes,fixture_cleanups,fixture_last_timeout;
#ifdef COMBINED_ISOLATION
extern uint64_t combined_clock_ms(void);
atomic_bool combined_open_blocked,combined_release_open;
static atomic_bool combined_worker_idle;
#endif
uint64_t fixture_action_now;
static atomic_uint released_jobs;
static fixture_http_case_t scenario;
static atomic_bool changed_zone;
static uint64_t clock_origin;
static atomic_bool copy_window,publisher_attempt;
static bool preempt_reader;
static QueueHandle_t mailbox;
void runtime_arm_mailbox_preemption(void){assert(mailbox);preempt_reader=true;atomic_store(&publisher_attempt,false);}
void runtime_assert_mailbox_preemption(void){assert(atomic_load(&publisher_attempt));}
uint64_t runtime_wall_ms(void) {
    struct timespec time;clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}
static void pause_ms(unsigned milliseconds) {
    struct timespec time={.tv_sec=milliseconds/1000,.tv_nsec=(long)(milliseconds%1000)*1000000};nanosleep(&time,NULL);
}
void runtime_http_case(fixture_http_case_t value) {
    scenario=value;atomic_store(&changed_zone,false);clock_origin=runtime_wall_ms();atomic_store(&fixture_body_writes,0);atomic_store(&fixture_cleanups,0);atomic_store(&released_jobs,0);
}
int64_t esp_timer_get_time(void) {
#ifdef COMBINED_ISOLATION
 return (int64_t)combined_clock_ms()*1000;
#else
 return (int64_t)(runtime_wall_ms()-clock_origin)*100*1000;
#endif
}
void runtime_wait_cleanup(unsigned count) {
    uint64_t deadline=runtime_wall_ms()+1000;
#ifdef COMBINED_ISOLATION
    /* UI jobs have no admin waiter/semaphore. HTTP cleanup closes each
     * exchange; result consumption below separately observes job completion. */
    while((atomic_load(&fixture_cleanups)<count||!atomic_load(&combined_worker_idle))&&runtime_wall_ms()<deadline)pause_ms(1);
    assert(atomic_load(&fixture_cleanups)>=count&&atomic_load(&combined_worker_idle));
#else
    while((atomic_load(&fixture_cleanups)<count || atomic_load(&released_jobs)<count) && runtime_wall_ms()<deadline)pause_ms(1);
    assert(atomic_load(&fixture_cleanups)>=count && atomic_load(&released_jobs)>=count);
#endif
}
struct fake_http { esp_http_client_method_t method;int timeout;char response[2048];size_t offset;bool weather;bool target_header; };
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    struct fake_http *client=calloc(1,sizeof *client);assert(client);
    assert(strstr(config->url,"/roon/dial/v1/"));
    client->method=config->method;client->timeout=config->timeout_ms;
    client->weather=strstr(config->url,"weather-settings")!=NULL;
    if(strstr(config->url,"capabilities"))strcpy(client->response,"{\"version\":1,\"max_favorites\":5,\"replace_queue\":true}");
    else if(strstr(config->url,"favorites"))strcpy(client->response,"{\"revision\":3,\"core_epoch\":7,\"zone_id\":\"sauna\",\"zone_name\":\"Sauna\",\"favorites\":[{\"id\":\"opaque\",\"title\":\"Music\",\"available\":true,\"reason\":null}]}");
    else if(strstr(config->url,"play-intents"))strcpy(client->response,"{\"intent_id\":\"intent\",\"expires_in_ms\":10000}");
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,const char *name,const char *value) {
    if(!strcmp(name,"X-Dial-Zone")){assert(!strcmp(value,"sauna"));client->target_header=true;}
    if(!strcmp(name,"Authorization"))assert(!strcmp(value,"Bearer synthetic-private"));
    if(!strcmp(name,"X-Display-Token"))assert(!client->weather && !strcmp(value,"private-test-token"));
    if(!strcmp(name,"X-Weather-Settings-Token"))assert(client->weather && !strcmp(value,"private-weather-http"));
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client,int value) {
    assert(value>0 && value<=3000);client->timeout=value;atomic_store(&fixture_last_timeout,(unsigned)value);return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client,int length) {
    (void)length;assert(client->target_header);
    if(scenario==HTTP_ACTION_EXPIRED_OPEN&&client->method==HTTP_METHOD_POST)atomic_store(&changed_zone,true);
    if(scenario==HTTP_WEATHER_STALLED_OPEN) {
#ifdef COMBINED_ISOLATION
        atomic_store(&combined_open_blocked,true);
        uint64_t limit=runtime_wall_ms()+2000;
        while(!atomic_load(&combined_release_open)&&runtime_wall_ms()<limit)pause_ms(1);
        assert(atomic_load(&combined_release_open));
#else
        pause_ms(300); /* DNS ignores socket timeout. */
#endif
    }
    return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t client,const char *body,int length) {
    assert(length>0);atomic_fetch_add(&fixture_body_writes,1);
    if(strstr(body,"request_id")) {
        const char *id=strstr(body,"request_id\":\"");assert(id);id+=strlen("request_id\":\"");
        snprintf(client->response,sizeof client->response,"{\"request_id\":\"%.*s\",\"status\":\"accepted\",\"zone_id\":\"sauna\"}",36,id);
    }
    return length;
}
int esp_http_client_fetch_headers(esp_http_client_handle_t client) { return (int)strlen(client->response); }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { (void)client;return 200; }
int esp_http_client_read(esp_http_client_handle_t client,char *out,int capacity) {
    size_t remaining=strlen(client->response)-client->offset;
    if(!remaining)return 0;
    if(scenario==HTTP_WEATHER_SLOW_RESPONSE) { pause_ms(20);capacity=1; }
    size_t length=remaining<(size_t)capacity?remaining:(size_t)capacity;
    memcpy(out,client->response+client->offset,length);client->offset+=length;return (int)length;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { (void)client;return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { free(client);atomic_fetch_add(&fixture_cleanups,1);return ESP_OK; }
struct fake_queue { pthread_mutex_t lock;pthread_cond_t changed;unsigned capacity,count,head;size_t item_size;unsigned char *data; };
QueueHandle_t xQueueCreate(unsigned capacity,size_t size) {
    struct fake_queue *queue=calloc(1,sizeof *queue);assert(queue);pthread_mutex_init(&queue->lock,NULL);pthread_cond_init(&queue->changed,NULL);
    if(capacity==1&&size>sizeof(void *)){assert(!mailbox);mailbox=queue;}
    queue->capacity=capacity;queue->item_size=size;queue->data=calloc(capacity,size);assert(queue->data);return queue;
}
int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait) {
    (void)wait;pthread_mutex_lock(&queue->lock);
    if(queue->count==queue->capacity){pthread_mutex_unlock(&queue->lock);return pdFALSE;}
#ifdef COMBINED_ISOLATION
    if(queue->item_size==sizeof(void *))atomic_store(&combined_worker_idle,false);
#endif
    memcpy(queue->data+((queue->head+queue->count)%queue->capacity)*queue->item_size,item,queue->item_size);
    ++queue->count;pthread_cond_signal(&queue->changed);pthread_mutex_unlock(&queue->lock);return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait) {
    assert(wait==portMAX_DELAY||wait==0);pthread_mutex_lock(&queue->lock);
    if(!queue->count&&!wait){pthread_mutex_unlock(&queue->lock);return pdFALSE;}
    while(!queue->count){
#ifdef COMBINED_ISOLATION
        if(queue->item_size==sizeof(void *))atomic_store(&combined_worker_idle,true);
#endif
        pthread_cond_wait(&queue->changed,&queue->lock);
    }
    if(queue==mailbox&&preempt_reader){
        /* Suspend the lower-priority receiver inside its kernel-owned copy.
         * The actual production worker reaches overwrite during that window;
         * it must wait in the RTOS primitive, leaving the owner able to resume. */
        preempt_reader=false;atomic_store(&copy_window,true);
        size_t half=queue->item_size/2;memcpy(item,queue->data+queue->head*queue->item_size,half);
        assert(playlist_client_request_favorites());uint64_t deadline=runtime_wall_ms()+200;
        while(!atomic_load(&publisher_attempt)&&runtime_wall_ms()<deadline)pause_ms(1);
        assert(atomic_load(&publisher_attempt));
        memcpy((char *)item+half,queue->data+queue->head*queue->item_size+half,queue->item_size-half);
        atomic_store(&copy_window,false);
    }else memcpy(item,queue->data+queue->head*queue->item_size,queue->item_size);
    queue->head=(queue->head+1)%queue->capacity;
    --queue->count;pthread_mutex_unlock(&queue->lock);return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t queue,const void *item){
    assert(queue->capacity==1);if(queue==mailbox&&atomic_load(&copy_window))atomic_store(&publisher_attempt,true);pthread_mutex_lock(&queue->lock);memcpy(queue->data,item,queue->item_size);queue->head=0;queue->count=1;pthread_cond_signal(&queue->changed);pthread_mutex_unlock(&queue->lock);return pdTRUE;
}
int xQueueReset(QueueHandle_t queue){pthread_mutex_lock(&queue->lock);queue->head=queue->count=0;pthread_mutex_unlock(&queue->lock);return pdTRUE;}
void vQueueDelete(QueueHandle_t queue){free(queue->data);free(queue);}
struct fake_semaphore { pthread_mutex_t lock;pthread_cond_t changed;unsigned available; };
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    struct fake_semaphore *semaphore=calloc(1,sizeof *semaphore);assert(semaphore);
    pthread_mutex_init(&semaphore->lock,NULL);pthread_cond_init(&semaphore->changed,NULL);return semaphore;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { SemaphoreHandle_t value=xSemaphoreCreateBinary();value->available=1;return value; }
int xSemaphoreTake(SemaphoreHandle_t semaphore,unsigned ticks) {
    pthread_mutex_lock(&semaphore->lock);int status=0;
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_nsec+=(long)ticks*1000000;
    deadline.tv_sec+=deadline.tv_nsec/1000000000;deadline.tv_nsec%=1000000000;
    while(!semaphore->available && status!=ETIMEDOUT) {
        if(!ticks){status=ETIMEDOUT;break;}
        status=ticks==portMAX_DELAY?pthread_cond_wait(&semaphore->changed,&semaphore->lock):pthread_cond_timedwait(&semaphore->changed,&semaphore->lock,&deadline);
    }
    int ok=semaphore->available?pdTRUE:pdFALSE;if(ok)--semaphore->available;
    pthread_mutex_unlock(&semaphore->lock);return ok;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    pthread_mutex_lock(&semaphore->lock);semaphore->available=1;pthread_cond_signal(&semaphore->changed);pthread_mutex_unlock(&semaphore->lock);return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { pthread_mutex_destroy(&semaphore->lock);pthread_cond_destroy(&semaphore->changed);free(semaphore);atomic_fetch_add(&released_jobs,1); }
struct launch {void (*fn)(void *);void *context;};
static void *thread_start(void *context) {struct launch job=*(struct launch *)context;free(context);job.fn(job.context);return NULL;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *context,unsigned priority,void *handle) {
    (void)handle;assert(!strcmp(name,"playlist")&&stack==12288&&priority==3);
    struct launch *launch=malloc(sizeof *launch);assert(launch);*launch=(struct launch){fn,context};
    pthread_t thread;assert(!pthread_create(&thread,NULL,thread_start,launch));pthread_detach(thread);return pdPASS;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){return c->offset==strlen(c->response);}
bool controller_config_snapshot(controller_config_snapshot_t *out){memset(out,0,sizeof *out);strcpy(out->value.bridge_base,"http://bridge/roon");return true;}
bool controller_config_capture_endpoint_token(controller_config_endpoint_token_t *out){*out=(controller_config_endpoint_token_t){.generation=1};strcpy(out->bridge_base,"http://bridge/roon");return true;}
bool bridge_client_get_current_zone_id(char *out,size_t capacity){assert(capacity>=6);strcpy(out,atomic_load(&changed_zone)?"other-zone":"sauna");return true;}
void esp_fill_random(void *out,size_t length){memset(out,1,length);}
static char private_blob[260];
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){(void)mode;assert(strlen(name)<NVS_NS_NAME_MAX_SIZE&&!strcmp(name,"playlist_priv"));*out=1;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size){(void)handle;assert(!strcmp(key,"credential")&&size<=sizeof private_blob);memcpy(private_blob,value,size);return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *value,size_t *size){(void)handle;assert(!strcmp(key,"credential"));size_t length=strlen(private_blob)+1;if(length>*size)return 1;memcpy(value,private_blob,length);*size=length;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t handle){(void)handle;return ESP_OK;}
void nvs_close(nvs_handle_t handle){(void)handle;}

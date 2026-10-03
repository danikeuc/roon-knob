#define _POSIX_C_SOURCE 200809L
#include "runtime_fixture.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "../valve_dial/weather_status_fixtures.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
atomic_uint fixture_body_writes,fixture_cleanups,fixture_last_timeout;
uint64_t fixture_action_now;
static atomic_uint released_jobs;
static fixture_http_case_t scenario;
static uint64_t clock_origin;
uint64_t runtime_wall_ms(void) {
    struct timespec time;clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}
static void pause_ms(unsigned milliseconds) {
    struct timespec time={.tv_sec=milliseconds/1000,.tv_nsec=(long)(milliseconds%1000)*1000000};nanosleep(&time,NULL);
}
void runtime_http_case(fixture_http_case_t value) {
    scenario=value;clock_origin=runtime_wall_ms();atomic_store(&fixture_body_writes,0);atomic_store(&fixture_cleanups,0);atomic_store(&released_jobs,0);
}
int64_t esp_timer_get_time(void) { return (int64_t)(runtime_wall_ms()-clock_origin)*100*1000; }
void runtime_wait_counts(unsigned exchanges,unsigned jobs) {
    uint64_t deadline=runtime_wall_ms()+1000;
    while((atomic_load(&fixture_cleanups)<exchanges || atomic_load(&released_jobs)<jobs) && runtime_wall_ms()<deadline)pause_ms(1);
    assert(atomic_load(&fixture_cleanups)>=exchanges && atomic_load(&released_jobs)>=jobs);
}
void runtime_wait_cleanup(unsigned count){runtime_wait_counts(count,count);}
struct fake_http { esp_http_client_method_t method;int timeout;char response[2048];size_t offset;bool weather; };
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    struct fake_http *client=calloc(1,sizeof *client);assert(client);
    client->method=config->method;client->timeout=config->timeout_ms;
    client->weather=strstr(config->url,"weather-settings")!=NULL;
    if(client->weather)strcpy(client->response,"{\"revision\":1,\"enabled\":true,\"latitude\":46.05,\"longitude\":14.5,\"timezone\":\"Europe/Ljubljana\"}");
    else {
        strcpy(client->response,weather_fixture_0);
        if(scenario==HTTP_OBSERVATION_MALFORMED){
            char *weather=strstr(client->response,"\"weather\":");assert(weather);
            strcpy(weather,"\"weather\":[1],\"weather_assistance_version\":1}");
        }
    }
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,const char *name,const char *value) {
    if(!strcmp(name,"X-Display-Token"))assert(!client->weather && !strcmp(value,"private-test-token"));
    if(!strcmp(name,"X-Weather-Settings-Token"))assert(client->weather && !strcmp(value,"private-weather-http"));
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client,int value) {
    assert(value>0 && value<=3000);client->timeout=value;atomic_store(&fixture_last_timeout,(unsigned)value);return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client,int length) {
    (void)length;
    if(scenario==HTTP_ACTION_EXPIRED_OPEN)fixture_action_now+=client->method==HTTP_METHOD_GET?500:2000;
    if(scenario==HTTP_WEATHER_STALLED_OPEN||(scenario==HTTP_OBSERVATION_STALLED_OPEN&&!client->weather))pause_ms(300); /* DNS ignores socket timeout. */
    return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t client,const char *body,int length) {
    assert(length>0);atomic_fetch_add(&fixture_body_writes,1);
    if(!client->weather) {
        const char *id=strstr(body,"request_id\":\"");assert(id);id+=strlen("request_id\":\"");
        snprintf(client->response,sizeof client->response,"{\"request_id\":\"%.*s\",\"accepted\":true,\"control_revision\":1,\"status\":%s}",36,id,weather_fixture_3);
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
    queue->capacity=capacity;queue->item_size=size;queue->data=calloc(capacity,size);assert(queue->data);return queue;
}
int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait) {
    (void)wait;pthread_mutex_lock(&queue->lock);
    if(queue->count==queue->capacity){pthread_mutex_unlock(&queue->lock);return pdFALSE;}
    memcpy(queue->data+((queue->head+queue->count)%queue->capacity)*queue->item_size,item,queue->item_size);
    ++queue->count;pthread_cond_signal(&queue->changed);pthread_mutex_unlock(&queue->lock);return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait) {
    assert(wait==portMAX_DELAY);pthread_mutex_lock(&queue->lock);
    while(!queue->count)pthread_cond_wait(&queue->changed,&queue->lock);
    memcpy(item,queue->data+queue->head*queue->item_size,queue->item_size);queue->head=(queue->head+1)%queue->capacity;
    --queue->count;pthread_mutex_unlock(&queue->lock);return pdTRUE;
}
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
    (void)name;(void)stack;(void)priority;(void)handle;
    struct launch *launch=malloc(sizeof *launch);assert(launch);*launch=(struct launch){fn,context};
    pthread_t thread;assert(!pthread_create(&thread,NULL,thread_start,launch));pthread_detach(thread);return pdPASS;
}

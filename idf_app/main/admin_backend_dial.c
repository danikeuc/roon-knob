#include "admin_backend_dial.h"
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"

static SemaphoreHandle_t record_mutex;
static QueueHandle_t work_queue;
static atomic_flag init_busy = ATOMIC_FLAG_INIT;
typedef struct {
    void (*fn)(void *);
    void *arg;
    SemaphoreHandle_t completed;
} work_t;
static void worker(void *unused) {
    (void)unused;
    work_t work;
    for (;;) {
        if (xQueueReceive(work_queue,&work,portMAX_DELAY)==pdTRUE) {
            work.fn(work.arg);
            xSemaphoreGive(work.completed);
        }
    }
}
static bool initialize(void) {
    while(atomic_flag_test_and_set_explicit(&init_busy,memory_order_acquire)) vTaskDelay(1);
    if(!record_mutex) record_mutex=xSemaphoreCreateMutex();
    if(record_mutex && !work_queue) {
        work_queue=xQueueCreate(4,sizeof(work_t));
        if(work_queue && xTaskCreate(worker,"dial_admin",8192,NULL,1,NULL)!=pdPASS) {
            vQueueDelete(work_queue);work_queue=NULL;
        }
    }
    bool ready=record_mutex && work_queue;
    atomic_flag_clear_explicit(&init_busy,memory_order_release);
    return ready;
}
bool admin_backend_lock(void) { return initialize() && xSemaphoreTake(record_mutex,portMAX_DELAY)==pdTRUE; }
void admin_backend_unlock(void) { xSemaphoreGive(record_mutex); }
bool admin_backend_execute(void (*fn)(void *),void *arg) {
    if(!initialize()) return false;
    SemaphoreHandle_t completed=xSemaphoreCreateBinary();
    if(!completed)return false;
    work_t work={fn,arg,completed};
    bool accepted=xQueueSend(work_queue,&work,0)==pdTRUE;
    /* The caller owns the request until completion, so no timeout may free it
     * while the worker still references it. Admission itself is nonblocking. */
    if(accepted) xSemaphoreTake(completed,portMAX_DELAY);
    vSemaphoreDelete(completed);
    return accepted;
}
esp_err_t admin_backend_read(const char *key,void *out,size_t size) {
    nvs_handle_t handle;
    esp_err_t err=nvs_open(ADMIN_NVS_NAMESPACE,NVS_READONLY,&handle);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_ERR_NOT_FOUND;
    if(err!=ESP_OK)return err;
    size_t actual=size;
    err=nvs_get_blob(handle,key,out,&actual);
    nvs_close(handle);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_ERR_NOT_FOUND;
    return err==ESP_OK && actual!=size ? ESP_FAIL : err;
}
esp_err_t admin_backend_write(const char *key,const void *data,size_t size) {
    nvs_handle_t handle;
    esp_err_t err=nvs_open(ADMIN_NVS_NAMESPACE,NVS_READWRITE,&handle);
    if(err!=ESP_OK)return err;
    err=nvs_set_blob(handle,key,data,size);
    nvs_close(handle);
    return err;
}
esp_err_t admin_backend_commit(void) {
    /* IDF 5.5.5 set_blob can persist before commit. Policy always reconciles
     * storage after uncertainty; it never assumes a failed commit rolled back. */
    nvs_handle_t handle;
    esp_err_t err=nvs_open(ADMIN_NVS_NAMESPACE,NVS_READWRITE,&handle);
    if(err!=ESP_OK)return err;
    err=nvs_commit(handle);nvs_close(handle);return err;
}
uint64_t admin_backend_seconds(void) { return (uint64_t)esp_timer_get_time()/1000000u; }
void admin_backend_random(void *out,size_t size) { esp_fill_random(out,size); }
void admin_secure_zero(void *ptr,size_t size) {
    volatile unsigned char *p=ptr;
    while(size--)*p++=0;
}

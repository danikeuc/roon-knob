"""Run actual artwork admission/worker/application with controlled RTOS and UI fakes."""
from pathlib import Path
import subprocess,tempfile
source=Path('common/ui.c').read_text()
a=source.index('typedef struct { char url[512]');b=source.index('\n#endif',a);worker=source[a:b]
a=source.index('void ui_set_artwork(');b=source.index('\n#endif',a);admission=source[a:b].replace('#if defined(RK_CUSTOM_VALVE_FIRMWARE) && RK_CUSTOM_VALVE_FIRMWARE','')+'\n}'
a=source.index('void ui_artwork_process(');b=source.index('\n#endif',a);application=source[a:b].replace('#if defined(RK_CUSTOM_VALVE_FIRMWARE) && RK_CUSTOM_VALVE_FIRMWARE','')+'\n}'
preamble=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#include "controller_config.h"
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define LV_OBJ_FLAG_HIDDEN 1
#define ESP_LOGI(...) ((void)0)
static unsigned uxTaskGetStackHighWaterMark(void *task){(void)task;return 4096;}
static jmp_buf worker_stop;
struct fake_queue {unsigned count;size_t size;char data[2048];};
typedef struct fake_queue *QueueHandle_t;
static QueueHandle_t first_queue;
static QueueHandle_t xQueueCreate(unsigned capacity,size_t size){assert(capacity==1&&size<=2048);QueueHandle_t q=calloc(1,sizeof *q);q->size=size;if(!first_queue)first_queue=q;return q;}
static int xQueueOverwrite(QueueHandle_t q,const void *value){memcpy(q->data,value,q->size);q->count=1;return pdTRUE;}
static int xQueueReceive(QueueHandle_t q,void *value,unsigned wait){if(!q->count){if(wait==portMAX_DELAY)longjmp(worker_stop,1);return 0;}assert(wait==0||wait==portMAX_DELAY);memcpy(value,q->data,q->size);q->count=0;return pdTRUE;}
static void vQueueDelete(QueueHandle_t q){free(q);}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *ctx,unsigned priority,void *handle){(void)fn;(void)name;(void)ctx;(void)handle;assert(stack>=8192&&stack<=16384&&priority==2);return pdPASS;}
static uint32_t endpoint_generation=1;
static uint64_t milliseconds;
static unsigned fetches,applied;
static bool fail_fetch=true,change_endpoint,fail_apply,hidden;
static char s_last_image_key[128];
static int image_object;
static void *s_artwork_image=&image_object;
static struct {int dsc;} s_artwork_img;
static uint64_t platform_millis(void){return milliseconds;}
bool controller_config_capture_endpoint_token(controller_config_endpoint_token_t *out){*out=(controller_config_endpoint_token_t){.generation=endpoint_generation};return true;}
static bool bridge_client_get_current_zone_id(char *out,size_t capacity){assert(capacity>=6);strcpy(out,"sauna");return true;}
static bool bridge_client_is_bridge_connected(void){return true;}
static bool bridge_client_get_artwork_url(char *out,size_t capacity,int width,int height){assert(capacity>=32&&width==360&&height==360);strcpy(out,"http://bridge/image");return true;}
static int platform_http_get_dial_artwork(const char *url,char **pixels,size_t *length,bool (*current)(void *),void *ctx){(void)url;assert(current(ctx));fetches++;*pixels=NULL;*length=0;if(change_endpoint){endpoint_generation++;change_endpoint=false;}if(fail_fetch)return -1;*pixels=malloc(259200);assert(*pixels);*length=259200;return 0;}
static bool ui_rgb565_from_buffer(const uint8_t *pixels,int w,int h,void *image){(void)image;assert(pixels&&w==360&&h==360);return !fail_apply;}
static void lv_obj_add_flag(void *obj,unsigned flag){(void)obj;(void)flag;hidden=true;}
static void lv_obj_clear_flag(void *obj,unsigned flag){(void)obj;(void)flag;hidden=false;}
static void lv_image_set_src(void *obj,void *src){(void)obj;(void)src;applied++;}
static void lv_obj_set_size(void *obj,int w,int h){(void)obj;(void)w;(void)h;}
static void lv_obj_center(void *obj){(void)obj;}
static void lv_obj_invalidate(void *obj){(void)obj;}
"""
main=r"""
static void run_worker(void){assert(first_queue->count==1);if(!setjmp(worker_stop))artwork_worker(NULL);}
int main(void){
 artwork_init();ui_set_artwork("same");run_worker();ui_artwork_process();assert(fetches==1&&applied==0);
 milliseconds=4999;ui_artwork_process();assert(first_queue->count==0);
 milliseconds=5000;ui_artwork_process();assert(first_queue->count==1);fail_fetch=false;run_worker();ui_artwork_process();assert(fetches==2&&applied==1&&!strcmp(s_last_image_key,"same")&&!hidden);
 ui_set_artwork("same");assert(first_queue->count==0); /* displayed identity deduplicates */
 endpoint_generation++;ui_set_artwork("same");assert(first_queue->count==1);change_endpoint=true;run_worker();ui_artwork_process();assert(applied==1);
 milliseconds+=5000;ui_artwork_process();assert(first_queue->count==1);run_worker();ui_artwork_process();assert(applied==2);
 fail_apply=true;ui_set_artwork("apply-failure");run_worker();ui_artwork_process();milliseconds+=5000;ui_artwork_process();assert(first_queue->count==1);fail_apply=false;run_worker();ui_artwork_process();assert(applied==3&&!strcmp(s_last_image_key,"apply-failure"));
 fail_fetch=true;ui_set_artwork("bounded");unsigned began=fetches;for(unsigned i=0;i<3;i++){run_worker();ui_artwork_process();milliseconds+=5000;ui_artwork_process();}assert(fetches==began+3&&first_queue->count==0);milliseconds+=5000;ui_artwork_process();assert(first_queue->count==0);
 ui_set_artwork("");assert(hidden);puts("production artwork failed/stale/application same-key bounded recovery PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'worker.c';p.write_text(preamble+worker+admission+application+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Icommon','-Iinclude',str(p),'-o',directory+'/worker'],check=True)
 subprocess.run([directory+'/worker'],check=True)

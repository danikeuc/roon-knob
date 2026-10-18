#include "light_client_dial.h"
#include "light_config_dial.h"
#include "esp_http_client.h"
#include <assert.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
static unsigned reply_allocations; static bool allocation_fails;
void *heap_caps_calloc(size_t n,size_t size,unsigned caps){assert(n==1&&size==sizeof(light_reply_t));assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));reply_allocations++;return allocation_fails?NULL:calloc(n,size);}
#include <stdio.h>
#include <string.h>
static light_config_t stored={.url="http://pi:8081",.display="dddddddddddddddddddddddddddddddd",.settings="ssssssssssssssssssssssssssssssss"};
bool light_test_store_read(void *v,size_t n){memcpy(v,&stored,n);return true;}bool light_test_store_write(const void *v,size_t n){memcpy(&stored,v,n);return true;}bool light_test_store_clear(void){return true;}
static unsigned opened,writes,cleanups,callbacks;static int code;
struct fake_http {int placeholder;};static struct fake_http client;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->method==HTTP_METHOD_POST&&c->disable_auto_redirect);assert(!strcmp(c->url,"http://pi:8081/api/v1/lights/actions"));return &client;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value){(void)h;if(!strcmp(key,"Authorization"))assert(!strcmp(value,"Bearer dddddddddddddddddddddddddddddddd"));return 0;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms){(void)h;assert(ms>0&&ms<=2000);return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;assert(n>0);opened++;light_client_test_clock(6000);return 0;}
int esp_http_client_write(esp_http_client_handle_t h,const char *b,int n){(void)h;(void)b;writes++;return n;}
int esp_http_client_fetch_headers(esp_http_client_handle_t h){(void)h;return 0;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return 200;}
int esp_http_client_read(esp_http_client_handle_t h,char *b,int n){(void)h;(void)b;(void)n;return 0;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return true;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){(void)h;return 0;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){(void)h;cleanups++;return 0;}
static void complete(const light_reply_t *r,void *ctx){(void)ctx;callbacks++;code=r?r->http_status:0;}
int main(void){light_client_link(true);light_client_test_clock(1000);assert(light_client_action(LIGHT_ACTION_MANUAL_ON,1,0,complete,NULL));light_client_test_run();assert(opened==1&&writes==0&&cleanups==1&&callbacks==1&&code==503);light_client_test_run();assert(opened==1&&reply_allocations==1);allocation_fails=true;assert(light_client_action(LIGHT_ACTION_OFF,1,0,complete,NULL));light_client_test_run();assert(callbacks==2&&code==0&&opened==1&&reply_allocations==2);allocation_fails=false;assert(light_client_action(LIGHT_ACTION_OFF,1,0,complete,NULL));puts("production light socket: Bearer, redirect refusal, total deadline recheck before action body, one exchange/no retry PASS");}

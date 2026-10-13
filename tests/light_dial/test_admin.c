#include "esp_http_server.h"
#include "light_client_dial.h"
#include "light_config_dial.h"
#include "admin_auth_dial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static light_config_t stored;
bool light_test_store_read(void *v,size_t n){memcpy(v,&stored,n);return true;}bool light_test_store_write(const void *v,size_t n){memcpy(&stored,v,n);return true;}bool light_test_store_clear(void){memset(&stored,0,sizeof stored);return true;}
static int upstream_code=200;
static bool auth=true,delayed;static unsigned exchanges,async_begin,async_end,replies;static light_callback_t late_cb;static void *late_ctx;static bool (*late_guard)(void *);
bool admin_http_authorize(httpd_req_t *r,bool mutation){if(!auth){strcpy(r->status,"401 Unauthorized");return false;}if(mutation&&(!r->csrf||strcmp(r->csrf,"csrf"))){strcpy(r->status,"403 Forbidden");return false;}return true;}
bool admin_auth_session_valid(const char *s,bool touch){(void)s;(void)touch;return auth;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *key){const char *v=!strcmp(key,"Cookie")?r->cookie:r->type;return v?strlen(v):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t n){const char *v=!strcmp(key,"Cookie")?r->cookie:r->type;if(!v||strlen(v)>=n)return -1;strcpy(out,v);return 0;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t n){size_t left=r->content_len-r->offset;if(n>left)n=left;memcpy(out,r->body+r->offset,n);r->offset+=n;return (int)n;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){strcpy(r->status,s);return 0;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *s){(void)r;(void)key;(void)s;return 0;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){strcpy(r->response,s);replies++;return 0;}
esp_err_t httpd_req_async_handler_begin(httpd_req_t *r,httpd_req_t **out){*out=r;async_begin++;return 0;}
esp_err_t httpd_req_async_handler_complete(httpd_req_t *r){(void)r;async_end++;return 0;}
struct fake_semaphore {bool given;};
SemaphoreHandle_t xSemaphoreCreateBinary(void){return calloc(1,sizeof(struct fake_semaphore));}
int xSemaphoreGive(SemaphoreHandle_t s){s->given=true;return pdTRUE;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned n){(void)n;return s->given?pdTRUE:pdFALSE;}
void vSemaphoreDelete(SemaphoreHandle_t s){free(s);}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned size,void *ctx,unsigned priority,void *out){(void)name;(void)size;(void)priority;(void)out;fn(ctx);return pdPASS;}
void vTaskDelete(void *unused){(void)unused;}
bool light_write_valid(const char *kind,const char *body,size_t n){(void)kind;(void)n;return strstr(body,"expected_revision")!=NULL;}
bool light_client_request_guarded(const char *kind,const char *body,unsigned offset,light_callback_t cb,void *ctx,bool (*guard)(void *)){assert(async_begin>async_end);assert(guard(ctx));exchanges++;if(!strcmp(kind,"history"))assert(!body&&offset==20);if(delayed){late_cb=cb;late_ctx=ctx;late_guard=guard;return true;}light_reply_t result={.http_status=upstream_code};strcpy(result.body,"{\"confirmed\":true}");cb(&result,ctx);return true;}
#include "../../idf_app/main/light_admin_dial.c"
static httpd_req_t req(const char *uri,httpd_method_t method,const char *body){httpd_req_t r={.uri=uri,.method=method,.body=body,.content_len=body?strlen(body):0,.type="application/json",.csrf="csrf",.cookie="dial_session=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};return r;}
int main(void){
 httpd_req_t r=req("/admin/api/light/config",HTTP_PUT,"{\"url\":\"http://pi:8081\",\"display_token\":\"dddddddddddddddddddddddddddddddd\",\"settings_token\":\"ssssssssssssssssssssssssssssssss\"}");light_admin_handler(&r);assert(!strcmp(r.status,"200 OK")&&!exchanges&&!strstr(r.response,"dddd")&&!strstr(r.response,"ssss"));
 r=req("/admin/api/light/config",HTTP_GET,NULL);light_admin_handler(&r);assert(strstr(r.response,"http://pi:8081")&&!strstr(r.response,"token"));
 r=req("/admin/api/light/actions",HTTP_POST,"{\"expected_revision\":1}");r.csrf="bad";light_admin_handler(&r);assert(!strcmp(r.status,"403 Forbidden")&&!exchanges);
 auth=false;r=req("/admin/api/light/status",HTTP_GET,NULL);light_admin_handler(&r);assert(!strcmp(r.status,"401 Unauthorized")&&!exchanges);auth=true;
 r=req("/admin/api/light/history?offset=20",HTTP_GET,NULL);light_admin_handler(&r);assert(!strcmp(r.status,"200 OK")&&exchanges==1&&async_begin==1&&async_end==1);
 r=req("/admin/api/light/history?offset=1000001",HTTP_GET,NULL);light_admin_handler(&r);assert(!strcmp(r.status,"422 Unprocessable Entity")&&exchanges==1);
 upstream_code=401;r=req("/admin/api/light/status",HTTP_GET,NULL);light_admin_handler(&r);assert(!strcmp(r.status,"503 Service Unavailable")&&auth);upstream_code=200;
 delayed=true;r=req("/admin/api/light/actions",HTTP_POST,"{\"expected_revision\":1}");light_admin_handler(&r);assert(!strcmp(r.status,"503 Service Unavailable")&&async_begin==async_end);unsigned before=replies;assert(!late_guard(late_ctx));light_reply_t late={.http_status=200};strcpy(late.body,"{}");late_cb(&late,late_ctx);assert(replies==before);
 puts("production light admin: private config/redaction, no network on save, auth/CSRF before exchange, async handoff, timeout cancellation/late completion ownership PASS");
}

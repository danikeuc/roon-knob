/* Host-only HTTP transport and hardware/config dependencies. Production handlers
 * are compiled unchanged; counters expose forbidden pre-auth reads/writes. */
#include "http_fixture.h"
#include "admin_auth_dial.h"
#include "controller_config.h"
#include "http_server_lifecycle.h"
#include "rk_ble_hid_host.h"
#include "esp_netif.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
httpd_uri_t routes[40];
int route_count,fail_registration=-1,reads,writes,auth_queries;
char fixture_logs[8192];
bool configured,valid_session,storage_error;
admin_result_t next_auth=ADMIN_OK;
static http_server_owner_t owner;
static bool dns;
static int elapsed;
const char *fixture_session="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
void fixture_log(const char *format,...) {
    va_list args;
    va_start(args,format);
    size_t n=strlen(fixture_logs);
    vsnprintf(fixture_logs+n,sizeof(fixture_logs)-n,format,args);
    va_end(args);
}
void fixture_reset(void) {
    fixture_logs[0]=0;
    route_count=reads=writes=auth_queries=elapsed=0;
    fail_registration=-1;
    configured=true;
    valid_session=false;
    storage_error=false;
    next_auth=ADMIN_OK;
}
httpd_req_t fixture_request(const char *uri,httpd_method_t method,const char *body) {
    return (httpd_req_t) {
        .uri=uri,.method=method,.body=body?body:"",.content_len=body?(int)strlen(body):0,.host="192.168.4.1",.origin="http://192.168.4.1",.type="application/json"
    };
}
void fixture_call(httpd_req_t *r) {
    size_t path_len=strcspn(r->uri,"?");
    for(int i=0;i<route_count;i++)if(routes[i].method==r->method&&strlen(routes[i].uri)==path_len&&!strncmp(routes[i].uri,r->uri,path_len)) {
        r->user_ctx=routes[i].user_ctx;
        routes[i].handler(r);
        return;
    }
    for(int i=0;i<route_count;i++)if(routes[i].method==r->method&&!strcmp(routes[i].uri,"/*")) {
        routes[i].handler(r);
        return;
    }
    strcpy(r->status,"404 Not Found");
}
int fixture_status(const httpd_req_t *r) {
    return r->status[0]?atoi(r->status):200;
}
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t*c) {
    assert(c->max_uri_handlers>=route_count);
    *h=(void*)1;
    return 0;
}
esp_err_t httpd_stop(httpd_handle_t h) {
    (void)h;
    route_count=0;
    return 0;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t*u) {
    (void)h;
    if(route_count==fail_registration)return ESP_FAIL;
    routes[route_count++]=*u;
    return 0;
}
bool httpd_uri_match_wildcard(const char*a,const char*b,size_t n) {
    (void)a;
    (void)b;
    (void)n;
    return true;
}
esp_err_t httpd_resp_set_status(httpd_req_t*r,const char*s) {
    snprintf(r->status,sizeof r->status,"%s",s);
    return 0;
}
esp_err_t httpd_resp_set_type(httpd_req_t*r,const char*s) {
    (void)r;
    (void)s;
    return 0;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v) {
    if(!strcmp(k,"Set-Cookie"))snprintf(r->set_cookie,sizeof r->set_cookie,"%s",v);
    if(!strcmp(k,"Cache-Control"))snprintf(r->cache,sizeof r->cache,"%s",v);
    if(!strcmp(k,"Location"))snprintf(r->location,sizeof r->location,"%s",v);
    assert(strcmp(k,"Access-Control-Allow-Origin"));
    return 0;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t*r,const char*s,ssize_t n) {
    if(!s)return 0;
    if(n<0)n=(ssize_t)strlen(s);
    size_t pos=strlen(r->response);
    assert(pos+(size_t)n<sizeof r->response);
    memcpy(r->response+pos,s,(size_t)n);
    r->response[pos+(size_t)n]=0;
    return 0;
}
esp_err_t httpd_resp_send(httpd_req_t*r,const char*s,ssize_t n) {
    r->response[0]=0;
    return httpd_resp_send_chunk(r,s,n);
}
esp_err_t httpd_resp_sendstr(httpd_req_t*r,const char*s) {
    return httpd_resp_send(r,s,-1);
}
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t*r,const char*s) {
    return httpd_resp_send_chunk(r,s,-1);
}
esp_err_t httpd_resp_send_err(httpd_req_t*r,int code,const char*s) {
    snprintf(r->status,sizeof r->status,"%d",code);
    return httpd_resp_sendstr(r,s);
}
int httpd_req_recv(httpd_req_t*r,char*b,size_t n) {
    size_t left=(size_t)r->content_len-r->offset;
    if(n>left)n=left;
    if(n>7)n=7;
    memcpy(b,r->body+r->offset,n);
    r->offset+=n;
    return (int)n;
}
static const char *hdr(httpd_req_t*r,const char*k) {
    if(!strcmp(k,"Host"))return r->host;
    if(!strcmp(k,"Origin"))return r->origin;
    if(!strcmp(k,"Cookie"))return r->cookie;
    if(!strcmp(k,"X-CSRF-Token"))return r->csrf;
    if(!strcmp(k,"Content-Type"))return r->type;
    return NULL;
}
size_t httpd_req_get_hdr_value_len(httpd_req_t*r,const char*k) {
    const char*v=hdr(r,k);
    return v?strlen(v):0;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*b,size_t n) {
    const char*v=hdr(r,k);
    if(!v||strlen(v)>=n)return ESP_FAIL;
    strcpy(b,v);
    return 0;
}
esp_err_t httpd_req_get_url_query_str(httpd_req_t*r,char*b,size_t n) {
    (void)r;
    (void)b;
    (void)n;
    return ESP_FAIL;
}
esp_err_t httpd_query_key_value(const char*a,const char*b,char*c,size_t n) {
    (void)a;
    (void)b;
    (void)c;
    (void)n;
    return ESP_FAIL;
}
int httpd_req_to_sockfd(httpd_req_t*r) {
    (void)r;
    return 123;
}
int getsockname(int fd,struct sockaddr *out,socklen_t *len) {
    (void)fd;
    assert(*len>=sizeof(struct sockaddr_in));
    struct sockaddr_in a= {
        .sin_family=AF_INET,.sin_port=htons(80)
    };
    inet_pton(AF_INET,"192.168.4.1",&a.sin_addr);
    memcpy(out,&a,sizeof a);
    *len=sizeof a;
    return 0;
}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char*k) {
    (void)k;
    return (esp_netif_t*)1;
}
esp_err_t esp_netif_get_hostname(esp_netif_t*n,const char**h) {
    (void)n;
    *h="test-dial";
    return 0;
}
admin_result_t admin_auth_is_configured(bool*out) {
    auth_queries++;
    if(storage_error)return ADMIN_STORAGE_ERROR;
    *out=configured;
    return ADMIN_OK;
}
admin_result_t admin_auth_setup(const char*p,const char*r,char*out) {
    if(next_auth!=ADMIN_OK)return next_auth;
    if(strcmp(p,r))return ADMIN_INVALID;
    if(configured)return ADMIN_CONFLICT;
    configured=true;
    strcpy(out,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    return ADMIN_OK;
}
admin_result_t admin_auth_login(const char*p,char*out) {
    (void)p;
    if(storage_error)return ADMIN_STORAGE_ERROR;
    if(next_auth!=ADMIN_OK)return next_auth;
    valid_session=true;
    elapsed=0;
    strcpy(out,fixture_session);
    return ADMIN_OK;
}
bool admin_auth_session_valid(const char*s,bool touch) {
    bool ok=valid_session&&!strcmp(s,fixture_session)&&elapsed<900;
    if(ok&&touch)elapsed=0;
    return ok;
}
void fixture_expire(void) {
    elapsed=900;
}
void admin_auth_logout(const char*s) {
    (void)s;
    valid_session=false;
}
admin_result_t admin_auth_recover(const char*c,const char*p,const char*r,char*out) {
    (void)c;
    (void)p;
    (void)r;
    valid_session=false;
    if(next_auth!=ADMIN_OK)return next_auth;
    strcpy(out,"cccccccccccccccccccccccccccccccc");
    return ADMIN_OK;
}
admin_result_t admin_auth_change_pin(const char*s,const char*p,const char*r) {
    (void)s;
    (void)p;
    (void)r;
    valid_session=false;
    return next_auth;
}
admin_result_t admin_auth_reissue_recovery(const char*s,const char*p,char*out) {
    (void)s;
    (void)p;
    strcpy(out,"dddddddddddddddddddddddddddddddd");
    return next_auth;
}
bool http_server_lifecycle_lock(void) {
    return true;
}
void http_server_lifecycle_unlock(void) {
}
http_server_owner_t http_server_lifecycle_owner_locked(void) {
    return owner;
}
void http_server_lifecycle_claim_locked(http_server_owner_t o) {
    owner=o;
}
void http_server_lifecycle_release_locked(http_server_owner_t o) {
    if(owner==o)owner=0;
}
bool controller_config_snapshot(controller_config_snapshot_t*out) {
    reads++;
    memset(out,0,sizeof *out);
    strcpy(out->value.bridge_base,"http://test/' onfocus='alert(1)'><script>bad()</script>");
    return true;
}
bool controller_config_wifi_snapshot(controller_config_wifi_snapshot_t*out) {
    reads++;
    memset(out,0,sizeof *out);
    return true;
}
controller_config_write_result_t controller_config_set_endpoint(const char*a,bool b,controller_config_snapshot_t*c) {
    (void)a;
    (void)b;
    (void)c;
    writes++;
    return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}
controller_config_write_result_t controller_config_upsert_wifi(const char*a,const char*b,bool c,controller_config_snapshot_t*d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    writes++;
    return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}
controller_config_write_result_t controller_config_remove_wifi(size_t a,controller_config_snapshot_t*b) {
    (void)a;
    (void)b;
    writes++;
    return CONTROLLER_CONFIG_COMMITTED_VERIFIED;
}
void wifi_mgr_apply_wifi(const controller_config_wifi_snapshot_t*w,bool r) {
    (void)w;
    (void)r;
}
bool platform_mdns_resolve_local(const char*h,char*i,size_t n) {
    (void)h;
    (void)i;
    (void)n;
    return false;
}
bool bridge_client_is_bridge_connected(void) {
    return false;
}
int bridge_client_get_bridge_retry_count(void) {
    return 0;
}
int bridge_client_get_bridge_retry_max(void) {
    return 0;
}
bool valve_config_save(const char*a,const char*b) {
    (void)a;
    (void)b;
    writes++;
    return true;
}
bool valve_config_clear(void) {
    writes++;
    return true;
}
rk_ble_hid_host_result_t rk_ble_hid_host_status_copy(rk_ble_hid_host_status_t*out) {
    reads++;
    memset(out,0,sizeof *out);
    return 0;
}
size_t rk_ble_hid_host_scan_results_copy(rk_ble_hid_host_device_t*a,size_t b,uint32_t*c) {
    (void)a;
    (void)b;
    *c=0;
    return 0;
}
const char *rk_ble_hid_host_result_name(rk_ble_hid_host_result_t r) {
    (void)r;
    return "error";
}
const char *rk_ble_hid_host_error_name(rk_ble_hid_host_error_t r) {
    (void)r;
    return "error";
}
rk_ble_hid_host_result_t rk_ble_hid_host_set_enabled(bool b) {
    (void)b;
    writes++;
    return 0;
}
rk_ble_hid_host_result_t rk_ble_hid_host_scan_start(void) {
    writes++;
    return 0;
}
rk_ble_hid_host_result_t rk_ble_hid_host_pair(const rk_ble_hid_host_device_t*d) {
    (void)d;
    writes++;
    return 0;
}
rk_ble_hid_host_result_t rk_ble_hid_host_forget(void) {
    writes++;
    return 0;
}
bool dns_server_start(void) {
    dns=true;
    return true;
}
void dns_server_stop(void) {
    dns=false;
}
bool dns_server_is_running(void) {
    return dns;
}
void ui_update(const char*a,const char*b,bool c,float d,float e,float f,float g,int h,int i) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    (void)i;
}
void esp_restart(void) {
}
const char *esp_err_to_name(esp_err_t e) {
    (void)e;
    return "error";
}
void vTaskDelay(unsigned t) {
    (void)t;
}
void vTaskDelete(void*p) {
    (void)p;
}
int xTaskCreate(void(*f)(void*),const char*n,unsigned s,void*p,unsigned a,void*q) {
    (void)f;
    (void)n;
    (void)s;
    free(p);
    (void)a;
    (void)q;
    return 1;
}

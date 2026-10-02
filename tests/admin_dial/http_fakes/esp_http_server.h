#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef enum {HTTP_GET,HTTP_POST} httpd_method_t;
typedef struct httpd_req {const char *uri; size_t content_len; httpd_method_t method; void *user_ctx; const char *body,*cookie,*origin,*host,*csrf,*type; size_t offset; char response[24000],status[40],set_cookie[256],cache[40],location[160];} httpd_req_t;
typedef struct {const char *uri; httpd_method_t method; esp_err_t (*handler)(httpd_req_t*); void *user_ctx;} httpd_uri_t;
typedef struct {int server_port,max_uri_handlers,stack_size; bool (*uri_match_fn)(const char*,const char*,size_t);} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){.server_port=80})
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_start(httpd_handle_t*,const httpd_config_t*);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
bool httpd_uri_match_wildcard(const char*,const char*,size_t);
esp_err_t httpd_resp_set_status(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_type(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
esp_err_t httpd_resp_send(httpd_req_t*,const char*,ssize_t);
esp_err_t httpd_resp_sendstr(httpd_req_t*,const char*);
esp_err_t httpd_resp_send_chunk(httpd_req_t*,const char*,ssize_t);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t*,const char*);
esp_err_t httpd_resp_send_err(httpd_req_t*,int,const char*);
int httpd_req_recv(httpd_req_t*,char*,size_t);
size_t httpd_req_get_hdr_value_len(httpd_req_t*,const char*);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
esp_err_t httpd_req_get_url_query_str(httpd_req_t*,char*,size_t);
esp_err_t httpd_query_key_value(const char*,const char*,char*,size_t);
int httpd_req_to_sockfd(httpd_req_t*);

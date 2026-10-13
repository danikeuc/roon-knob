#pragma once
#include "../../admin_dial/http_fakes/esp_http_server.h"
esp_err_t httpd_req_async_handler_begin(httpd_req_t *,httpd_req_t **);
esp_err_t httpd_req_async_handler_complete(httpd_req_t *);

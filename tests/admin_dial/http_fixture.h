#pragma once
#include "esp_http_server.h"
#include "admin_auth_dial.h"
extern httpd_uri_t routes[40];
extern int route_count,fail_registration,reads,writes,auth_queries;
extern bool configured,valid_session,storage_error;
extern admin_result_t next_auth;
extern const char *fixture_session;
void fixture_reset(void);
void fixture_expire(void);
httpd_req_t fixture_request(const char*,httpd_method_t,const char*);
void fixture_call(httpd_req_t*);
int fixture_status(const httpd_req_t*);

extern char fixture_logs[8192];

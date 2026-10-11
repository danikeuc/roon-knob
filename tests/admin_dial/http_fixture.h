#pragma once
#include "esp_http_server.h"
#include "admin_auth_dial.h"
#include "rk_ble_hid_host.h"
extern rk_ble_hid_host_status_t fixture_ble_status;
extern httpd_uri_t routes[40];
extern int route_count,fail_registration,reads,writes,auth_queries,auth_mutations;
extern int fixture_http_stack_size;
extern bool configured,valid_session,storage_error;
extern admin_result_t next_auth;
extern const char *fixture_session;
void fixture_reset(void);
void fixture_expire(void);
httpd_req_t fixture_request(const char*,httpd_method_t,const char*);
void fixture_call(httpd_req_t*);
int fixture_status(const httpd_req_t*);

extern char fixture_logs[8192];

extern bool fixture_socket_ipv6;
extern const char *fixture_socket_address;

extern bool fixture_zones_online, fixture_zone_persist, fixture_zone_queue, fixture_zone_delay;
extern char fixture_current_zone[64];
extern int fixture_zone_changes;
void fixture_run_zone_job(void);

#pragma once
#include "esp_http_server.h"
#include "admin_auth_dial.h"
#include <stdint.h>
/* Registration is nonblocking with respect to auth/storage. The owning server
 * must stop on any registration failure. No server handle is retained here. */
esp_err_t admin_server_register(httpd_handle_t server);
/* HTTP task only: may wait on auth/storage. Sends denial response itself. */
bool admin_http_authorize(httpd_req_t *req, bool mutation);
/* Task 6 binds static callbacks before starting either HTTP server. The adapter
 * owns CAS, persistence/readback and application to UI/defaults. ADMIN_OK means
 * out is the confirmed applied snapshot, never merely queued work. No callback
 * may issue a valve command or alter an active deadline. */
typedef struct {
    uint16_t duration_minutes;       /* 1..10 */
    uint16_t rotation_degrees;       /* effective 0/90/180/270, including legacy */
    uint32_t generation;
} admin_http_settings_t;
typedef struct {
    admin_result_t (*read)(admin_http_settings_t *out);
    admin_result_t (*shower)(uint16_t minutes, uint32_t expected_generation,
                             admin_http_settings_t *out);
    admin_result_t (*rotation)(uint16_t degrees, uint32_t expected_generation,
                               admin_http_settings_t *out);
} admin_settings_binding_t;
void admin_server_bind_settings(const admin_settings_binding_t *binding);

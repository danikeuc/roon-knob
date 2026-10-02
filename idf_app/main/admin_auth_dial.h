#pragma once
#include <stdbool.h>
typedef enum {
    ADMIN_OK, ADMIN_INVALID, ADMIN_DENIED, ADMIN_LOCKED,
    ADMIN_CONFLICT, ADMIN_STORAGE_ERROR
} admin_result_t;
/* Blocking APIs for HTTP/background tasks. Crypto executes on a bounded worker.
 * UI integrations must enqueue work and return to LVGL before awaiting results.
 * Output buffers must have capacities 33 (recovery) / 65 (session) bytes. */
admin_result_t admin_auth_setup(const char *pin, const char *repeated_pin, char recovery_out[33]);
admin_result_t admin_auth_login(const char *pin, char session_out[65]);
admin_result_t admin_auth_recover(const char *code, const char *new_pin, const char *repeated_pin, char recovery_out[33]);
admin_result_t admin_auth_change_pin(const char *session, const char *new_pin, const char *repeated_pin);
admin_result_t admin_auth_reissue_recovery(const char *session, const char *pin, char recovery_out[33]);
bool admin_auth_session_valid(const char *session, bool touch);
void admin_auth_logout(const char *session);
/* Confirmed persistent state; errors never imply first setup is available. */
admin_result_t admin_auth_is_configured(bool *configured);

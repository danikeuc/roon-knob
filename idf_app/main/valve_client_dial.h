#ifndef VALVE_CLIENT_DIAL_H
#define VALVE_CLIENT_DIAL_H
#include "valve_logic.h"
typedef enum { VALVE_CLIENT_OK, VALVE_CLIENT_UNAUTHORIZED, VALVE_CLIENT_UNAVAILABLE, VALVE_CLIENT_INVALID,
               VALVE_CLIENT_CONFLICT, VALVE_CLIENT_SERVER_ERROR, VALVE_CLIENT_TIMEOUT } valve_client_result_t;
typedef int (*valve_client_transport_fn)(const char *method, const char *url,
                                          const char *token, int *http_status,
                                          char *response, size_t response_cap,
                                          size_t *response_len, void *context);
typedef enum { VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_EVENT_POST_STATUS,
               VALVE_CLIENT_EVENT_RECOVERY_GET } valve_client_event_kind_t;
typedef struct {
    valve_client_result_t result;
    valve_status_t status;
    uint32_t request_id;
    uint32_t session;
    valve_client_event_kind_t kind;
} valve_client_event_t;
typedef void (*valve_client_callback_fn)(const valve_client_event_t *event, void *context);
/* Synchronous calls execute only on the client worker task in firmware.
 * Use request_get/request_post from UI tasks. Callback runs on the worker and
 * must copy the status before returning if it needs it later, then hand the
 * copy to the UI task before touching widgets. The event pointer is valid
 * only until the callback returns. Each event carries its originating Wi-Fi
 * session and request ID, including a recovery GET after a failed POST. */
valve_client_result_t valve_client_get_status(valve_status_t *out);
valve_client_result_t valve_client_post(valve_action_t action, valve_status_t *out);
bool valve_client_start(valve_client_callback_fn callback, void *context);
bool valve_client_request_get(void);
bool valve_client_request_post(valve_action_t action);
bool valve_client_request_get_tagged(uint32_t *request_id);
bool valve_client_request_post_tagged(valve_action_t action, uint32_t *request_id);
uint32_t valve_client_current_session(void);
void valve_client_on_disconnect(void);
void valve_client_on_reconnect(void);
#ifdef VALVE_CLIENT_HOST_TEST
void valve_client_test_transport(valve_client_transport_fn transport, void *context);
void valve_client_test_run_pending(void);
#endif
#endif

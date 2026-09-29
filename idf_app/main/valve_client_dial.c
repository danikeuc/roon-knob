#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include <stdio.h>
#include <string.h>
#ifndef VALVE_CLIENT_HOST_TEST
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

#define RESPONSE_CAP 2048
#define STATUS_ROUTE "/api/v1/display/status"
#define START_ROUTE "/api/v1/display/actions/timed-shower"
#define DRAIN_ROUTE "/api/v1/display/actions/drain"

typedef struct work_item {
    bool post;
    valve_action_t action;
    uint32_t session;
} work_item_t;
static bool s_connected;
static uint32_t s_session;
#ifndef VALVE_CLIENT_HOST_TEST
static portMUX_TYPE s_session_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static bool session_snapshot(uint32_t *session) {
#ifndef VALVE_CLIENT_HOST_TEST
    portENTER_CRITICAL(&s_session_lock);
#endif
    bool connected = s_connected;
    *session = s_session;
#ifndef VALVE_CLIENT_HOST_TEST
    portEXIT_CRITICAL(&s_session_lock);
#endif
    return connected;
}
static void session_change(bool connected) {
#ifndef VALVE_CLIENT_HOST_TEST
    portENTER_CRITICAL(&s_session_lock);
#endif
    s_connected = connected;
    s_session++;
#ifndef VALVE_CLIENT_HOST_TEST
    portEXIT_CRITICAL(&s_session_lock);
#endif
}
static valve_client_callback_fn s_callback;
static void *s_callback_context;

static bool post_allowed(const work_item_t *item) {
    uint32_t current;
    return session_snapshot(&current) && item->session == current;
}

#ifdef VALVE_CLIENT_HOST_TEST
static valve_client_transport_fn s_transport;
static void *s_transport_context;
static bool s_pending_get;
static struct work_item s_host_queue[8];
static unsigned s_host_head, s_host_count;
void valve_client_test_transport(valve_client_transport_fn transport, void *context) {
    s_transport = transport; s_transport_context = context;
}
#else
static QueueHandle_t s_queue;
static TaskHandle_t s_worker;

static int esp_transport(const char *method, const char *url, const char *token,
                         int *http_status, char *response, size_t cap,
                         size_t *response_len, void *context) {
    (void)context;
    esp_http_client_config_t config = {
        .url = url, .method = strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;
    int result = -1;
    if (esp_http_client_set_header(client, "X-Display-Token", token) != ESP_OK ||
        esp_http_client_set_header(client, "Accept", "application/json") != ESP_OK) goto done;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) { result = err == ESP_ERR_TIMEOUT ? -3 : -1; goto done; }
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) goto done;
    *http_status = esp_http_client_get_status_code(client);
    if (*http_status != 200) { result = 0; goto done; }
    if ((size_t)content_length > cap) { result = -2; goto done; }
    size_t used = 0;
    while (used < cap) {
        int n = esp_http_client_read(client, response + used, cap - used);
        if (n < 0) { result = -3; goto done; }
        if (n == 0) { *response_len = used; result = 0; goto done; }
        used += (size_t)n;
    }
    char extra;
    if (esp_http_client_read(client, &extra, 1) == 0) {
        *response_len = used; result = 0;
    } else result = -2;
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}
#endif

static valve_client_result_t request(const char *method, const char *route,
                                     bool parse_status, valve_status_t *out) {
    if (!out) return VALVE_CLIENT_INVALID;
    memset(out, 0, sizeof *out); out->state = VALVE_UNKNOWN;
#ifndef VALVE_CLIENT_HOST_TEST
    if (!s_worker || xTaskGetCurrentTaskHandle() != s_worker) return VALVE_CLIENT_INVALID;
#endif
    char url_base[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
    if (!valve_config_load(url_base, sizeof url_base, token, sizeof token))
        return VALVE_CLIENT_INVALID;
    char url[VALVE_CONFIG_URL_LEN + sizeof START_ROUTE];
    int n = snprintf(url, sizeof url, "%s%s", url_base, route);
    if (n < 0 || (size_t)n >= sizeof url) { memset(token, 0, sizeof token); return VALVE_CLIENT_INVALID; }
    char response[RESPONSE_CAP]; size_t response_len = 0; int http_status = 0;
#ifdef VALVE_CLIENT_HOST_TEST
    int rc = s_transport ? s_transport(method, url, token, &http_status, response,
                                        sizeof response, &response_len, s_transport_context) : -1;
#else
    int rc = esp_transport(method, url, token, &http_status, response,
                           sizeof response, &response_len, NULL);
#endif
    memset(token, 0, sizeof token);
    if (rc == -2 || response_len > sizeof response) return VALVE_CLIENT_INVALID;
    if (rc == -3) return VALVE_CLIENT_TIMEOUT;
    if (rc != 0) return VALVE_CLIENT_UNAVAILABLE;
    if (http_status == 401) return VALVE_CLIENT_UNAUTHORIZED;
    if (http_status == 409) return VALVE_CLIENT_CONFLICT;
    if (http_status >= 500) return VALVE_CLIENT_SERVER_ERROR;
    if (http_status != 200) return VALVE_CLIENT_INVALID;
    if (parse_status && !valve_status_parse(response, response_len, out))
        return VALVE_CLIENT_INVALID;
    return VALVE_CLIENT_OK;
}
valve_client_result_t valve_client_get_status(valve_status_t *out) {
    return request("GET", STATUS_ROUTE, true, out);
}
valve_client_result_t valve_client_post(valve_action_t action, valve_status_t *out) {
    if (!out) return VALVE_CLIENT_INVALID;
    memset(out, 0, sizeof *out); out->state = VALVE_UNKNOWN;
    const char *route = action == VALVE_ACTION_START_600S ? START_ROUTE :
                        action == VALVE_ACTION_DRAIN ? DRAIN_ROUTE : NULL;
    if (!route) return VALVE_CLIENT_INVALID;
    valve_client_result_t result = request("POST", route, false, out);
    if (result != VALVE_CLIENT_OK) {
#ifdef VALVE_CLIENT_HOST_TEST
        if (result != VALVE_CLIENT_INVALID) s_pending_get = true;
#endif
        return result;
    }
    result = valve_client_get_status(out);
#ifdef VALVE_CLIENT_HOST_TEST
    if (result != VALVE_CLIENT_OK) s_pending_get = true;
#endif
    return result;
}
static void dispatch(const work_item_t *work) {
    if (work->post && !post_allowed(work)) return;
    uint32_t current;
    if (!session_snapshot(&current)) return;
    valve_status_t status;
    valve_client_result_t result = work->post ? valve_client_post(work->action, &status)
                                               : valve_client_get_status(&status);
    if (s_callback) s_callback(result, &status, s_callback_context);
    if (work->post && result != VALVE_CLIENT_OK && session_snapshot(&current)) {
        valve_status_t reconciled;
        valve_client_result_t check = valve_client_get_status(&reconciled);
        if (s_callback) s_callback(check, &reconciled, s_callback_context);
    }
}
static void clear_queue(void) {
#ifdef VALVE_CLIENT_HOST_TEST
    s_host_head = 0;
    s_host_count = 0;
#else
    if (s_queue) xQueueReset(s_queue);
#endif
}
void valve_client_on_disconnect(void) {
    session_change(false);
    clear_queue();
}
void valve_client_on_reconnect(void) {
    session_change(false);
    clear_queue();
    session_change(true);
    (void)valve_client_request_get();
}
#ifdef VALVE_CLIENT_HOST_TEST
static bool host_enqueue(work_item_t item) {
    if (s_host_count >= sizeof s_host_queue / sizeof s_host_queue[0]) return false;
    s_host_queue[(s_host_head + s_host_count) % 8] = item;
    s_host_count++;
    return true;
}
void valve_client_test_run_pending(void) {
    if (s_pending_get) {
        s_pending_get = false;
        valve_status_t status;
        (void)valve_client_get_status(&status);
    }
    while (s_host_count) {
        work_item_t item = s_host_queue[s_host_head];
        s_host_head = (s_host_head + 1) % 8;
        s_host_count--;
        dispatch(&item);
    }
}
bool valve_client_start(valve_client_callback_fn callback, void *context) {
    s_callback = callback; s_callback_context = context; return true;
}
bool valve_client_request_get(void) {
    uint32_t session;
    if (!session_snapshot(&session)) return false;
    return host_enqueue((work_item_t){.post = false, .session = session});
}
bool valve_client_request_post(valve_action_t action) {
    uint32_t session;
    if (!session_snapshot(&session) ||
        (action != VALVE_ACTION_START_600S && action != VALVE_ACTION_DRAIN)) return false;
    return host_enqueue((work_item_t){.post = true, .action = action, .session = session});
}
#else
static void worker_task(void *context) {
    (void)context;
    s_worker = xTaskGetCurrentTaskHandle();
    work_item_t work;
    for (;;) {
        if (xQueueReceive(s_queue, &work, portMAX_DELAY) != pdTRUE) continue;
        dispatch(&work);
    }
}
bool valve_client_start(valve_client_callback_fn callback, void *context) {
    if (s_queue) return false;
    s_callback = callback; s_callback_context = context;
    s_queue = xQueueCreate(4, sizeof(work_item_t));
    if (!s_queue) return false;
    if (xTaskCreate(worker_task, "valve_http", 6144, NULL, 4, &s_worker) != pdPASS) {
        vQueueDelete(s_queue); s_queue = NULL; return false;
    }
    return true;
}
bool valve_client_request_get(void) {
    uint32_t session;
    if (!s_queue || !session_snapshot(&session)) return false;
    work_item_t item = {.post = false, .session = session};
    return xQueueSend(s_queue, &item, 0) == pdTRUE;
}
bool valve_client_request_post(valve_action_t action) {
    uint32_t session;
    if (!s_queue || !session_snapshot(&session) ||
        (action != VALVE_ACTION_START_600S && action != VALVE_ACTION_DRAIN)) return false;
    work_item_t item = {.post = true, .action = action, .session = session};
    return xQueueSend(s_queue, &item, 0) == pdTRUE;
}
#endif

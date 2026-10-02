#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#ifndef VALVE_CLIENT_HOST_TEST
#include "admin_store_dial.h"
#endif
#ifndef VALVE_CLIENT_HOST_TEST
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

#define RESPONSE_CAP 2048
#define VALVE_HTTP_STACK_SIZE (12 * 1024)
#define STATUS_ROUTE "/api/v1/display/status"
#define START_ROUTE "/api/v1/display/actions/timed-shower"
#define DRAIN_ROUTE "/api/v1/display/actions/drain"
#define START_MAX_QUEUED_MS UINT64_C(10000)

typedef struct work_item {
    bool post;
    valve_request_t request;
    uint32_t session;
    uint32_t config_generation;
    uint32_t request_id;
    uint64_t enqueued_ms;
} work_item_t;
static bool s_connected;
static uint32_t s_session;
static uint32_t s_next_request_id;
static atomic_uint s_selected_duration = ATOMIC_VAR_INIT(0);
#ifdef VALVE_CLIENT_HOST_TEST
static uint64_t (*s_test_clock_ms)(void);
static uint64_t monotonic_ms(void) { return s_test_clock_ms ? s_test_clock_ms() : 0; }
#else
static uint64_t monotonic_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
#endif
bool valve_client_selected_duration_get(uint16_t *seconds) {
    unsigned value = atomic_load(&s_selected_duration);
    if (!seconds || value < 60 || value > 600 || value % 60) return false;
    *seconds = (uint16_t)value;
    return true;
}
static void finish_duration_load(uint16_t seconds) {
    if (seconds < 60 || seconds > 600 || seconds % 60) return;
    unsigned expected = 0;
    (void)atomic_compare_exchange_strong(&s_selected_duration, &expected, seconds);
}
bool valve_client_selected_duration_publish(uint16_t seconds) {
    if (seconds < 60 || seconds > 600 || seconds % 60) return false;
    atomic_store(&s_selected_duration, seconds);
    return true;
}
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
uint32_t valve_client_current_session(void) {
    uint32_t session;
    (void)session_snapshot(&session);
    return session;
}
static bool prepare_work(bool post, const valve_request_t *request, work_item_t *work,
                         uint32_t *request_id) {
    if (!work || !request || (post && request->action != VALVE_ACTION_START &&
        request->action != VALVE_ACTION_DRAIN) ||
        (post && request->action == VALVE_ACTION_START &&
         (request->duration_seconds < 60 || request->duration_seconds > 600 ||
          request->duration_seconds % 60))) return false;
#ifndef VALVE_CLIENT_HOST_TEST
    portENTER_CRITICAL(&s_session_lock);
#endif
    bool connected = s_connected && !(valve_config_generation() & 1u);
    if (connected) {
        work->post = post;
        work->request = *request;
        work->session = s_session;
        work->config_generation = valve_config_generation();
        work->request_id = ++s_next_request_id;
        work->enqueued_ms = monotonic_ms();
        if (work->request_id == 0) work->request_id = ++s_next_request_id;
        if (request_id) *request_id = work->request_id;
    }
#ifndef VALVE_CLIENT_HOST_TEST
    portEXIT_CRITICAL(&s_session_lock);
#endif
    return connected;
}
static valve_client_callback_fn s_callback;
static void *s_callback_context;

static bool work_allowed(const work_item_t *item) {
    uint32_t current;
    return session_snapshot(&current) && item->session == current &&
           !(item->config_generation & 1u) &&
           item->config_generation == valve_config_generation();
}
static bool start_expired(const work_item_t *item) {
    if (!item || !item->post || item->request.action != VALVE_ACTION_START) return false;
    uint64_t now = monotonic_ms();
    return now < item->enqueued_ms || now - item->enqueued_ms >= START_MAX_QUEUED_MS;
}

#ifdef VALVE_CLIENT_HOST_TEST
static valve_client_transport_fn s_transport;
static void *s_transport_context;
static bool s_pending_get;
static struct work_item s_host_queue[8];
static unsigned s_host_head, s_host_count;
void valve_client_test_finish_duration_load(uint16_t seconds) { finish_duration_load(seconds); }
void valve_client_test_transport(valve_client_transport_fn transport, void *context) {
    s_transport = transport; s_transport_context = context;
}
void valve_client_test_clock(uint64_t (*clock_ms)(void)) { s_test_clock_ms = clock_ms; }
#else
static QueueHandle_t s_queue;
static TaskHandle_t s_worker;
static const char *TAG = "valve_client";

static int esp_transport(const char *method, const char *url, const char *token,
                         const char *body, size_t body_len, int *http_status, char *response, size_t cap,
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
    if (body_len && esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK) goto done;
    esp_err_t err = esp_http_client_open(client, (int)body_len);
    if (err != ESP_OK) { result = err == ESP_ERR_TIMEOUT ? -3 : -1; goto done; }
    if (body_len && esp_http_client_write(client, body, (int)body_len) != (int)body_len) goto done;
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

typedef struct {
    char url[VALVE_CONFIG_URL_LEN];
    char token[VALVE_CONFIG_TOKEN_LEN];
    uint32_t generation;
} credentials_t;
static bool credentials_load(credentials_t *credentials) {
    credentials->generation = valve_config_generation();
    return !(credentials->generation & 1u) &&
        valve_config_load(credentials->url, sizeof credentials->url,
                          credentials->token, sizeof credentials->token) &&
        credentials->generation == valve_config_generation();
}
static valve_client_result_t request(const char *method, const char *route,
                                     const char *body, size_t body_len,
                                     bool parse_status, valve_status_t *out,
                                     const credentials_t *credentials) {
    if (!out) return VALVE_CLIENT_INVALID;
    memset(out, 0, sizeof *out); out->state = VALVE_UNKNOWN;
#ifndef VALVE_CLIENT_HOST_TEST
    if (!s_worker || xTaskGetCurrentTaskHandle() != s_worker) return VALVE_CLIENT_INVALID;
#endif
    if (credentials->generation != valve_config_generation() ||
        (credentials->generation & 1u)) return VALVE_CLIENT_INVALID;
    char url[VALVE_CONFIG_URL_LEN + sizeof START_ROUTE];
    int n = snprintf(url, sizeof url, "%s%s", credentials->url, route);
    if (n < 0 || (size_t)n >= sizeof url) return VALVE_CLIENT_INVALID;
    char response[RESPONSE_CAP]; size_t response_len = 0; int http_status = 0;
#ifdef VALVE_CLIENT_HOST_TEST
    int rc = s_transport ? s_transport(method, url, credentials->token, body, body_len, &http_status, response,
                                        sizeof response, &response_len, s_transport_context) : -1;
#else
    int rc = esp_transport(method, url, credentials->token, body, body_len, &http_status, response,
                           sizeof response, &response_len, NULL);
#endif
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
static valve_client_result_t post_snapshot(const valve_request_t *action, valve_status_t *out,
                                           const credentials_t *credentials,
                                           const work_item_t *work) {
    if (!out || !action) return VALVE_CLIENT_INVALID;
    memset(out, 0, sizeof *out); out->state = VALVE_UNKNOWN;
    const char *route = action->action == VALVE_ACTION_START ? START_ROUTE :
                        action->action == VALVE_ACTION_DRAIN ? DRAIN_ROUTE : NULL;
    if (!route) return VALVE_CLIENT_INVALID;
    char body[32] = {0}; size_t body_len = 0;
    if (action->action == VALVE_ACTION_START) {
        if (action->duration_seconds < 60 || action->duration_seconds > 600 ||
            action->duration_seconds % 60) return VALVE_CLIENT_INVALID;
        valve_status_t preflight;
        valve_client_result_t check = request("GET", STATUS_ROUTE, NULL, 0, true,
                                              &preflight, credentials);
        if (check != VALVE_CLIENT_OK) return check;
        if (preflight.state != VALVE_DRAIN || (work && !work_allowed(work)) ||
            credentials->generation != valve_config_generation()) return VALVE_CLIENT_INVALID;
        if (preflight.timed_shower_duration_supported) {
            int n = snprintf(body, sizeof body, "{\"duration_seconds\":%u}",
                             action->duration_seconds);
            if (n < 0 || (size_t)n >= sizeof body) return VALVE_CLIENT_INVALID;
            body_len = (size_t)n;
        } else if (action->duration_seconds != 600) return VALVE_CLIENT_INVALID;
        if (start_expired(work)) return VALVE_CLIENT_TIMEOUT;
    }
    valve_client_result_t result = request("POST", route, body_len ? body : NULL,
                                           body_len, false, out, credentials);
    if (result == VALVE_CLIENT_OK)
        result = request("GET", STATUS_ROUTE, NULL, 0, true, out, credentials);
    return result;
}
valve_client_result_t valve_client_get_status(valve_status_t *out) {
    credentials_t credentials = {0};
    valve_client_result_t result = credentials_load(&credentials) ?
        request("GET", STATUS_ROUTE, NULL, 0, true, out, &credentials) : VALVE_CLIENT_INVALID;
    memset(&credentials, 0, sizeof credentials);
    return result;
}
valve_client_result_t valve_client_post(const valve_request_t *action, valve_status_t *out) {
    credentials_t credentials = {0};
    valve_client_result_t result = credentials_load(&credentials) ?
        post_snapshot(action, out, &credentials, NULL) : VALVE_CLIENT_INVALID;
#ifdef VALVE_CLIENT_HOST_TEST
    if (result != VALVE_CLIENT_OK && result != VALVE_CLIENT_INVALID) s_pending_get = true;
#endif
    memset(&credentials, 0, sizeof credentials);
    return result;
}
static void dispatch(const work_item_t *work) {
    if (!work_allowed(work)) return;
    credentials_t credentials = {0};
    if (!credentials_load(&credentials) || credentials.generation != work->config_generation)
        goto done;
    uint32_t current;
    if (!session_snapshot(&current)) goto done;
    valve_status_t status;
    valve_client_result_t result;
    if (start_expired(work)) {
        memset(&status, 0, sizeof status);
        status.state = VALVE_UNKNOWN;
        result = VALVE_CLIENT_TIMEOUT;
    } else {
        result = work->post ? post_snapshot(&work->request, &status, &credentials, work)
            : request("GET", STATUS_ROUTE, NULL, 0, true, &status, &credentials);
    }
    if (!work_allowed(work)) goto done;
    if (s_callback) {
        valve_client_event_t event = {.result = result, .status = status,
            .request_id = work->request_id, .session = work->session,
            .config_generation = work->config_generation,
            .kind = work->post ? VALVE_CLIENT_EVENT_POST_STATUS : VALVE_CLIENT_EVENT_GET};
        s_callback(&event, s_callback_context);
    }
    if (work->post && result != VALVE_CLIENT_OK &&
        session_snapshot(&current) && current == work->session) {
        valve_status_t reconciled;
        valve_client_result_t check = request("GET", STATUS_ROUTE, NULL, 0, true, &reconciled, &credentials);
        if (!work_allowed(work)) goto done;
        if (s_callback) {
            valve_client_event_t event = {.result = check, .status = reconciled,
                .request_id = work->request_id, .session = work->session,
                .config_generation = work->config_generation,
                .kind = VALVE_CLIENT_EVENT_RECOVERY_GET};
            s_callback(&event, s_callback_context);
        }
    }
done:
    memset(&credentials, 0, sizeof credentials);
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
    return valve_client_request_get_tagged(NULL);
}
bool valve_client_request_get_tagged(uint32_t *request_id) {
    work_item_t item;
    return prepare_work(false, &(valve_request_t){.action = VALVE_ACTION_DRAIN}, &item, request_id) && host_enqueue(item);
}
bool valve_client_request_post_for_config(const valve_request_t *request, uint32_t generation, uint32_t *request_id) {
    work_item_t item;
    return prepare_work(true, request, &item, request_id) &&
           item.config_generation == generation && host_enqueue(item);
}
#else
static void worker_task(void *context) {
    (void)context;
    s_worker = xTaskGetCurrentTaskHandle();
    admin_settings_t settings;
    if (admin_store_load(&settings) == ESP_OK) {
        finish_duration_load(settings.duration_seconds);
    }
    work_item_t work;
    UBaseType_t lowest_free_bytes = VALVE_HTTP_STACK_SIZE;
    for (;;) {
        if (xQueueReceive(s_queue, &work, portMAX_DELAY) != pdTRUE) continue;
        dispatch(&work);
        UBaseType_t free_bytes = uxTaskGetStackHighWaterMark(NULL);
        if (free_bytes < lowest_free_bytes) {
            lowest_free_bytes = free_bytes;
            ESP_LOGI(TAG, "valve_http stack high-water mark: %u/%u bytes free",
                     (unsigned)free_bytes, VALVE_HTTP_STACK_SIZE);
        }
    }
}
bool valve_client_start(valve_client_callback_fn callback, void *context) {
    if (s_queue) return false;
    s_callback = callback; s_callback_context = context;
    s_queue = xQueueCreate(4, sizeof(work_item_t));
    if (!s_queue) return false;
    if (xTaskCreate(worker_task, "valve_http", VALVE_HTTP_STACK_SIZE,
                    NULL, 4, &s_worker) != pdPASS) {
        vQueueDelete(s_queue); s_queue = NULL; return false;
    }
    return true;
}
bool valve_client_request_get(void) {
    return valve_client_request_get_tagged(NULL);
}
bool valve_client_request_get_tagged(uint32_t *request_id) {
    work_item_t item;
    return s_queue && prepare_work(false, &(valve_request_t){.action = VALVE_ACTION_DRAIN}, &item, request_id) &&
           xQueueSend(s_queue, &item, 0) == pdTRUE;
}
bool valve_client_request_post_for_config(const valve_request_t *request, uint32_t generation, uint32_t *request_id) {
    work_item_t item;
    return s_queue && prepare_work(true, request, &item, request_id) &&
           item.config_generation == generation && xQueueSend(s_queue, &item, 0) == pdTRUE;
}
#endif

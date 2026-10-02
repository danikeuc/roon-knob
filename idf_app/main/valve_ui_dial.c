#include "valve_ui_dial.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#ifdef VALVE_UI_HOST_TEST
#include <pthread.h>
#endif

#define POLL_MS 5000u
#define STALE_MS 10000u
#define SCREEN_COLOR 0x05080Bu
#define ICON_COLOR 0xB8EBFFu
#define ACTIVE_COLOR 0x48D7E8u
#define ACTION_DARK_COLOR 0x141C22u

typedef struct { valve_client_event_t event; uint64_t received_at_ms; } completion_t;
static QueueHandle_t s_completions;
static lv_obj_t *s_overlay, *s_state, *s_countdown, *s_action, *s_action_label;
static lv_obj_t *s_temperature;
static lv_obj_t *s_rain, *s_snow, *s_relay_left, *s_relay_right;
static valve_status_t s_status;
static valve_hold_t s_hold;
static uint32_t s_status_at, s_last_get;
static bool s_visible, s_configured, s_touch_supply, s_touch_drain;
static bool s_touch_target_initialized;
static atomic_bool s_connected = ATOMIC_VAR_INIT(false);
static atomic_uint s_link_epoch = ATOMIC_VAR_INIT(0);
static atomic_bool s_completion_overflow = ATOMIC_VAR_INIT(false);
static unsigned s_processed_epoch;
static uint32_t s_pending_since;
static valve_observation_gate_t s_gate;
static bool s_recovery_needed;
static bool s_touch_moved;
static uint32_t s_config_generation;
/* UI owns writes. The exported getter takes a zero-wait lock or fails closed,
 * so the HTTP task never observes a partial publication or waits on LVGL. */
static valve_duration_capability_snapshot_t s_capability;
#ifdef VALVE_UI_HOST_TEST
static pthread_mutex_t s_capability_lock = PTHREAD_MUTEX_INITIALIZER;
static void capability_write_lock(void) { (void)pthread_mutex_lock(&s_capability_lock); }
static bool capability_read_try_lock(void) { return pthread_mutex_trylock(&s_capability_lock) == 0; }
static void capability_unlock(void) { (void)pthread_mutex_unlock(&s_capability_lock); }
void valve_ui_test_capability_lock(void) { capability_write_lock(); }
void valve_ui_test_capability_unlock(void) { capability_unlock(); }
#else
static portMUX_TYPE s_capability_lock = portMUX_INITIALIZER_UNLOCKED;
static void capability_write_lock(void) { portENTER_CRITICAL(&s_capability_lock); }
static bool capability_read_try_lock(void) {
    return portTRY_ENTER_CRITICAL(&s_capability_lock, 0) == pdPASS;
}
static void capability_unlock(void) { portEXIT_CRITICAL(&s_capability_lock); }
#endif
static uint64_t monotonic_full_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
static void capability_publish(bool valid, bool supported, uint64_t receipt_ms,
                               uint32_t session, uint32_t generation) {
    capability_write_lock();
    s_capability = (valve_duration_capability_snapshot_t){
        .valid = valid, .supported = supported, .received_at_ms = receipt_ms,
        .session = session, .config_generation = generation,
    };
    capability_unlock();
}
static void capability_invalidate(void) {
    capability_publish(false, false, 0, 0, 0);
}
static void consume_touch(void) {
    if (!s_touch_target_initialized) return;
    s_touch_supply = s_touch_drain = false;
    s_touch_moved = true;
    (void)valve_hold_update(&s_hold, true, true, 0);
}

static uint32_t monotonic_ms(void) { return (uint32_t)monotonic_full_ms(); }
static void completion_cb(const valve_client_event_t *event, void *context) {
    (void)context;
    if (!s_completions) return;
    /* The client owns the event only through this callback. Queue an owned copy. */
    completion_t copy = {.event = *event, .received_at_ms = monotonic_full_ms()};
    if (xQueueSend(s_completions, &copy, 0) != pdTRUE)
        atomic_store(&s_completion_overflow, true);
}

static bool selected_start_request(valve_request_t *request) {
    uint16_t seconds;
    if (!valve_client_selected_duration_get(&seconds) ||
        (seconds != 600 && !s_status.timed_shower_duration_supported)) return false;
    if (request) *request = (valve_request_t){.action = VALVE_ACTION_START,
                                                .duration_seconds = seconds};
    return true;
}

static bool fresh(uint32_t now_ms) {
    return atomic_load(&s_connected) &&
           s_gate.session == valve_client_current_session() &&
           s_config_generation == valve_config_generation() &&
           !(s_config_generation & 1u) && !s_gate.pending && s_status.state != VALVE_UNKNOWN &&
           now_ms - s_status_at < STALE_MS;
}

bool valve_ui_duration_capability_snapshot_get(valve_duration_capability_snapshot_t *out) {
    if (!out || !capability_read_try_lock()) return false;
    *out = s_capability;
    capability_unlock();
    return true;
}
bool valve_ui_duration_capability_get(bool *supported) {
    valve_duration_capability_snapshot_t snapshot;
    if (!supported || !valve_ui_duration_capability_snapshot_get(&snapshot) ||
        !snapshot.valid || !atomic_load(&s_connected) ||
        snapshot.session != valve_client_current_session() ||
        snapshot.config_generation != valve_config_generation() ||
        (snapshot.config_generation & 1u)) return false;
    uint64_t now_ms = monotonic_full_ms();
    if (now_ms < snapshot.received_at_ms ||
        now_ms - snapshot.received_at_ms >= STALE_MS) return false;
    *supported = snapshot.supported;
    return true;
}

static lv_obj_t *make_container(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_opa(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *make_solid(lv_obj_t *parent, int x, int y, int width, int height,
                            uint32_t color, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void create_shower_symbol(void) {
    lv_obj_t *symbol = make_container(s_overlay, 106, 68, 148, 145);
    make_solid(symbol, 21, 0, 6, 20, ICON_COLOR, 3);
    make_solid(symbol, 21, 14, 58, 6, ICON_COLOR, 3);
    make_solid(symbol, 73, 14, 6, 30, ICON_COLOR, 3);
    make_solid(symbol, 39, 41, 91, 6, ICON_COLOR, 3);

    s_rain = make_container(symbol, 44, 55, 82, 77);
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 6; ++col)
            make_solid(s_rain, col * 14, row * 14, 7, 7, ICON_COLOR, 4);
    }

    s_snow = make_container(symbol, 103, 96, 45, 45);
    make_solid(s_snow, 20, 5, 5, 35, ICON_COLOR, 3);
    make_solid(s_snow, 5, 20, 35, 5, ICON_COLOR, 3);
    make_solid(s_snow, 8, 8, 5, 5, ICON_COLOR, 3);
    make_solid(s_snow, 32, 8, 5, 5, ICON_COLOR, 3);
    make_solid(s_snow, 8, 32, 5, 5, ICON_COLOR, 3);
    make_solid(s_snow, 32, 32, 5, 5, ICON_COLOR, 3);

    lv_obj_t *relay = make_container(s_overlay, 246, 37, 34, 14);
    make_solid(relay, 8, 6, 10, 2, ICON_COLOR, 1);
    s_relay_left = make_solid(relay, 0, 2, 10, 10, SCREEN_COLOR, 5);
    s_relay_right = make_solid(relay, 16, 2, 10, 10, SCREEN_COLOR, 5);
    lv_obj_set_style_border_width(s_relay_left, 2, 0);
    lv_obj_set_style_border_width(s_relay_right, 2, 0);
    lv_obj_set_style_border_color(s_relay_left, lv_color_hex(ICON_COLOR), 0);
    lv_obj_set_style_border_color(s_relay_right, lv_color_hex(ICON_COLOR), 0);
}

static void render(uint32_t now_ms) {
    if (!s_overlay) return;
    bool current = fresh(now_ms);
    char temperature_text[32];
    valve_temperature_format(current ? &s_status : NULL, temperature_text,
                             sizeof temperature_text);
    lv_label_set_text(s_temperature, temperature_text);
    if (!current) {
        lv_label_set_text(s_state, "FAULT");
        lv_label_set_text(s_countdown, s_gate.pending ? "Checking Pi..." : "Status unavailable");
        lv_label_set_text(s_action_label, "DRAIN");
        lv_obj_add_flag(s_rain, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_snow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_relay_left, lv_color_hex(SCREEN_COLOR), 0);
        lv_obj_set_style_bg_color(s_relay_right, lv_color_hex(SCREEN_COLOR), 0);
        lv_obj_set_style_bg_color(s_action, lv_color_hex(ACTION_DARK_COLOR), 0);
    } else if (s_status.state == VALVE_DRAIN) {
        lv_label_set_text(s_state, "OFF");
        lv_label_set_text(s_countdown, "");
        uint16_t seconds;
        char label[32];
        if (valve_client_selected_duration_get(&seconds))
            snprintf(label, sizeof label, "HOLD 2s\n%u MIN", seconds / 60);
        else snprintf(label, sizeof label, "LOADING");
        lv_label_set_text(s_action_label, label);
        lv_obj_add_flag(s_rain, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_snow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_relay_left, lv_color_hex(SCREEN_COLOR), 0);
        lv_obj_set_style_bg_color(s_relay_right, lv_color_hex(SCREEN_COLOR), 0);
        lv_obj_set_style_bg_color(s_action, lv_color_hex(ACTIVE_COLOR), 0);
    } else {
        lv_label_set_text(s_state, "ON");
        lv_label_set_text(s_countdown, "");
        uint32_t elapsed = (now_ms - s_status_at) / 1000u;
        uint32_t remaining = s_status.remaining_seconds > elapsed ?
            s_status.remaining_seconds - elapsed : 0;
        char text[32];
        snprintf(text, sizeof text, "%lu:%02lu", (unsigned long)(remaining / 60),
                 (unsigned long)(remaining % 60));
        lv_label_set_text(s_action_label, text);
        lv_obj_clear_flag(s_rain, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_snow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_relay_left, lv_color_hex(ACTIVE_COLOR), 0);
        lv_obj_set_style_bg_color(s_relay_right, lv_color_hex(ACTIVE_COLOR), 0);
        lv_obj_set_style_bg_color(s_action, lv_color_hex(ACTION_DARK_COLOR), 0);
    }
    bool supply = selected_start_request(NULL) &&
        valve_supply_allowed(s_status.state, s_configured, s_connected,
                             current ? now_ms - s_status_at : STALE_MS);
    bool drain = valve_drain_allowed(s_configured &&
                                     s_config_generation == valve_config_generation() &&
                                     !(s_config_generation & 1u), s_connected);
    bool enabled = current && s_status.state == VALVE_DRAIN ? supply : drain;
    if (enabled) lv_obj_clear_state(s_action, LV_STATE_DISABLED);
    else lv_obj_add_state(s_action, LV_STATE_DISABLED);
    ui_set_valve_active(current && s_status.state == VALVE_SUPPLY);
}
static void request_get(uint32_t now_ms) {
    s_last_get = now_ms;
    uint32_t request_id;
    if (valve_client_request_get_tagged(&request_id) && s_recovery_needed) {
        valve_gate_overflow(&s_gate, request_id);
        capability_invalidate();
    }
}
static void request_action(const valve_request_t *request) {
    uint32_t now_ms = monotonic_ms();
    s_gate.pending = true;
    capability_invalidate();
    s_pending_since = now_ms;
    s_status.state = VALVE_UNKNOWN;
    ui_set_valve_active(false);
    render(now_ms);
    uint32_t request_id;
    if (!valve_client_request_post_for_config(request, s_config_generation, &request_id)) {
        valve_ui_set_unknown("Request unavailable");
        request_get(now_ms);
    } else {
        valve_gate_action(&s_gate, request_id);
    }
}
void valve_ui_init(void) {
    s_completions = xQueueCreate(4, sizeof(completion_t));
    char url[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
    s_configured = valve_config_load(url, sizeof url, token, sizeof token);
    memset(token, 0, sizeof token);
    s_config_generation = valve_config_generation();
    s_status.state = VALVE_UNKNOWN;
    valve_gate_link(&s_gate, valve_client_current_session());
    capability_invalidate();
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_overlay, 360, 360);
    lv_obj_center(s_overlay);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(SCREEN_COLOR), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, "");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);
    s_state = lv_label_create(s_overlay);
    lv_obj_set_pos(s_state, 285, 31);
    s_countdown = lv_label_create(s_overlay);
    lv_obj_align(s_countdown, LV_ALIGN_TOP_MID, 0, 40);
    s_action = lv_btn_create(s_overlay);
    lv_obj_set_size(s_action, 176, 58);
    lv_obj_align(s_action, LV_ALIGN_TOP_MID, 0, 234);
    lv_obj_set_style_radius(s_action, 29, 0);
    s_action_label = lv_label_create(s_action);
    lv_obj_center(s_action_label);
    create_shower_symbol();
    s_temperature = lv_label_create(s_overlay);
    lv_obj_set_pos(s_temperature, 90, 31);
    lv_obj_set_style_text_font(s_temperature, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_temperature, lv_color_hex(ICON_COLOR), 0);
    render(0);
    (void)valve_client_start(completion_cb, NULL);
}
void valve_ui_show(bool visible) {
    if (s_visible == visible) return;
    s_visible = visible;
    valve_ui_cancel_touch();
    if (visible) {
        char url[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
        s_configured = valve_config_load(url, sizeof url, token, sizeof token);
        memset(token, 0, sizeof token);
        lv_obj_move_foreground(s_overlay);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        request_get(monotonic_ms());
    } else lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
void valve_ui_toggle_page(void) { valve_ui_show(!valve_ui_visible()); }
bool valve_ui_visible(void) { return s_visible; }
void valve_ui_set_status(const valve_status_t *status) {
    if (!status) return;
    s_status = *status;
    uint64_t received_at_ms = monotonic_full_ms();
    s_status_at = (uint32_t)received_at_ms;
    s_gate.pending = false;
    capability_publish(status->state != VALVE_UNKNOWN,
                       status->timed_shower_duration_supported, received_at_ms,
                       s_gate.session, s_config_generation);
    render(s_status_at);
}
void valve_ui_set_unknown(const char *reason) {
    (void)reason;
    s_status.state = VALVE_UNKNOWN;
    s_gate.pending = false;
    capability_invalidate();
    consume_touch();
    render(monotonic_ms());
}
static void fail_closed_overflow(uint32_t now_ms) {
    if (!atomic_exchange(&s_completion_overflow, false)) return;
    xQueueReset(s_completions);
    valve_ui_set_unknown("Completion overflow");
    s_recovery_needed = true;
    valve_gate_overflow(&s_gate, 0);
    capability_invalidate();
    s_pending_since = now_ms;
    request_get(now_ms);
}
void valve_ui_process(uint32_t now_ms, bool awake) {
    uint32_t generation = valve_config_generation();
    if (generation != s_config_generation) {
        s_config_generation = generation;
        valve_ui_set_unknown("Configuration changed");
        valve_gate_link(&s_gate, valve_client_current_session());
        s_recovery_needed = false;
        char url[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
        s_configured = !(generation & 1u) &&
            valve_config_load(url, sizeof url, token, sizeof token) &&
            generation == valve_config_generation();
        memset(token, 0, sizeof token);
        if (s_configured) request_get(now_ms);
    }
    bool connected = atomic_load(&s_connected);
    unsigned epoch = atomic_load(&s_link_epoch);
    if (epoch != s_processed_epoch) {
        s_processed_epoch = epoch;
        valve_ui_set_unknown(connected ? "Reconnecting" : "Wi-Fi disconnected");
        valve_gate_link(&s_gate, valve_client_current_session());
        s_pending_since = now_ms;
        s_recovery_needed = false;
    }
    fail_closed_overflow(now_ms);
    completion_t completion;
    while (s_completions && xQueueReceive(s_completions, &completion, 0) == pdTRUE) {
        const valve_client_event_t *event = &completion.event;
        if (!connected || event->session != valve_client_current_session() ||
            event->config_generation != s_config_generation ||
            s_config_generation != valve_config_generation()) continue;
        uint64_t received_age_ms = monotonic_full_ms() - completion.received_at_ms;
        bool valid = event->result == VALVE_CLIENT_OK &&
                     event->status.state != VALVE_UNKNOWN &&
                     completion.received_at_ms <= monotonic_full_ms() &&
                     received_age_ms < STALE_MS;
        if (valve_gate_accept(&s_gate, event->session, event->request_id, valid)) {
            s_status = event->status;
            s_status_at = (uint32_t)completion.received_at_ms;
            capability_publish(true, s_status.timed_shower_duration_supported,
                               completion.received_at_ms, event->session,
                               event->config_generation);
            s_recovery_needed = false;
        } else if (event->request_id >= s_gate.minimum_request_id &&
                   event->session == s_gate.session && !valid) {
            s_status.state = VALVE_UNKNOWN;
            capability_invalidate();
            consume_touch();
        }
    }
    fail_closed_overflow(now_ms);
    uint32_t current_ms = monotonic_ms();
    if (awake && s_connected && current_ms - s_last_get >= POLL_MS) request_get(current_ms);
    if (s_gate.pending && current_ms - s_pending_since >= STALE_MS)
        valve_ui_set_unknown("Request timed out");
    if (s_status.state != VALVE_UNKNOWN && current_ms - s_status_at >= STALE_MS)
        valve_ui_set_unknown("Status stale");
    render(current_ms);
}
void valve_ui_touch(int x, int y, bool pressed, bool moved, uint32_t now_ms) {
    if (s_config_generation != valve_config_generation() || (s_config_generation & 1u)) {
        if (pressed) { s_touch_target_initialized = true; consume_touch(); }
        else valve_ui_cancel_touch();
        return;
    }
    bool action_area = x >= 45 && x <= 315 && y >= 230 && y <= 305;
    if (!pressed) {
        if (action_area && valve_drain_tap_allowed(s_touch_drain, s_touch_moved,
             VALVE_GESTURE_NONE, s_configured, atomic_load(&s_connected)))
            request_action(&(valve_request_t){.action = VALVE_ACTION_DRAIN});
        valve_ui_cancel_touch();
        return;
    }
    if (!s_touch_target_initialized) {
        s_touch_target_initialized = true;
        bool supply_action = action_area && fresh(now_ms) && s_status.state == VALVE_DRAIN &&
            selected_start_request(NULL) &&
            valve_supply_allowed(s_status.state, s_configured, atomic_load(&s_connected),
                                 now_ms - s_status_at);
        s_touch_supply = supply_action;
        s_touch_drain = action_area && !supply_action;
    }
    s_touch_moved |= moved || ((s_touch_supply || s_touch_drain) && !action_area);
    if (s_touch_supply && fresh(now_ms) &&
        valve_supply_allowed(s_status.state, s_configured, atomic_load(&s_connected),
                             now_ms - s_status_at) &&
        valve_hold_update(&s_hold, true, s_touch_moved, now_ms)) {
        valve_request_t request;
        if (selected_start_request(&request)) request_action(&request);
    }
    if (!valve_supply_allowed(s_status.state, s_configured, s_connected,
                              now_ms - s_status_at))
        (void)valve_hold_update(&s_hold, true, true, now_ms);
}
void valve_ui_cancel_touch(void) {
    s_touch_supply = s_touch_drain = s_touch_moved = false;
    s_touch_target_initialized = false;
    (void)valve_hold_update(&s_hold, false, false, 0);
}
void valve_ui_connected(bool connected) {
    atomic_store(&s_connected, connected);
    atomic_fetch_add(&s_link_epoch, 1);
}
void valve_ui_wake(void) {
    if (s_connected) request_get(monotonic_ms());
}

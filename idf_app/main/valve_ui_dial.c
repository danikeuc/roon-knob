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

#define POLL_MS 5000u
#define STALE_MS 10000u

typedef struct { valve_client_event_t event; uint32_t received_at_ms; } completion_t;
static QueueHandle_t s_completions;
static lv_obj_t *s_overlay, *s_state, *s_countdown, *s_supply, *s_drain;
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

static uint32_t monotonic_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void completion_cb(const valve_client_event_t *event, void *context) {
    (void)context;
    if (!s_completions) return;
    /* The client owns the event only through this callback. Queue an owned copy. */
    completion_t copy = {.event = *event, .received_at_ms = monotonic_ms()};
    if (xQueueSend(s_completions, &copy, 0) != pdTRUE)
        atomic_store(&s_completion_overflow, true);
}

static bool fresh(uint32_t now_ms) {
    return !s_gate.pending && s_status.state != VALVE_UNKNOWN &&
           now_ms - s_status_at < STALE_MS;
}
static void render(uint32_t now_ms) {
    if (!s_overlay) return;
    bool current = fresh(now_ms);
    if (!current) {
        lv_label_set_text(s_state, "UNKNOWN / FAULT");
        lv_label_set_text(s_countdown, s_gate.pending ? "Checking Pi..." : "Status unavailable");
    } else if (s_status.state == VALVE_DRAIN) {
        lv_label_set_text(s_state, "DRAIN (0)");
        lv_label_set_text(s_countdown, "Ready for timed supply");
    } else {
        lv_label_set_text(s_state, "SUPPLY (1)");
        uint32_t elapsed = (now_ms - s_status_at) / 1000u;
        uint32_t remaining = s_status.remaining_seconds > elapsed ?
            s_status.remaining_seconds - elapsed : 0;
        char text[32];
        snprintf(text, sizeof text, "%lu:%02lu remaining", (unsigned long)(remaining / 60),
                 (unsigned long)(remaining % 60));
        lv_label_set_text(s_countdown, text);
    }
    bool supply = valve_supply_allowed(s_status.state, s_configured, s_connected,
                                        current ? now_ms - s_status_at : STALE_MS);
    if (supply) lv_obj_clear_state(s_supply, LV_STATE_DISABLED);
    else lv_obj_add_state(s_supply, LV_STATE_DISABLED);
    if (valve_drain_allowed(s_configured, s_connected))
        lv_obj_clear_state(s_drain, LV_STATE_DISABLED);
    else lv_obj_add_state(s_drain, LV_STATE_DISABLED);
    ui_set_valve_active(current && s_status.state == VALVE_SUPPLY);
}
static void request_get(uint32_t now_ms) {
    s_last_get = now_ms;
    uint32_t request_id;
    if (valve_client_request_get_tagged(&request_id) && s_recovery_needed)
        valve_gate_overflow(&s_gate, request_id);
}
static void request_action(valve_action_t action) {
    uint32_t now_ms = monotonic_ms();
    s_gate.pending = true;
    s_pending_since = now_ms;
    s_status.state = VALVE_UNKNOWN;
    ui_set_valve_active(false);
    uint32_t request_id;
    if (!valve_client_request_post_tagged(action, &request_id)) {
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
    s_status.state = VALVE_UNKNOWN;
    valve_gate_link(&s_gate, valve_client_current_session());
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_overlay, 360, 360);
    lv_obj_center(s_overlay);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x101318), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, "VALVES");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);
    s_state = lv_label_create(s_overlay);
    lv_obj_align(s_state, LV_ALIGN_TOP_MID, 0, 88);
    s_countdown = lv_label_create(s_overlay);
    lv_obj_align(s_countdown, LV_ALIGN_TOP_MID, 0, 125);
    s_supply = lv_btn_create(s_overlay);
    lv_obj_set_size(s_supply, 270, 60);
    lv_obj_align(s_supply, LV_ALIGN_TOP_MID, 0, 172);
    lv_obj_t *label = lv_label_create(s_supply);
    lv_label_set_text(label, "Hold 2s: SUPPLY (1)");
    lv_obj_center(label);
    s_drain = lv_btn_create(s_overlay);
    lv_obj_set_size(s_drain, 270, 60);
    lv_obj_align(s_drain, LV_ALIGN_TOP_MID, 0, 250);
    label = lv_label_create(s_drain);
    lv_label_set_text(label, "DRAIN (0)");
    lv_obj_center(label);
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
    s_status_at = monotonic_ms();
    s_gate.pending = false;
    render(s_status_at);
}
void valve_ui_set_unknown(const char *reason) {
    (void)reason;
    s_status.state = VALVE_UNKNOWN;
    s_gate.pending = false;
    valve_ui_cancel_touch();
    render(monotonic_ms());
}
static void fail_closed_overflow(uint32_t now_ms) {
    if (!atomic_exchange(&s_completion_overflow, false)) return;
    xQueueReset(s_completions);
    valve_ui_set_unknown("Completion overflow");
    s_recovery_needed = true;
    valve_gate_overflow(&s_gate, 0);
    s_pending_since = now_ms;
    request_get(now_ms);
}
void valve_ui_process(uint32_t now_ms, bool awake) {
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
        if (!connected || event->session != valve_client_current_session()) continue;
        uint32_t received_age_ms = monotonic_ms() - completion.received_at_ms;
        bool valid = event->result == VALVE_CLIENT_OK &&
                     event->status.state != VALVE_UNKNOWN &&
                     received_age_ms < STALE_MS;
        if (valve_gate_accept(&s_gate, event->session, event->request_id, valid)) {
            s_status = event->status;
            s_status_at = completion.received_at_ms;
            s_recovery_needed = false;
        } else if (event->request_id >= s_gate.minimum_request_id &&
                   event->session == s_gate.session && !valid) {
            s_status.state = VALVE_UNKNOWN;
            valve_ui_cancel_touch();
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
    bool supply_area = x >= 45 && x <= 315 && y >= 172 && y <= 232;
    bool drain_area = x >= 45 && x <= 315 && y >= 250 && y <= 310;
    if (!pressed) {
        if (drain_area && valve_drain_tap_allowed(s_touch_drain, s_touch_moved,
             VALVE_GESTURE_NONE, s_configured, atomic_load(&s_connected)))
            request_action(VALVE_ACTION_DRAIN);
        valve_ui_cancel_touch();
        return;
    }
    if (!s_touch_target_initialized) {
        s_touch_target_initialized = true;
        s_touch_supply = supply_area;
        s_touch_drain = drain_area;
    }
    s_touch_moved |= moved || (s_touch_supply && !supply_area) || (s_touch_drain && !drain_area);
    if (s_touch_supply && valve_supply_allowed(s_status.state, s_configured, s_connected,
                                                now_ms - s_status_at) &&
        valve_hold_update(&s_hold, true, s_touch_moved, now_ms))
        request_action(VALVE_ACTION_START_600S);
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

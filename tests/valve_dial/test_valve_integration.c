#include "valve_ui_dial.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include "bridge_command_plan.h"
#include "controller_input.h"
#include "controller_action_router.h"
#include "controller_config.h"
#include "platform/platform_http.h"
#include "platform/platform_task.h"
#include "platform/platform_log.h"
#include "controller_presentation.h"
#include "lvgl.h"
#include "freertos/queue.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t fake_now = 1000;
static int posts, gets, valve_indicator;
static lv_obj_t objects[80];
static unsigned object_count;
static lv_obj_t screen;
struct fake_queue { unsigned capacity, count, head; size_t item_size; unsigned char data[4][256]; };
static struct fake_queue queue;
int64_t esp_timer_get_time(void) { return (int64_t)fake_now * 1000; }
QueueHandle_t xQueueCreate(unsigned capacity, size_t item_size) {
    assert(capacity == 4 && item_size <= 256);
    queue = (struct fake_queue){.capacity = capacity, .item_size = item_size};
    return &queue;
}
int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks) {
    (void)ticks;
    if (q->count == q->capacity) return pdFALSE;
    memcpy(q->data[(q->head + q->count) % q->capacity], item, q->item_size);
    q->count++;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks) {
    (void)ticks;
    if (!q->count) return pdFALSE;
    memcpy(item, q->data[q->head], q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return pdTRUE;
}
void xQueueReset(QueueHandle_t q) { q->head = q->count = 0; }
lv_obj_t *lv_screen_active(void) { return &screen; }
lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; assert(object_count < 80); return &objects[object_count++]; }
lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
lv_obj_t *lv_btn_create(lv_obj_t *p) { return lv_obj_create(p); }
void lv_obj_set_size(lv_obj_t *o, int w, int h) { (void)o;(void)w;(void)h; }
void lv_obj_center(lv_obj_t *o) { (void)o; }
void lv_obj_set_style_bg_color(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_bg_opa(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_border_width(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_radius(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_pad_all(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_add_flag(lv_obj_t *o, unsigned f) { o->flags |= f; }
void lv_obj_clear_flag(lv_obj_t *o, unsigned f) { o->flags &= ~f; }
void lv_obj_align(lv_obj_t *o, int a, int x, int y) {(void)o;(void)a;(void)x;(void)y;}
void lv_obj_set_pos(lv_obj_t *o, int x, int y) {(void)o;(void)x;(void)y;}
void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof o->text, "%s", s); }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
void lv_obj_add_state(lv_obj_t *o, unsigned s) { o->state |= s; }
void lv_obj_clear_state(lv_obj_t *o, unsigned s) { o->state &= ~s; }
int lv_color_hex(unsigned c) { return (int)c; }
void lv_obj_set_style_border_color(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void ui_set_valve_active(bool active) { valve_indicator = active; }

static const char *const secret = "integration-private-token";
static unsigned start_posts, drain_posts, bridge_posts;
static bool pi_online = true, bridge_playing;
static uint32_t pi_deadline_ms;
static char request_trace[128];
static uint32_t config_generation;
uint32_t valve_config_generation(void) { return config_generation; }
bool valve_config_load(char *url, size_t ul, char *token, size_t tl) {
    assert(ul > 25 && tl > strlen(secret));
    strcpy(url, "http://fake-pi:8081");
    strcpy(token, secret);
    return true;
}
static int fake_pi_transport(const char *method, const char *url, const char *token,
                             int *status, char *response, size_t cap, size_t *len, void *ctx) {
    (void)ctx;
    assert(strcmp(token, secret) == 0);
    assert(strstr(url, secret) == NULL && strchr(url, '?') == NULL);
    assert(strncmp(url, "http://fake-pi:8081/api/v1/display/", strlen("http://fake-pi:8081/api/v1/display/")) == 0);
    if (!pi_online) return -3;
    const char *route = strrchr(url, '/');
    assert(route);
    if (strcmp(method, "POST") == 0) {
        posts++;
        if (strcmp(route, "/timed-shower") == 0) {
            start_posts++; pi_deadline_ms = fake_now + 600000;
            strcat(request_trace, "S");
        } else {
            assert(strcmp(route, "/drain") == 0);
            drain_posts++; pi_deadline_ms = 0;
            strcat(request_trace, "D");
        }
        *status = 200;
        *len = 2; memcpy(response, "{}", 2);
    } else {
        assert(strcmp(method, "GET") == 0 && strcmp(route, "/status") == 0);
        gets++; strcat(request_trace, "G");
        unsigned remaining = pi_deadline_ms > fake_now ? (pi_deadline_ms - fake_now) / 1000 : 0;
        bool supply = pi_deadline_ms > fake_now;
        int n = snprintf(response, cap,
            "{\"mode\":\"manual_timed\",\"state\":\"%s\",\"command\":\"%s\",\"reason\":\"mock\",\"remaining_seconds\":%u}",
            supply ? "TIMED_SHOWER" : "MANUAL_DRAIN", supply ? "SUPPLY" : "DRAIN", remaining);
        assert(n > 0 && (size_t)n < cap);
        *status = 200; *len = (size_t)n;
    }
    return 0;
}
bool controller_config_snapshot(controller_config_snapshot_t *out) {
    assert(out);
    memset(out, 0, sizeof *out);
    strcpy(out->value.bridge_base, "http://fake-bridge:8088");
    strcpy(out->value.zone_id, "roon:living");
    return true;
}
int platform_http_post_json(const char *url, const char *json, char **out, size_t *out_len) {
    assert(strcmp(url, "http://fake-bridge:8088/control") == 0);
    assert(strstr(json, "\"zone_id\":\"roon:living\"") != NULL);
    assert(strstr(url, secret) == NULL && strstr(json, secret) == NULL);
    if (strstr(json, "\"action\":\"play_pause\"")) bridge_playing = !bridge_playing;
    else assert(strstr(json, "\"action\":\"next\"") != NULL);
    bridge_posts++;
    *out = NULL; *out_len = 0;
    return 0;
}
void platform_http_free(char *ptr) { free(ptr); }
void platform_log_backend(const char *level, const char *fmt, va_list args) {
    (void)level; vfprintf(stderr, fmt, args); fputc('\n', stderr);
}
bool platform_task_post_to_ui(platform_task_fn_t fn, void *arg) {
    (void)fn; (void)arg; return false;
}
void controller_presentation_zone_picker_scroll(int delta) { (void)delta; }
void controller_presentation_show_settings(void) {}
void controller_presentation_show_volume_change(float volume, float step) {
    (void)volume; (void)step;
}
void controller_presentation_hide_zone_picker(void) {}
void controller_presentation_zone_picker_get_selected_id(char *out, size_t len) {
    if (len) out[0] = 0;
}
bool controller_presentation_zone_picker_is_current_selection(void) { return false; }
void controller_presentation_set_network_status(const char *status) { (void)status; }
void controller_presentation_set_zone_name(const char *name) { (void)name; }
void controller_presentation_set_message(const char *msg) { (void)msg; }
void controller_presentation_show_zone_picker(const char **names, const char **ids,
                                               int count, int selected) {
    (void)names; (void)ids; (void)count; (void)selected;
}
controller_config_write_result_t controller_config_set_zone(
    const char *id, controller_config_snapshot_t *out) {
    (void)id; (void)out; return CONTROLLER_CONFIG_NOT_COMMITTED;
}
static void flush(void) {
    valve_client_test_run_pending();
    valve_ui_process(fake_now, true);
}
int main(void) {
    valve_client_test_transport(fake_pi_transport, NULL);
    valve_ui_init();
    valve_client_on_reconnect();
    valve_ui_connected(true);
    valve_ui_show(true);
    flush();
    assert(strcmp(objects[2].text, "OFF") == 0);
    assert(posts == 0 && gets >= 1);
    valve_gesture_context_t media = {0};
    assert(valve_gesture_classify(80, 4, 250, 0, media) == VALVE_GESTURE_SWITCH_SCREEN);
    assert(valve_gesture_classify(4, -80, 250, 0, media) == VALVE_GESTURE_ART_UP);
    media.zone_picker = true;
    assert(valve_gesture_classify(80, 0, 250, 0, media) == VALVE_GESTURE_NONE);
    media.zone_picker = false; media.settings = true;
    assert(valve_gesture_classify(80, 0, 250, 0, media) == VALVE_GESTURE_NONE);
    media.settings = false; media.art_mode = true;
    assert(valve_gesture_classify(80, 0, 250, 0, media) == VALVE_GESTURE_NONE);
    media.art_mode = false; media.wake_touch = true;
    assert(valve_gesture_classify(80, 0, 250, 0, media) == VALVE_GESTURE_NONE);

    valve_ui_touch(100, 260, true, false, fake_now);
    fake_now += 1999; valve_ui_touch(100, 260, true, false, fake_now);
    assert(posts == 0);
    fake_now++; valve_ui_touch(100, 260, true, false, fake_now);
    assert(posts == 0);  /* queued until worker runs */
    flush();
    assert(start_posts == 1 && drain_posts == 0);
    assert(strcmp(request_trace + strlen(request_trace) - 2, "SG") == 0);
    assert(strcmp(objects[2].text, "ON") == 0);
    assert(strcmp(objects[5].text, "10:00") == 0);
    fake_now += 30000; valve_ui_wake(); flush();
    assert(strcmp(objects[5].text, "9:30") == 0);
    assert(bridge_posts == 0 && !bridge_playing);

    valve_ui_touch(100, 260, false, false, fake_now);
    valve_ui_touch(100, 260, true, false, fake_now);
    valve_ui_touch(100, 260, false, false, fake_now + 1);
    flush();
    assert(drain_posts == 1 && pi_deadline_ms == 0);
    assert(strcmp(request_trace + strlen(request_trace) - 2, "DG") == 0);
    assert(strcmp(objects[2].text, "OFF") == 0);

    /* A queued action is dropped by the real client on disconnect. */
    valve_ui_touch(100, 260, true, false, fake_now + 2);
    valve_ui_touch(100, 260, true, false, fake_now + 2002);
    assert(start_posts == 1);
    pi_online = false;
    valve_client_on_disconnect(); valve_ui_connected(false);
    valve_ui_process(fake_now + 2002, true);
    assert(strcmp(objects[2].text, "FAULT") == 0);
    fake_now += 2100; pi_online = true;
    valve_client_on_reconnect(); valve_ui_connected(true); flush();
    assert(start_posts == 1 && strcmp(objects[2].text, "OFF") == 0);
    assert(bridge_posts == 0);
    assert(strstr(request_trace, secret) == NULL);
    assert(valve_ui_visible());
    assert(valve_gesture_classify(85, 2, 200, 0, (valve_gesture_context_t){0}) ==
           VALVE_GESTURE_SWITCH_SCREEN);
    valve_ui_toggle_page();
    assert(!valve_ui_visible());
    controller_action_router_init();
    controller_input_set_action_handler(controller_action_router_handle);
    controller_physical_event_t encoder = {
        .source_id = CONTROLLER_INPUT_SOURCE_DIAL_BUILTIN,
        .control_id = CONTROLLER_INPUT_CONTROL_DIAL_ROTATION,
        .kind = CONTROLLER_PHYSICAL_EVENT_ROTATION,
        .gesture = CONTROLLER_PHYSICAL_GESTURE_NONE,
        .value = 1,
    };
    /* The fake bridge has not published an operational volume state. */
    assert(!controller_input_dispatch_physical(&encoder));
    assert(bridge_posts == 0);
    assert(controller_input_dispatch_control(CONTROLLER_CONTROL_INTENT_ACTIVATE));
    assert(bridge_posts == 1 && bridge_playing);
    assert(controller_input_dispatch_control(CONTROLLER_CONTROL_INTENT_NEXT));
    assert(bridge_posts == 2);
    assert(strcmp(objects[2].text, "OFF") == 0);
    puts("mock Pi + bridge integration session passed (Roon input/router/client, page switch, hold, 600s, DRAIN, reconnect, no replay, token redaction)");
    return 0;
}

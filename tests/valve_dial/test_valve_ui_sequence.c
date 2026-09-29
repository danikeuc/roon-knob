#include "valve_ui_dial.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include "lvgl.h"
#include "freertos/queue.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t fake_now = 1000, next_id, session = 1;
static valve_client_callback_fn client_callback;
static void *client_context;
static int posts, gets, valve_indicator;
static lv_obj_t objects[16];
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
lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; assert(object_count < 16); return &objects[object_count++]; }
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
void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof o->text, "%s", s); }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
void lv_obj_add_state(lv_obj_t *o, unsigned s) { o->state |= s; }
void lv_obj_clear_state(lv_obj_t *o, unsigned s) { o->state &= ~s; }
int lv_color_hex(unsigned c) { return (int)c; }
void ui_set_valve_active(bool active) { valve_indicator = active; }
bool valve_config_load(char *url, size_t ul, char *token, size_t tl) {
    assert(ul > 25 && tl > 10); strcpy(url, "http://192.0.2.10:8081"); strcpy(token, "test-token"); return true;
}
bool valve_client_start(valve_client_callback_fn callback, void *context) {
    client_callback = callback; client_context = context; return true;
}
bool valve_client_request_get_tagged(uint32_t *id) { gets++; next_id++; if (id) *id = next_id; return true; }
bool valve_client_request_post_tagged(valve_action_t action, uint32_t *id) {
    assert(action == VALVE_ACTION_START_600S || action == VALVE_ACTION_DRAIN);
    posts++; next_id++; if (id) *id = next_id; return true;
}
uint32_t valve_client_current_session(void) { return session; }
static void emit(uint32_t id, uint32_t origin_session, valve_client_event_kind_t kind,
                 valve_client_result_t result, valve_state_t state) {
    valve_client_event_t event = {.request_id = id, .session = origin_session, .kind = kind,
                                  .result = result, .status = {.state = state, .remaining_seconds = state == VALVE_SUPPLY ? 600 : 0}};
    client_callback(&event, client_context);
}
static void process(void) { valve_ui_process(fake_now, true); }
int main(void) {
    valve_ui_init();
    valve_ui_connected(true);
    process();
    valve_ui_show(true);
    uint32_t initial = next_id;
    emit(initial, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    /* Worker receipt can occur after ui_loop_task captured its entry time. */
    valve_ui_process(fake_now - 1, true);
    assert(strcmp(objects[2].text, "DRAIN (0)") == 0);
    assert(!(objects[4].state & LV_STATE_DISABLED));
    valve_ui_wake();
    uint32_t old_get = next_id;
    valve_ui_touch(100, 180, true, false, 1100);
    valve_ui_touch(100, 180, true, false, 3100);
    assert(posts == 1);
    uint32_t action = next_id;
    emit(old_get, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    fake_now = 3100; process();
    assert(strcmp(objects[2].text, "UNKNOWN / FAULT") == 0);
    assert(objects[4].state & LV_STATE_DISABLED);
    emit(action, session, VALVE_CLIENT_EVENT_POST_STATUS, VALVE_CLIENT_OK, VALVE_SUPPLY);
    process();
    assert(strcmp(objects[2].text, "SUPPLY (1)") == 0);
    assert(valve_indicator);
    valve_ui_touch(100, 180, false, false, 3101);
    valve_ui_touch(100, 270, true, false, 3102);
    valve_ui_touch(100, 270, false, false, 3103);
    assert(posts == 2);
    uint32_t drain_action = next_id;
    emit(drain_action, session, VALVE_CLIENT_EVENT_POST_STATUS, VALVE_CLIENT_TIMEOUT, VALVE_UNKNOWN);
    process();
    assert(strcmp(objects[2].text, "UNKNOWN / FAULT") == 0);
    assert(objects[4].state & LV_STATE_DISABLED);
    emit(drain_action, session, VALVE_CLIENT_EVENT_RECOVERY_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(objects[2].text, "DRAIN (0)") == 0);
    for (int i = 0; i < 4; i++) {
        assert(valve_client_request_get_tagged(NULL));
        emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    }
    emit(next_id + 1, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_TIMEOUT, VALVE_UNKNOWN);
    process();
    assert(strcmp(objects[2].text, "UNKNOWN / FAULT") == 0);
    assert(objects[4].state & LV_STATE_DISABLED);
    uint32_t barrier = next_id;
    emit(barrier - 1, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(objects[4].state & LV_STATE_DISABLED);
    emit(barrier, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(!(objects[4].state & LV_STATE_DISABLED));
    uint32_t old_session = session++;
    valve_ui_connected(false);
    valve_ui_connected(true);
    process();
    emit(++next_id, old_session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(objects[2].text, "UNKNOWN / FAULT") == 0);
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(objects[2].text, "DRAIN (0)") == 0);
    fake_now += 11000;
    process();
    assert(strcmp(objects[2].text, "UNKNOWN / FAULT") == 0);
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    fake_now += 11000;  // Leave the completion queued beyond the freshness limit.
    process();
    assert(strcmp(objects[2].text, "UNKNOWN / FAULT") == 0);
    assert(objects[4].state & LV_STATE_DISABLED);
    puts("valve UI sequence tests passed");
}

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
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

static atomic_uint fake_now = ATOMIC_VAR_INIT(1000);
static uint32_t next_id, session = 1;
static valve_client_callback_fn client_callback;
static void *client_context;
static int posts, gets, valve_indicator;
static valve_action_t last_action;
static uint16_t last_duration, selected_duration = 600;
static bool selected_ready;
static lv_obj_t objects[80];
static unsigned object_count;
static lv_obj_t screen;
struct fake_queue { unsigned capacity, count, head; size_t item_size; unsigned char data[4][256]; };
static struct fake_queue queue;
int64_t esp_timer_get_time(void) { return (int64_t)atomic_load(&fake_now) * 1000; }
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
lv_obj_t *lv_obj_create(lv_obj_t *p) {
    assert(object_count < 80);
    lv_obj_t *obj = &objects[object_count++];
    obj->parent = p;
    return obj;
}
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
void lv_obj_set_pos(lv_obj_t *o, int x, int y) {o->x = x; o->y = y;}
void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof o->text, "%s", s); }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
void lv_obj_add_state(lv_obj_t *o, unsigned s) { o->state |= s; }
void lv_obj_clear_state(lv_obj_t *o, unsigned s) { o->state &= ~s; }
int lv_color_hex(unsigned c) { return (int)c; }
void lv_obj_set_style_border_color(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void ui_set_valve_active(bool active) { valve_indicator = active; }
static uint32_t config_generation;
uint32_t valve_config_generation(void) { return config_generation; }
bool valve_config_load(char *url, size_t ul, char *token, size_t tl) {
    assert(ul > 25 && tl > 10); strcpy(url, "http://192.0.2.10:8081"); strcpy(token, "test-token"); return true;
}
bool valve_client_start(valve_client_callback_fn callback, void *context) {
    client_callback = callback; client_context = context; return true;
}
bool valve_client_request_get_tagged(uint32_t *id) { gets++; next_id++; if (id) *id = next_id; return true; }
bool valve_client_request_post_for_config(const valve_request_t *request, uint32_t generation, uint32_t *id) {
    if (!request || generation != config_generation) return false;
    assert(request->action == VALVE_ACTION_START || request->action == VALVE_ACTION_DRAIN);
    last_action = request->action; last_duration = request->duration_seconds;
    posts++; next_id++; if (id) *id = next_id; return true;
}
bool valve_client_selected_duration_get(uint16_t *seconds) {
    if (!selected_ready || !seconds) return false;
    *seconds = selected_duration; return true;
}
uint32_t valve_client_current_session(void) { return session; }
static lv_obj_t *find_label(const char *text) {
    for (unsigned i = 0; i < object_count; ++i)
        if (strcmp(objects[i].text, text) == 0) return &objects[i];
    return NULL;
}
static void assert_temperature_inside_round_display(const lv_obj_t *label) {
    /* The enabled LVGL Montserrat 20 glyph descriptors fit these characters
     * within 16px advances. Reserve 16px per glyph, 24px height (font line
     * height is 22px), and 4px at the visible circle's edge. */
    const char *representative[] = {"-50,0 °C", "120,0 °C", "6,4 °C", "---"};
    assert(label->text_font && label->text_font->size == 20);
    for (size_t i = 0; i < sizeof representative / sizeof representative[0]; i++) {
        unsigned glyphs = 0;
        for (const unsigned char *p = (const unsigned char *)representative[i]; *p; p++)
            if ((*p & 0xc0u) != 0x80u) glyphs++;
        int corners_x[] = {label->x, label->x + (int)glyphs * 16};
        int corners_y[] = {label->y, label->y + 24};
        for (size_t x = 0; x < 2; x++) for (size_t y = 0; y < 2; y++) {
            int dx = corners_x[x] - 180, dy = corners_y[y] - 180;
            assert(dx * dx + dy * dy <= 176 * 176);
        }
    }
}
static void emit_temperature(uint32_t id, uint32_t origin_session,
                             valve_client_event_kind_t kind, valve_client_result_t result,
                             valve_state_t state, bool available, float temperature_c) {
    valve_client_event_t event = {.request_id = id, .session = origin_session, .kind = kind,
                                  .config_generation = config_generation, .result = result,
                                  .status = {.state = state,
                                             .remaining_seconds = state == VALVE_SUPPLY ? 600 : 0,
                                             .temperature_available = available,
                                             .pipe_temperature_c = temperature_c}};
    client_callback(&event, client_context);
}
static void emit(uint32_t id, uint32_t origin_session, valve_client_event_kind_t kind,
                 valve_client_result_t result, valve_state_t state) {
    emit_temperature(id, origin_session, kind, result, state, false, 0);
}
static void process(void) { valve_ui_process(fake_now, true); }
extern void valve_ui_test_capability_lock(void);
extern void valve_ui_test_capability_unlock(void);
static atomic_bool snapshot_reader_done = ATOMIC_VAR_INIT(false);
static atomic_uint snapshot_reader_count = ATOMIC_VAR_INIT(0);
static void *snapshot_reader(void *unused) {
    (void)unused;
    while (!atomic_load(&snapshot_reader_done)) {
        valve_duration_capability_snapshot_t copy;
        if (!valve_ui_duration_capability_snapshot_get(&copy)) continue;
        if (copy.valid) {
            assert(copy.session == session && copy.config_generation == config_generation);
            assert(copy.supported == (bool)(copy.received_at_ms & 1u));
            atomic_fetch_add(&snapshot_reader_count, 1);
        }
        bool supported;
        (void)valve_ui_duration_capability_get(&supported);
    }
    return NULL;
}
static void test_cross_task_capability_snapshot(void) {
    valve_status_t current = {.state = VALVE_DRAIN};
    pthread_t reader;
    atomic_store(&snapshot_reader_done, false);
    assert(pthread_create(&reader, NULL, snapshot_reader, NULL) == 0);
    for (unsigned i = 0; i < 5000; ++i) {
        atomic_store(&fake_now, 100000u + i);
        current.timed_shower_duration_supported = (bool)(i & 1u);
        valve_ui_set_status(&current);
        if (i % 64u == 0) sched_yield();
    }
    atomic_store(&snapshot_reader_done, true);
    assert(pthread_join(reader, NULL) == 0);
    assert(atomic_load(&snapshot_reader_count) > 0);
    valve_duration_capability_snapshot_t copy;
    assert(valve_ui_duration_capability_snapshot_get(&copy));
    assert(copy.valid && copy.supported == (bool)(copy.received_at_ms & 1u));
    valve_duration_capability_snapshot_t sentinel = {
        .valid = true, .supported = true, .received_at_ms = 123,
        .session = 456, .config_generation = 789,
    };
    valve_ui_test_capability_lock();
    assert(!valve_ui_duration_capability_snapshot_get(&sentinel));
    assert(sentinel.valid && sentinel.supported && sentinel.received_at_ms == 123 &&
           sentinel.session == 456 && sentinel.config_generation == 789);
    bool supported = false;
    assert(!valve_ui_duration_capability_get(&supported));
    valve_ui_test_capability_unlock();
    assert(valve_ui_duration_capability_get(&supported) && supported == copy.supported);
    session++;
    assert(!valve_ui_duration_capability_get(&supported));
    session--;
    config_generation += 2;
    assert(!valve_ui_duration_capability_get(&supported));
    config_generation -= 2;
    valve_ui_connected(false);
    assert(!valve_ui_duration_capability_get(&supported));
    valve_ui_connected(true);
    valve_ui_set_unknown("test invalidation");
    assert(valve_ui_duration_capability_snapshot_get(&copy) && !copy.valid);
    supported = true;
    assert(!valve_ui_duration_capability_get(&supported) && supported);
}
int main(void) {
    valve_ui_init();
    valve_ui_connected(true);
    process();
    valve_ui_show(true);
    lv_obj_t *temperature = find_label("---");
    assert(temperature != NULL);
    assert(temperature->parent == &objects[0]);
    assert(temperature->y == 31);
    assert(temperature->text_font != NULL && temperature->text_font->size == 20);
    assert(temperature->text_color == lv_color_hex(0xB8EBFFu));
    assert_temperature_inside_round_display(temperature);
    uint32_t initial = next_id;
    emit_temperature(initial, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK,
                     VALVE_DRAIN, true, 6.44f);
    /* Worker receipt can occur after ui_loop_task captured its entry time. */
    valve_ui_process(fake_now - 1, true);
    assert(strcmp(objects[2].text, "OFF") == 0);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    assert(strcmp(objects[5].text, "LOADING") == 0);
    valve_ui_touch(100, 260, true, false, 1100);
    valve_ui_touch(100, 260, true, false, 3100);
    assert(posts == 0);
    valve_ui_cancel_touch();
    selected_ready = true;
    process();
    assert(strcmp(objects[5].text, "HOLD 2s\n10 MIN") == 0);
    assert(objects[11].flags & LV_OBJ_FLAG_HIDDEN);   /* no water while DRAIN */
    assert(!(objects[48].flags & LV_OBJ_FLAG_HIDDEN)); /* snowflake visible */
    assert(!(objects[4].state & LV_STATE_DISABLED));
    valve_ui_wake();
    uint32_t old_get = next_id;
    valve_ui_touch(100, 260, true, false, 1100);
    valve_ui_touch(100, 260, true, false, 3100);
    assert(posts == 1 && last_action == VALVE_ACTION_START && last_duration == 600);
    assert(strcmp(temperature->text, "---") == 0); /* pending action clears old reading */
    uint32_t action = next_id;
    emit(old_get, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    fake_now = 3100; process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    assert(strcmp(temperature->text, "---") == 0);
    assert(strcmp(objects[3].text, "Checking Pi...") == 0);
    assert(!(objects[4].state & LV_STATE_DISABLED)); /* deliberate DRAIN remains available */
    emit_temperature(action, session, VALVE_CLIENT_EVENT_POST_STATUS, VALVE_CLIENT_OK,
                     VALVE_SUPPLY, true, 6.44f);
    process();
    assert(strcmp(objects[2].text, "ON") == 0);
    assert(strcmp(objects[5].text, "10:00") == 0);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    assert(!(objects[11].flags & LV_OBJ_FLAG_HIDDEN));
    assert(objects[48].flags & LV_OBJ_FLAG_HIDDEN);
    assert(valve_indicator);
    valve_ui_touch(100, 260, false, false, 3101);
    valve_ui_touch(100, 260, true, false, 3102);
    valve_ui_touch(100, 260, false, false, 3103);
    assert(posts == 2);
    uint32_t drain_action = next_id;
    emit(drain_action, session, VALVE_CLIENT_EVENT_POST_STATUS, VALVE_CLIENT_TIMEOUT, VALVE_UNKNOWN);
    process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    assert(strcmp(temperature->text, "---") == 0);
    assert(!(objects[4].state & LV_STATE_DISABLED));
    emit(drain_action, session, VALVE_CLIENT_EVENT_RECOVERY_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(objects[2].text, "OFF") == 0);
    assert(strcmp(temperature->text, "---") == 0); /* valid valve, absent telemetry */
    /* A failed hold remains consumed across recovery until physical release. */
    valve_ui_touch(100, 260, true, false, fake_now);
    fake_now += 2000; valve_ui_touch(100, 260, true, false, fake_now);
    assert(posts == 3);
    emit(next_id, session, VALVE_CLIENT_EVENT_POST_STATUS, VALVE_CLIENT_TIMEOUT, VALVE_UNKNOWN);
    process();
    emit(next_id, session, VALVE_CLIENT_EVENT_RECOVERY_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    valve_ui_touch(100, 260, true, false, fake_now);
    fake_now += 2000; valve_ui_touch(100, 260, true, false, fake_now);
    assert(posts == 3);
    valve_status_t healthy = {.state = VALVE_DRAIN, .temperature_available = true,
                              .pipe_temperature_c = 6.44f};
    valve_ui_set_status(&healthy);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    session++; valve_ui_connected(false); process();
    assert(strcmp(temperature->text, "---") == 0);
    session++; valve_ui_connected(true); process();
    assert(strcmp(temperature->text, "---") == 0);
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(temperature->text, "---") == 0);
    valve_ui_touch(100, 260, true, false, fake_now);
    fake_now += 2000; valve_ui_touch(100, 260, true, false, fake_now);
    assert(posts == 3);
    valve_ui_touch(100, 260, false, false, fake_now);
    for (int i = 0; i < 4; i++) {
        assert(valve_client_request_get_tagged(NULL));
        emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    }
    emit(next_id + 1, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_TIMEOUT, VALVE_UNKNOWN);
    process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    assert(!(objects[4].state & LV_STATE_DISABLED));
    uint32_t barrier = next_id;
    emit(barrier - 1, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(!(objects[4].state & LV_STATE_DISABLED));
    emit(barrier, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(!(objects[4].state & LV_STATE_DISABLED));
    uint32_t old_session = session++;
    valve_ui_connected(false);
    valve_ui_connected(true);
    process();
    emit(++next_id, old_session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    assert(strcmp(objects[2].text, "OFF") == 0);
    /* Config changes during a hold and while an old response is queued. */
    valve_ui_touch(100, 260, true, false, fake_now);
    int before_change = posts;
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    config_generation += 2;
    fake_now += 2000;
    valve_ui_touch(100, 260, true, false, fake_now); /* before next UI process */
    assert(posts == before_change);
    process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    process();
    valve_ui_touch(100, 260, true, false, fake_now);
    fake_now += 2000; valve_ui_touch(100, 260, true, false, fake_now);
    assert(posts == before_change);
    valve_ui_touch(100, 260, false, false, fake_now);
    fake_now += 11000;
    process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    assert(strcmp(temperature->text, "---") == 0); /* stale status */
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_OK, VALVE_DRAIN);
    fake_now += 11000;  // Leave the completion queued beyond the freshness limit.
    process();
    assert(strcmp(objects[2].text, "FAULT") == 0);
    assert(!(objects[4].state & LV_STATE_DISABLED));
    valve_ui_set_status(&healthy);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    fake_now += 11000;
    process();
    assert(strcmp(temperature->text, "---") == 0); /* old numeric status expired */
    /* Every new configuration can DRAIN despite fault, malformed status, or timeout.
     * The client represents FAULT/SAFE_DRAIN and malformed payloads as INVALID. */
    const valve_client_result_t failures[] = {VALVE_CLIENT_INVALID, VALVE_CLIENT_TIMEOUT};
    for (unsigned i = 0; i < sizeof failures / sizeof failures[0]; ++i) {
        valve_ui_touch(100, 260, true, false, fake_now);
        int before = posts;
        config_generation += 2;
        process();
        valve_ui_touch(100, 260, false, false, fake_now);
        assert(posts == before); /* old-generation contact cannot act */
        emit(next_id, session, VALVE_CLIENT_EVENT_GET, failures[i], VALVE_UNKNOWN);
        process();
        assert(strcmp(objects[2].text, "FAULT") == 0);
        assert(strcmp(temperature->text, "---") == 0); /* malformed or timed-out completion */
        assert(!(objects[4].state & LV_STATE_DISABLED));
        valve_ui_touch(100, 260, true, false, fake_now);
        valve_ui_touch(100, 260, false, false, fake_now);
        assert(posts == before + 1);
        assert(last_action == VALVE_ACTION_DRAIN); /* unknown status can only request DRAIN */
    }
    valve_ui_set_status(&healthy);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_INVALID, VALVE_UNKNOWN);
    process();
    assert(strcmp(temperature->text, "---") == 0); /* malformed completion */
    valve_ui_set_status(&healthy);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    valve_ui_wake();
    emit(next_id, session, VALVE_CLIENT_EVENT_GET, VALVE_CLIENT_TIMEOUT, VALVE_UNKNOWN);
    process();
    assert(strcmp(temperature->text, "---") == 0); /* timed-out completion */
    valve_ui_set_status(&healthy);
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    valve_ui_show(false);
    assert(objects[0].flags & LV_OBJ_FLAG_HIDDEN);
    assert(temperature->parent == &objects[0]);
    valve_ui_show(true);
    assert(!(objects[0].flags & LV_OBJ_FLAG_HIDDEN));
    assert(strcmp(temperature->text, "6,4 °C") == 0);
    /* Saved short duration without a fresh capability cannot authorize START. */
    valve_ui_cancel_touch();
    selected_duration = 300;
    healthy.timed_shower_duration_supported = false;
    valve_ui_set_status(&healthy);
    assert(strcmp(objects[5].text, "HOLD 2s\n5 MIN") == 0);
    bool supported = true;
    assert(valve_ui_duration_capability_get(&supported) && !supported);
    int before_short = posts;
    valve_ui_touch(100, 260, true, false, fake_now);
    valve_ui_touch(100, 260, true, false, fake_now + 2000);
    assert(posts == before_short);
    valve_ui_cancel_touch();
    healthy.timed_shower_duration_supported = true;
    valve_ui_set_status(&healthy);
    assert(valve_ui_duration_capability_get(&supported) && supported);
    valve_duration_capability_snapshot_t snapshot;
    assert(valve_ui_duration_capability_snapshot_get(&snapshot));
    assert(snapshot.valid && snapshot.supported && snapshot.session == session &&
           snapshot.config_generation == config_generation && snapshot.received_at_ms == fake_now);
    valve_ui_touch(100, 260, true, false, fake_now);
    valve_ui_touch(100, 260, true, false, fake_now + 2000);
    assert(posts == before_short + 1 && last_duration == 300);
    assert(valve_ui_duration_capability_snapshot_get(&snapshot) && !snapshot.valid);
    valve_ui_cancel_touch();
    healthy.timed_shower_duration_supported = true;
    valve_ui_set_status(&healthy);
    fake_now += 11000;
    process();
    assert(!valve_ui_duration_capability_get(&supported));
    int before_stale = posts;
    valve_ui_touch(100, 260, true, false, fake_now);
    valve_ui_touch(100, 260, true, false, fake_now + 2000);
    assert(posts == before_stale); /* stale capability is not authorization */
    valve_ui_touch(100, 260, false, false, fake_now + 2001);
    assert(posts == before_stale + 1 && last_action == VALVE_ACTION_DRAIN);
    test_cross_task_capability_snapshot();
    puts("valve UI sequence tests passed");
}

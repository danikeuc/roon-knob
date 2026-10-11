#include "valve_ui_dial.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include "weather_admin_dial.h"
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
static lv_obj_t *s_temperature, *s_forecast_label, *s_forecast_icon;
static lv_obj_t *s_weather_sun, *s_weather_cloud, *s_weather_rain, *s_weather_snow;
static lv_obj_t *s_weather_fog, *s_weather_bolt, *s_weather_sleet, *s_weather_drizzle;
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
static uint32_t s_weather_generation;
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
    if (request) *request = (valve_request_t){.action = s_status.weather_assisted && s_status.weather_assistance_version==1 ?
                                                VALVE_ACTION_WEATHER_START : VALVE_ACTION_START,
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

/* Small vector equivalents of the approved weather-icons-v1 assets. Static
 * points remain alive for LVGL; no raster allocation or network asset loading. */
static void weather_line(lv_obj_t *parent, const lv_point_precise_t *points,
                         unsigned count, uint32_t color) {
    lv_obj_t *line=lv_line_create(parent);
    lv_line_set_points(line,points,count);
    lv_obj_set_style_line_color(line,lv_color_hex(color),0);
    lv_obj_set_style_line_width(line,2,0);
    lv_obj_set_style_line_rounded(line,true,0);
}
static void create_forecast(void) {
    s_forecast_icon=make_container(s_overlay,94,307,34,34);
    s_weather_sun=make_container(s_forecast_icon,0,0,34,34);
    lv_obj_t *disc=make_solid(s_weather_sun,11,9,12,12,SCREEN_COLOR,6);
    lv_obj_set_style_border_width(disc,2,0);
    lv_obj_set_style_border_color(disc,lv_color_hex(0xF2BF69),0);
    static const lv_point_precise_t rays[][2]={
        {{17,2},{17,5}},{{17,25},{17,28}},{{4,15},{7,15}},{{27,15},{30,15}},
        {{7,5},{9,7}},{{25,23},{27,25}},{{7,25},{9,23}},{{25,7},{27,5}}};
    for(unsigned i=0;i<8;++i)weather_line(s_weather_sun,rays[i],2,0xF2BF69);
    s_weather_cloud=make_container(s_forecast_icon,0,0,34,34);
    static const lv_point_precise_t cloud[]={
        {8,22},{5,21},{3,18},{3,15},{5,12},{9,11},{10,7},{13,4},
        {17,3},{21,4},{24,7},{25,11},{28,11},{31,13},{32,16},{31,19},
        {29,21},{26,22},{8,22}};
    weather_line(s_weather_cloud,cloud,sizeof cloud/sizeof cloud[0],ICON_COLOR);
    s_weather_rain=make_container(s_forecast_icon,0,0,34,34);
    static const lv_point_precise_t rain[][2]={{{10,26},{8,31}},{{18,26},{16,31}},{{26,26},{24,31}}};
    for(unsigned i=0;i<3;++i)weather_line(s_weather_rain,rain[i],2,0x74D9CD);
    s_weather_snow=make_container(s_forecast_icon,0,0,34,34);
    static const lv_point_precise_t snow[][2]={
        {{7,29},{15,29}},{{9,26},{13,32}},{{9,32},{13,26}},
        {{21,29},{29,29}},{{23,26},{27,32}},{{23,32},{27,26}}};
    for(unsigned i=0;i<6;++i)weather_line(s_weather_snow,snow[i],2,0x9BD9EC);
    s_weather_sleet=make_container(s_forecast_icon,0,0,34,34);
    weather_line(s_weather_sleet,rain[0],2,0x74D9CD);
    for(unsigned i=3;i<6;++i)weather_line(s_weather_sleet,snow[i],2,0x9BD9EC);
    s_weather_drizzle=make_container(s_forecast_icon,0,0,34,34);
    for(unsigned i=0;i<3;++i)make_solid(s_weather_drizzle,8+(int)i*8,27,3,3,0x74D9CD,2);
    s_weather_fog=make_container(s_forecast_icon,0,0,34,34);
    static const lv_point_precise_t fog[][2]={{{5,26},{28,26}},{{9,31},{24,31}}};
    for(unsigned i=0;i<2;++i)weather_line(s_weather_fog,fog[i],2,0x8CABB9);
    s_weather_bolt=make_container(s_forecast_icon,0,0,34,34);
    static const lv_point_precise_t bolt[]={{19,21},{14,27},{20,27},{15,33}};
    weather_line(s_weather_bolt,bolt,4,0xF2BF69);
    s_forecast_label=lv_label_create(s_overlay);
    lv_obj_set_pos(s_forecast_label,139,306);
    lv_obj_set_style_text_font(s_forecast_label,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(s_forecast_label,lv_color_hex(ICON_COLOR),0);
}
static void weather_visible(lv_obj_t *obj,bool visible) {
    if(visible)lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
static void render_forecast(bool current) {
    const valve_forecast_t *forecast=&s_status.forecast;
    bool available=current && forecast->available;
    weather_visible(s_forecast_icon,available && forecast->codes_available);
    if(!available) {
        lv_label_set_text(s_forecast_label,current && forecast->stale ? "Forecast\nstale" : "Forecast\nunavailable");
        return;
    }
    valve_status_t minimum={.temperature_available=true,.pipe_temperature_c=forecast->minima_c[1]};
    char temperature[24],label[48];
    valve_temperature_format(&minimum,temperature,sizeof temperature);
    snprintf(label,sizeof label,"Tomorrow\nmin %s",temperature);
    lv_label_set_text(s_forecast_label,label);
    if(!forecast->codes_available)return;
    unsigned code=forecast->weather_codes[1];
    bool freezing=code==56||code==57||code==66||code==67;
    bool snow=(code>=71&&code<=77)||code==85||code==86;
    weather_visible(s_weather_sun,code<=2);
    weather_visible(s_weather_cloud,code!=0);
    weather_visible(s_weather_fog,code==45||code==48);
    weather_visible(s_weather_rain,(!freezing&&code>=61&&code<=65)||(code>=80&&code<=82));
    weather_visible(s_weather_snow,snow);
    weather_visible(s_weather_drizzle,code==51||code==53||code==55);
    weather_visible(s_weather_sleet,freezing);
    weather_visible(s_weather_bolt,code>=95);
}
static void render(uint32_t now_ms) {
    if (!s_overlay) return;
    bool current = fresh(now_ms);
    render_forecast(current);
    lv_obj_align(s_countdown, LV_ALIGN_TOP_MID, 0, 40);
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
        if (valve_client_selected_duration_get(&seconds)) {
            bool unsupported = seconds < 600 && !s_status.timed_shower_duration_supported;
            snprintf(label, sizeof label, "%s\n%u MIN",
                     unsupported ? "BLOCKED" : "HOLD 2s", seconds / 60);
            if (unsupported) {
                /* Keep the saved duration visible; explain the disabled action
                 * below it without covering the temperature/status row. */
                lv_label_set_text(s_countdown, "Pi: 10 min only");
                lv_obj_align(s_countdown, LV_ALIGN_TOP_MID, 0, 213);
            }
        } else snprintf(label, sizeof label, "LOADING");
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
        lv_label_set_text(s_action_label, s_status.operation==VALVE_AUTO_SUPPLY ? "AUTO" : text);
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
    s_weather_generation = weather_admin_generation();
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
    create_forecast();
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
    uint32_t weather_generation = weather_admin_generation();
    if(weather_generation!=s_weather_generation) {
        s_weather_generation=weather_generation;
        consume_touch();valve_ui_set_unknown("Weather settings changed");
        valve_gate_link(&s_gate,valve_client_current_session());
        request_get(now_ms);
    }
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
            event->weather_generation != s_weather_generation ||
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
    if (s_weather_generation != weather_admin_generation() ||
        s_config_generation != valve_config_generation() || (s_config_generation & 1u)) {
        if (pressed) { s_touch_target_initialized = true; consume_touch(); }
        else valve_ui_cancel_touch();
        return;
    }
    bool action_area = x >= 45 && x <= 315 && y >= 230 && y <= 305;
    if (!pressed) {
        if (action_area && valve_drain_tap_allowed(s_touch_drain, s_touch_moved,
             VALVE_GESTURE_NONE, s_configured, atomic_load(&s_connected)))
            request_action(&(valve_request_t){.action = fresh(now_ms) && s_status.weather_assisted &&
                s_status.weather_assistance_version==1 && s_status.control_revision_available && s_status.action_nonce[0] ?
                VALVE_ACTION_WEATHER_STOP : VALVE_ACTION_DRAIN});
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

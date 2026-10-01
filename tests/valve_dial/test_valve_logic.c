#include "valve_logic.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *ready = "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":0}";
static const char *active = "{\"mode\":\"manual_timed\",\"state\":\"TIMED_SHOWER\",\"command\":\"SUPPLY\",\"reason\":\"timed\",\"remaining_seconds\":600}";
static void temperature_parse_cases(void) {
    valve_status_t s;
    assert(valve_status_parse(ready, strlen(ready), &s));
    assert(s.state == VALVE_DRAIN && !s.temperature_available);
    assert(valve_status_parse(active, strlen(active), &s));
    assert(s.state == VALVE_SUPPLY && !s.temperature_available);

    const struct { const char *number; float expected; } healthy[] = {
        {"6", 6.0f}, {"6.44", 6.44f}, {"-7.2", -7.2f},
        {"6.4e0", 6.4f}, {"-50", -50.0f}, {"120", 120.0f},
    };
    for (size_t i = 0; i < sizeof healthy / sizeof healthy[0]; i++) {
        char json[256];
        int n = snprintf(json, sizeof json,
                         "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":0,\"pipe_temperature_c\":%s,\"sensor_health\":\"HEALTHY\"}",
                         healthy[i].number);
        assert(n > 0 && (size_t)n < sizeof json);
        assert(valve_status_parse(json, (size_t)n, &s));
        assert(s.state == VALVE_DRAIN && s.temperature_available);
        assert(s.pipe_temperature_c == healthy[i].expected);
    }

    const char *unavailable[] = {
        "\"pipe_temperature_c\":null,\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"STALE\"",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"INVALID\"",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"CALIBRATION_REQUIRED\"",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"healthy\"",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"UNKNOWN\"",
        "\"pipe_temperature_c\":6.4",
        "\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":\"6.4\",\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":true,\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":[],\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":{},\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":7",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":null",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"HEALTHY\",\"pipe_temperature_c\":6.5",
        "\"pipe_temperature_c\":6.4,\"sensor_health\":\"HEALTHY\",\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":-50.01,\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":120.01,\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":-50.000001,\"sensor_health\":\"HEALTHY\"",
        "\"pipe_temperature_c\":120.000001,\"sensor_health\":\"HEALTHY\"",
        "\"nested\":{\"pipe_temperature_c\":6.4,\"sensor_health\":\"HEALTHY\"}",
    };
    for (size_t i = 0; i < sizeof unavailable / sizeof unavailable[0]; i++) {
        char json[512];
        int n = snprintf(json, sizeof json,
                         "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":0,%s}",
                         unavailable[i]);
        assert(n > 0 && (size_t)n < sizeof json);
        assert(valve_status_parse(json, (size_t)n, &s));
        assert(s.state == VALVE_DRAIN && s.remaining_seconds == 0);
        assert(!s.temperature_available);
    }
}
static void temperature_format_cases(void) {
    char buffer[32];
    valve_status_t s = {.pipe_temperature_c = 6.44f, .temperature_available = true};
    valve_temperature_format(&s, buffer, sizeof buffer);
    assert(strcmp(buffer, "6,4 °C") == 0);

    s.pipe_temperature_c = 6.46f;
    valve_temperature_format(&s, buffer, sizeof buffer);
    assert(strcmp(buffer, "6,5 °C") == 0);

    s.pipe_temperature_c = -7.2f;
    valve_temperature_format(&s, buffer, sizeof buffer);
    assert(strcmp(buffer, "-7,2 °C") == 0);

    s.pipe_temperature_c = -0.04f;
    valve_temperature_format(&s, buffer, sizeof buffer);
    assert(strcmp(buffer, "0,0 °C") == 0);

    s.temperature_available = false;
    valve_temperature_format(&s, buffer, sizeof buffer);
    assert(strcmp(buffer, "---") == 0);
    valve_temperature_format(NULL, buffer, sizeof buffer);
    assert(strcmp(buffer, "---") == 0);

    strcpy(buffer, "keep");
    valve_temperature_format(&s, NULL, sizeof buffer);
    valve_temperature_format(&s, buffer, 0);
    assert(strcmp(buffer, "keep") == 0);

    s.temperature_available = true;
    s.pipe_temperature_c = 6.44f;
    char short_buffer[4] = {'x', 'x', 'x', 'x'};
    valve_temperature_format(&s, short_buffer, sizeof short_buffer);
    assert(strcmp(short_buffer, "6,4") == 0);
}
static void parse_cases(void) {
    valve_status_t s;
    assert(valve_status_parse(ready, strlen(ready), &s) && s.state == VALVE_DRAIN && s.remaining_seconds == 0);
    assert(valve_status_parse(active, strlen(active), &s) && s.state == VALVE_SUPPLY && s.remaining_seconds == 600);
    const char *bad[] = {
        "{}", "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"SUPPLY\",\"reason\":\"x\",\"remaining_seconds\":0}",
        "{\"mode\":\"auto\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"x\",\"remaining_seconds\":0}",
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"x\",\"remaining_seconds\":1}",
        "{\"mode\":\"manual_timed\",\"state\":\"TIMED_SHOWER\",\"command\":\"SUPPLY\",\"reason\":\"x\",\"remaining_seconds\":-1}",
        "{\"mode\":\"manual_timed\",\"state\":\"TIMED_SHOWER\",\"command\":\"SUPPLY\",\"reason\":\"x\",\"remaining_seconds\":601}",
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"x\"}",
        "{\"mode\":\"manual_timed\",\"state\":\"FAULT\",\"command\":\"DRAIN\",\"reason\":\"x\",\"remaining_seconds\":0}",
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"x\",\"remaining_seconds\":0",
        "{\"mode\":\"manual_timed\",\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"x\",\"remaining_seconds\":0}",
        "{\"mode\":\"manual_timed\" \"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"x\",\"remaining_seconds\":0}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        assert(!valve_status_parse(bad[i], strlen(bad[i]), &s));
        assert(s.state == VALVE_UNKNOWN);
    }
    const char *nested = "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":0,\"forecast\":{\"days\":[{\"rain\":true},null,3.5]}}";
    assert(valve_status_parse(nested, strlen(nested), &s) && s.state == VALVE_DRAIN);
    const char *extra_bad[] = {
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":0,\"forecast\":{\"a\":1 \"b\":2}}",
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":0,\"forecast\":garbage}",
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"line\nfeed\",\"remaining_seconds\":0}",
        "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"ready\",\"remaining_seconds\":00}",
        "{\"mode\":\"manual_timed\",\"state\":\"TIMED_SHOWER\",\"command\":\"SUPPLY\",\"reason\":\"ready\",\"remaining_seconds\":0}",
    };
    for (size_t i = 0; i < sizeof extra_bad / sizeof extra_bad[0]; i++) {
        assert(!valve_status_parse(extra_bad[i], strlen(extra_bad[i]), &s));
        assert(s.state == VALVE_UNKNOWN);
    }
    const char invalid_utf8[] = "{\"mode\":\"manual_timed\",\"state\":\"MANUAL_DRAIN\",\"command\":\"DRAIN\",\"reason\":\"\xC0\xAF\",\"remaining_seconds\":0}";
    assert(!valve_status_parse(invalid_utf8, strlen(invalid_utf8), &s));
    char huge[2050]; memset(huge, ' ', sizeof huge); memcpy(huge, ready, strlen(ready));
    assert(!valve_status_parse(huge, sizeof huge, &s));
}

static uint32_t config_generation;
uint32_t valve_config_generation(void) { return config_generation; }
bool valve_config_load(char *url, size_t ul, char *token, size_t tl) {
    assert(ul >= 32 && tl >= 32);
    strcpy(url, "http://192.0.2.10:8081"); strcpy(token, "private-test-token"); return true;
}
static int next_status = 200, posts, gets, fail_transport, disconnect_during_request;
static int change_config_during_request;
static int reconnect_on_get, callbacks, recovery_callbacks;
static char trace[256];
static int mock_transport(const char *method, const char *url, const char *token,
                          int *status, char *body, size_t cap, size_t *len, void *ctx) {
    (void)ctx; assert(strcmp(token, "private-test-token") == 0);
    assert(strstr(url, token) == NULL && strstr(url, "?") == NULL);
    assert(cap == 2048);
    if (strcmp(method, "POST") == 0) {
        posts++;
        assert(strcmp(url, posts == 1 ? "http://192.0.2.10:8081/api/v1/display/actions/timed-shower"
                                      : "http://192.0.2.10:8081/api/v1/display/actions/drain") == 0);
        strcat(trace, "P");
    } else { gets++; assert(strcmp(method, "GET") == 0);
        assert(strcmp(url, "http://192.0.2.10:8081/api/v1/display/status") == 0); strcat(trace, "G"); }
    if (disconnect_during_request) {
        disconnect_during_request = 0;
        valve_client_on_disconnect();
    }
    if (reconnect_on_get && strcmp(method, "GET") == 0) {
        reconnect_on_get = 0;
        valve_client_on_disconnect();
        valve_client_on_reconnect();
    }
    if (change_config_during_request) {
        change_config_during_request = 0; config_generation += 2;
    }
    if (fail_transport) return -3;
    *status = next_status;
    const char *payload = strcmp(method, "POST") == 0 ? "{}" : ready;
    *len = strlen(payload); memcpy(body, payload, *len);
    return 0;
}
static void request_cases(void) {
    valve_status_t s;
    valve_client_test_transport(mock_transport, NULL);
    assert(valve_client_get_status(&s) == VALVE_CLIENT_OK && s.state == VALVE_DRAIN);
    next_status = 401; assert(valve_client_get_status(&s) == VALVE_CLIENT_UNAUTHORIZED && s.state == VALVE_UNKNOWN);
    next_status = 409; assert(valve_client_get_status(&s) == VALVE_CLIENT_CONFLICT);
    next_status = 500; assert(valve_client_get_status(&s) == VALVE_CLIENT_SERVER_ERROR);
    next_status = 200; fail_transport = 1;
    assert(valve_client_get_status(&s) == VALVE_CLIENT_TIMEOUT && s.state == VALVE_UNKNOWN);
    fail_transport = 0;
    posts = gets = 0; trace[0] = 0;
    assert(valve_client_post(VALVE_ACTION_START_600S, &s) == VALVE_CLIENT_OK);
    assert(posts == 1 && gets == 1 && strcmp(trace, "PG") == 0);
    valve_client_on_reconnect(); valve_client_test_run_pending();
    assert(posts == 1 && gets == 2 && strcmp(trace, "PGG") == 0);
    fail_transport = 1;
    assert(valve_client_post(VALVE_ACTION_DRAIN, &s) == VALVE_CLIENT_TIMEOUT);
    assert(s.state == VALVE_UNKNOWN);
    fail_transport = 0; valve_client_test_run_pending();
    assert(posts == 2 && gets == 3 && strcmp(trace, "PGGPG") == 0);
    assert(valve_client_post((valve_action_t)99, &s) == VALVE_CLIENT_INVALID && posts == 2);
}
static valve_client_event_t last_event;
static void callback(const valve_client_event_t *event, void *context) {
    (void)context; callbacks++; last_event = *event;
    if (event->kind == VALVE_CLIENT_EVENT_RECOVERY_GET) recovery_callbacks++;
}
static void scheduling_cases(void) {
    posts = gets = 0; trace[0] = 0; next_status = 200; fail_transport = 0;
    assert(valve_client_start(callback, NULL));
    valve_client_on_disconnect();
    assert(!valve_client_request_post(VALVE_ACTION_START_600S));
    valve_client_on_reconnect();
    assert(valve_client_request_post(VALVE_ACTION_START_600S));
    valve_client_on_disconnect();
    valve_client_on_reconnect();
    valve_client_test_run_pending();
    assert(posts == 0 && gets >= 1);
    assert(valve_client_request_post(VALVE_ACTION_START_600S));
    valve_client_test_run_pending();
    assert(posts == 1 && gets >= 2);
    posts = gets = 0; trace[0] = 0;
    valve_client_on_disconnect(); valve_client_on_reconnect(); valve_client_test_run_pending();
    for (int i = 0; i < 8; i++) assert(valve_client_request_post(VALVE_ACTION_START_600S));
    gets = 0;
    valve_client_on_disconnect(); valve_client_on_reconnect();
    valve_client_test_run_pending();
    assert(posts == 0 && gets == 1);
    callbacks = 0;
    valve_client_on_reconnect();
    valve_client_test_run_pending();
    callbacks = 0;
    disconnect_during_request = 1;
    assert(valve_client_request_get());
    valve_client_test_run_pending();
    assert(callbacks == 0);
}
static void completion_order_cases(void) {
    valve_observation_gate_t gate = {0};
    valve_client_on_reconnect();
    valve_client_test_run_pending();
    valve_gate_link(&gate, valve_client_current_session());
    uint32_t old_get, action;
    assert(valve_client_request_get_tagged(&old_get));
    assert(valve_client_request_post_tagged(VALVE_ACTION_START_600S, &action));
    assert(action > old_get);
    valve_gate_action(&gate, action);
    assert(!valve_gate_accept(&gate, gate.session, old_get, true));
    assert(gate.pending);
    assert(valve_gate_accept(&gate, gate.session, action, true));
    assert(!gate.pending);
    valve_client_test_run_pending();
    assert(last_event.request_id == action);
    assert(last_event.kind == VALVE_CLIENT_EVENT_POST_STATUS);

    valve_gate_link(&gate, valve_client_current_session());
    uint32_t ids[5];
    for (int i = 0; i < 5; i++) assert(valve_client_request_get_tagged(&ids[i]));
    uint32_t barrier;
    assert(valve_client_request_get_tagged(&barrier));
    valve_gate_overflow(&gate, barrier);
    for (int i = 0; i < 5; i++)
        assert(!valve_gate_accept(&gate, gate.session, ids[i], true));
    assert(gate.pending);
    assert(valve_gate_accept(&gate, gate.session, barrier, true));
    valve_client_test_run_pending();
}
static void recovery_session_cases(void) {
    valve_client_on_disconnect();
    valve_client_on_reconnect();
    valve_client_test_run_pending();
    posts = gets = callbacks = recovery_callbacks = 0;
    next_status = 500;
    reconnect_on_get = 1;
    uint32_t request_id;
    uint32_t original_session = valve_client_current_session();
    assert(valve_client_request_post_tagged(VALVE_ACTION_START_600S, &request_id));
    valve_client_test_run_pending();
    assert(posts == 1);
    assert(valve_client_current_session() != original_session);
    assert(recovery_callbacks == 0);
    assert(last_event.session == valve_client_current_session());
    next_status = 200;
}
static void dial_cases(void) {
    valve_gesture_context_t normal = {0};
    for (int rotation = 0; rotation <= 180; rotation += 180) {
        assert(valve_gesture_classify(80, 5, 250, rotation, normal) == VALVE_GESTURE_SWITCH_SCREEN);
        assert(valve_gesture_classify(-80, 5, 250, rotation, normal) == VALVE_GESTURE_SWITCH_SCREEN);
        assert(valve_gesture_classify(5, rotation == 180 ? 80 : -80, 250, rotation, normal) == VALVE_GESTURE_ART_UP);
        assert(valve_gesture_classify(5, rotation == 180 ? -80 : 80, 250, rotation, normal) == VALVE_GESTURE_ART_DOWN);
        assert(valve_gesture_classify(70, 70, 250, rotation, normal) == VALVE_GESTURE_NONE);
        assert(valve_gesture_classify(80, 0, 501, rotation, normal) == VALVE_GESTURE_NONE);
    }
    normal.zone_picker = true;
    assert(valve_gesture_classify(100, 0, 100, 0, normal) == VALVE_GESTURE_NONE);
    normal.zone_picker = false; normal.settings = true;
    assert(valve_gesture_classify(100, 0, 100, 0, normal) == VALVE_GESTURE_NONE);
    normal.settings = false; normal.art_mode = true;
    assert(valve_gesture_classify(100, 0, 100, 0, normal) == VALVE_GESTURE_NONE);
    normal.art_mode = false; normal.wake_touch = true;
    assert(valve_gesture_classify(100, 0, 100, 0, normal) == VALVE_GESTURE_NONE);

    valve_hold_t hold = {0};
    assert(!valve_hold_update(&hold, true, false, 100));
    assert(!valve_hold_update(&hold, true, false, 2099));
    assert(!valve_hold_update(&hold, false, false, 2100));
    assert(!valve_hold_update(&hold, true, false, 3000));
    assert(!valve_hold_update(&hold, true, true, 4000));
    assert(!valve_hold_update(&hold, false, false, 4001));
    assert(!valve_hold_update(&hold, true, false, 5000));
    assert(!valve_hold_update(&hold, true, false, 6999));
    assert(valve_hold_update(&hold, true, false, 7000));
    assert(!valve_hold_update(&hold, true, false, 8000));
    assert(!valve_hold_update(&hold, false, false, 8001));
    assert(valve_supply_allowed(VALVE_DRAIN, true, true, 9999));
    assert(!valve_supply_allowed(VALVE_DRAIN, true, true, 10000));
    assert(!valve_supply_allowed(VALVE_UNKNOWN, true, true, 0));
    assert(!valve_supply_allowed(VALVE_SUPPLY, true, true, 0));
    assert(!valve_supply_allowed(VALVE_DRAIN, false, true, 0));
    assert(valve_drain_allowed(true, true));
    assert(!valve_drain_allowed(true, false));
    assert(!valve_drain_allowed(false, true));
    assert(valve_drain_tap_allowed(true, false, VALVE_GESTURE_NONE, true, true));
    assert(!valve_drain_tap_allowed(true, true, VALVE_GESTURE_SWITCH_SCREEN, true, true));
    assert(!valve_drain_tap_allowed(false, false, VALVE_GESTURE_NONE, true, true));
    assert(!valve_drain_tap_allowed(true, false, VALVE_GESTURE_NONE, true, false));
    assert(valve_touch_coordinate(180, 0) == 180);
    assert(valve_touch_coordinate(70, 180) == 289);
}
static void config_change_cases(void) {
    valve_client_on_reconnect(); valve_client_test_run_pending();
    posts = gets = callbacks = 0;
    assert(valve_client_request_post(VALVE_ACTION_START_600S));
    config_generation += 2;
    valve_client_test_run_pending();
    assert(posts == 0 && gets == 0 && callbacks == 0);
    assert(!valve_client_request_post_for_config(VALVE_ACTION_START_600S, config_generation - 2, NULL));
    /* A config mutation inside POST must not trigger GET using new credentials. */
    change_config_during_request = 1;
    assert(valve_client_request_post(VALVE_ACTION_START_600S));
    valve_client_test_run_pending();
    assert(posts == 1 && gets == 0 && callbacks == 0);
    change_config_during_request = 1;
    assert(valve_client_request_get());
    valve_client_test_run_pending();
    assert(gets == 1 && callbacks == 0);
    /* An odd generation represents save/clear still in progress. */
    config_generation++;
    assert(!valve_client_request_get());
    assert(!valve_client_request_post(VALVE_ACTION_DRAIN));
    config_generation++;
}
int main(void) { temperature_parse_cases(); temperature_format_cases(); parse_cases(); request_cases(); scheduling_cases(); completion_order_cases(); recovery_session_cases(); dial_cases(); config_change_cases(); puts("valve parser/client tests passed"); }

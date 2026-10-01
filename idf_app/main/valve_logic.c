#include "valve_logic.h"
#define JSMN_STATIC
#include "third_party/jsmn.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define STATUS_MAX_BYTES 2048
#define STATUS_MAX_TOKENS 128

typedef struct { const char *text; size_t len; size_t pos; } json_cursor_t;
static void json_space(json_cursor_t *c) {
    while (c->pos < c->len && (c->text[c->pos] == ' ' || c->text[c->pos] == '\t' ||
           c->text[c->pos] == '\n' || c->text[c->pos] == '\r')) c->pos++;
}
static bool json_hex(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}
static bool json_utf8(json_cursor_t *c, unsigned char first) {
    unsigned count;
    if (first >= 0xC2 && first <= 0xDF) count = 1;
    else if (first >= 0xE0 && first <= 0xEF) count = 2;
    else if (first >= 0xF0 && first <= 0xF4) count = 3;
    else return false;
    if (c->len - c->pos < count) return false;
    unsigned char second = (unsigned char)c->text[c->pos];
    if (second < 0x80 || second > 0xBF) return false;
    if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second > 0x9F) ||
        (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F)) return false;
    for (unsigned i = 1; i < count; i++) {
        unsigned char follow = (unsigned char)c->text[c->pos + i];
        if (follow < 0x80 || follow > 0xBF) return false;
    }
    c->pos += count;
    return true;
}
static bool json_string(json_cursor_t *c) {
    if (c->pos >= c->len || c->text[c->pos++] != '"') return false;
    while (c->pos < c->len) {
        unsigned char ch = (unsigned char)c->text[c->pos++];
        if (ch == '"') return true;
        if (ch < 0x20) return false;
        if (ch >= 0x80 && !json_utf8(c, ch)) return false;
        if (ch != '\\') continue;
        if (c->pos >= c->len) return false;
        char escape = c->text[c->pos++];
        if (escape && strchr("\"\\/bfnrt", escape)) continue;
        if (escape != 'u' || c->len - c->pos < 4) return false;
        for (int i = 0; i < 4; i++) if (!json_hex(c->text[c->pos++])) return false;
    }
    return false;
}
static bool json_number(json_cursor_t *c) {
    if (c->pos < c->len && c->text[c->pos] == '-') c->pos++;
    if (c->pos >= c->len) return false;
    if (c->text[c->pos] == '0') c->pos++;
    else if (c->text[c->pos] >= '1' && c->text[c->pos] <= '9') {
        do { c->pos++; } while (c->pos < c->len && isdigit((unsigned char)c->text[c->pos]));
    } else return false;
    if (c->pos < c->len && c->text[c->pos] == '.') {
        c->pos++;
        if (c->pos >= c->len || !isdigit((unsigned char)c->text[c->pos])) return false;
        do { c->pos++; } while (c->pos < c->len && isdigit((unsigned char)c->text[c->pos]));
    }
    if (c->pos < c->len && (c->text[c->pos] == 'e' || c->text[c->pos] == 'E')) {
        c->pos++;
        if (c->pos < c->len && (c->text[c->pos] == '+' || c->text[c->pos] == '-')) c->pos++;
        if (c->pos >= c->len || !isdigit((unsigned char)c->text[c->pos])) return false;
        do { c->pos++; } while (c->pos < c->len && isdigit((unsigned char)c->text[c->pos]));
    }
    return true;
}
static bool json_value(json_cursor_t *c, unsigned depth);
static bool json_object(json_cursor_t *c, unsigned depth) {
    c->pos++; json_space(c);
    if (c->pos < c->len && c->text[c->pos] == '}') { c->pos++; return true; }
    for (;;) {
        if (!json_string(c)) return false;
        json_space(c);
        if (c->pos >= c->len || c->text[c->pos++] != ':') return false;
        if (!json_value(c, depth + 1)) return false;
        json_space(c);
        if (c->pos >= c->len) return false;
        if (c->text[c->pos] == '}') { c->pos++; return true; }
        if (c->text[c->pos++] != ',') return false;
        json_space(c);
    }
}
static bool json_array(json_cursor_t *c, unsigned depth) {
    c->pos++; json_space(c);
    if (c->pos < c->len && c->text[c->pos] == ']') { c->pos++; return true; }
    for (;;) {
        if (!json_value(c, depth + 1)) return false;
        json_space(c);
        if (c->pos >= c->len) return false;
        if (c->text[c->pos] == ']') { c->pos++; return true; }
        if (c->text[c->pos++] != ',') return false;
        json_space(c);
    }
}
static bool json_value(json_cursor_t *c, unsigned depth) {
    if (depth > 16) return false;
    json_space(c);
    if (c->pos >= c->len) return false;
    char ch = c->text[c->pos];
    if (ch == '{') return json_object(c, depth);
    if (ch == '[') return json_array(c, depth);
    if (ch == '"') return json_string(c);
    const char *literal = ch == 't' ? "true" : ch == 'f' ? "false" : ch == 'n' ? "null" : NULL;
    if (literal) {
        size_t n = strlen(literal);
        if (c->len - c->pos < n || memcmp(c->text + c->pos, literal, n) != 0) return false;
        c->pos += n; return true;
    }
    return json_number(c);
}
static bool json_well_formed(const char *json, size_t len) {
    json_cursor_t c = {.text = json, .len = len, .pos = 0};
    json_space(&c);
    if (c.pos >= len || c.text[c.pos] != '{' || !json_value(&c, 0)) return false;
    json_space(&c);
    return c.pos == len;
}
static bool equal_token(const char *json, const jsmntok_t *tok, const char *value) {
    size_t n = (size_t)(tok->end - tok->start);
    return tok->type == JSMN_STRING && strlen(value) == n &&
           memcmp(json + tok->start, value, n) == 0;
}
static int next_token(const jsmntok_t *tokens, int count, int index) {
    int end = tokens[index].end;
    index++;
    while (index < count && tokens[index].start < end) index++;
    return index;
}
static bool gap(const char *json, int start, int end, char delimiter) {
    int found = 0;
    for (int p = start; p < end; p++) {
        if (json[p] == delimiter && !found) found = 1;
        else if (!isspace((unsigned char)json[p])) return false;
    }
    return found == 1;
}
static bool number_token(const char *json, const jsmntok_t *tok, uint16_t *value) {
    if (tok->type != JSMN_PRIMITIVE || tok->start == tok->end) return false;
    unsigned result = 0;
    for (int i = tok->start; i < tok->end; i++) {
        unsigned char c = (unsigned char)json[i];
        if (c < '0' || c > '9') return false;
        result = result * 10 + c - '0';
        if (result > 600) return false;
    }
    *value = (uint16_t)result;
    return true;
}
static bool temperature_token(const char *json, const jsmntok_t *tok, float *value) {
    enum { TEMPERATURE_TOKEN_MAX = 48 };
    if (tok->type != JSMN_PRIMITIVE || tok->start == tok->end) return false;
    size_t length = (size_t)(tok->end - tok->start);
    if (length >= TEMPERATURE_TOKEN_MAX) return false;
    char text[TEMPERATURE_TOKEN_MAX];
    memcpy(text, json + tok->start, length);
    text[length] = '\0';
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (end != text + length || !isfinite(parsed) || parsed < -50.0 || parsed > 120.0)
        return false;
    *value = (float)parsed;
    return true;
}
bool valve_status_parse(const char *json, size_t len, valve_status_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    out->state = VALVE_UNKNOWN;
    if (!json || len == 0 || len > STATUS_MAX_BYTES || !json_well_formed(json, len)) return false;
    jsmntok_t tokens[STATUS_MAX_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, len, tokens, STATUS_MAX_TOKENS);
    if (count < 1 || tokens[0].type != JSMN_OBJECT) return false;
    for (size_t p = (size_t)tokens[0].end; p < len; p++)
        if (!isspace((unsigned char)json[p])) return false;
    for (int p = 0; p < tokens[0].start; p++)
        if (!isspace((unsigned char)json[p])) return false;
    unsigned seen = 0;
    bool temperature_seen = false, temperature_valid = false;
    bool health_seen = false, health_valid = false;
    float temperature = 0.0f;
    int previous_end = tokens[0].start + 1;
    bool first = true;
    bool mode = false, drain_state = false, supply_state = false;
    bool drain_command = false, supply_command = false;
    uint16_t remaining = 0;
    char reason[sizeof out->reason] = {0};
    for (int i = 1; i < count && tokens[i].start < tokens[0].end;) {
        if (tokens[i].type != JSMN_STRING || i + 1 >= count) return false;
        const jsmntok_t *key = &tokens[i];
        const jsmntok_t *val = &tokens[i + 1];
        int key_open = key->start - 1;
        int value_open = val->type == JSMN_STRING ? val->start - 1 : val->start;
        if (first) {
            for (int p = previous_end; p < key_open; p++)
                if (!isspace((unsigned char)json[p])) return false;
        } else if (!gap(json, previous_end, key_open, ',')) return false;
        if (!gap(json, key->end + 1, value_open, ':')) return false;
        unsigned bit = 0;
        if (equal_token(json, key, "mode")) { bit = 1; mode = equal_token(json, val, "manual_timed"); }
        else if (equal_token(json, key, "state")) {
            bit = 2; drain_state = equal_token(json, val, "MANUAL_DRAIN");
            supply_state = equal_token(json, val, "TIMED_SHOWER");
        } else if (equal_token(json, key, "command")) {
            bit = 4; drain_command = equal_token(json, val, "DRAIN");
            supply_command = equal_token(json, val, "SUPPLY");
        } else if (equal_token(json, key, "reason")) {
            bit = 8;
            size_t n = (size_t)(val->end - val->start);
            if (val->type != JSMN_STRING || n >= sizeof reason) return false;
            memcpy(reason, json + val->start, n); reason[n] = 0;
        } else if (equal_token(json, key, "remaining_seconds")) {
            bit = 16; if (!number_token(json, val, &remaining)) return false;
        } else if (equal_token(json, key, "pipe_temperature_c")) {
            if (temperature_seen) temperature_valid = false;
            else temperature_valid = temperature_token(json, val, &temperature);
            temperature_seen = true;
        } else if (equal_token(json, key, "sensor_health")) {
            if (health_seen) health_valid = false;
            else health_valid = equal_token(json, val, "HEALTHY");
            health_seen = true;
        }
        if (bit && (seen & bit)) return false;
        seen |= bit;
        previous_end = val->end + (val->type == JSMN_STRING ? 1 : 0);
        first = false;
        i = next_token(tokens, count, i + 1);
    }
    for (int p = previous_end; p < tokens[0].end - 1; p++)
        if (!isspace((unsigned char)json[p])) return false;
    if (seen != 31 || !mode) return false;
    if (drain_state && drain_command && remaining == 0) out->state = VALVE_DRAIN;
    else if (supply_state && supply_command && remaining > 0) out->state = VALVE_SUPPLY;
    else return false;
    out->remaining_seconds = remaining;
    memcpy(out->reason, reason, sizeof reason);
    if (temperature_seen && temperature_valid && health_seen && health_valid) {
        out->pipe_temperature_c = temperature;
        out->temperature_available = true;
    }
    return true;
}

void valve_temperature_format(const valve_status_t *status, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    if (!status || !status->temperature_available) {
        (void)snprintf(buffer, buffer_size, "---");
        return;
    }

    char formatted[32];
    (void)snprintf(formatted, sizeof formatted, "%.1f °C", (double)status->pipe_temperature_c);
    if (formatted[0] == '-' && formatted[1] == '0' &&
        formatted[2] == '.' && formatted[3] == '0') {
        memmove(formatted, formatted + 1, strlen(formatted));
    }
    for (char *p = formatted; *p; p++) {
        if (*p == '.') {
            *p = ',';
            break;
        }
    }
    (void)snprintf(buffer, buffer_size, "%s", formatted);
}

valve_gesture_t valve_gesture_classify(int dx, int dy, uint32_t elapsed_ms,
                                       int rotation, valve_gesture_context_t context) {
    if (context.wake_touch || context.zone_picker || context.settings || elapsed_ms > 500)
        return VALVE_GESTURE_NONE;
    if (rotation == 180) { dx = -dx; dy = -dy; }
    if (abs(dx) >= 60 && abs(dx) > abs(dy) && !context.art_mode)
        return VALVE_GESTURE_SWITCH_SCREEN;
    if (abs(dy) >= 60 && abs(dy) > abs(dx))
        return dy < 0 ? VALVE_GESTURE_ART_UP : VALVE_GESTURE_ART_DOWN;
    return VALVE_GESTURE_NONE;
}

bool valve_hold_update(valve_hold_t *hold, bool pressed, bool moved, uint32_t now_ms) {
    if (!hold) return false;
    if (!pressed) { *hold = (valve_hold_t){0}; return false; }
    if (moved) { hold->cancelled = true; return false; }
    if (!hold->tracking) { hold->tracking = true; hold->started_ms = now_ms; return false; }
    if (hold->cancelled || hold->emitted || now_ms - hold->started_ms < 2000) return false;
    hold->emitted = true;
    return true;
}

bool valve_supply_allowed(valve_state_t state, bool configured, bool connected, uint32_t age_ms) {
    return state == VALVE_DRAIN && configured && connected && age_ms < 10000;
}
bool valve_drain_allowed(bool configured, bool connected) {
    return configured && connected;
}
bool valve_drain_tap_allowed(bool started_on_drain, bool moved,
                             valve_gesture_t gesture, bool configured, bool connected) {
    return started_on_drain && !moved && gesture == VALVE_GESTURE_NONE &&
           valve_drain_allowed(configured, connected);
}
int valve_touch_coordinate(int raw, int rotation) {
    return rotation == 180 ? 359 - raw : raw;
}
void valve_gate_link(valve_observation_gate_t *gate, uint32_t session) {
    if (!gate) return;
    *gate = (valve_observation_gate_t){.session = session, .pending = true};
}
void valve_gate_action(valve_observation_gate_t *gate, uint32_t request_id) {
    if (!gate) return;
    gate->minimum_request_id = request_id;
    gate->pending = true;
}
void valve_gate_overflow(valve_observation_gate_t *gate, uint32_t barrier_id) {
    if (!gate) return;
    gate->minimum_request_id = barrier_id ? barrier_id : UINT32_MAX;
    gate->pending = true;
}
bool valve_gate_accept(valve_observation_gate_t *gate, uint32_t session,
                       uint32_t request_id, bool valid_status) {
    if (!gate || !valid_status || session != gate->session || request_id == 0 ||
        request_id < gate->minimum_request_id) return false;
    gate->minimum_request_id = request_id;
    gate->pending = false;
    return true;
}

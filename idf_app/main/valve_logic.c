#include "valve_logic.h"
#define JSMN_STATIC
#include "third_party/jsmn.h"
#include <ctype.h>
#include <string.h>

#define STATUS_MAX_BYTES 2048
#define STATUS_MAX_TOKENS 128

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
bool valve_status_parse(const char *json, size_t len, valve_status_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    out->state = VALVE_UNKNOWN;
    if (!json || len == 0 || len > STATUS_MAX_BYTES) return false;
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
    else if (supply_state && supply_command) out->state = VALVE_SUPPLY;
    else return false;
    out->remaining_seconds = remaining;
    memcpy(out->reason, reason, sizeof reason);
    return true;
}

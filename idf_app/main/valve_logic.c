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
bool valve_json_object_valid(const char *json,size_t length) { return json && json_well_formed(json,length); }
static bool equal_token(const char *json, const jsmntok_t *tok, const char *value) {
    size_t n = (size_t)(tok->end - tok->start);
    return tok->type == JSMN_STRING && strlen(value) == n &&
           memcmp(json + tok->start, value, n) == 0;
}
static bool equal_optional_key(const char *json, const jsmntok_t *tok, const char *value) {
    if (tok->type != JSMN_STRING) return false;
    int p = tok->start;
    for (const unsigned char *expected = (const unsigned char *)value; *expected; expected++) {
        if (p >= tok->end) return false;
        unsigned char ch = (unsigned char)json[p++];
        if (ch == '\\') {
            if (p >= tok->end || json[p++] != 'u' || tok->end - p < 4) return false;
            unsigned codepoint = 0;
            for (int digit = 0; digit < 4; digit++) {
                unsigned char hex = (unsigned char)json[p++];
                if (!json_hex((char)hex)) return false;
                codepoint = codepoint * 16u +
                    (hex <= '9' ? (unsigned)(hex - '0') : (unsigned)(hex <= 'F' ? hex - 'A' : hex - 'a') + 10u);
            }
            if (codepoint != *expected) return false;
        } else if (ch != *expected) return false;
    }
    return p == tok->end;
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
static bool revision_token(const char *json, const jsmntok_t *tok, uint64_t *value) {
    if (tok->type != JSMN_PRIMITIVE || tok->start == tok->end) return false;
    uint64_t result = 0;
    for (int i=tok->start; i<tok->end; ++i) {
        unsigned char c=(unsigned char)json[i];
        if (c<'0' || c>'9' || result > (VALVE_MAX_REVISION - (c-'0'))/10) return false;
        result=result*10+(c-'0');
    }
    *value=result; return true;
}
static bool null_token(const char *json, const jsmntok_t *tok) {
    return tok->type==JSMN_PRIMITIVE && tok->end-tok->start==4 && !memcmp(json+tok->start,"null",4);
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
bool valve_weather_code_valid(unsigned code) {
    switch (code) {
    case 0: case 1: case 2: case 3: case 45: case 48:
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 71: case 73: case 75: case 77: case 80: case 81: case 82:
    case 85: case 86: case 95: case 96: case 97: case 99: return true;
    default: return false;
    }
}
static bool primitive_true(const char *json, const jsmntok_t *tok) {
    return tok->type == JSMN_PRIMITIVE && tok->end - tok->start == 4 &&
        !memcmp(json + tok->start, "true", 4);
}
static bool forecast_date(const char *text, int length, char out[11]) {
    if (length != 10 || text[4] != '-' || text[7] != '-') return false;
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9')) return false;
    int year = (text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0';
    int month = (text[5]-'0')*10+text[6]-'0', day = (text[8]-'0')*10+text[9]-'0';
    const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 1900 || month < 1 || month > 12 || day < 1 ||
        day > days[month-1] + (month == 2 && year%4 == 0 && (year%100 != 0 || year%400 == 0))) return false;
    memcpy(out, text, 10); out[10] = 0; return true;
}
static int forecast_day_number(const char date[11]) {
    int year=atoi(date),month=atoi(date+5),day=atoi(date+8);
    static const int before_month[]={0,31,59,90,120,151,181,212,243,273,304,334};
    int previous=year-1;
    return previous*365+previous/4-previous/100+previous/400+before_month[month-1]+day+
        (month>2 && year%4==0 && (year%100!=0 || year%400==0));
}
static bool forecast_timestamp(const char *text,int length) {
    char date[11];
    if(length<20 || length>40 || !forecast_date(text,10,date) || text[10]!='T' || text[13]!=':' || text[16]!=':')return false;
    const int positions[]={11,12,14,15,17,18};
    for(unsigned i=0;i<6;++i)if(text[positions[i]]<'0'||text[positions[i]]>'9')return false;
    if((text[11]-'0')*10+text[12]-'0'>23 || (text[14]-'0')*10+text[15]-'0'>59 ||
       (text[17]-'0')*10+text[18]-'0'>59)return false;
    int i=19;
    if(text[i]=='.') {
        int start=++i;
        while(i<length && text[i]>='0'&&text[i]<='9')++i;
        if(i==start || i-start>6)return false;
    }
    if(i==length-1 && text[i]=='Z')return true;
    if(length-i!=6 || (text[i]!='+'&&text[i]!='-') || text[i+3]!=':')return false;
    for(int n=1;n<6;++n)if(n!=3&&(text[i+n]<'0'||text[i+n]>'9'))return false;
    return (text[i+1]-'0')*10+text[i+2]-'0'<=23 && (text[i+4]-'0')*10+text[i+5]-'0'<=59;
}
static bool forecast_parse(const char *json, const jsmntok_t *tokens, int count,
                           int object, valve_forecast_t *out) {
    if (tokens[object].type != JSMN_OBJECT) return false;
    const char *names[] = {"available", "reason", "dates", "minima_c", "last_successful_check", "weather_codes"};
    int fields[6] = {-1,-1,-1,-1,-1,-1};
    for (int i = object+1; i < count && tokens[i].start < tokens[object].end;) {
        if (tokens[i].type != JSMN_STRING || i+1 >= count) return false;
        for (int n = 0; n < 6; ++n) if (equal_optional_key(json, &tokens[i], names[n])) {
            if (fields[n] != -1) {
                if (n == 5) fields[n] = -2; /* Invalid optional codes cannot hide minima. */
                else return false;
            } else fields[n] = i+1;
        }
        i = next_token(tokens, count, i+1);
    }
    for (int n = 0; n < 5; ++n) if (fields[n] < 0) return false;
    out->stale = equal_token(json, &tokens[fields[1]], "weather_stale");
    if (!primitive_true(json, &tokens[fields[0]]) ||
        (!equal_token(json, &tokens[fields[1]], "eligible") &&
         !equal_token(json, &tokens[fields[1]], "cold_forecast"))) return false;
    const jsmntok_t *last = &tokens[fields[4]];
    int length = last->end-last->start;
    if (last->type != JSMN_STRING || !forecast_timestamp(json+last->start,length)) return false;
    memcpy(out->checked_at, json+last->start, (size_t)length);
    for (int field = 2; field <= 3; ++field) {
        int array = fields[field], item = array+1;
        if (tokens[array].type != JSMN_ARRAY || tokens[array].size != VALVE_FORECAST_DAYS) return false;
        for (int day = 0; day < VALVE_FORECAST_DAYS; ++day) {
            if (item >= count) return false;
            const jsmntok_t *tok = &tokens[item];
            if (field == 2) {
                if (tok->type != JSMN_STRING || !forecast_date(json+tok->start, tok->end-tok->start, out->dates[day]) ||
                    (day && forecast_day_number(out->dates[day])-forecast_day_number(out->dates[day-1]) != 1)) return false;
            } else {
                /* Weather range is wider below zero than pipe telemetry. */
                if (tok->type != JSMN_PRIMITIVE || tok->end-tok->start >= 48) return false;
                char number[48]; int n=tok->end-tok->start;
                memcpy(number,json+tok->start,(size_t)n); number[n]=0;
                char *end; double minimum=strtod(number,&end);
                if (end != number+n || !isfinite(minimum) || minimum < -100 || minimum > 100) return false;
                out->minima_c[day]=(float)minimum;
            }
            item = next_token(tokens,count,item);
        }
    }
    out->available = true;
    int codes = fields[5];
    if (codes >= 0 && tokens[codes].type == JSMN_ARRAY && tokens[codes].size == VALVE_FORECAST_DAYS) {
        bool valid = true; int item = codes+1;
        for (int day=0; day<VALVE_FORECAST_DAYS; ++day) {
            uint16_t code;
            if (item >= count || !number_token(json,&tokens[item],&code) || !valve_weather_code_valid(code)) { valid=false; break; }
            out->weather_codes[day]=(uint8_t)code;
            item=next_token(tokens,count,item);
        }
        out->codes_available=valid;
        if (!valid) memset(out->weather_codes,0,sizeof out->weather_codes);
    }
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
    int forecast_object = -1;
    bool forecast_duplicate = false;
    bool temperature_seen = false, temperature_valid = false;
    bool health_seen = false, health_valid = false;
    bool capability_seen = false, capability_valid = false;
    float temperature = 0.0f;
    int previous_end = tokens[0].start + 1;
    bool first = true;
    bool mode = false, drain_state = false, supply_state = false, auto_state = false, fault_state = false;
    unsigned weather_seen = 0;
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
        if (equal_optional_key(json, key, "mode")) {
            bit = 1; mode = equal_token(json, val, "manual_timed");
            out->weather_assisted = equal_token(json, val, "weather_assisted");
        }
        else if (equal_token(json, key, "state")) {
            bit = 2; drain_state = equal_token(json, val, "MANUAL_DRAIN");
            supply_state = equal_token(json, val, "TIMED_SHOWER");
            auto_state = equal_token(json, val, "AUTO_SUPPLY");
            fault_state = equal_token(json, val, "FAULT");
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
        } else if (equal_optional_key(json, key, "weather_assistance_version")) {
            if (weather_seen & 1) return false;
            weather_seen |= 1;
            uint64_t version;
            if (!revision_token(json,val,&version) || version != 1) return false;
            out->weather_assistance_version = 1;
        } else if (equal_optional_key(json, key, "operation")) {
            if (weather_seen & 2) return false;
            weather_seen |= 2;
            const char *names[] = {"", "MANUAL_IDLE", "MANUAL_ACTIVE", "AUTO_SUPPLY", "USER_OFF", "FAULT"};
            for (unsigned op=1; op<6; ++op) if (equal_token(json,val,names[op])) out->operation=(valve_operation_t)op;
            if (out->operation == VALVE_OPERATION_UNKNOWN) return false;
        } else if (equal_optional_key(json, key, "control_revision")) {
            if (weather_seen & 4) return false;
            weather_seen |= 4;
            if (!null_token(json,val)) {
                if (!revision_token(json,val,&out->control_revision)) return false;
                out->control_revision_available = true;
            }
        } else if (equal_optional_key(json, key, "action_nonce")) {
            if (weather_seen & 8) return false;
            weather_seen |= 8;
            if (!null_token(json,val)) {
                if (val->type!=JSMN_STRING || val->end-val->start!=43) return false;
                for (int n=val->start; n<val->end; ++n) {
                    char c=json[n];
                    if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return false;
                }
                memcpy(out->action_nonce,json+val->start,43);
            }
        } else if (equal_optional_key(json, key, "weather")) {
            if (forecast_object >= 0) forecast_duplicate = true;
            else forecast_object = i+1;
        } else if (equal_optional_key(json, key, "pipe_temperature_c")) {
            if (temperature_seen) temperature_valid = false;
            else temperature_valid = temperature_token(json, val, &temperature);
            temperature_seen = true;
        } else if (equal_optional_key(json, key, "timed_shower_duration_supported")) {
            if (capability_seen) capability_valid = false;
            else capability_valid = val->type == JSMN_PRIMITIVE &&
                val->end - val->start == 4 && memcmp(json + val->start, "true", 4) == 0;
            capability_seen = true;
        } else if (equal_optional_key(json, key, "sensor_health")) {
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
    if (seen != 31 || (!mode && !out->weather_assisted)) return false;
    if (out->weather_assisted) {
        if (weather_seen != 15 || out->weather_assistance_version != 1) return false;
        if (out->operation!=VALVE_FAULT && !out->control_revision_available) return false;
        bool drain_op=out->operation==VALVE_MANUAL_IDLE || out->operation==VALVE_USER_OFF;
        if (drain_op && drain_state && drain_command && remaining==0) out->state=VALVE_DRAIN;
        else if (out->operation==VALVE_MANUAL_ACTIVE && supply_state && supply_command && remaining>0) out->state=VALVE_SUPPLY;
        else if (out->operation==VALVE_AUTO_SUPPLY && auto_state && supply_command && remaining==0) out->state=VALVE_SUPPLY;
        else if (out->operation==VALVE_FAULT && fault_state && drain_command && remaining==0) out->state=VALVE_UNKNOWN;
        else return false;
    } else if (drain_state && drain_command && remaining == 0) out->state = VALVE_DRAIN;
    else if (supply_state && supply_command && remaining > 0) out->state = VALVE_SUPPLY;
    else return false;
    if (forecast_object >= 0 && !forecast_duplicate) {
        valve_forecast_t forecast = {0};
        if (forecast_parse(json,tokens,count,forecast_object,&forecast)) out->forecast=forecast;
        else out->forecast.stale=forecast.stale;
    }
    out->remaining_seconds = remaining;
    out->timed_shower_duration_supported = capability_seen && capability_valid;
    memcpy(out->reason, reason, sizeof reason);
    if (temperature_seen && temperature_valid && health_seen && health_valid) {
        out->pipe_temperature_c = temperature;
        out->temperature_available = true;
    }
    return true;
}

bool valve_action_response_parse(const char *json, size_t len, const char *request_id, valve_status_t *out) {
    if (!out) return false;
    memset(out,0,sizeof *out);
    if (!json || !request_id || !json_well_formed(json,len)) return false;
    jsmntok_t tokens[STATUS_MAX_TOKENS]; jsmn_parser parser; jsmn_init(&parser);
    int count=jsmn_parse(&parser,json,len,tokens,STATUS_MAX_TOKENS), nested=-1;
    if (count<1 || tokens[0].type!=JSMN_OBJECT) return false;
    unsigned seen=0;
    for (int i=1; i<count && tokens[i].start<tokens[0].end;) {
        if (tokens[i].type!=JSMN_STRING || i+1>=count) return false;
        const jsmntok_t *key=&tokens[i], *val=&tokens[i+1]; unsigned bit=0;
        if (equal_optional_key(json,key,"request_id")) { bit=1; if (!equal_token(json,val,request_id)) return false; }
        else if (equal_optional_key(json,key,"accepted")) { bit=2; if (val->type!=JSMN_PRIMITIVE || val->end-val->start!=4 || memcmp(json+val->start,"true",4)) return false; }
        else if (equal_optional_key(json,key,"control_revision")) { uint64_t revision; bit=4; if (!revision_token(json,val,&revision)) return false; }
        else if (equal_optional_key(json,key,"status")) { bit=8; nested=i+1; if (val->type!=JSMN_OBJECT) return false; }
        else return false;
        if (seen&bit) return false;
        seen|=bit; i=next_token(tokens,count,i+1);
    }
    return seen==15 && nested>=0 && valve_status_parse(json+tokens[nested].start,
        (size_t)(tokens[nested].end-tokens[nested].start),out);
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
    int physical_dx = dx;
    if (rotation == 90) { dx = dy; dy = -physical_dx; }
    else if (rotation == 180) { dx = -dx; dy = -dy; }
    else if (rotation == 270) { dx = -dy; dy = physical_dx; }
    else if (rotation != 0) return VALVE_GESTURE_NONE;
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

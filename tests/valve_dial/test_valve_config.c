#include "valve_config_dial.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char stored_url[VALVE_CONFIG_URL_LEN + 16];
static char stored_token[VALVE_CONFIG_TOKEN_LEN + 16];
static bool has_url, has_token, corrupt_url, corrupt_token, fail_commit;

void fake_nvs_reset(void) {
    memset(stored_url, 0, sizeof(stored_url));
    memset(stored_token, 0, sizeof(stored_token));
    has_url = has_token = corrupt_url = corrupt_token = fail_commit = false;
}

int fake_nvs_get(const char *key, char *out, size_t *len) {
    const bool token = strcmp(key, "pi_token") == 0;
    const char *value = token ? stored_token : stored_url;
    if (!(token ? has_token : has_url) || (token ? corrupt_token : corrupt_url)) return -1;
    size_t needed = strlen(value) + 1;
    if (*len < needed) { *len = needed; return -2; }
    memcpy(out, value, needed);
    *len = needed;
    return 0;
}

int fake_nvs_set(const char *key, const char *value) {
    bool token = strcmp(key, "pi_token") == 0;
    char *dest = token ? stored_token : stored_url;
    size_t capacity = token ? sizeof(stored_token) : sizeof(stored_url);
    if (strlen(value) >= capacity) return -1;
    strcpy(dest, value);
    if (token) has_token = true; else has_url = true;
    return 0;
}

int fake_nvs_erase(const char *key) {
    if (strcmp(key, "pi_token") == 0) has_token = false;
    else has_url = false;
    return 0;
}

int fake_nvs_commit(void) { return fail_commit ? -1 : 0; }

static void test_round_trip(void) {
    char url[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
    fake_nvs_reset();
    assert(!valve_config_load(url, sizeof(url), token, sizeof(token)));
    assert(url[0] == 0 && token[0] == 0);
    assert(valve_config_save("http://192.168.1.20:8081", "placeholder-secret"));
    assert(valve_config_load(url, sizeof(url), token, sizeof(token)));
    assert(strcmp(url, "http://192.168.1.20:8081") == 0);
    assert(strcmp(token, "placeholder-secret") == 0);
    assert(valve_config_clear());
    assert(!valve_config_load(url, sizeof(url), token, sizeof(token)));
    assert(url[0] == 0 && token[0] == 0);
}

static void test_rejections(void) {
    char long_url[VALVE_CONFIG_URL_LEN + 20];
    char long_token[VALVE_CONFIG_TOKEN_LEN + 20];
    memset(long_url, 'a', sizeof(long_url)); long_url[sizeof(long_url)-1] = 0;
    memset(long_token, 'b', sizeof(long_token)); long_token[sizeof(long_token)-1] = 0;
    const char *bad[] = {"", "https://pi:8081", "http://", "http://u@pi:8081",
                         "http://pi:8081/path", "http://pi?q=1", "http://pi#x",
                         "http://pi:0", "http://pi:65536", "http://pi:abc", "http://pi:8081/"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        assert(!valve_config_save(bad[i], "placeholder-secret"));
    assert(!valve_config_save(long_url, "placeholder-secret"));
    assert(!valve_config_save("http://pi:8081", ""));
    assert(!valve_config_save("http://pi:8081", long_token));
    char max_token[VALVE_CONFIG_TOKEN_LEN];
    memset(max_token, 'x', sizeof(max_token) - 1);
    max_token[sizeof(max_token) - 1] = '\0';
    assert(valve_config_save("http://pi:8081", max_token));
    char url[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
    assert(valve_config_load(url, sizeof(url), token, sizeof(token)));
    assert(strcmp(token, max_token) == 0);
}

static void test_corruption_and_readback(void) {
    char url[VALVE_CONFIG_URL_LEN], token[VALVE_CONFIG_TOKEN_LEN];
    fake_nvs_reset();
    strcpy(stored_url, "http://pi:8081"); has_url = true;
    assert(!valve_config_load(url, sizeof(url), token, sizeof(token)));
    strcpy(stored_token, "placeholder-secret"); has_token = true;
    corrupt_token = true;
    assert(!valve_config_load(url, sizeof(url), token, sizeof(token)));
    corrupt_token = false;
    strcpy(stored_url, "http://pi/path");
    assert(!valve_config_load(url, sizeof(url), token, sizeof(token)));
    fake_nvs_reset();
    fail_commit = true;
    assert(!valve_config_save("http://pi:8081", "placeholder-secret"));
    fail_commit = false;
    corrupt_url = true;
    assert(!valve_config_save("http://pi:8081", "placeholder-secret"));
}

int main(void) {
    test_round_trip();
    test_rejections();
    test_corruption_and_readback();
    puts("valve config tests passed");
    return 0;
}

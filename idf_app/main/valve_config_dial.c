#include "valve_config_dial.h"

#include <string.h>

#ifndef VALVE_CONFIG_HOST_TEST
#include <nvs.h>
#else
extern int fake_nvs_get(const char *key, char *out, size_t *len);
extern int fake_nvs_set(const char *key, const char *value);
extern int fake_nvs_erase(const char *key);
extern int fake_nvs_commit(void);
#endif

#define VALVE_NVS_NAMESPACE "valve_cfg"
#define VALVE_NVS_URL "pi_url"
#define VALVE_NVS_TOKEN "pi_token"
#define VALVE_NVS_SEAL "pi_seal"
#define VALVE_SEAL_LEN (VALVE_CONFIG_URL_LEN + VALVE_CONFIG_TOKEN_LEN)

static size_t bounded_len(const char *value, size_t limit) {
    if (!value) return limit;
    size_t i = 0;
    while (i < limit && value[i]) i++;
    return i;
}

static bool valid_url(const char *url) {
    size_t len = bounded_len(url, VALVE_CONFIG_URL_LEN);
    if (len < 8 || len >= VALVE_CONFIG_URL_LEN || strncmp(url, "http://", 7) != 0)
        return false;
    const char *host = url + 7;
    const char *p = host;
    while (*p && *p != ':') {
        char c = *p++;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    if (p == host || host[0] == '.' || host[0] == '-' || p[-1] == '.' || p[-1] == '-')
        return false;
    if (*p == ':') {
        unsigned port = 0;
        p++;
        if (!*p) return false;
        while (*p) {
            if (*p < '0' || *p > '9') return false;
            port = port * 10 + (unsigned)(*p++ - '0');
            if (port > 65535) return false;
        }
        if (port == 0) return false;
    }
    return true;
}

static bool valid_token(const char *token) {
    size_t len = bounded_len(token, VALVE_CONFIG_TOKEN_LEN);
    if (len == 0 || len >= VALVE_CONFIG_TOKEN_LEN) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)token[i];
        if (c < 0x21 || c > 0x7e) return false;
    }
    return true;
}

#ifdef VALVE_CONFIG_HOST_TEST
static bool store_read(const char *key, char *value, size_t capacity) {
    size_t len = capacity;
    return fake_nvs_get(key, value, &len) == 0 && len > 0 && len <= capacity &&
           value[len - 1] == '\0' && bounded_len(value, len) + 1 == len;
}
static bool store_invalidate(void) {
    return fake_nvs_erase(VALVE_NVS_SEAL) == 0 && fake_nvs_commit() == 0;
}
static bool store_stage(const char *url, const char *token) {
    return fake_nvs_set(VALVE_NVS_URL, url) == 0 &&
           fake_nvs_set(VALVE_NVS_TOKEN, token) == 0 && fake_nvs_commit() == 0;
}
static bool store_activate(const char *seal) {
    return fake_nvs_set(VALVE_NVS_SEAL, seal) == 0;
}
static bool store_clear(void) {
    return fake_nvs_erase(VALVE_NVS_URL) == 0 &&
           fake_nvs_erase(VALVE_NVS_TOKEN) == 0 &&
           fake_nvs_erase(VALVE_NVS_SEAL) == 0 && fake_nvs_commit() == 0;
}
#else
static bool store_read(const char *key, char *value, size_t capacity) {
    nvs_handle_t handle;
    if (nvs_open(VALVE_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t len = capacity;
    esp_err_t err = nvs_get_blob(handle, key, value, &len);
    nvs_close(handle);
    return err == ESP_OK && len > 0 && len <= capacity &&
           value[len - 1] == '\0' && bounded_len(value, len) + 1 == len;
}
static bool store_invalidate(void) {
    nvs_handle_t handle;
    if (nvs_open(VALVE_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(handle, VALVE_NVS_SEAL);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
static bool store_stage(const char *url, const char *token) {
    nvs_handle_t handle;
    if (nvs_open(VALVE_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, VALVE_NVS_URL, url, strlen(url) + 1);
    if (err == ESP_OK) err = nvs_set_blob(handle, VALVE_NVS_TOKEN, token, strlen(token) + 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
static bool store_activate(const char *seal) {
    nvs_handle_t handle;
    if (nvs_open(VALVE_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    /* ESP-IDF 5.5.5 persists set_blob immediately; nvs_commit is a no-op.
     * Activation must be the final fallible step. */
    esp_err_t err = nvs_set_blob(handle, VALVE_NVS_SEAL, seal, strlen(seal) + 1);
    nvs_close(handle);
    return err == ESP_OK;
}
static bool store_clear(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(VALVE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err == ESP_ERR_NVS_NOT_FOUND;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
#endif

bool valve_config_load(char *base_url, size_t url_len, char *token, size_t token_len) {
    if (base_url && url_len) base_url[0] = '\0';
    if (token && token_len) token[0] = '\0';
    if (!base_url || !token || url_len < VALVE_CONFIG_URL_LEN ||
        token_len < VALVE_CONFIG_TOKEN_LEN) return false;

    char url_copy[VALVE_CONFIG_URL_LEN] = {0};
    char token_copy[VALVE_CONFIG_TOKEN_LEN] = {0};
    char seal[VALVE_SEAL_LEN] = {0};
    bool ok = store_read(VALVE_NVS_URL, url_copy, sizeof(url_copy)) &&
              store_read(VALVE_NVS_TOKEN, token_copy, sizeof(token_copy)) &&
              store_read(VALVE_NVS_SEAL, seal, sizeof(seal)) &&
              valid_url(url_copy) && valid_token(token_copy);
    if (ok) {
        size_t url_size = strlen(url_copy);
        size_t token_size = strlen(token_copy);
        ok = strlen(seal) == url_size + 1 + token_size &&
             memcmp(seal, url_copy, url_size) == 0 &&
             seal[url_size] == '\n' &&
             memcmp(seal + url_size + 1, token_copy, token_size) == 0;
    }
    if (ok) {
        strcpy(base_url, url_copy);
        strcpy(token, token_copy);
    }
    memset(token_copy, 0, sizeof(token_copy));
    memset(seal, 0, sizeof(seal));
    return ok;
}

bool valve_config_save(const char *base_url, const char *token) {
    if (!valid_url(base_url) || !valid_token(token) || !store_invalidate() ||
        !store_stage(base_url, token))
        return false;
    char verify_url[VALVE_CONFIG_URL_LEN] = {0};
    char verify_token[VALVE_CONFIG_TOKEN_LEN] = {0};
    bool ok = store_read(VALVE_NVS_URL, verify_url, sizeof(verify_url)) &&
              store_read(VALVE_NVS_TOKEN, verify_token, sizeof(verify_token)) &&
              strcmp(base_url, verify_url) == 0 && strcmp(token, verify_token) == 0;
    memset(verify_token, 0, sizeof(verify_token));
    if (!ok) return false;
    char seal[VALVE_SEAL_LEN] = {0};
    size_t url_size = strlen(base_url);
    size_t token_size = strlen(token);
    memcpy(seal, base_url, url_size);
    seal[url_size] = '\n';
    memcpy(seal + url_size + 1, token, token_size + 1);
    ok = store_activate(seal);
    memset(seal, 0, sizeof(seal));
    return ok;
}

bool valve_config_clear(void) {
    if (!store_clear()) return false;
    char url[VALVE_CONFIG_URL_LEN] = {0};
    char token[VALVE_CONFIG_TOKEN_LEN] = {0};
    bool cleared = !valve_config_load(url, sizeof(url), token, sizeof(token)) &&
                   url[0] == '\0' && token[0] == '\0';
    memset(token, 0, sizeof(token));
    return cleared;
}

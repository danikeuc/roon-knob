#pragma once
/* Private platform boundary. All record access is serialized by this mutex. */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#define ADMIN_NVS_NAMESPACE "dial_admin_v1"
#define ADMIN_KDF_ITERATIONS 100000u
bool admin_backend_lock(void);
void admin_backend_unlock(void);
esp_err_t admin_backend_read(const char *key, void *out, size_t size);
esp_err_t admin_backend_write(const char *key, const void *data, size_t size);
esp_err_t admin_backend_commit(void);
uint64_t admin_backend_seconds(void);
void admin_backend_random(void *out, size_t size);
bool admin_backend_kdf(const char *secret, const uint8_t salt[16], uint8_t out[32]);
/* Synchronous dispatch; rejects a full queue. fn runs on the crypto worker. */
bool admin_backend_execute(void (*fn)(void *), void *arg);
void admin_secure_zero(void *ptr, size_t size);

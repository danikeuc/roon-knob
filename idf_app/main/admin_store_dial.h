#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ADMIN_ERR_CONFLICT (ESP_ERR_INVALID_STATE + 0x1000)
typedef struct {
    uint16_t duration_seconds;
    uint16_t rotation_degrees;
    bool rotation_override;
    uint32_t generation;
} admin_settings_t;
esp_err_t admin_store_load(admin_settings_t *out);
esp_err_t admin_store_save(const admin_settings_t *candidate, uint32_t expected_generation);

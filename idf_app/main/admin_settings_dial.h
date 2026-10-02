#pragma once
#include "admin_auth_dial.h"
#include <stdbool.h>
#include <stdint.h>
/* Boot/background initialization, before either HTTP server starts. */
void admin_settings_init(void);
/* Background task, after UI loop starts; restores persisted defaults. */
admin_result_t admin_settings_restore(void);
/* Any task, before stopping an HTTP server. Never calls LVGL. */
void admin_settings_cancel_pending(void);
/* UI task only; never performs storage or auth work. */
void admin_settings_process_ui(void);
void admin_settings_cancel_ui(void);
bool admin_settings_input_blocked(void);
admin_result_t admin_settings_set_duration(uint16_t seconds, uint32_t expected_generation);
admin_result_t admin_settings_set_rotation(uint16_t degrees, uint32_t expected_generation);

#pragma once
#include <stdint.h>
#include <limits.h>
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define portTICK_PERIOD_MS 100u
#define pdMS_TO_TICKS(ms) ((unsigned)(ms)/portTICK_PERIOD_MS)
#define portMAX_DELAY UINT_MAX
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0

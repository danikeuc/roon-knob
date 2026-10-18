#pragma once
#include "../../../weather_runtime/fakes/freertos/task.h"
typedef uint8_t StackType_t;
#define portBYTE_ALIGNMENT 16
typedef struct { unsigned marker; } StaticTask_t;
typedef void *TaskHandle_t;
TaskHandle_t xTaskCreateStatic(void (*)(void *),const char *,unsigned,void *,unsigned,StackType_t *,StaticTask_t *);

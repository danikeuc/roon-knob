#pragma once
#include "FreeRTOS.h"
void vTaskDelay(unsigned);
void vTaskDelete(void *);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);

#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
void vQueueDelete(QueueHandle_t);

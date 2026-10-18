#pragma once
#include "../../../valve_dial/fakes/freertos/queue.h"
int xQueueOverwrite(QueueHandle_t,const void *);

typedef struct { unsigned marker; } StaticQueue_t;
QueueHandle_t xQueueCreateStatic(unsigned,size_t,unsigned char *,StaticQueue_t *);

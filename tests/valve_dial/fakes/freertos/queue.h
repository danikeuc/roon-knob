#ifndef FAKE_QUEUE_H
#define FAKE_QUEUE_H
#include <stddef.h>
typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned capacity, size_t item_size);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned ticks);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned ticks);
void xQueueReset(QueueHandle_t queue);
#endif

#ifndef VALVE_LOGIC_H
#define VALVE_LOGIC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { VALVE_UNKNOWN, VALVE_DRAIN, VALVE_SUPPLY } valve_state_t;
typedef enum { VALVE_ACTION_START_600S, VALVE_ACTION_DRAIN } valve_action_t;
typedef struct {
    valve_state_t state;
    uint16_t remaining_seconds;
    char reason[96];
} valve_status_t;
bool valve_status_parse(const char *json, size_t len, valve_status_t *out);
#endif

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
typedef enum { VALVE_GESTURE_NONE, VALVE_GESTURE_SWITCH_SCREEN,
               VALVE_GESTURE_ART_UP, VALVE_GESTURE_ART_DOWN } valve_gesture_t;
typedef struct { bool zone_picker, settings, art_mode, wake_touch; } valve_gesture_context_t;
valve_gesture_t valve_gesture_classify(int dx, int dy, uint32_t elapsed_ms,
                                       int rotation, valve_gesture_context_t context);
typedef struct { bool tracking, cancelled, emitted; uint32_t started_ms; } valve_hold_t;
bool valve_hold_update(valve_hold_t *hold, bool pressed, bool moved, uint32_t now_ms);
bool valve_supply_allowed(valve_state_t state, bool configured, bool connected, uint32_t age_ms);
bool valve_drain_allowed(bool configured, bool connected);
bool valve_drain_tap_allowed(bool started_on_drain, bool moved,
                             valve_gesture_t gesture, bool configured, bool connected);
int valve_touch_coordinate(int raw, int rotation);
#endif

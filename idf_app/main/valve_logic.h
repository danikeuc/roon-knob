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
    float pipe_temperature_c;
    bool temperature_available;
} valve_status_t;
bool valve_status_parse(const char *json, size_t len, valve_status_t *out);
void valve_temperature_format(const valve_status_t *status, char *buffer, size_t buffer_size);
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
typedef struct {
    uint32_t session;
    uint32_t minimum_request_id;
    bool pending;
} valve_observation_gate_t;
void valve_gate_link(valve_observation_gate_t *gate, uint32_t session);
void valve_gate_action(valve_observation_gate_t *gate, uint32_t request_id);
void valve_gate_overflow(valve_observation_gate_t *gate, uint32_t barrier_id);
bool valve_gate_accept(valve_observation_gate_t *gate, uint32_t session,
                       uint32_t request_id, bool valid_status);
#endif

#ifndef VALVE_UI_DIAL_H
#define VALVE_UI_DIAL_H
#include "valve_logic.h"
#include <stdbool.h>
#include <stdint.h>
void valve_ui_init(void);
void valve_ui_show(bool visible);
/* Apply a deferred horizontal page-switch gesture. */
void valve_ui_toggle_page(void);
bool valve_ui_visible(void);
void valve_ui_set_status(const valve_status_t *status);
typedef struct {
    bool valid;
    bool supported;
    uint64_t received_at_ms;
    uint32_t session;
    uint32_t config_generation;
} valve_duration_capability_snapshot_t;
/* Coherent, nonblocking copy. False means publication was in progress; out is unchanged.
 * A true result may still be invalid or stale; use the checked getter for decisions. */
bool valve_ui_duration_capability_snapshot_get(valve_duration_capability_snapshot_t *out);
/* False means no fresh same-session/config status; supported is then unchanged. */
bool valve_ui_duration_capability_get(bool *supported);
void valve_ui_set_unknown(const char *reason);
void valve_ui_process(uint32_t now_ms, bool awake);
/* Raw pointer input is withheld from LVGL while the valve page is visible. */
void valve_ui_touch(int x, int y, bool pressed, bool moved, uint32_t now_ms);
void valve_ui_cancel_touch(void);
void valve_ui_connected(bool connected);
void valve_ui_wake(void);
#endif

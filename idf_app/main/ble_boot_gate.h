#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "rk_ble_hid_host.h"
/* Accepted means queued, not initialized. Bound deferral to keep admin useful
 * even when BLE reports failure or never publishes completion. */
static inline bool ble_boot_gate_open(bool accepted, bool status_known,
                                     rk_ble_hid_host_state_t state,
                                     uint64_t elapsed_ms) {
    if (!accepted || elapsed_ms >= 10000) return true;
    if (!status_known) return false;
    return state != RK_BLE_HID_HOST_STATE_UNAVAILABLE &&
           state != RK_BLE_HID_HOST_STATE_STARTING;
}

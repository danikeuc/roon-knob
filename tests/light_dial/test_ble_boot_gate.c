#include "ble_boot_gate.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(!ble_boot_gate_open(true,false,RK_BLE_HID_HOST_STATE_UNAVAILABLE,0));
    assert(!ble_boot_gate_open(true,true,RK_BLE_HID_HOST_STATE_STARTING,9999));
    assert(ble_boot_gate_open(true,true,RK_BLE_HID_HOST_STATE_STARTING,10000));
    assert(ble_boot_gate_open(false,false,RK_BLE_HID_HOST_STATE_UNAVAILABLE,0));
    assert(ble_boot_gate_open(true,true,RK_BLE_HID_HOST_STATE_DISABLED,1));
    for (int state=RK_BLE_HID_HOST_STATE_READY;state<=RK_BLE_HID_HOST_STATE_ERROR;state++)
        assert(ble_boot_gate_open(true,true,(rk_ble_hid_host_state_t)state,1));
    puts("BLE startup: acceptance is not readiness, disabled/error release, bounded wait PASS");
}

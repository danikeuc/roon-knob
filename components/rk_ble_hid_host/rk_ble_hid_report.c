#include "rk_ble_hid_host.h"
#include <string.h>

const char *rk_ble_hid_host_result_name(rk_ble_hid_host_result_t result) {
    switch (result) {
    case RK_BLE_HID_HOST_OK: return "OK";
    case RK_BLE_HID_HOST_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case RK_BLE_HID_HOST_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
    case RK_BLE_HID_HOST_ERR_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
    case RK_BLE_HID_HOST_ERR_QUEUE_FULL: return "QUEUE_FULL";
    case RK_BLE_HID_HOST_ERR_INVALID_STATE: return "INVALID_STATE";
    default: return "UNKNOWN";
    }
}

const char *rk_ble_hid_host_error_name(rk_ble_hid_host_error_t error) {
    switch (error) {
    case RK_BLE_HID_HOST_ERROR_NONE: return "NONE";
    case RK_BLE_HID_HOST_ERROR_UNAVAILABLE: return "UNAVAILABLE";
    case RK_BLE_HID_HOST_ERROR_NVS: return "NVS";
    case RK_BLE_HID_HOST_ERROR_NIMBLE_INIT: return "NIMBLE_INIT";
    case RK_BLE_HID_HOST_ERROR_HID_INIT: return "HID_INIT";
    case RK_BLE_HID_HOST_ERROR_SYNC: return "SYNC";
    case RK_BLE_HID_HOST_ERROR_SCAN: return "SCAN";
    case RK_BLE_HID_HOST_ERROR_CONNECT: return "CONNECT";
    case RK_BLE_HID_HOST_ERROR_BOND_STORE: return "BOND_STORE";
    case RK_BLE_HID_HOST_ERROR_INVALID_STATE: return "INVALID_STATE";
    case RK_BLE_HID_HOST_ERROR_STALE_SCAN: return "STALE_SCAN";
    case RK_BLE_HID_HOST_ERROR_TEARDOWN: return "TEARDOWN";
    case RK_BLE_HID_HOST_ERROR_TEARDOWN_TIMEOUT: return "TEARDOWN_TIMEOUT";
    default: return "UNKNOWN";
    }
}

bool rk_ble_hid_host_map_consumer_report(const uint8_t *data, size_t len,
                                         rk_ble_media_key_t *key) {
    if (key) {
        *key = RK_BLE_MEDIA_KEY_NONE;
    }
    if (!data || len < 2 || !key) {
        return false;
    }

    const uint16_t usage = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    switch (usage) {
    case 0x00cd:
        *key = RK_BLE_MEDIA_KEY_PLAY_PAUSE;
        return true;
    case 0x00b5:
        *key = RK_BLE_MEDIA_KEY_NEXT_TRACK;
        return true;
    case 0x00b6:
        *key = RK_BLE_MEDIA_KEY_PREVIOUS_TRACK;
        return true;
    case 0x00e9:
        *key = RK_BLE_MEDIA_KEY_VOLUME_UP;
        return true;
    case 0x00ea:
        *key = RK_BLE_MEDIA_KEY_VOLUME_DOWN;
        return true;
    default:
        return false;
    }
}

bool rk_ble_hid_host_map_device_report(const char *name, bool media_usage_supported,
    uint8_t report_id, const uint8_t *data, size_t len,
    rk_ble_media_key_t *held_key, rk_ble_media_key_t *key) {
    if (key) *key = RK_BLE_MEDIA_KEY_NONE;
    if (!name || strcmp(name, "QGSQ") != 0)
        return rk_ble_hid_host_map_consumer_report(data, len, key);
    if (!key || !held_key || !data || !media_usage_supported ||
        report_id != 1 || len != 2 || data[1] != 0) return false;
    rk_ble_media_key_t pressed;
    switch (data[0]) {
    case 0: *held_key = RK_BLE_MEDIA_KEY_NONE; return false;
    case 0x01: pressed = RK_BLE_MEDIA_KEY_VOLUME_UP; break;
    case 0x02: pressed = RK_BLE_MEDIA_KEY_VOLUME_DOWN; break;
    case 0x04: pressed = RK_BLE_MEDIA_KEY_PLAY_PAUSE; break;
    case 0x20: pressed = RK_BLE_MEDIA_KEY_NEXT_TRACK; break;
    case 0x10: pressed = RK_BLE_MEDIA_KEY_PREVIOUS_TRACK; break;
    default: return false;
    }
    if (*held_key == pressed) return false;
    *held_key = pressed;
    *key = pressed;
    return true;
}

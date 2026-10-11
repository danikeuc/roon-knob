#include "rk_ble_hid_host.h"

#include <assert.h>
#include <stdio.h>

static void expect(const uint8_t report[2], rk_ble_media_key_t expected) {
    rk_ble_media_key_t key = RK_BLE_MEDIA_KEY_NONE;
    assert(rk_ble_hid_host_map_consumer_report(report, 2, &key));
    assert(key == expected);
}

static void expect_three_byte_report(uint8_t usage,
                                     rk_ble_media_key_t expected) {
    const uint8_t report[] = {usage, 0x00, 0x00};
    rk_ble_media_key_t key = RK_BLE_MEDIA_KEY_NONE;
    assert(rk_ble_hid_host_map_consumer_report(report, sizeof(report), &key));
    assert(key == expected);
}

int main(void) {
    assert(rk_ble_hid_host_result_name(RK_BLE_HID_HOST_ERR_QUEUE_FULL)[0] == 'Q');
    assert(rk_ble_hid_host_error_name(RK_BLE_HID_HOST_ERROR_STALE_SCAN)[0] == 'S');
    expect((const uint8_t[]){0xcd, 0x00}, RK_BLE_MEDIA_KEY_PLAY_PAUSE);
    expect((const uint8_t[]){0xb5, 0x00}, RK_BLE_MEDIA_KEY_NEXT_TRACK);
    expect((const uint8_t[]){0xb6, 0x00}, RK_BLE_MEDIA_KEY_PREVIOUS_TRACK);
    expect((const uint8_t[]){0xe9, 0x00}, RK_BLE_MEDIA_KEY_VOLUME_UP);
    expect((const uint8_t[]){0xea, 0x00}, RK_BLE_MEDIA_KEY_VOLUME_DOWN);
    /* The tested remote emits these three-byte payloads while ESP-IDF 5.5
     * incorrectly labels their report metadata as GENERIC. */
    expect_three_byte_report(0xb5, RK_BLE_MEDIA_KEY_NEXT_TRACK);
    expect_three_byte_report(0xb6, RK_BLE_MEDIA_KEY_PREVIOUS_TRACK);
    expect_three_byte_report(0xe9, RK_BLE_MEDIA_KEY_VOLUME_UP);
    expect_three_byte_report(0xea, RK_BLE_MEDIA_KEY_VOLUME_DOWN);

    rk_ble_media_key_t key = RK_BLE_MEDIA_KEY_PLAY_PAUSE;
    assert(!rk_ble_hid_host_map_consumer_report((const uint8_t[]){0, 0}, 2, &key));
    assert(key == RK_BLE_MEDIA_KEY_NONE);
    assert(!rk_ble_hid_host_map_consumer_report((const uint8_t[]){0xe2, 0}, 2, &key));
    assert(!rk_ble_hid_host_map_consumer_report((const uint8_t[]){0xcd}, 1, &key));
    assert(!rk_ble_hid_host_map_consumer_report(NULL, 0, &key));
    /* Operator-labelled QGSQ captures: +, -, play, next, previous. */
    const uint8_t bits[] = {0x01, 0x02, 0x04, 0x20, 0x10};
    const rk_ble_media_key_t keys[] = {RK_BLE_MEDIA_KEY_VOLUME_UP,
        RK_BLE_MEDIA_KEY_VOLUME_DOWN, RK_BLE_MEDIA_KEY_PLAY_PAUSE,
        RK_BLE_MEDIA_KEY_NEXT_TRACK, RK_BLE_MEDIA_KEY_PREVIOUS_TRACK};
    rk_ble_media_key_t held = RK_BLE_MEDIA_KEY_NONE;
    for (size_t i = 0; i < sizeof(bits); ++i) {
        const uint8_t data[] = {bits[i], 0};
        assert(rk_ble_hid_host_map_device_report("QGSQ", true, 1, data, 2, &held, &key));
        assert(key == keys[i]);
        assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, data, 2, &held, &key));
        assert(key == RK_BLE_MEDIA_KEY_NONE);
        assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, (const uint8_t[]){0,0}, 2, &held, &key));
        assert(held == RK_BLE_MEDIA_KEY_NONE);
    }
    /* Reject unknown/multi-button payloads and reports outside this profile. */
    for (unsigned i = 0; i < 256; ++i) {
        if (i == 1 || i == 2 || i == 4 || i == 16 || i == 32) continue;
        assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, (const uint8_t[]){i,0}, 2, &held, &key));
    }
    const uint8_t play[] = {4,0};
    assert(!rk_ble_hid_host_map_device_report("Other", true, 1, play, 2, &held, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", false, 1, play, 2, &held, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 2, play, 2, &held, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, play, 1, &held, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, (const uint8_t[]){4,1}, 2, &held, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, NULL, 2, &held, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, play, 2, NULL, &key));
    assert(!rk_ble_hid_host_map_device_report("QGSQ", true, 1, play, 2, &held, NULL));
    assert(rk_ble_hid_host_map_device_report("Legacy", false, 9, (const uint8_t[]){0xcd,0}, 2, &held, &key));
    assert(key == RK_BLE_MEDIA_KEY_PLAY_PAUSE);
    puts("rk_ble_hid_host report mapping tests passed");
    return 0;
}

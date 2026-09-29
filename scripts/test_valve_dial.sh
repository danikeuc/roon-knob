#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
cc -std=c11 -Wall -Wextra -Werror -DVALVE_CONFIG_HOST_TEST \
    -Iidf_app/main tests/valve_dial/test_valve_config.c \
    idf_app/main/valve_config_dial.c -o "$tmp_dir/test_valve_config"
"$tmp_dir/test_valve_config"
python3 - <<'PY'
from pathlib import Path
s = Path('idf_app/main/config_server.c').read_text()
assert '"/valves-config"' in s
assert "type='password'" in s
valves_html = s[s.index('static const char *HTML_VALVES_CONFIG'):s.index('static esp_err_t valves_config_get_handler')]
assert "name='pi_token' maxlength='128' value=''" in valves_html
valves_get = s[s.index('static esp_err_t valves_config_get_handler'):s.index('static bool decode_form_value')]
assert 'valve_config_load' not in valves_get
assert 'HTML_VALVES_CONFIG' in valves_get
assert 'valve_config_save(' in s and 'valve_config_clear(' in s
new_handlers = s[s.index('static esp_err_t valves_config_get_handler'):s.index('static esp_err_t wifi_add_handler')]
assert 'ESP_LOG' not in new_handlers
assert 'static esp_err_t config_post_handler(' not in new_handlers
assert 'token' not in s[s.index('static const char *HTML_CONFIG'):s.index('static const char *HTML_SUCCESS')]
assert 'Received config: %s' not in s
storage = Path('idf_app/main/valve_config_dial.c').read_text()
assert 'nvs_set_str(' not in storage
assert 'nvs_set_blob(' in storage
PY
cc -std=c11 -Wall -Wextra -Werror -DVALVE_CLIENT_HOST_TEST -Iidf_app/main \
    tests/valve_dial/test_valve_logic.c idf_app/main/valve_logic.c \
    idf_app/main/valve_client_dial.c -o "$tmp_dir/test_valve_logic"
"$tmp_dir/test_valve_logic"
cc -std=c11 -Wall -Wextra -Werror -Itests/valve_dial/fakes -Iidf_app/main -Icommon \
    tests/valve_dial/test_valve_ui_sequence.c idf_app/main/valve_ui_dial.c \
    idf_app/main/valve_logic.c -o "$tmp_dir/test_valve_ui_sequence"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -DVALVE_CLIENT_HOST_TEST \
    -ffunction-sections -fdata-sections \
    -Itests/valve_dial/fakes -Iidf_app/main -Icommon -Iinclude \
    tests/valve_dial/test_valve_integration.c idf_app/main/valve_ui_dial.c \
    idf_app/main/valve_client_dial.c idf_app/main/valve_logic.c \
    common/bridge_command_plan.c common/bridge_client.c common/controller_input.c \
    common/controller_action_router.c idf_app/main/controller_input_profile_dial.c \
    -Wl,--gc-sections -o "$tmp_dir/test_valve_integration"
"$tmp_dir/test_valve_integration" > "$tmp_dir/integration.log" 2>&1
! grep -q 'integration-private-token' "$tmp_dir/integration.log"
cat "$tmp_dir/integration.log"
"$tmp_dir/test_valve_ui_sequence"
python3 - <<'PY'
from pathlib import Path
touch = Path('idf_app/main/platform_display_idf.c').read_text()
ui = Path('idf_app/main/valve_ui_dial.c').read_text()
assert 'if (s_touch_started_on_valve)' in touch
press = touch[touch.index('if (s_touch_started_on_valve)'):touch.index('return;', touch.index('if (s_touch_started_on_valve)'))]
assert 'valve_ui_touch(valve_touch_coordinate(x, s_current_rotation)' in press
assert 'data->state = LV_INDEV_STATE_RELEASED' in press
release = touch[touch.index('valve_gesture_t gesture = valve_gesture_classify', touch.index('/* Use the last real touch coordinate')):]
assert release.index('gesture == VALVE_GESTURE_SWITCH_SCREEN') < release.index('valve_ui_touch(valve_touch_coordinate(s_touch_last_x, s_current_rotation)')
assert 'lv_indev_reset(indev, NULL)' in release[:release.index('valve_ui_touch(valve_touch_coordinate(s_touch_last_x, s_current_rotation)')]
pending = touch[touch.index('void platform_display_process_pending(void) {'):]
assert 'if (s_pending_page_switch)' in pending
assert pending.index('s_pending_page_switch = false;') < pending.index('valve_ui_toggle_page();')
assert 'LV_EVENT_CLICKED' not in ui
assert 'data->state = s_touch_consumed ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED' in touch
encoder = Path('idf_app/main/platform_input_idf.c').read_text()
assert encoder.index('if (valve_ui_visible()) return;') < encoder.index('controller_input_dispatch_physical(&event)')
assert encoder.index('if (display_get_state() != DISPLAY_STATE_NORMAL) valve_ui_wake();') < encoder.index('display_activity_detected();')
PY

# Custom valve firmware must not reach the upstream bridge OTA feed.
python3 - <<'PY_GATE'
from pathlib import Path
main = Path('idf_app/main/main_idf.c').read_text()
network = Path('idf_app/main/ui_network.c').read_text()
ota = Path('idf_app/main/ota_update.c').read_text()
shared = Path('common/ui.c').read_text()
cmake = Path('idf_app/main/CMakeLists.txt').read_text()
assert 'RK_CUSTOM_VALVE_FIRMWARE=1' in cmake
assert 'ota_check_for_update(' not in main
assert 'ota_check_for_update(' not in network
assert 'ota_start_update(' not in network
assert 'Check for Update' not in network
assert '#ifdef RK_CUSTOM_VALVE_FIRMWARE' in ota
assert 'Upstream bridge OTA is disabled' in ota
for signature in ('void ota_check_for_update(bool force) {', 'void ota_start_update(void) {'):
    disabled_branch = ota.split(signature, 1)[1].split('#else', 1)[0]
    assert '#ifdef RK_CUSTOM_VALVE_FIRMWARE' in disabled_branch
    assert 'return;' in disabled_branch
assert 'check_update_task' not in ota.split('#ifndef RK_CUSTOM_VALVE_FIRMWARE', 1)[0]
assert '#if !defined(RK_CUSTOM_VALVE_FIRMWARE)' in shared
available = shared.split('void ui_set_update_available(const char *version) {', 1)[1].split('void ui_set_update_progress(', 1)[0]
assert available.index('#ifdef RK_CUSTOM_VALVE_FIRMWARE') < available.index('lv_btn_create(')
assert available.index('return;') < available.index('#else') < available.index('lv_btn_create(')
trigger = shared.split('void ui_trigger_update(void) {', 1)[1].split('// ===', 1)[0]
assert '#if defined(ESP_PLATFORM) && !defined(RK_CUSTOM_VALVE_FIRMWARE)' in trigger
assert trigger.index('#if defined(ESP_PLATFORM) && !defined(RK_CUSTOM_VALVE_FIRMWARE)') < trigger.index('ota_start_update();') < trigger.index('#else')
PY_GATE

python3 tests/valve_dial/test_settings_gesture.py

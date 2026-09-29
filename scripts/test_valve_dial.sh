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
assert 'LV_EVENT_CLICKED' not in ui
assert 'data->state = s_touch_consumed ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED' in touch
encoder = Path('idf_app/main/platform_input_idf.c').read_text()
assert encoder.index('if (valve_ui_visible()) return;') < encoder.index('controller_input_dispatch_physical(&event)')
PY

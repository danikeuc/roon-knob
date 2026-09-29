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

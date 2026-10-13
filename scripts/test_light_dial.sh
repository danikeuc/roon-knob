#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
idf="${IDF_PATH:-/tmp/esp-idf-5.5.5}"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -DLIGHT_CLIENT_HOST_TEST -DLIGHT_CONFIG_HOST_TEST -Iidf_app/main -I"$idf/components/json/cJSON" tests/light_dial/test_light.c idf_app/main/light_client_dial.c idf_app/main/light_config_dial.c idf_app/main/valve_logic.c "$idf/components/json/cJSON/cJSON.c" -lm -o "$tmp_dir/light"
"$tmp_dir/light"
cc -std=c11 -Wall -Wextra -Werror -Itests/light_dial/fakes -Itests/valve_dial/fakes -Iidf_app/main -Icommon -Iinclude tests/light_dial/test_ui.c -o "$tmp_dir/ui"
"$tmp_dir/ui"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -DLIGHT_CLIENT_HOST_TEST -DLIGHT_CLIENT_HTTP_HOST_TEST -DLIGHT_CONFIG_HOST_TEST -Itests/weather_runtime/fakes -Itests/admin_dial/fakes -Iidf_app/main -I"$idf/components/json/cJSON" tests/light_dial/test_transport.c idf_app/main/light_client_dial.c idf_app/main/light_config_dial.c idf_app/main/valve_logic.c "$idf/components/json/cJSON/cJSON.c" -lm -o "$tmp_dir/transport"
"$tmp_dir/transport"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -DLIGHT_CONFIG_HOST_TEST -Itests/light_dial/fakes -Itests/weather_runtime/fakes -Itests/admin_dial/fakes -Iidf_app/main -I"$idf/components/json/cJSON" tests/light_dial/test_admin.c idf_app/main/light_config_dial.c idf_app/main/valve_logic.c "$idf/components/json/cJSON/cJSON.c" -lm -o "$tmp_dir/admin"
"$tmp_dir/admin"

#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
idf="${IDF_PATH:-/tmp/esp-idf-5.5.5}"
cc -std=c11 -Wall -Wextra -Werror -pthread -DVALVE_CLIENT_HOST_TEST -DVALVE_CLIENT_HTTP_HOST_TEST \
    -Itests/weather_runtime/fakes -Itests/admin_dial/fakes -Iidf_app/main \
    tests/weather_runtime/test_action_transport.c tests/weather_runtime/runtime_fixture.c \
    idf_app/main/valve_client_dial.c idf_app/main/valve_logic.c -o "$tmp_dir/action"
if [ "${1:-all}" != worker ]; then "$tmp_dir/action"; fi
cc -std=c11 -Wall -Wextra -Werror -pthread \
    -DADMIN_HTTP_RUNTIME_TEST -DMBEDTLS_CONFIG_FILE='"mbedtls_test_config.h"' \
    -Itests/weather_runtime/fakes -Itests/admin_dial/http_fakes -Itests/admin_dial/fakes -Itests/admin_dial \
    -I"$idf/components/json/cJSON" -I"$idf/components/mbedtls/mbedtls/include" -I"$idf/components/mbedtls/mbedtls/library" \
    -Iidf_app/main -Iinclude -Icommon -Icomponents/rk_ble_hid_host/include \
    tests/weather_runtime/test_weather_worker.c tests/weather_runtime/runtime_fixture.c tests/admin_dial/http_fixture.c \
    idf_app/main/weather_admin_dial.c idf_app/main/valve_logic.c idf_app/main/admin_server_dial.c \
    "$idf/components/json/cJSON/cJSON.c" "$idf/components/mbedtls/mbedtls/library/sha256.c" \
    "$idf/components/mbedtls/mbedtls/library/platform_util.c" -lm -o "$tmp_dir/worker"
if [ "${1:-all}" != action ]; then "$tmp_dir/worker"; fi

#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
idf="${IDF_PATH:-/tmp/esp-idf-5.5.5}"
# Rename only the host parser boundary so tests can observe entry, while
# retaining the real parser implementation and recursion behavior.
cc -std=c11 -DcJSON_ParseWithLengthOpts=admin_test_real_parse \
 -I"$idf"/components/json/cJSON -c "$idf"/components/json/cJSON/cJSON.c \
 -o "$tmp_dir/json.o"
cc -std=c11 -DcJSON_ParseWithLengthOpts=admin_test_observed_parse -Wall -Wextra -Werror -Wno-unused-parameter \
 -DMBEDTLS_CONFIG_FILE='"mbedtls_test_config.h"' \
 -I"$idf"/components/mbedtls/mbedtls/include \
 -I"$idf"/components/mbedtls/mbedtls/library \
 -Itests/admin_dial/http_fakes -Itests/admin_dial/fakes -Itests/admin_dial \
 -I"$idf"/components/json/cJSON -Iidf_app/main -Iinclude -Icommon -Icomponents/rk_ble_hid_host/include \
 tests/admin_dial/test_admin_http.c tests/admin_dial/http_fixture.c \
 idf_app/main/admin_server_dial.c \
 "$idf"/components/mbedtls/mbedtls/library/sha256.c \
 "$idf"/components/mbedtls/mbedtls/library/platform_util.c \
 idf_app/main/config_server.c idf_app/main/captive_portal.c \
 "$tmp_dir/json.o" -lm \
 -o "$tmp_dir/http"
"$tmp_dir/http"

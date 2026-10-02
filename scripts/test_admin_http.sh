#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
idf="${IDF_PATH:-/tmp/esp-idf-5.5.5}"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
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
 "$idf"/components/json/cJSON/cJSON.c -lm \
 -o "$tmp_dir/http"
"$tmp_dir/http"

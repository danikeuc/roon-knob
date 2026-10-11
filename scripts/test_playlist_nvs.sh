#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
task_tmp="$(mktemp -d)"
trap 'rm -rf "$task_tmp"' EXIT
idf="${IDF_PATH:-/tmp/esp-idf-5.5.5}"
# Real IDF nvs.h/esp_err.h/esp_attr.h; the host needs no target sdkconfig options.
: > "$task_tmp/sdkconfig.h"
cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
 -I"$task_tmp" -I"$idf/components/nvs_flash/include" -I"$idf/components/esp_common/include" \
 -I"$idf/components/json/cJSON" -Icommon -Iinclude -Iidf_app/main \
 tests/playlist_dial/test_playlist_nvs.c idf_app/main/playlist_admin_dial.c \
 -Wl,--gc-sections -lm -o "$task_tmp/nvs"
mkdir "$task_tmp/flash"
"$task_tmp/nvs" save "$task_tmp/flash"
"$task_tmp/nvs" restart-load "$task_tmp/flash"
"$task_tmp/nvs" failures "$task_tmp/flash"

#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
task_tmp="$(mktemp -d)"
trap 'rm -rf "$task_tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Icommon tests/test_controller_playlist.c common/controller_playlist.c -o "$task_tmp/state"
"$task_tmp/state"
idf="${IDF_PATH:-/tmp/esp-idf-5.5.5}"
for suite in admin client; do
 cc -std=c11 -Wall -Wextra -Werror -DPLAYLIST_HOST_TEST -Icommon -Iinclude -Iidf_app/main -I"$idf/components/json/cJSON" \
 "tests/playlist_dial/test_playlist_$suite.c" common/controller_playlist.c idf_app/main/playlist_client_dial.c idf_app/main/playlist_admin_dial.c \
 "$idf/components/json/cJSON/cJSON.c" -lm -o "$task_tmp/$suite"
 "$task_tmp/$suite"
done
cc -std=c11 -Wall -Wextra -Werror -DPLAYLIST_UI_HOST_TEST -Icommon -Iinclude -Iidf_app/main \
 tests/playlist_dial/test_playlist_ui.c idf_app/main/playlist_ui_dial.c common/controller_playlist.c common/controller_input.c common/platform/platform_task.c \
 tests/playlist_dial/input_profile_fake.c -o "$task_tmp/ui"
"$task_tmp/ui"
python3 tests/playlist_dial/test_artwork_transport.py
cc -std=c11 -Wall -Wextra -Werror -pthread \
 -Itests/playlist_dial/runtime_fakes -Itests/weather_runtime/fakes -Itests/admin_dial/http_fakes -Itests/admin_dial/fakes \
 -Itests/weather_runtime -Icommon -Iinclude -Iidf_app/main -I"$idf/components/json/cJSON" \
 tests/playlist_dial/test_playlist_worker.c tests/playlist_dial/runtime_fixture.c \
 idf_app/main/playlist_client_dial.c idf_app/main/playlist_admin_dial.c common/controller_playlist.c \
 "$idf/components/json/cJSON/cJSON.c" -lm -o "$task_tmp/worker"
"$task_tmp/worker"

python3 tests/playlist_dial/test_driver_playlist.py

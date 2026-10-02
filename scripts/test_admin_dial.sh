#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
for suite in store auth; do
    extra_source=""
    if [ "$suite" = auth ]; then extra_source="idf_app/main/admin_store_dial.c"; fi
    cc -std=c11 -Wall -Wextra -Werror -pthread \
        -Itests/admin_dial/fakes -Itests/admin_dial -Iidf_app/main \
        "tests/admin_dial/test_admin_$suite.c" "idf_app/main/admin_${suite}_dial.c" $extra_source \
        -o "$tmp_dir/test_$suite"
    "$tmp_dir/test_$suite"
done
# IDF_PATH selects the same mbedTLS source used for the target build.
mbedtls="${IDF_PATH:-/tmp/esp-idf-5.5.5}/components/mbedtls/mbedtls"
if [ ! -f "$mbedtls/library/pkcs5.c" ]; then
    echo "Set IDF_PATH to ESP-IDF 5.5.5 for the production crypto vector" >&2
    exit 1
fi
cc -std=c11 -Wall -Wextra -Werror \
    -DMBEDTLS_CONFIG_FILE='"mbedtls_test_config.h"' \
    -Itests/admin_dial/fakes -Itests/admin_dial -Iidf_app/main \
    -I"$mbedtls/include" -I"$mbedtls/library" \
    tests/admin_dial/test_admin_crypto.c idf_app/main/admin_crypto_dial.c \
    "$mbedtls/library/pkcs5.c" "$mbedtls/library/md.c" \
    "$mbedtls/library/sha256.c" "$mbedtls/library/platform_util.c" \
    "$mbedtls/library/constant_time.c" -o "$tmp_dir/test_crypto"
"$tmp_dir/test_crypto"

python3 tests/admin_dial/test_boot_storage.py

# Execute registered STA/AP/admin handlers against the host HTTP transport.
sh scripts/test_admin_http.sh

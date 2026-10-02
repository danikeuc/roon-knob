#include "admin_backend_dial.h"
#include "mbedtls/pkcs5.h"
#include <string.h>
bool admin_backend_kdf(const char *secret,const uint8_t salt[16],uint8_t out[32]) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        (const unsigned char *)secret,strlen(secret),salt,16,
        ADMIN_KDF_ITERATIONS,32,out)==0;
}

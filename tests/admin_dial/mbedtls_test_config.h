/* Minimal host build of the installed IDF mbedTLS source, no crypto mocks. */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_PKCS5_C

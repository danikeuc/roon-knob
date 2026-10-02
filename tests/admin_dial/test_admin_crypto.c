#include "admin_backend_dial.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    /* Independently generated with Python/OpenSSL hashlib.pbkdf2_hmac:
     * SHA256, password='password', salt=00..0f, iterations=100000, dklen=32. */
    const uint8_t expected[32]={0xa2,0x9f,0xea,0x0f,0xed,0x85,0xc5,0xb8,
        0x61,0x0c,0x2e,0x56,0x97,0xea,0x41,0xb5,0x58,0x71,0x39,0xe5,
        0x8a,0x38,0x8e,0x0c,0x7b,0x7c,0xed,0x30,0xd4,0xe6,0xd8,0xdf};
    uint8_t salt[16],out[32];for(unsigned i=0;i<16;i++)salt[i]=(uint8_t)i;
    if(!admin_backend_kdf("password",salt,out) || memcmp(out,expected,32)) {
        fputs("FAIL production PBKDF2 vector\n",stderr);return 1;
    }
    puts("production PBKDF2-SHA256 (100000 iterations): PASS");return 0;
}

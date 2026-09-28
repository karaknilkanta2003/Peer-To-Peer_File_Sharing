#include "../include/sha1.h"
#include <openssl/sha.h>
#include <cstdio>

void computeSHA1(const char *data, size_t len, char *outputHex) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data), len, hash);

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(outputHex + (i * 2), "%02x", hash[i]);
    }
    outputHex[40] = '\0';
}
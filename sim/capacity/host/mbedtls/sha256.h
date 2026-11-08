#pragma once
#include <openssl/sha.h>
inline int mbedtls_sha256(const unsigned char* in, size_t n, unsigned char out[32], int is224) {
  (void)is224; SHA256(in, n, out); return 0;
}

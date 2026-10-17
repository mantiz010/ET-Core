#pragma once
#include <openssl/evp.h>
#include <cstring>
#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL -0x002A
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER -0x002C
inline int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  size_t need = 4 * ((slen + 2) / 3) + 1;
  if (dlen < need) { *olen = need; return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL; }
  *olen = (size_t)EVP_EncodeBlock(dst, src, (int)slen); return 0;
}
inline int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  if (slen % 4) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
  size_t pad = (slen && src[slen - 1] == '=') + (slen > 1 && src[slen - 2] == '=');
  size_t need = slen / 4 * 3 - pad;
  if (!dst || dlen < need) { *olen = need; return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL; }
  unsigned char tmp[4096]; if (need + 3 > sizeof tmp) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  if (EVP_DecodeBlock(tmp, src, (int)slen) < 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
  memcpy(dst, tmp, need); *olen = need; return 0;
}

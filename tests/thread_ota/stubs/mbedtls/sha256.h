#pragma once
#include <openssl/sha.h>
using mbedtls_sha256_context=SHA256_CTX;
inline void mbedtls_sha256_init(mbedtls_sha256_context*){}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){return SHA256_Init(c)?0:1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const void* p,size_t n){return SHA256_Update(c,p,n)?0:1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,uint8_t* h){return SHA256_Final(h,c)?0:1;}

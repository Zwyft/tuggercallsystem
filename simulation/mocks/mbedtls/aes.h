#pragma once
#include <cstring>
#include <iostream>

// Mock AES for Simulation
// Simple XOR "encryption" to verify pipeline logic without full AES
// implementation

#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0

typedef struct {
  unsigned char key[16];
} mbedtls_aes_context;

inline void mbedtls_aes_init(mbedtls_aes_context *ctx) {
  memset(ctx, 0, sizeof(mbedtls_aes_context));
}

inline void mbedtls_aes_free(mbedtls_aes_context *ctx) {
  // nothing
}

inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,
                                  const unsigned char *key,
                                  unsigned int keybits) {
  memcpy(ctx->key, key, 16);
  return 0;
}

inline int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx,
                                  const unsigned char *key,
                                  unsigned int keybits) {
  memcpy(ctx->key, key, 16);
  return 0;
}

inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx, int mode,
                                 const unsigned char input[16],
                                 unsigned char output[16]) {
  // Simple XOR with Key for visualization
  for (int i = 0; i < 16; i++) {
    output[i] = input[i] ^ ctx->key[i];
  }
  return 0;
}

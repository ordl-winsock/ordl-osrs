/*
 * ORDL GovCon - Cryptographic Primitives
 * SHA-3 (Keccak), HMAC-SHA3-256, AES-256-GCM interfaces
 * Pure C23, zero external dependencies, FIPS-aligned
 */

#ifndef GOVCON_CRYPTO_H
#define GOVCON_CRYPTO_H

#include "core/compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA3-256 */
#define GC_SHA3_256_RATE    136  /* 1600 - 2*256 bits */
#define GC_SHA3_256_DIGEST  32
#define GC_SHA3_512_DIGEST  64

/* SHA-3 context */
typedef struct {
    uint64_t state[25];
    uint8_t  buf[200];
    size_t   rate;       /* Bytes absorbed per block */
    size_t   buf_used;
    size_t   digest_bits;
} gc_sha3_ctx_t;

/* Initialize SHA3-256 */
void gc_sha3_256_init(gc_sha3_ctx_t *ctx);

/* Initialize SHA3-512 */
void gc_sha3_512_init(gc_sha3_ctx_t *ctx);

/* Absorb data */
void gc_sha3_update(gc_sha3_ctx_t *ctx, const uint8_t *data, size_t len);

/* Finalize and output digest */
void gc_sha3_final(gc_sha3_ctx_t *ctx, uint8_t *digest);

/* One-shot SHA3-256 */
void gc_sha3_256(const uint8_t *data, size_t len, uint8_t digest[32]);

/* One-shot SHA3-512 */
void gc_sha3_512(const uint8_t *data, size_t len, uint8_t digest[64]);

/* HMAC-SHA3-256 */
void gc_hmac_sha3_256(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len,
                      uint8_t mac[32]);

/* Secure memory zero */
void gc_memzero(void *p, size_t n);

/* Constant-time compare */
GC_NODISCARD int gc_memeq(const uint8_t *a, const uint8_t *b, size_t n);

/* Base64 encode (no padding optional) */
GC_NODISCARD size_t gc_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);

/* Base64 decode (RFC 4648) */
GC_NODISCARD size_t gc_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

/* AES-256-GCM (interface only; full implementation later) */
#define GC_AES_256_KEY_SIZE 32
#define GC_AES_256_IV_SIZE  12
#define GC_AES_GCM_TAG_SIZE 16

typedef struct {
    uint32_t rk[60];     /* Round keys */
    uint8_t  iv[12];
} gc_aes256_gcm_ctx_t;

void gc_aes256_gcm_init(gc_aes256_gcm_ctx_t *ctx, const uint8_t key[32], const uint8_t iv[12]);

void gc_aes256_gcm_encrypt(gc_aes256_gcm_ctx_t *ctx,
                           const uint8_t *plaintext, size_t pt_len,
                           const uint8_t *aad, size_t aad_len,
                           uint8_t *ciphertext,
                           uint8_t tag[16]);

GC_NODISCARD int gc_aes256_gcm_decrypt(gc_aes256_gcm_ctx_t *ctx,
                          const uint8_t *ciphertext, size_t ct_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t tag[16],
                          uint8_t *plaintext);

#ifdef __cplusplus
}
#endif

#endif /* GOVCON_CRYPTO_H */

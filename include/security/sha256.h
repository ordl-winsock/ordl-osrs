/*
 * ORDL GovCon - SHA-256 (FIPS 180-4)
 * Required for TLS 1.3 HKDF and transcript hashing
 * Pure C23, zero dependencies
 */

#ifndef GOVCON_SHA256_H
#define GOVCON_SHA256_H

#include "core/compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-384 */
typedef struct {
    uint64_t state[8];
    uint64_t bit_count;
    uint8_t  buffer[128];
    size_t   buflen;
} gc_sha384_t;

void gc_sha384_init(gc_sha384_t *ctx);
void gc_sha384_update(gc_sha384_t *ctx, const uint8_t *data, size_t len);
void gc_sha384_final(gc_sha384_t *ctx, uint8_t digest[48]);
void gc_sha384(const uint8_t *data, size_t len, uint8_t digest[48]);

/* SHA-1 (for WebSocket handshake RFC 6455) */
typedef struct {
    uint32_t state[5];
    uint64_t bit_count;
    uint8_t  buffer[64];
    size_t   buflen;
} gc_sha1_t;

void gc_sha1_init(gc_sha1_t *ctx);
void gc_sha1_update(gc_sha1_t *ctx, const uint8_t *data, size_t len);
void gc_sha1_final(gc_sha1_t *ctx, uint8_t digest[20]);
void gc_sha1(const uint8_t *data, size_t len, uint8_t digest[20]);

/* HMAC-SHA384 */
void gc_hmac_sha384(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t mac[48]);

/* HKDF-SHA384 (RFC 5869) */
void gc_hkdf_sha384_extract(const uint8_t *salt, size_t salt_len,
                            const uint8_t *ikm, size_t ikm_len,
                            uint8_t prk[48]);
void gc_hkdf_sha384_expand(const uint8_t prk[48],
                           const uint8_t *info, size_t info_len,
                           uint8_t *okm, size_t okm_len);

/* SHA-256 */
#define GC_SHA256_BLOCK_SIZE 64
#define GC_SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buffer[64];
    size_t   buflen;
} gc_sha256_t;

void gc_sha256_init(gc_sha256_t *ctx);
void gc_sha256_update(gc_sha256_t *ctx, const uint8_t *data, size_t len);
void gc_sha256_final(gc_sha256_t *ctx, uint8_t digest[32]);
void gc_sha256(const uint8_t *data, size_t len, uint8_t digest[32]);

/* HMAC-SHA256 */
void gc_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t mac[32]);

/* HKDF-SHA256 (RFC 5869) */
void gc_hkdf_sha256_extract(const uint8_t *salt, size_t salt_len,
                            const uint8_t *ikm, size_t ikm_len,
                            uint8_t prk[32]);
void gc_hkdf_sha256_expand(const uint8_t prk[32],
                           const uint8_t *info, size_t info_len,
                           uint8_t *okm, size_t okm_len);

#ifdef __cplusplus
}
#endif

#endif /* GOVCON_SHA256_H */

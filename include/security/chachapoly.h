/*
 * ORDL GovCon - ChaCha20-Poly1305 AEAD
 * RFC 8439 compliant. Adapted from ordl-infercli reference.
 * Pure C23, zero dependencies
 */

#ifndef GOVCON_CHACHAPOLY_H
#define GOVCON_CHACHAPOLY_H

#include "core/compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC_CHACHA_KEY_LEN   32
#define GC_CHACHA_NONCE_LEN 12

void gc_chacha20_crypt(const uint8_t key[GC_CHACHA_KEY_LEN],
                        const uint8_t nonce[GC_CHACHA_NONCE_LEN],
                        uint32_t counter,
                        uint8_t *data, size_t len);

void gc_chachapoly_seal(const uint8_t key[GC_CHACHA_KEY_LEN],
                         const uint8_t nonce[GC_CHACHA_NONCE_LEN],
                         const uint8_t *ad, size_t ad_len,
                         const uint8_t *pt, size_t pt_len,
                         uint8_t *ct, uint8_t tag[16]);

GC_NODISCARD int gc_chachapoly_open(const uint8_t key[GC_CHACHA_KEY_LEN],
                        const uint8_t nonce[GC_CHACHA_NONCE_LEN],
                        const uint8_t *ad, size_t ad_len,
                        const uint8_t *ct, size_t ct_len,
                        const uint8_t tag[16],
                        uint8_t *pt);

#ifdef __cplusplus
}
#endif

#endif /* GOVCON_CHACHAPOLY_H */

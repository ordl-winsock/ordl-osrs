/*
 * ORDL GovCon - X25519 Key Exchange
 * RFC 7748 compliant. Adapted from ordl-infercli reference.
 * Pure C23, zero dependencies
 */

#ifndef GOVCON_X25519_H
#define GOVCON_X25519_H

#include "core/compat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC_X25519_KEY_LEN   32
#define GC_X25519_POINT_LEN 32

void gc_x25519_gen_private(uint8_t out[GC_X25519_KEY_LEN], const uint8_t entropy[GC_X25519_KEY_LEN]);
void gc_x25519_public_from_private(uint8_t out[GC_X25519_POINT_LEN], const uint8_t priv[GC_X25519_KEY_LEN]);
void gc_x25519_shared_secret(uint8_t out[GC_X25519_POINT_LEN],
                              const uint8_t priv[GC_X25519_KEY_LEN],
                              const uint8_t pub[GC_X25519_POINT_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* GOVCON_X25519_H */

/*
 * osrs/bignum.h - Fixed-size big integer arithmetic for RSA
 * Pure C23, zero external dependencies.
 *
 * Numbers are little-endian arrays of uint32_t limbs:
 * limb[0] is least significant. Fixed capacity; operations
 * never allocate.
 */

#ifndef OSRS_BIGNUM_H
#define OSRS_BIGNUM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 2048-bit capacity: enough for 1024-bit RSA products */
#define OSRS_BN_MAX_LIMBS 64

typedef struct {
  uint32_t limb[OSRS_BN_MAX_LIMBS];
  int len; /* significant limbs (no trailing zeros beyond len) */
} osrs_bn_t;

/* Set from raw big-endian bytes */
void osrs_bn_from_bytes(osrs_bn_t *a, const uint8_t *bytes, size_t len);

/* Export as big-endian bytes into out (exactly out_len bytes, zero-padded) */
void osrs_bn_to_bytes(const osrs_bn_t *a, uint8_t *out, size_t out_len);

/* Basic ops */
void osrs_bn_set_zero(osrs_bn_t *a);
void osrs_bn_copy(osrs_bn_t *dst, const osrs_bn_t *src);
bool osrs_bn_is_zero(const osrs_bn_t *a);
int osrs_bn_cmp(const osrs_bn_t *a, const osrs_bn_t *b); /* -1/0/1 */
int osrs_bn_bits(const osrs_bn_t *a);
bool osrs_bn_test_bit(const osrs_bn_t *a, int bit);

/* a += b */
void osrs_bn_add(osrs_bn_t *a, const osrs_bn_t *b);

/* a -= b (requires a >= b) */
void osrs_bn_sub(osrs_bn_t *a, const osrs_bn_t *b);

/* r = a * b */
void osrs_bn_mul(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *b);

/* r = a mod m (binary long division; a may be larger than capacity/2) */
void osrs_bn_mod(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *m);

/* r = (a * b) mod m */
void osrs_bn_mulmod(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *b,
                    const osrs_bn_t *m);

/* r = base^exp mod m (square-and-multiply) */
void osrs_bn_modpow(osrs_bn_t *r, const osrs_bn_t *base, const osrs_bn_t *exp,
                    const osrs_bn_t *m);

#endif /* OSRS_BIGNUM_H */

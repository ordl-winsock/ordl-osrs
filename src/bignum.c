/*
 * osrs/bignum.c - Fixed-size big integer arithmetic for RSA
 * Pure C23, zero external dependencies.
 */

#include "osrs/bignum.h"
#include <string.h>

static void bn_normalize(osrs_bn_t *a) {
  while (a->len > 0 && a->limb[a->len - 1] == 0)
    a->len--;
}

void osrs_bn_set_zero(osrs_bn_t *a) {
  memset(a->limb, 0, sizeof(a->limb));
  a->len = 0;
}

void osrs_bn_copy(osrs_bn_t *dst, const osrs_bn_t *src) {
  memcpy(dst->limb, src->limb, sizeof(dst->limb));
  dst->len = src->len;
}

bool osrs_bn_is_zero(const osrs_bn_t *a) { return a->len == 0; }

void osrs_bn_from_bytes(osrs_bn_t *a, const uint8_t *bytes, size_t len) {
  osrs_bn_set_zero(a);
  for (size_t i = 0; i < len; i++) {
    /* bytes are big-endian: bytes[0] is most significant */
    size_t from_end = len - 1 - i; /* position from the end */
    int limb_idx = (int)(from_end / 4);
    int byte_idx = (int)(from_end % 4);
    if (limb_idx < OSRS_BN_MAX_LIMBS) {
      a->limb[limb_idx] |= (uint32_t)bytes[i] << (byte_idx * 8);
    }
  }
  a->len = (int)((len + 3) / 4);
  if (a->len > OSRS_BN_MAX_LIMBS)
    a->len = OSRS_BN_MAX_LIMBS;
  bn_normalize(a);
}

void osrs_bn_to_bytes(const osrs_bn_t *a, uint8_t *out, size_t out_len) {
  memset(out, 0, out_len);
  for (size_t i = 0; i < out_len; i++) {
    size_t from_end = out_len - 1 - i;
    int limb_idx = (int)(from_end / 4);
    int byte_idx = (int)(from_end % 4);
    if (limb_idx < a->len) {
      out[i] = (uint8_t)(a->limb[limb_idx] >> (byte_idx * 8));
    }
  }
}

int osrs_bn_cmp(const osrs_bn_t *a, const osrs_bn_t *b) {
  if (a->len != b->len)
    return a->len < b->len ? -1 : 1;
  for (int i = a->len - 1; i >= 0; i--) {
    if (a->limb[i] != b->limb[i])
      return a->limb[i] < b->limb[i] ? -1 : 1;
  }
  return 0;
}

int osrs_bn_bits(const osrs_bn_t *a) {
  if (a->len == 0)
    return 0;
  uint32_t top = a->limb[a->len - 1];
  int bits = (a->len - 1) * 32;
  while (top) {
    bits++;
    top >>= 1;
  }
  return bits;
}

bool osrs_bn_test_bit(const osrs_bn_t *a, int bit) {
  if (bit < 0)
    return false;
  int limb_idx = bit / 32;
  if (limb_idx >= a->len)
    return false;
  return (a->limb[limb_idx] >> (bit % 32)) & 1;
}

void osrs_bn_add(osrs_bn_t *a, const osrs_bn_t *b) {
  uint64_t carry = 0;
  int max_len = a->len > b->len ? a->len : b->len;
  for (int i = 0; i < max_len || carry; i++) {
    if (i >= OSRS_BN_MAX_LIMBS)
      break;
    uint64_t sum = carry;
    if (i < a->len)
      sum += a->limb[i];
    if (i < b->len)
      sum += b->limb[i];
    a->limb[i] = (uint32_t)sum;
    carry = sum >> 32;
    if (i >= a->len)
      a->len = i + 1;
  }
}

void osrs_bn_sub(osrs_bn_t *a, const osrs_bn_t *b) {
  uint64_t borrow = 0;
  for (int i = 0; i < a->len; i++) {
    uint64_t bi = i < b->len ? b->limb[i] : 0;
    uint64_t diff = (uint64_t)a->limb[i] - bi - borrow;
    a->limb[i] = (uint32_t)diff;
    borrow = (diff >> 32) & 1;
  }
  bn_normalize(a);
}

void osrs_bn_mul(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *b) {
  osrs_bn_t tmp;
  osrs_bn_set_zero(&tmp);
  if (a->len == 0 || b->len == 0) {
    osrs_bn_set_zero(r);
    return;
  }
  for (int i = 0; i < a->len; i++) {
    uint64_t carry = 0;
    for (int j = 0; j < b->len; j++) {
      if (i + j >= OSRS_BN_MAX_LIMBS)
        break;
      uint64_t cur =
          (uint64_t)a->limb[i] * b->limb[j] + tmp.limb[i + j] + carry;
      tmp.limb[i + j] = (uint32_t)cur;
      carry = cur >> 32;
    }
    if (i + b->len < OSRS_BN_MAX_LIMBS)
      tmp.limb[i + b->len] = (uint32_t)carry;
  }
  tmp.len = a->len + b->len;
  if (tmp.len > OSRS_BN_MAX_LIMBS)
    tmp.len = OSRS_BN_MAX_LIMBS;
  bn_normalize(&tmp);
  osrs_bn_copy(r, &tmp);
}

/* Shift left by one bit */
static void bn_shl1(osrs_bn_t *a) {
  uint32_t carry = 0;
  for (int i = 0; i < a->len; i++) {
    uint32_t next_carry = a->limb[i] >> 31;
    a->limb[i] = (a->limb[i] << 1) | carry;
    carry = next_carry;
  }
  if (carry && a->len < OSRS_BN_MAX_LIMBS)
    a->limb[a->len++] = carry;
}

void osrs_bn_mod(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *m) {
  osrs_bn_set_zero(r);
  if (osrs_bn_is_zero(m))
    return;
  if (osrs_bn_cmp(a, m) < 0) {
    osrs_bn_copy(r, a);
    return;
  }
  /* Binary long division: walk bits from MSB to LSB */
  int nbits = osrs_bn_bits(a);
  osrs_bn_t rem;
  osrs_bn_set_zero(&rem);
  for (int i = nbits - 1; i >= 0; i--) {
    bn_shl1(&rem);
    if (osrs_bn_test_bit(a, i)) {
      rem.limb[0] |= 1;
      if (rem.len == 0)
        rem.len = 1;
    }
    if (osrs_bn_cmp(&rem, m) >= 0)
      osrs_bn_sub(&rem, m);
  }
  osrs_bn_copy(r, &rem);
}

void osrs_bn_mulmod(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *b,
                    const osrs_bn_t *m) {
  osrs_bn_t prod;
  osrs_bn_mul(&prod, a, b);
  osrs_bn_mod(r, &prod, m);
}

void osrs_bn_modpow(osrs_bn_t *r, const osrs_bn_t *base, const osrs_bn_t *exp,
                    const osrs_bn_t *m) {
  osrs_bn_t result, b;
  osrs_bn_set_zero(&result);
  result.limb[0] = 1;
  result.len = 1;
  osrs_bn_mod(&b, base, m);

  int ebits = osrs_bn_bits(exp);
  for (int i = 0; i < ebits; i++) {
    if (osrs_bn_test_bit(exp, i)) {
      osrs_bn_t t;
      osrs_bn_mulmod(&t, &result, &b, m);
      osrs_bn_copy(&result, &t);
    }
    if (i < ebits - 1) {
      osrs_bn_t t;
      osrs_bn_mulmod(&t, &b, &b, m);
      osrs_bn_copy(&b, &t);
    }
  }
  osrs_bn_copy(r, &result);
}

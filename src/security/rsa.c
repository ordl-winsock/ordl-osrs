/*
 * ORDL GovCon - Minimal Big Integer & RSA-PSS-SHA256 Signing
 * Compact implementation for TLS 1.3 CertificateVerify.
 */

#include "security/rsa.h"
#include "security/sha256.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef explicit_bzero
#define explicit_bzero(ptr, len) memset(ptr, 0, len)
#endif

#define BN_MAX_WORDS 64

typedef struct { uint64_t d[BN_MAX_WORDS]; int n; } bn_t;

static void bn_zero(bn_t *a) { memset(a->d, 0, sizeof(a->d)); a->n = 1; }

static void bn_from_bytes_be(bn_t *a, const uint8_t *data, size_t len) {
    bn_zero(a);
    for (size_t i = 0; i < len; i++) {
        int word = (int)(i / 8);
        int shift = (int)((7 - (i % 8)) * 8);
        a->d[word] |= ((uint64_t)data[i]) << shift;
    }
    a->n = (int)((len + 7) / 8);
    while (a->n > 1 && a->d[a->n - 1] == 0) a->n--;
}

static void bn_to_bytes_be(const bn_t *a, uint8_t *out, size_t len) {
    memset(out, 0, len);
    for (int i = 0; i < a->n; i++) {
        for (int j = 0; j < 8; j++) {
            size_t idx = len - 1 - (size_t)(i * 8 + j);
            if (idx < len) out[idx] = (uint8_t)(a->d[i] >> (j * 8));
        }
    }
}

static int bn_cmp(const bn_t *a, const bn_t *b) {
    int n = a->n > b->n ? a->n : b->n;
    for (int i = n - 1; i >= 0; i--) {
        uint64_t av = i < a->n ? a->d[i] : 0;
        uint64_t bv = i < b->n ? b->d[i] : 0;
        if (av > bv) return 1;
        if (av < bv) return -1;
    }
    return 0;
}

static void bn_sub(bn_t *r, const bn_t *a, const bn_t *b) {
    uint64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t av = a->d[i];
        uint64_t bv = i < b->n ? b->d[i] : 0;
        __uint128_t diff = (__uint128_t)av - bv - borrow;
        r->d[i] = (uint64_t)diff;
        borrow = (uint64_t)(diff >> 127);
    }
    r->n = a->n;
    while (r->n > 1 && r->d[r->n - 1] == 0) r->n--;
}

static void bn_shl1(bn_t *a) {
    uint64_t carry = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t new_carry = a->d[i] >> 63;
        a->d[i] = (a->d[i] << 1) | carry;
        carry = new_carry;
    }
    if (carry) a->d[a->n++] = carry;
}

static void bn_shr1(bn_t *a) {
    uint64_t carry = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        uint64_t new_carry = a->d[i] & 1;
        a->d[i] = (a->d[i] >> 1) | (carry << 63);
        carry = new_carry;
    }
    while (a->n > 1 && a->d[a->n - 1] == 0) a->n--;
}

static void bn_mul(bn_t *r, const bn_t *a, const bn_t *b) {
    bn_t t; bn_zero(&t);
    int max_n = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b->n || carry; j++) {
            if (i + j >= BN_MAX_WORDS) break;
            uint64_t bv = j < b->n ? b->d[j] : 0;
            __uint128_t prod = (__uint128_t)a->d[i] * bv + t.d[i + j] + carry;
            t.d[i + j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
            if (i + j + 1 > max_n) max_n = i + j + 1;
        }
        if (carry && i + b->n < BN_MAX_WORDS) {
            t.d[i + b->n] = carry;
            if (i + b->n + 1 > max_n) max_n = i + b->n + 1;
        }
    }
    t.n = max_n;
    while (t.n > 1 && t.d[t.n - 1] == 0) t.n--;
    if (t.n > BN_MAX_WORDS) t.n = BN_MAX_WORDS;
    *r = t;
}

static void bn_divmod(const bn_t *a, const bn_t *m, bn_t *q, bn_t *r) {
    if (m->n == 0 || (m->n == 1 && m->d[0] == 0)) { bn_zero(q); bn_zero(r); return; }
    if (bn_cmp(a, m) < 0) { bn_zero(q); *r = *a; return; }
    bn_t A = *a, M = *m;
    bn_zero(q); bn_zero(r);
    int total_bits = A.n * 64;
    while (total_bits > 0) {
        int word = (total_bits - 1) / 64, bit = (total_bits - 1) % 64;
        if (word < A.n && ((A.d[word] >> bit) & 1)) break;
        total_bits--;
    }
    for (int i = total_bits - 1; i >= 0; i--) {
        bn_shl1(r);
        int word = i / 64, bit = i % 64;
        if (word < A.n) r->d[0] |= (A.d[word] >> bit) & 1;
        if (bn_cmp(r, &M) >= 0) { bn_sub(r, r, &M); q->d[i / 64] |= (1ULL << (i % 64)); }
    }
}

static void bn_mod(bn_t *r, const bn_t *a, const bn_t *m) {
    bn_t q; bn_divmod(a, m, &q, r);
}

static void bn_mod_exp(bn_t *r, const bn_t *base, const bn_t *exp, const bn_t *mod) {
    bn_t result, b; bn_zero(&result); result.d[0] = 1; result.n = 1; b = *base;
    bn_t e = *exp;
    bn_mod(&b, &b, mod);
    while (e.n > 1 || e.d[0] > 0) {
        if (e.d[0] & 1) { bn_mul(&result, &result, &b); bn_mod(&result, &result, mod); }
        bn_mul(&b, &b, &b); bn_mod(&b, &b, mod); bn_shr1(&e);
    }
    *r = result;
}

/* MGF1-SHA256 */
static void mgf1_sha256(const uint8_t *seed, size_t seed_len, uint8_t *mask, size_t mask_len) {
    uint32_t counter = 0;
    size_t off = 0;
    while (off < mask_len) {
        uint8_t cbuf[4] = { (uint8_t)(counter >> 24), (uint8_t)(counter >> 16),
                            (uint8_t)(counter >> 8), (uint8_t)counter };
        uint8_t hash[32];
        gc_sha256_t ctx; gc_sha256_init(&ctx);
        gc_sha256_update(&ctx, seed, seed_len);
        gc_sha256_update(&ctx, cbuf, 4);
        gc_sha256_final(&ctx, hash);
        size_t chunk = mask_len - off < 32 ? mask_len - off : 32;
        memcpy(mask + off, hash, chunk);
        off += chunk; counter++;
    }
}

#define RSA_PSS_MAX_EMSIZE 1024

/* RSA-PSS-SHA256 sign */
int gc_rsa_pss_sha256_sign(const uint8_t *tbs_hash,
                            const uint8_t *n, size_t n_len,
                            const uint8_t *d, size_t d_len,
                            uint8_t *sig_out, size_t sig_len) {
    if (!tbs_hash || !n || !d || !sig_out) return -1;
    size_t key_len = n_len;
    if (key_len > 1 && n[0] == 0) key_len--;
    if (sig_len < key_len) return -1;
    if (sig_len > RSA_PSS_MAX_EMSIZE) return -1;

    size_t hLen = 32, sLen = 32;
    if (sig_len < hLen + sLen + 2) return -1;

    /* Generate random salt */
    uint8_t salt[32];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t rn = read(fd, salt, sLen);
    close(fd);
    if (rn != (ssize_t)sLen) return -1;

    /* M' = 0x00 0x00 ... 0x00 || mHash || salt */
    uint8_t Mprime[10 + 32 + 32];
    memset(Mprime, 0, 8);
    memcpy(Mprime + 8, tbs_hash, hLen);
    Mprime[8 + hLen] = 0;
    memcpy(Mprime + 9 + hLen, salt, sLen);

    uint8_t H[32];
    gc_sha256(Mprime, 10 + hLen + sLen, H);

    size_t dbLen = sig_len - hLen - 1;

    uint8_t EM[RSA_PSS_MAX_EMSIZE];
    uint8_t mask[RSA_PSS_MAX_EMSIZE];

    memset(EM, 0, dbLen);
    EM[dbLen - sLen - 1] = 0x01;
    memcpy(EM + dbLen - sLen, salt, sLen);

    mgf1_sha256(H, hLen, mask, dbLen);
    for (size_t i = 0; i < dbLen; i++) EM[i] ^= mask[i];
    EM[0] &= (uint8_t)(0xFF >> (8 * (dbLen + 1) - (n_len * 8 - 1) - 1));

    memcpy(EM + dbLen, H, hLen);
    EM[sig_len - 1] = 0xbc;

    /* RSA private key operation: s = EM^d mod n */
    bn_t n_bn, d_bn, em_bn, s_bn;
    bn_from_bytes_be(&n_bn, n, n_len);
    bn_from_bytes_be(&d_bn, d, d_len);
    bn_from_bytes_be(&em_bn, EM, sig_len);
    bn_mod_exp(&s_bn, &em_bn, &d_bn, &n_bn);
    bn_to_bytes_be(&s_bn, sig_out, sig_len);

    explicit_bzero(EM, sig_len);
    return 0;
}

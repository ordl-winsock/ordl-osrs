/*
 * ORDL GovCon - X25519 Implementation
 * RFC 7748. 5-limb base-2^51 representation.
 * Adapted from ordl-infercli reference (proven).
 */

#include "security/x25519.h"
#include <string.h>

typedef int64_t fe[5];

static void fe_0(fe h) { memset(h, 0, sizeof(fe)); }
static void fe_1(fe h) { fe_0(h); h[0] = 1; }

static void fe_add(fe h, const fe f, const fe g) {
    for (int i = 0; i < 5; i++) h[i] = f[i] + g[i];
}

static void fe_sub(fe h, const fe f, const fe g) {
    for (int i = 0; i < 5; i++) h[i] = f[i] - g[i];
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static void fe_mul(fe h, const fe f, const fe g) {
    __int128 f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    __int128 g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
    __int128 t0 = f0*g0 + 19*(f1*g4 + f2*g3 + f3*g2 + f4*g1);
    __int128 t1 = f0*g1 + f1*g0 + 19*(f2*g4 + f3*g3 + f4*g2);
    __int128 t2 = f0*g2 + f1*g1 + f2*g0 + 19*(f3*g4 + f4*g3);
    __int128 t3 = f0*g3 + f1*g2 + f2*g1 + f3*g0 + 19*f4*g4;
    __int128 t4 = f0*g4 + f1*g3 + f2*g2 + f3*g1 + f4*g0;
    __int128 c0 = t0 >> 51; t1 += c0; t0 -= c0 << 51;
    __int128 c1 = t1 >> 51; t2 += c1; t1 -= c1 << 51;
    __int128 c2 = t2 >> 51; t3 += c2; t2 -= c2 << 51;
    __int128 c3 = t3 >> 51; t4 += c3; t3 -= c3 << 51;
    __int128 c4 = t4 >> 51; t0 += c4 * 19; t4 -= c4 << 51;
    __int128 c5 = t0 >> 51; t1 += c5; t0 -= c5 << 51;
    h[0] = (int64_t)t0; h[1] = (int64_t)t1; h[2] = (int64_t)t2;
    h[3] = (int64_t)t3; h[4] = (int64_t)t4;
}
#pragma GCC diagnostic pop

static void fe_sq(fe h, const fe f) { fe_mul(h, f, f); }

static void fe_cswap(fe f, fe g, uint32_t swap) {
    int64_t mask = -(int64_t)swap;
    for (int i = 0; i < 5; i++) {
        int64_t t = mask & (f[i] ^ g[i]);
        f[i] ^= t; g[i] ^= t;
    }
}

static void fe_inv(fe out, const fe a) {
    fe t0, t1, t2, t3;
    fe_sq(t0, a); fe_sq(t1, t0); fe_sq(t1, t1);
    fe_mul(t1, a, t1); fe_mul(t0, t0, t1); fe_sq(t2, t0);
    fe_mul(t1, t1, t2);
    fe_sq(t2, t1);
    for (int i = 0; i < 4; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1); fe_sq(t2, t1);
    for (int i = 0; i < 9; i++) fe_sq(t2, t2);
    fe_mul(t2, t2, t1); fe_sq(t3, t2);
    for (int i = 0; i < 19; i++) fe_sq(t3, t3);
    fe_mul(t2, t3, t2); fe_sq(t3, t2);
    for (int i = 0; i < 9; i++) fe_sq(t3, t3);
    fe_mul(t1, t3, t1); fe_sq(t3, t1);
    for (int i = 0; i < 49; i++) fe_sq(t3, t3);
    fe_mul(t2, t3, t1); fe_sq(t3, t2);
    for (int i = 0; i < 99; i++) fe_sq(t3, t3);
    fe_mul(t3, t3, t2); fe_sq(t2, t3);
    for (int i = 0; i < 49; i++) fe_sq(t2, t2);
    fe_mul(t2, t2, t1); fe_sq(t1, t2);
    for (int i = 0; i < 4; i++) fe_sq(t1, t1);
    fe_mul(t0, t1, t0);
    for (int i = 0; i < 5; i++) out[i] = t0[i];
}

static void fe_frombytes(fe h, const uint8_t s[32]) {
    h[0] = s[0] | ((uint64_t)s[1] << 8) | ((uint64_t)s[2] << 16)
         | ((uint64_t)s[3] << 24) | ((uint64_t)s[4] << 32)
         | ((uint64_t)s[5] << 40) | (((uint64_t)s[6] & 0x07) << 48);
    h[1] = (s[6] >> 3) | ((uint64_t)s[7] << 5) | ((uint64_t)s[8] << 13)
         | ((uint64_t)s[9] << 21) | ((uint64_t)s[10] << 29)
         | ((uint64_t)s[11] << 37) | (((uint64_t)s[12] & 0x3F) << 45);
    h[2] = (s[12] >> 6) | ((uint64_t)s[13] << 2) | ((uint64_t)s[14] << 10)
         | ((uint64_t)s[15] << 18) | ((uint64_t)s[16] << 26)
         | ((uint64_t)s[17] << 34) | ((uint64_t)s[18] << 42)
         | (((uint64_t)s[19] & 0x01) << 50);
    h[3] = (s[19] >> 1) | ((uint64_t)s[20] << 7) | ((uint64_t)s[21] << 15)
         | ((uint64_t)s[22] << 23) | ((uint64_t)s[23] << 31)
         | ((uint64_t)s[24] << 39) | (((uint64_t)s[25] & 0x0F) << 47);
    h[4] = (s[25] >> 4) | ((uint64_t)s[26] << 4) | ((uint64_t)s[27] << 12)
         | ((uint64_t)s[28] << 20) | ((uint64_t)s[29] << 28)
         | ((uint64_t)s[30] << 36) | (((uint64_t)s[31] & 0x7F) << 44);
}

static void fe_tobytes(uint8_t s[32], const fe h) {
    int64_t t[5];
    for (int i = 0; i < 5; i++) t[i] = h[i];
    int64_t c = 0;
    for (int i = 0; i < 5; i++) {
        int64_t v = t[i] + c;
        c = v >> 51;
        t[i] = v & ((1LL << 51) - 1);
    }
    t[0] += c * 19; c = t[0] >> 51; t[0] &= ((1LL << 51) - 1);
    for (int i = 1; i < 5 && c; i++) {
        int64_t v = t[i] + c; c = v >> 51; t[i] = v & ((1LL << 51) - 1);
    }
    int64_t p0 = (1LL << 51) - 19, pm = (1LL << 51) - 1;
    int ge = 1;
    for (int i = 4; i >= 0; i--) {
        int64_t pi = (i == 0) ? p0 : pm;
        if (t[i] > pi) { ge = 1; break; }
        if (t[i] < pi) { ge = 0; break; }
    }
    if (ge) {
        int64_t borrow = 0;
        for (int i = 0; i < 5; i++) {
            int64_t pi = (i == 0) ? p0 : pm;
            int64_t v = t[i] + (1LL << 51) - pi - borrow;
            borrow = (v < (1LL << 51)) ? 1 : 0;
            t[i] = v & ((1LL << 51) - 1);
        }
    }
    s[0]  = (uint8_t)(t[0]); s[1]  = (uint8_t)(t[0] >> 8); s[2]  = (uint8_t)(t[0] >> 16);
    s[3]  = (uint8_t)(t[0] >> 24); s[4]  = (uint8_t)(t[0] >> 32); s[5]  = (uint8_t)(t[0] >> 40);
    s[6]  = (uint8_t)((t[0] >> 48) | (t[1] << 3)); s[7]  = (uint8_t)(t[1] >> 5);
    s[8]  = (uint8_t)(t[1] >> 13); s[9]  = (uint8_t)(t[1] >> 21); s[10] = (uint8_t)(t[1] >> 29);
    s[11] = (uint8_t)(t[1] >> 37); s[12] = (uint8_t)((t[1] >> 45) | (t[2] << 6));
    s[13] = (uint8_t)(t[2] >> 2); s[14] = (uint8_t)(t[2] >> 10); s[15] = (uint8_t)(t[2] >> 18);
    s[16] = (uint8_t)(t[2] >> 26); s[17] = (uint8_t)(t[2] >> 34); s[18] = (uint8_t)(t[2] >> 42);
    s[19] = (uint8_t)((t[2] >> 50) | (t[3] << 1)); s[20] = (uint8_t)(t[3] >> 7);
    s[21] = (uint8_t)(t[3] >> 15); s[22] = (uint8_t)(t[3] >> 23); s[23] = (uint8_t)(t[3] >> 31);
    s[24] = (uint8_t)(t[3] >> 39); s[25] = (uint8_t)((t[3] >> 47) | (t[4] << 4));
    s[26] = (uint8_t)(t[4] >> 4); s[27] = (uint8_t)(t[4] >> 12); s[28] = (uint8_t)(t[4] >> 20);
    s[29] = (uint8_t)(t[4] >> 28); s[30] = (uint8_t)(t[4] >> 36); s[31] = (uint8_t)(t[4] >> 44);
}

static void x25519_ladder(uint8_t out[32],
                          const uint8_t scalar[32],
                          const uint8_t point[32]) {
    uint8_t e[32];
    memcpy(e, scalar, 32);
    e[0] &= 0xf8; e[31] &= 0x7f; e[31] |= 0x40;
    fe x1, x2, z2, x3, z3;
    fe_frombytes(x1, point); fe_1(x2); fe_0(z2);
    fe_frombytes(x3, point); fe_1(z3);
    uint32_t swap = 0;
    for (int pos = 254; pos >= 0; pos--) {
        uint32_t bit = (e[pos / 8] >> (pos % 8)) & 1;
        swap ^= bit; fe_cswap(x2, x3, swap); fe_cswap(z2, z3, swap); swap = bit;
        fe a, b, aa, bb, ee, c, d, da, cb, a24;
        fe_add(a, x2, z2); fe_sub(b, x2, z2); fe_sq(aa, a); fe_sq(bb, b); fe_sub(ee, aa, bb);
        fe_add(c, x3, z3); fe_sub(d, x3, z3); fe_mul(da, d, a); fe_mul(cb, c, b);
        fe_add(x3, da, cb); fe_sq(x3, x3); fe_sub(z3, da, cb); fe_sq(z3, z3); fe_mul(z3, z3, x1);
        fe_mul(x2, aa, bb); fe_0(a24); a24[0] = 121665;
        fe_mul(a24, a24, ee); fe_add(a24, aa, a24); fe_mul(z2, ee, a24);
    }
    fe_cswap(x2, x3, swap); fe_cswap(z2, z3, swap);
    fe_inv(z2, z2); fe_mul(x2, x2, z2); fe_tobytes(out, x2);
}

static const uint8_t x25519_base_point[32] = {9};

void gc_x25519_gen_private(uint8_t out[GC_X25519_KEY_LEN],
                           const uint8_t entropy[GC_X25519_KEY_LEN]) {
    memcpy(out, entropy, GC_X25519_KEY_LEN);
    out[0] &= 0xf8; out[31] &= 0x7f; out[31] |= 0x40;
}

void gc_x25519_public_from_private(uint8_t out[GC_X25519_POINT_LEN],
                                    const uint8_t priv[GC_X25519_KEY_LEN]) {
    x25519_ladder(out, priv, x25519_base_point);
}

void gc_x25519_shared_secret(uint8_t out[GC_X25519_POINT_LEN],
                              const uint8_t priv[GC_X25519_KEY_LEN],
                              const uint8_t pub[GC_X25519_POINT_LEN]) {
    x25519_ladder(out, priv, pub);
}

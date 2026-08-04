/*
 * ORDL GovCon - SHA-3 (Keccak) Implementation
 * Pure C23, zero dependencies, constant-time where possible
 * Adapted from ORDL InferCLI proven implementation
 */

#include "core/compat.h"
#include "security/crypto.h"
#include <string.h>
#include <stdlib.h>

static const uint64_t RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

static_assert(sizeof(RC) / sizeof(RC[0]) == 24, "RC must have 24 elements");

/* Keccak rho offsets for lane (x,y) indexed as x + 5*y */
static const int rho_offsets[25] = {
    0, 1, 62, 28, 27,
    36, 44, 6, 55, 20,
    3, 10, 43, 25, 39,
    41, 45, 15, 21, 8,
    18, 2, 61, 56, 14
};

static_assert(sizeof(rho_offsets) / sizeof(rho_offsets[0]) == 25,
              "rho_offsets must have 25 elements");

static inline uint64_t ROTL64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

static void keccak_f1600(uint64_t st[25]) {
    for (int round = 0; round < 24; round++) {
        uint64_t C[5], D[5];

        /* Theta */
        for (int x = 0; x < 5; x++) {
            C[x] = st[x] ^ st[x+5] ^ st[x+10] ^ st[x+15] ^ st[x+20];
        }
        for (int x = 0; x < 5; x++) {
            D[x] = C[(x+4)%5] ^ ROTL64(C[(x+1)%5], 1);
            for (int y = 0; y < 5; y++) {
                st[x + 5*y] ^= D[x];
            }
        }

        /* Rho and Pi */
        uint64_t B[25];
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                int src_idx = x + 5*y;
                int dst_idx = y + 5*((2*x + 3*y) % 5);
                B[dst_idx] = ROTL64(st[src_idx], rho_offsets[src_idx]);
            }
        }

        /* Chi */
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                int idx = x + 5*y;
                int idx1 = (x+1)%5 + 5*y;
                int idx2 = (x+2)%5 + 5*y;
                st[idx] = B[idx] ^ ((~B[idx1]) & B[idx2]);
            }
        }

        /* Iota */
        st[0] ^= RC[round];
    }
}

void gc_sha3_256_init(gc_sha3_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->rate = 136;       /* 1600/8 - 256*2/8 */
    ctx->digest_bits = 256;
}

void gc_sha3_512_init(gc_sha3_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->rate = 72;        /* 1600/8 - 512*2/8 */
    ctx->digest_bits = 512;
}

void gc_sha3_update(gc_sha3_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) return;

    size_t i = 0;
    /* Process any buffered data first */
    if (ctx->buf_used > 0) {
        size_t take = ctx->rate - ctx->buf_used;
        if (take > len) take = len;
        memcpy(ctx->buf + ctx->buf_used, data, take);
        ctx->buf_used += take;
        data += take;
        len -= take;
        i = take;

        if (ctx->buf_used == ctx->rate) {
            for (size_t j = 0; j < ctx->rate / 8; j++) {
                ctx->state[j] ^=
                    ((uint64_t)ctx->buf[j*8+0]      ) | ((uint64_t)ctx->buf[j*8+1] <<  8) |
                    ((uint64_t)ctx->buf[j*8+2] << 16) | ((uint64_t)ctx->buf[j*8+3] << 24) |
                    ((uint64_t)ctx->buf[j*8+4] << 32) | ((uint64_t)ctx->buf[j*8+5] << 40) |
                    ((uint64_t)ctx->buf[j*8+6] << 48) | ((uint64_t)ctx->buf[j*8+7] << 56);
            }
            keccak_f1600(ctx->state);
            ctx->buf_used = 0;
        }
    }

    /* Process full blocks directly from input */
    for (; i + ctx->rate <= len; i += ctx->rate) {
        for (size_t j = 0; j < ctx->rate / 8; j++) {
            ctx->state[j] ^=
                ((uint64_t)data[i+j*8+0]      ) | ((uint64_t)data[i+j*8+1] <<  8) |
                ((uint64_t)data[i+j*8+2] << 16) | ((uint64_t)data[i+j*8+3] << 24) |
                ((uint64_t)data[i+j*8+4] << 32) | ((uint64_t)data[i+j*8+5] << 40) |
                ((uint64_t)data[i+j*8+6] << 48) | ((uint64_t)data[i+j*8+7] << 56);
        }
        keccak_f1600(ctx->state);
    }

    /* Buffer remaining data */
    if (i < len) {
        size_t tail = len - i;
        memcpy(ctx->buf + ctx->buf_used, data + i, tail);
        ctx->buf_used += tail;
    }
}

void gc_sha3_final(gc_sha3_ctx_t *ctx, uint8_t *digest) {
    if (!ctx || !digest) return;

    uint8_t buf[200];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, ctx->buf, ctx->buf_used);
    buf[ctx->buf_used] = 0x06; /* SHA3 delimiter */
    buf[ctx->rate - 1] |= 0x80;

    for (size_t j = 0; j < ctx->rate / 8; j++) {
        ctx->state[j] ^=
            ((uint64_t)buf[j*8+0]      ) | ((uint64_t)buf[j*8+1] <<  8) |
            ((uint64_t)buf[j*8+2] << 16) | ((uint64_t)buf[j*8+3] << 24) |
            ((uint64_t)buf[j*8+4] << 32) | ((uint64_t)buf[j*8+5] << 40) |
            ((uint64_t)buf[j*8+6] << 48) | ((uint64_t)buf[j*8+7] << 56);
    }
    keccak_f1600(ctx->state);

    size_t out_bytes = ctx->digest_bits / 8;
    for (size_t i = 0; i < out_bytes; i++) {
        digest[i] = (uint8_t)(ctx->state[i/8] >> (8 * (i % 8)));
    }
}

void gc_sha3_256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    gc_sha3_ctx_t ctx;
    gc_sha3_256_init(&ctx);
    gc_sha3_update(&ctx, data, len);
    gc_sha3_final(&ctx, digest);
}

void gc_sha3_512(const uint8_t *data, size_t len, uint8_t digest[64]) {
    gc_sha3_ctx_t ctx;
    gc_sha3_512_init(&ctx);
    gc_sha3_update(&ctx, data, len);
    gc_sha3_final(&ctx, digest);
}

/* HMAC-SHA3-256: key ipad/opad with SHA3-256 */
void gc_hmac_sha3_256(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len,
                      uint8_t mac[32]) {
    uint8_t k_ipad[136] = {0};
    uint8_t k_opad[136] = {0};
    uint8_t tk[32];

    /* If key longer than block, hash it */
    if (key_len > 136) {
        gc_sha3_256(key, key_len, tk);
        key = tk;
        key_len = 32;
    }

    for (size_t i = 0; i < key_len; i++) {
        k_ipad[i] = key[i] ^ 0x36;
        k_opad[i] = key[i] ^ 0x5c;
    }
    for (size_t i = key_len; i < 136; i++) {
        k_ipad[i] = 0x36;
        k_opad[i] = 0x5c;
    }

    gc_sha3_ctx_t ctx;

    /* inner = SHA3-256(k_ipad || msg) */
    gc_sha3_256_init(&ctx);
    gc_sha3_update(&ctx, k_ipad, 136);
    gc_sha3_update(&ctx, msg, msg_len);
    gc_sha3_final(&ctx, mac);

    /* outer = SHA3-256(k_opad || inner) */
    gc_sha3_256_init(&ctx);
    gc_sha3_update(&ctx, k_opad, 136);
    gc_sha3_update(&ctx, mac, 32);
    gc_sha3_final(&ctx, mac);
}

void gc_memzero(void *p, size_t n) {
    volatile uint8_t *vp = p;
    for (size_t i = 0; i < n; i++) vp[i] = 0;
}

int gc_memeq(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
    return (1 & (((unsigned)d - 1) >> 8));
}

/* ==========================================================================
 * Base64 Encode (RFC 4648)
 * ========================================================================== */

static const char b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t gc_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return 0;
    size_t need = ((in_len + 2) / 3) * 4 + 1;
    if (out_cap < need) return 0;
    size_t i = 0, j = 0;
    while (i + 3 <= in_len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        out[j++] = b64_enc[(v >> 18) & 0x3f];
        out[j++] = b64_enc[(v >> 12) & 0x3f];
        out[j++] = b64_enc[(v >> 6) & 0x3f];
        out[j++] = b64_enc[v & 0x3f];
        i += 3;
    }
    if (i < in_len) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) v |= (uint32_t)in[i+1] << 8;
        out[j++] = b64_enc[(v >> 18) & 0x3f];
        out[j++] = b64_enc[(v >> 12) & 0x3f];
        out[j++] = (i + 1 < in_len) ? b64_enc[(v >> 6) & 0x3f] : '=';
        out[j++] = '=';
    }
    out[j] = '\0';
    return j;
}

/* Base64 decode (RFC 4648) */
size_t gc_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return 0;
    size_t need = (in_len / 4) * 3;
    if (out_cap < need) return 0;

    static const uint8_t b64_dec[256] = {
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
        52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,
        64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
        64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64
    };

    size_t i = 0, j = 0;
    while (i + 4 <= in_len) {
        uint8_t a = b64_dec[(uint8_t)in[i]];
        uint8_t b = b64_dec[(uint8_t)in[i+1]];
        uint8_t c = b64_dec[(uint8_t)in[i+2]];
        uint8_t d = b64_dec[(uint8_t)in[i+3]];
        if (a > 63 || b > 63) return 0;
        out[j++] = (uint8_t)((a << 2) | (b >> 4));
        if (c > 63) break;
        out[j++] = (uint8_t)((b << 4) | (c >> 2));
        if (d > 63) break;
        out[j++] = (uint8_t)((c << 6) | d);
        i += 4;
    }
    return j;
}

/* ==========================================================================
 * AES-256-GCM Implementation
 * Pure C23, zero dependencies, first-principles
 * ========================================================================== */

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static uint32_t aes_subword(uint32_t w) {
    return ((uint32_t)aes_sbox[w & 0xff]) |
           (((uint32_t)aes_sbox[(w >> 8) & 0xff]) << 8) |
           (((uint32_t)aes_sbox[(w >> 16) & 0xff]) << 16) |
           (((uint32_t)aes_sbox[(w >> 24) & 0xff]) << 24);
}

static uint32_t aes_rotword(uint32_t w) {
    return (w << 8) | (w >> 24);
}

static const uint32_t aes_rcon[11] = {
    0x00000000, 0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x1b000000, 0x36000000
};

static void aes256_keyexp(const uint8_t *key, uint32_t *rk) {
    for (int i = 0; i < 8; i++) {
        rk[i] = ((uint32_t)key[4*i]) | (((uint32_t)key[4*i+1]) << 8) |
                (((uint32_t)key[4*i+2]) << 16) | (((uint32_t)key[4*i+3]) << 24);
    }
    for (int i = 8; i < 60; i++) {
        uint32_t temp = rk[i-1];
        if (i % 8 == 0) {
            temp = aes_subword(aes_rotword(temp)) ^ aes_rcon[i/8];
        } else if (i % 8 == 4) {
            temp = aes_subword(temp);
        }
        rk[i] = rk[i-8] ^ temp;
    }
}

static void aes_addroundkey(uint8_t state[16], const uint32_t *rk) {
    for (int i = 0; i < 4; i++) {
        uint32_t w = rk[i];
        state[i*4+0] ^= (uint8_t)(w);
        state[i*4+1] ^= (uint8_t)(w >> 8);
        state[i*4+2] ^= (uint8_t)(w >> 16);
        state[i*4+3] ^= (uint8_t)(w >> 24);
    }
}

static void aes_subbytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++) state[i] = aes_sbox[state[i]];
}

static void aes_shiftrows(uint8_t state[16]) {
    uint8_t t[16];
    memcpy(t, state, 16);
    state[0]  = t[0];  state[1]  = t[5];  state[2]  = t[10]; state[3]  = t[15];
    state[4]  = t[4];  state[5]  = t[9];  state[6]  = t[14]; state[7]  = t[3];
    state[8]  = t[8];  state[9]  = t[13]; state[10] = t[2];  state[11] = t[7];
    state[12] = t[12]; state[13] = t[1];  state[14] = t[6];  state[15] = t[11];
}

static uint8_t aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}

static void aes_mixcolumns(uint8_t state[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *col = &state[c*4];
        uint8_t a = col[0], b = col[1], c2 = col[2], d = col[3];
        uint8_t e = a ^ b ^ c2 ^ d;
        col[0] = e ^ a ^ aes_xtime(a ^ b);
        col[1] = e ^ b ^ aes_xtime(b ^ c2);
        col[2] = e ^ c2 ^ aes_xtime(c2 ^ d);
        col[3] = e ^ d ^ aes_xtime(d ^ a);
    }
}

static void aes_encrypt_block(const uint32_t *rk, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[16];
    memcpy(state, in, 16);

    aes_addroundkey(state, rk);
    for (int r = 1; r < 14; r++) {
        aes_subbytes(state);
        aes_shiftrows(state);
        aes_mixcolumns(state);
        aes_addroundkey(state, rk + r*4);
    }
    aes_subbytes(state);
    aes_shiftrows(state);
    aes_addroundkey(state, rk + 14*4);

    memcpy(out, state, 16);
}

/* GCM: multiplication in GF(2^128) */
static void gcm_gf_mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]) {
    uint8_t v[16];
    memcpy(v, y, 16);
    uint8_t x_copy[16];
    memcpy(x_copy, x, 16);
    memset(z, 0, 16);

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 8; j++) {
            if ((x_copy[i] >> (7-j)) & 1) {
                for (int k = 0; k < 16; k++) z[k] ^= v[k];
            }
            uint8_t carry = v[15] & 1;
            for (int k = 15; k > 0; k--) v[k] = (v[k] >> 1) | (v[k-1] << 7);
            v[0] >>= 1;
            if (carry) v[0] ^= 0xe1;
        }
    }
}

static void gcm_inc32(uint8_t ctr[16]) {
    uint32_t n = ((uint32_t)ctr[12] << 24) | ((uint32_t)ctr[13] << 16) |
                 ((uint32_t)ctr[14] << 8) | (uint32_t)ctr[15];
    n++;
    ctr[12] = (uint8_t)(n >> 24);
    ctr[13] = (uint8_t)(n >> 16);
    ctr[14] = (uint8_t)(n >> 8);
    ctr[15] = (uint8_t)n;
}

static void gcm_ghash_update(const uint8_t h[16], const uint8_t *data, size_t len, uint8_t y[16]) {
    for (size_t i = 0; i < len; i += 16) {
        size_t chunk = (len - i < 16) ? (len - i) : 16;
        uint8_t block[16] = {0};
        memcpy(block, data + i, chunk);
        for (int j = 0; j < 16; j++) y[j] ^= block[j];
        gcm_gf_mul(y, h, y);
    }
}

static void gcm_compute_tag(const uint32_t *rk, const uint8_t iv[12],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *ct, size_t ct_len,
                            uint8_t tag[16]) {
    uint8_t h[16] = {0};
    aes_encrypt_block(rk, h, h);

    uint8_t y[16] = {0};

    /* GHASH AAD */
    gcm_ghash_update(h, aad, aad_len, y);
    /* GHASH ciphertext */
    gcm_ghash_update(h, ct, ct_len, y);
    /* GHASH length block */
    uint8_t len_block[16] = {0};
    for (int i = 0; i < 8; i++) {
        len_block[i] = (uint8_t)((aad_len * 8) >> (unsigned)(56 - i * 8));
        len_block[8 + i] = (uint8_t)((ct_len * 8) >> (unsigned)(56 - i * 8));
    }
    gcm_ghash_update(h, len_block, 16, y);

    memcpy(tag, y, 16);

    uint8_t j0[16];
    memcpy(j0, iv, 12);
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;

    uint8_t s[16];
    aes_encrypt_block(rk, j0, s);
    for (int i = 0; i < 16; i++) tag[i] ^= s[i];
}

void gc_aes256_gcm_init(gc_aes256_gcm_ctx_t *ctx, const uint8_t key[32], const uint8_t iv[12]) {
    if (!ctx) return;
    aes256_keyexp(key, ctx->rk);
    memcpy(ctx->iv, iv, 12);
}

void gc_aes256_gcm_encrypt(gc_aes256_gcm_ctx_t *ctx,
                           const uint8_t *plaintext, size_t pt_len,
                           const uint8_t *aad, size_t aad_len,
                           uint8_t *ciphertext,
                           uint8_t tag[16]) {
    if (!ctx || !ciphertext || !tag) return;
    if (!plaintext && pt_len > 0) return;
    if (!aad && aad_len > 0) return;

    uint8_t ctr[16];
    memcpy(ctr, ctx->iv, 12);
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 2;

    for (size_t i = 0; i < pt_len; i += 16) {
        uint8_t ks[16];
        aes_encrypt_block(ctx->rk, ctr, ks);
        size_t chunk = (pt_len - i < 16) ? (pt_len - i) : 16;
        for (size_t j = 0; j < chunk; j++) ciphertext[i+j] = plaintext[i+j] ^ ks[j];
        gcm_inc32(ctr);
    }

    gcm_compute_tag(ctx->rk, ctx->iv, aad, aad_len, ciphertext, pt_len, tag);
}

int gc_aes256_gcm_decrypt(gc_aes256_gcm_ctx_t *ctx,
                          const uint8_t *ciphertext, size_t ct_len,
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t tag[16],
                          uint8_t *plaintext) {
    if (!ctx || !ciphertext || !tag || !plaintext) return -1;

    uint8_t computed[16];
    gcm_compute_tag(ctx->rk, ctx->iv, aad, aad_len, ciphertext, ct_len, computed);

    if (!gc_memeq(tag, computed, 16)) return -1;

    uint8_t ctr[16];
    memcpy(ctr, ctx->iv, 12);
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 2;

    for (size_t i = 0; i < ct_len; i += 16) {
        uint8_t ks[16];
        aes_encrypt_block(ctx->rk, ctr, ks);
        size_t chunk = (ct_len - i < 16) ? (ct_len - i) : 16;
        for (size_t j = 0; j < chunk; j++) plaintext[i+j] = ciphertext[i+j] ^ ks[j];
        gcm_inc32(ctr);
    }
    return 0;
}

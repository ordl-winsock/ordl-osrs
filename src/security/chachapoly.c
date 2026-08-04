/*
 * ORDL GovCon - ChaCha20-Poly1305 Implementation
 * RFC 8439. Adapted from ordl-infercli reference.
 */

#include "security/chachapoly.h"
#include <string.h>
#include <stdlib.h>

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void chacha_quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = ROTL32(*d, 16);
    *c += *d; *b ^= *c; *b = ROTL32(*b, 12);
    *a += *b; *d ^= *a; *d = ROTL32(*d, 8);
    *c += *d; *b ^= *c; *b = ROTL32(*b, 7);
}

static void chacha_block(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t counter, uint8_t out[64]) {
    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) {
        state[4 + i] = key[i*4] | ((uint32_t)key[i*4+1] << 8) |
                       ((uint32_t)key[i*4+2] << 16) | ((uint32_t)key[i*4+3] << 24);
    }
    state[12] = counter;
    state[13] = nonce[0] | ((uint32_t)nonce[1] << 8) | ((uint32_t)nonce[2] << 16) | ((uint32_t)nonce[3] << 24);
    state[14] = nonce[4] | ((uint32_t)nonce[5] << 8) | ((uint32_t)nonce[6] << 16) | ((uint32_t)nonce[7] << 24);
    state[15] = nonce[8] | ((uint32_t)nonce[9] << 8) | ((uint32_t)nonce[10] << 16) | ((uint32_t)nonce[11] << 24);

    uint32_t working[16];
    memcpy(working, state, sizeof(working));
    for (int round = 0; round < 10; round++) {
        chacha_quarter_round(&working[0], &working[4], &working[8], &working[12]);
        chacha_quarter_round(&working[1], &working[5], &working[9], &working[13]);
        chacha_quarter_round(&working[2], &working[6], &working[10], &working[14]);
        chacha_quarter_round(&working[3], &working[7], &working[11], &working[15]);
        chacha_quarter_round(&working[0], &working[5], &working[10], &working[15]);
        chacha_quarter_round(&working[1], &working[6], &working[11], &working[12]);
        chacha_quarter_round(&working[2], &working[7], &working[8], &working[13]);
        chacha_quarter_round(&working[3], &working[4], &working[9], &working[14]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t v = working[i] + state[i];
        out[i*4]   = (uint8_t)(v);
        out[i*4+1] = (uint8_t)(v >> 8);
        out[i*4+2] = (uint8_t)(v >> 16);
        out[i*4+3] = (uint8_t)(v >> 24);
    }
}

void gc_chacha20_crypt(const uint8_t key[GC_CHACHA_KEY_LEN],
                        const uint8_t nonce[GC_CHACHA_NONCE_LEN],
                        uint32_t counter,
                        uint8_t *data, size_t len) {
    uint8_t block[64];
    size_t offset = 0;
    while (len > 0) {
        chacha_block(key, nonce, counter, block);
        size_t chunk = len < 64 ? len : 64;
        for (size_t i = 0; i < chunk; i++) data[offset + i] ^= block[i];
        offset += chunk; len -= chunk; counter++;
    }
}

/* Poly1305 */
typedef struct { uint32_t limbs[5]; } poly1305_limbs_t;

static void poly1305_add(poly1305_limbs_t *a, const poly1305_limbs_t *b) {
    uint32_t c = 0;
    for (int i = 0; i < 5; i++) {
        uint64_t v = (uint64_t)a->limbs[i] + b->limbs[i] + c;
        c = (uint32_t)(v >> 26);
        a->limbs[i] = (uint32_t)(v & 0x3ffffff);
    }
    uint64_t v = (uint64_t)a->limbs[0] + c * 5;
    a->limbs[0] = (uint32_t)(v & 0x3ffffff);
    c = (uint32_t)(v >> 26);
    for (int i = 1; i < 5 && c; i++) {
        uint64_t vv = (uint64_t)a->limbs[i] + c;
        a->limbs[i] = (uint32_t)(vv & 0x3ffffff);
        c = (uint32_t)(vv >> 26);
    }
}

static void poly1305_mul(poly1305_limbs_t *out, const poly1305_limbs_t *a, const poly1305_limbs_t *b) {
    uint64_t t[10] = {0};
    for (int i = 0; i < 5; i++)
        for (int j = 0; j <= i; j++) t[i] += (uint64_t)a->limbs[j] * b->limbs[i - j];
    for (int i = 5; i < 9; i++)
        for (int j = i - 4; j < 5; j++) t[i] += (uint64_t)a->limbs[j] * b->limbs[i - j];
    t[0] += t[5] * 5; t[1] += t[6] * 5; t[2] += t[7] * 5; t[3] += t[8] * 5;
    uint64_t c = 0;
    for (int i = 0; i < 5; i++) {
        uint64_t v = t[i] + c; c = v >> 26; out->limbs[i] = (uint32_t)(v & 0x3ffffff);
    }
    uint64_t v0 = (uint64_t)out->limbs[0] + c * 5;
    out->limbs[0] = (uint32_t)(v0 & 0x3ffffff);
    c = v0 >> 26;
    for (int i = 1; i < 5 && c; i++) {
        uint64_t vv = (uint64_t)out->limbs[i] + c;
        out->limbs[i] = (uint32_t)(vv & 0x3ffffff);
        c = vv >> 26;
    }
}

static void poly1305_from_bytes(poly1305_limbs_t *out, const uint8_t bytes[16]) {
    out->limbs[0] = bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | (((uint32_t)bytes[3] & 0x03) << 24);
    out->limbs[1] = (bytes[3] >> 2) | ((uint32_t)bytes[4] << 6) | ((uint32_t)bytes[5] << 14) | (((uint32_t)bytes[6] & 0x0f) << 22);
    out->limbs[2] = (bytes[6] >> 4) | ((uint32_t)bytes[7] << 4) | ((uint32_t)bytes[8] << 12) | (((uint32_t)bytes[9] & 0x3f) << 20);
    out->limbs[3] = (bytes[9] >> 6) | ((uint32_t)bytes[10] << 2) | ((uint32_t)bytes[11] << 10) | ((uint32_t)bytes[12] << 18);
    out->limbs[4] = bytes[13] | ((uint32_t)bytes[14] << 8) | ((uint32_t)bytes[15] << 16);
}

typedef struct {
    poly1305_limbs_t r;
    poly1305_limbs_t a;
} poly1305_ctx_t;

static void poly1305_init(poly1305_ctx_t *ctx, const uint8_t key[32]) {
    uint8_t r_bytes[16];
    memcpy(r_bytes, key, 16);
    r_bytes[3] &= 0x0f; r_bytes[7] &= 0x0f; r_bytes[11] &= 0x0f; r_bytes[15] &= 0x0f;
    r_bytes[4] &= 0xfc; r_bytes[8] &= 0xfc; r_bytes[12] &= 0xfc;
    poly1305_from_bytes(&ctx->r, r_bytes);
    memset(&ctx->a, 0, sizeof(ctx->a));
}

static void poly1305_feed_block(poly1305_ctx_t *ctx, const uint8_t block[16]) {
    poly1305_limbs_t n;
    poly1305_from_bytes(&n, block);
    n.limbs[4] += 1U << 24;
    poly1305_add(&ctx->a, &n);
    poly1305_mul(&ctx->a, &ctx->a, &ctx->r);
}

static void poly1305_final(poly1305_ctx_t *ctx, const uint8_t key[32], uint8_t tag[16]) {
    poly1305_limbs_t s;
    poly1305_from_bytes(&s, key + 16);
    poly1305_add(&ctx->a, &s);
    uint32_t c = 0;
    for (int i = 0; i < 5; i++) {
        uint64_t v = (uint64_t)ctx->a.limbs[i] + c;
        c = (uint32_t)(v >> 26);
        ctx->a.limbs[i] = (uint32_t)(v & 0x3ffffff);
    }
    tag[0]  = (uint8_t)(ctx->a.limbs[0]); tag[1]  = (uint8_t)(ctx->a.limbs[0] >> 8);
    tag[2]  = (uint8_t)(ctx->a.limbs[0] >> 16); tag[3]  = (uint8_t)((ctx->a.limbs[0] >> 24) | (ctx->a.limbs[1] << 2));
    tag[4]  = (uint8_t)(ctx->a.limbs[1] >> 6); tag[5]  = (uint8_t)(ctx->a.limbs[1] >> 14);
    tag[6]  = (uint8_t)((ctx->a.limbs[1] >> 22) | (ctx->a.limbs[2] << 4));
    tag[7]  = (uint8_t)(ctx->a.limbs[2] >> 4); tag[8]  = (uint8_t)(ctx->a.limbs[2] >> 12);
    tag[9]  = (uint8_t)((ctx->a.limbs[2] >> 20) | (ctx->a.limbs[3] << 6));
    tag[10] = (uint8_t)(ctx->a.limbs[3] >> 2); tag[11] = (uint8_t)(ctx->a.limbs[3] >> 10);
    tag[12] = (uint8_t)(ctx->a.limbs[3] >> 18); tag[13] = (uint8_t)(ctx->a.limbs[4]);
    tag[14] = (uint8_t)(ctx->a.limbs[4] >> 8); tag[15] = (uint8_t)(ctx->a.limbs[4] >> 16);
}

void gc_chachapoly_seal(const uint8_t key[GC_CHACHA_KEY_LEN],
                         const uint8_t nonce[GC_CHACHA_NONCE_LEN],
                         const uint8_t *ad, size_t ad_len,
                         const uint8_t *pt, size_t pt_len,
                         uint8_t *ct, uint8_t tag[16]) {
    memcpy(ct, pt, pt_len);
    gc_chacha20_crypt(key, nonce, 1, ct, pt_len);
    uint8_t poly_key[32];
    memset(poly_key, 0, 32);
    gc_chacha20_crypt(key, nonce, 0, poly_key, 32);

    poly1305_ctx_t pctx;
    poly1305_init(&pctx, poly_key);

    uint8_t block[16] = {0};

    /* Feed AAD in 16-byte chunks with zero-padding */
    for (size_t i = 0; i < ad_len; i += 16) {
        size_t chunk = ad_len - i < 16 ? ad_len - i : 16;
        memcpy(block, ad + i, chunk);
        if (chunk < 16) memset(block + chunk, 0, 16 - chunk);
        poly1305_feed_block(&pctx, block);
    }

    /* Feed ciphertext in 16-byte chunks with zero-padding */
    for (size_t i = 0; i < pt_len; i += 16) {
        size_t chunk = pt_len - i < 16 ? pt_len - i : 16;
        memcpy(block, ct + i, chunk);
        if (chunk < 16) memset(block + chunk, 0, 16 - chunk);
        poly1305_feed_block(&pctx, block);
    }

    /* Feed lengths block */
    memset(block, 0, 16);
    for (int i = 0; i < 8; i++) {
        block[i] = (uint8_t)(ad_len >> (i * 8));
        block[8 + i] = (uint8_t)(pt_len >> (i * 8));
    }
    poly1305_feed_block(&pctx, block);

    poly1305_final(&pctx, poly_key, tag);
}

int gc_chachapoly_open(const uint8_t key[GC_CHACHA_KEY_LEN],
                        const uint8_t nonce[GC_CHACHA_NONCE_LEN],
                        const uint8_t *ad, size_t ad_len,
                        const uint8_t *ct, size_t ct_len,
                        const uint8_t tag[16],
                        uint8_t *pt) {
    uint8_t poly_key[32];
    memset(poly_key, 0, 32);
    gc_chacha20_crypt(key, nonce, 0, poly_key, 32);

    poly1305_ctx_t pctx;
    poly1305_init(&pctx, poly_key);

    uint8_t block[16] = {0};

    /* Feed AAD in 16-byte chunks with zero-padding */
    for (size_t i = 0; i < ad_len; i += 16) {
        size_t chunk = ad_len - i < 16 ? ad_len - i : 16;
        memcpy(block, ad + i, chunk);
        if (chunk < 16) memset(block + chunk, 0, 16 - chunk);
        poly1305_feed_block(&pctx, block);
    }

    /* Feed ciphertext in 16-byte chunks with zero-padding */
    for (size_t i = 0; i < ct_len; i += 16) {
        size_t chunk = ct_len - i < 16 ? ct_len - i : 16;
        memcpy(block, ct + i, chunk);
        if (chunk < 16) memset(block + chunk, 0, 16 - chunk);
        poly1305_feed_block(&pctx, block);
    }

    /* Feed lengths block */
    memset(block, 0, 16);
    for (int i = 0; i < 8; i++) {
        block[i] = (uint8_t)(ad_len >> (i * 8));
        block[8 + i] = (uint8_t)(ct_len >> (i * 8));
    }
    poly1305_feed_block(&pctx, block);

    uint8_t computed[16];
    poly1305_final(&pctx, poly_key, computed);
    if (memcmp(computed, tag, 16) != 0) return -1;
    memcpy(pt, ct, ct_len);
    gc_chacha20_crypt(key, nonce, 1, pt, ct_len);
    return 0;
}

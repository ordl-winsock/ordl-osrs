/*
 * osrs/xtea.c — XTEA cipher implementation
 * Pure C23, zero external dependencies.
 *
 * Reference: "TEA, a Tiny Encryption Algorithm" by David Wheeler & Roger Needham
 * XTEA variant with 32 rounds, golden ratio delta.
 */

#include "osrs/xtea.h"
#include <string.h>

#define XTEA_DELTA 0x9E3779B9u

/* Load 32-bit big-endian value */
static inline uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | ((uint32_t)p[3]);
}

/* Store 32-bit big-endian value */
static inline void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

void osrs_xtea_init(osrs_xtea_t *ctx, const uint8_t key[OSRS_XTEA_KEY_SIZE])
{
    for (int i = 0; i < 4; i++) {
        ctx->key[i] = load_be32(key + i * 4);
    }
}

void osrs_xtea_init_u32(osrs_xtea_t *ctx, const uint32_t key[4])
{
    memcpy(ctx->key, key, sizeof(ctx->key));
}

void osrs_xtea_encrypt_block(const osrs_xtea_t *ctx, uint8_t block[OSRS_XTEA_BLOCK_SIZE])
{
    uint32_t v0 = load_be32(block);
    uint32_t v1 = load_be32(block + 4);
    uint32_t sum = 0;

    for (int i = 0; i < OSRS_XTEA_ROUNDS; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
        sum += XTEA_DELTA;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
    }

    store_be32(block, v0);
    store_be32(block + 4, v1);
}

void osrs_xtea_decrypt_block(const osrs_xtea_t *ctx, uint8_t block[OSRS_XTEA_BLOCK_SIZE])
{
    uint32_t v0 = load_be32(block);
    uint32_t v1 = load_be32(block + 4);
    uint32_t sum = XTEA_DELTA * OSRS_XTEA_ROUNDS;

    for (int i = 0; i < OSRS_XTEA_ROUNDS; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
        sum -= XTEA_DELTA;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
    }

    store_be32(block, v0);
    store_be32(block + 4, v1);
}

void osrs_xtea_encrypt(const osrs_xtea_t *ctx, uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i += OSRS_XTEA_BLOCK_SIZE) {
        osrs_xtea_encrypt_block(ctx, data + i);
    }
}

void osrs_xtea_decrypt(const osrs_xtea_t *ctx, uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i += OSRS_XTEA_BLOCK_SIZE) {
        osrs_xtea_decrypt_block(ctx, data + i);
    }
}

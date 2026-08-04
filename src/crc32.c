/*
 * osrs/crc32.c — CRC32 implementation
 * Pure C23, zero external dependencies.
 *
 * CRC-32 (IEEE 802.3) polynomial: 0xEDB88320
 * Same as used by gzip, PNG, Ethernet, and OSRS cache.
 */

#include "osrs/crc32.h"

/* CRC32 lookup table — generated at compile time via static initializer */
static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

/* Generate CRC32 lookup table */
static void crc32_init_table(void)
{
    if (crc32_table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

void osrs_crc32_init(osrs_crc32_t *ctx)
{
    crc32_init_table();
    ctx->state = 0xFFFFFFFFu;
}

void osrs_crc32_update(osrs_crc32_t *ctx, const uint8_t *data, size_t len)
{
    uint32_t crc = ctx->state;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    ctx->state = crc;
}

uint32_t osrs_crc32_final(osrs_crc32_t *ctx)
{
    return ctx->state ^ 0xFFFFFFFFu;
}

uint32_t osrs_crc32(const uint8_t *data, size_t len)
{
    osrs_crc32_t ctx;
    osrs_crc32_init(&ctx);
    osrs_crc32_update(&ctx, data, len);
    return osrs_crc32_final(&ctx);
}

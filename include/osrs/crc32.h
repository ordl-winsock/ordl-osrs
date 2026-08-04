/*
 * osrs/crc32.h — CRC32 checksum (IEEE 802.3 polynomial)
 * Pure C23, zero external dependencies.
 *
 * Used for OSRS cache file integrity verification.
 */

#ifndef OSRS_CRC32_H
#define OSRS_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC32 context */
typedef struct {
    uint32_t state;
} osrs_crc32_t;

/* Initialize CRC32 context */
void osrs_crc32_init(osrs_crc32_t *ctx);

/* Update CRC32 with data */
void osrs_crc32_update(osrs_crc32_t *ctx, const uint8_t *data, size_t len);

/* Finalize and get CRC32 value */
uint32_t osrs_crc32_final(osrs_crc32_t *ctx);

/* One-shot CRC32 computation */
uint32_t osrs_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_CRC32_H */

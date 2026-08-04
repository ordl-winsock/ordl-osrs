/*
 * osrs/bzip2.h — BZip2 decompressor
 * Pure C23, zero external dependencies.
 *
 * Implements BZip2 decompression (Burrows-Wheeler transform,
 * move-to-front coding, Huffman coding, RLE).
 * Used for OSRS cache file decompression.
 */

#ifndef OSRS_BZIP2_H
#define OSRS_BZIP2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BZip2 decompressor context — opaque */
typedef struct osrs_bzip2_ctx osrs_bzip2_t;

/* Create BZip2 decompressor */
osrs_bzip2_t *osrs_bzip2_create(void);

/* Destroy BZip2 decompressor */
void osrs_bzip2_destroy(osrs_bzip2_t *ctx);

/* Decompress BZip2 data
 * compressed: input compressed data
 * compressed_len: length of compressed data
 * output: buffer for decompressed data
 * output_len: on input, size of output buffer; on output, actual decompressed size
 * Returns: true on success, false on error
 */
bool osrs_bzip2_decompress(osrs_bzip2_t *ctx,
                           const uint8_t *compressed, size_t compressed_len,
                           uint8_t *output, size_t *output_len);

/* One-shot decompression (allocates context internally)
 * Returns: true on success, false on error
 */
bool osrs_bzip2_decompress_oneshot(const uint8_t *compressed, size_t compressed_len,
                                   uint8_t *output, size_t *output_len);

/* Get decompressed size from BZip2 header (reads block header)
 * Returns: decompressed size, or 0 on error
 */
size_t osrs_bzip2_decompressed_size(const uint8_t *compressed, size_t compressed_len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_BZIP2_H */

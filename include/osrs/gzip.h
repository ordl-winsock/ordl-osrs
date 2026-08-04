/*
 * osrs/gzip.h — GZip decompressor
 * Pure C23, zero external dependencies.
 *
 * Implements GZip/DEFLATE decompression (LZ77 + Huffman).
 * Used for OSRS cache file decompression.
 */

#ifndef OSRS_GZIP_H
#define OSRS_GZIP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GZip decompressor context — opaque */
typedef struct osrs_gzip_ctx osrs_gzip_t;

/* Create GZip decompressor */
osrs_gzip_t *osrs_gzip_create(void);

/* Destroy GZip decompressor */
void osrs_gzip_destroy(osrs_gzip_t *ctx);

/* Decompress GZip data
 * compressed: input compressed data
 * compressed_len: length of compressed data
 * output: buffer for decompressed data
 * output_len: on input, size of output buffer; on output, actual decompressed size
 * Returns: true on success, false on error
 */
bool osrs_gzip_decompress(osrs_gzip_t *ctx,
                          const uint8_t *compressed, size_t compressed_len,
                          uint8_t *output, size_t *output_len);

/* One-shot decompression */
bool osrs_gzip_decompress_oneshot(const uint8_t *compressed, size_t compressed_len,
                                  uint8_t *output, size_t *output_len);

/* Get decompressed size from GZip footer (ISIZE field)
 * Returns: decompressed size, or 0 on error
 */
size_t osrs_gzip_decompressed_size(const uint8_t *compressed, size_t compressed_len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_GZIP_H */

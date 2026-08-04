/*
 * osrs/cache.h — OSRS cache file system reader
 * Pure C23, zero external dependencies.
 *
 * Reads OSRS flatfile cache format (main_file_cache.dat2 +
 * main_file_cache.idx*). Supports index loading, archive reading, and container
 * decompression.
 */

#ifndef OSRS_CACHE_H
#define OSRS_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compression types */
#define OSRS_COMPRESSION_NONE 0
#define OSRS_COMPRESSION_BZ2 1
#define OSRS_COMPRESSION_GZ 2

/* Index IDs (255 = master index) */
#define OSRS_INDEX_MASTER 255

/* Maximum sizes */
#define OSRS_MAX_INDEXES 256
#define OSRS_MAX_ARCHIVES 65536
#define OSRS_SECTOR_SIZE 520
#define OSRS_SECTOR_HEADER 8
#define OSRS_SECTOR_DATA (OSRS_SECTOR_SIZE - OSRS_SECTOR_HEADER)
#define OSRS_INDEX_ENTRY_SIZE 6

/* Archive metadata */
typedef struct {
  int id;
  int name_hash;
  int crc;
  int compressed_size;
  int decompressed_size;
  int revision;
  int *file_ids;
  int file_count;
} osrs_archive_t;

/* Index metadata */
typedef struct {
  int id;
  int protocol;
  bool named;
  bool sized;
  int revision;
  int crc;
  int compression;
  osrs_archive_t *archives;
  int archive_count;
} osrs_index_t;

/* Cache store context */
typedef struct {
  char path[4096];
  FILE *data_file;
  FILE *index_255;
  FILE *index_files[OSRS_MAX_INDEXES];
  osrs_index_t *indexes;
  int index_count;
} osrs_cache_t;

/* Container decompression result */
typedef struct {
  uint8_t *data;
  size_t data_len;
  int compression;
  int revision;
  uint32_t crc;
} osrs_container_t;

/* Open cache store at path */
osrs_cache_t *osrs_cache_open(const char *path);

/* Close cache store */
void osrs_cache_close(osrs_cache_t *cache);

/* Load all indexes */
bool osrs_cache_load_indexes(osrs_cache_t *cache);

/* Read raw index data (from index 255) */
uint8_t *osrs_cache_read_index(osrs_cache_t *cache, int index_id,
                               size_t *out_len);

/* Read raw archive data */
uint8_t *osrs_cache_read_archive(osrs_cache_t *cache, int index_id,
                                 int archive_id, size_t *out_len);

/* Archive file data */
typedef struct {
  int id;
  uint8_t *data;
  size_t len;
} osrs_archive_file_t;

/* Split archive files result */
typedef struct {
  osrs_archive_file_t *files;
  int count;
} osrs_archive_files_t;

/* Read and decompress an archive, splitting into individual files.
 * If xtea_key is non-NULL, decrypts the container before decompression. */
osrs_archive_files_t *osrs_cache_read_archive_files(osrs_cache_t *cache,
                                                    int index_id,
                                                    int archive_id,
                                                    const uint32_t *xtea_key);

/* Read a single file from within an archive. Convenience wrapper around
 * osrs_cache_read_archive_files; returns a malloc'd copy of the file data
 * (caller frees) or NULL. */
uint8_t *osrs_cache_read_archive_file(osrs_cache_t *cache, int index_id,
                                      int archive_id, int file_id,
                                      size_t *out_len);

/* Free archive files */
void osrs_archive_files_free(osrs_archive_files_t *files);

/* Decompress container */
osrs_container_t *osrs_container_decompress(const uint8_t *data, size_t len,
                                            const uint32_t *xtea_key);

/* Free container */
void osrs_container_free(osrs_container_t *container);

/* Get index by ID */
osrs_index_t *osrs_cache_get_index(osrs_cache_t *cache, int index_id);

/* Find archive by name hash */
osrs_archive_t *osrs_index_find_archive_by_hash(osrs_index_t *index,
                                                int name_hash);

/* Free index data */
void osrs_index_free(osrs_index_t *index);

/* Input stream for reading cache data */
typedef struct {
  const uint8_t *data;
  size_t len;
  size_t pos;
} osrs_stream_t;

/* Stream helpers */
void osrs_stream_init(osrs_stream_t *s, const uint8_t *data, size_t len);
uint8_t osrs_stream_u8(osrs_stream_t *s);
int8_t osrs_stream_i8(osrs_stream_t *s);
uint16_t osrs_stream_u16(osrs_stream_t *s);
int16_t osrs_stream_i16(osrs_stream_t *s);
uint32_t osrs_stream_u32(osrs_stream_t *s);
int32_t osrs_stream_i32(osrs_stream_t *s);
uint64_t osrs_stream_u64(osrs_stream_t *s);
void osrs_stream_bytes(osrs_stream_t *s, uint8_t *out, size_t len);
void osrs_stream_skip(osrs_stream_t *s, size_t len);
size_t osrs_stream_remaining(const osrs_stream_t *s);

uint16_t osrs_stream_g2_alt1(osrs_stream_t *s);
uint16_t osrs_stream_g2_alt2(osrs_stream_t *s);
uint16_t osrs_stream_g2_alt3(osrs_stream_t *s);
int osrs_stream_string(osrs_stream_t *s, char *out, size_t max_len);

/* Read "smart" (variable-length integer) */
int32_t osrs_stream_smart(osrs_stream_t *s);

/* Read unsigned "smart" */
uint32_t osrs_stream_usmart(osrs_stream_t *s);

/* Read unsigned short smart (for location data) */
uint32_t osrs_stream_ushort_smart(osrs_stream_t *s);

/* Read unsigned int smart short compat (for location ID deltas) */
uint32_t osrs_stream_uint_smart_short_compat(osrs_stream_t *s);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_CACHE_H */

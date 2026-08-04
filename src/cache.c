/*
 * osrs/cache.c — OSRS cache file system reader implementation
 * Pure C23, zero external dependencies.
 */

#include "osrs/cache.h"
#include "osrs/bzip2.h"
#include "osrs/crc32.h"
#include "osrs/gzip.h"
#include "osrs/log.h"
#include "osrs/xtea.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stream implementation */
void osrs_stream_init(osrs_stream_t *s, const uint8_t *data, size_t len) {
  s->data = data;
  s->len = len;
  s->pos = 0;
}

uint8_t osrs_stream_u8(osrs_stream_t *s) {
  if (s->pos >= s->len)
    return 0;
  return s->data[s->pos++];
}

int8_t osrs_stream_i8(osrs_stream_t *s) { return (int8_t)osrs_stream_u8(s); }

uint16_t osrs_stream_u16(osrs_stream_t *s) {
  if (s->pos + 2 > s->len)
    return 0;
  uint16_t v = (uint16_t)((s->data[s->pos] << 8) | s->data[s->pos + 1]);
  s->pos += 2;
  return v;
}

int16_t osrs_stream_i16(osrs_stream_t *s) {
  return (int16_t)osrs_stream_u16(s);
}

uint16_t osrs_stream_g2_alt1(osrs_stream_t *s) {
  if (s->pos + 2 > s->len)
    return 0;
  uint16_t v = (uint16_t)(((s->data[s->pos + 1] & 0xFF) << 8) |
                          ((s->data[s->pos] - 128) & 0xFF));
  s->pos += 2;
  return v;
}

uint16_t osrs_stream_g2_alt2(osrs_stream_t *s) {
  if (s->pos + 2 > s->len)
    return 0;
  uint16_t v = (uint16_t)(((s->data[s->pos] & 0xFF) << 8) |
                          ((s->data[s->pos + 1] - 128) & 0xFF));
  s->pos += 2;
  return v;
}

uint16_t osrs_stream_g2_alt3(osrs_stream_t *s) {
  if (s->pos + 2 > s->len)
    return 0;
  uint16_t v = (uint16_t)(((s->data[s->pos + 1] & 0xFF) << 8) |
                          ((s->data[s->pos] - 128) & 0xFF));
  s->pos += 2;
  return v;
}

uint32_t osrs_stream_u32(osrs_stream_t *s) {
  if (s->pos + 4 > s->len)
    return 0;
  uint32_t v = ((uint32_t)s->data[s->pos] << 24) |
               ((uint32_t)s->data[s->pos + 1] << 16) |
               ((uint32_t)s->data[s->pos + 2] << 8) |
               ((uint32_t)s->data[s->pos + 3]);
  s->pos += 4;
  return v;
}

int32_t osrs_stream_i32(osrs_stream_t *s) {
  return (int32_t)osrs_stream_u32(s);
}

uint64_t osrs_stream_u64(osrs_stream_t *s) {
  if (s->pos + 8 > s->len)
    return 0;
  uint64_t v = ((uint64_t)s->data[s->pos] << 56) |
               ((uint64_t)s->data[s->pos + 1] << 48) |
               ((uint64_t)s->data[s->pos + 2] << 40) |
               ((uint64_t)s->data[s->pos + 3] << 32) |
               ((uint64_t)s->data[s->pos + 4] << 24) |
               ((uint64_t)s->data[s->pos + 5] << 16) |
               ((uint64_t)s->data[s->pos + 6] << 8) |
               ((uint64_t)s->data[s->pos + 7]);
  s->pos += 8;
  return v;
}

void osrs_stream_bytes(osrs_stream_t *s, uint8_t *out, size_t len) {
  if (s->pos + len > s->len)
    len = s->len - s->pos;
  memcpy(out, s->data + s->pos, len);
  s->pos += len;
}

void osrs_stream_skip(osrs_stream_t *s, size_t len) {
  s->pos += len;
  if (s->pos > s->len)
    s->pos = s->len;
}

size_t osrs_stream_remaining(const osrs_stream_t *s) { return s->len - s->pos; }

int osrs_stream_string(osrs_stream_t *s, char *out, size_t max_len) {
  int i = 0;
  while (s->pos < s->len && i < (int)max_len - 1) {
    uint8_t c = s->data[s->pos++];
    if (c == 0)
      break;
    out[i++] = (char)c;
  }
  out[i] = '\0';
  return i;
}

int32_t osrs_stream_smart(osrs_stream_t *s) {
  if (s->pos >= s->len)
    return 0;
  uint8_t peek = s->data[s->pos];
  if (peek < 128) {
    return osrs_stream_u8(s) - 64;
  }
  return osrs_stream_u16(s) - 49152;
}

uint32_t osrs_stream_usmart(osrs_stream_t *s) {
  if (s->pos >= s->len)
    return 0;
  int8_t peek = (int8_t)s->data[s->pos];
  if (peek >= 0)
    return osrs_stream_u16(s);
  return (uint32_t)(osrs_stream_i32(s) & 0x7FFFFFFF);
}

/* Unsigned short smart (for location data).
 * Matches RuneLite readUnsignedShortSmart:
 *   peek < 128 → 1 byte
 *   peek >= 128 → 2 bytes - 0x8000 */
uint32_t osrs_stream_ushort_smart(osrs_stream_t *s) {
  if (s->pos >= s->len)
    return 0;
  uint8_t peek = s->data[s->pos];
  if (peek < 128)
    return osrs_stream_u8(s);
  return osrs_stream_u16(s) - 0x8000;
}

/* Unsigned int smart short compat (for location ID deltas).
 * Matches RuneLite readUnsignedIntSmartShortCompat:
 *   accumulate ushort_smart values while each == 32767 */
uint32_t osrs_stream_uint_smart_short_compat(osrs_stream_t *s) {
  uint32_t result = 0;
  uint32_t part;
  while ((part = osrs_stream_ushort_smart(s)) == 32767)
    result += 32767;
  return result + part;
}

/* Data file sector reading */
static bool data_file_read(osrs_cache_t *cache, int index_id, int archive_id,
                           int sector, int length, uint8_t *out) {
  if (!cache->data_file)
    return false;

  size_t offset = (size_t)sector * OSRS_SECTOR_SIZE;
  if (fseek(cache->data_file, (long)offset, SEEK_SET) != 0) {
    return false;
  }

  int remaining = length;
  int current_sector = sector;
  int bytes_read = 0;
  int expected_chunk = 0;

  while (remaining > 0) {
    int header_size = (archive_id > 0xFFFF) ? 10 : 8;
    uint8_t header[10];
    if (fread(header, 1, (size_t)header_size, cache->data_file) !=
        (size_t)header_size) {
      return false;
    }

    int current_archive, current_chunk, next_sector, current_index;
    if (archive_id > 0xFFFF) {
      current_archive = ((int)header[0] << 24) | ((int)header[1] << 16) |
                        ((int)header[2] << 8) | (int)header[3];
      current_chunk = ((int)header[4] << 8) | (int)header[5];
      next_sector =
          ((int)header[6] << 16) | ((int)header[7] << 8) | (int)header[8];
      current_index = header[9];
    } else {
      current_archive = ((int)header[0] << 8) | (int)header[1];
      current_chunk = ((int)header[2] << 8) | (int)header[3];
      next_sector =
          ((int)header[4] << 16) | ((int)header[5] << 8) | (int)header[6];
      current_index = header[7];
    }

    if (current_archive != archive_id || current_index != index_id ||
        current_chunk != expected_chunk) {
      return false;
    }

    int data_size = OSRS_SECTOR_SIZE - header_size;
    int chunk = remaining > data_size ? data_size : remaining;
    if (fread(out + bytes_read, 1, (size_t)chunk, cache->data_file) !=
        (size_t)chunk) {
      return false;
    }

    bytes_read += chunk;
    remaining -= chunk;
    expected_chunk++;

    if (remaining > 0) {
      current_sector = next_sector;
      offset = (size_t)current_sector * OSRS_SECTOR_SIZE;
      if (fseek(cache->data_file, (long)offset, SEEK_SET) != 0)
        return false;
    }
  }

  return true;
}

/* Index file entry reading */
static bool index_file_read(FILE *index_file, int archive_id, int *out_sector,
                            int *out_length) {
  if (!index_file)
    return false;

  size_t offset = (size_t)archive_id * OSRS_INDEX_ENTRY_SIZE;
  if (fseek(index_file, (long)offset, SEEK_SET) != 0)
    return false;

  uint8_t entry[OSRS_INDEX_ENTRY_SIZE];
  if (fread(entry, 1, OSRS_INDEX_ENTRY_SIZE, index_file) !=
      OSRS_INDEX_ENTRY_SIZE) {
    return false;
  }

  *out_length = (entry[0] << 16) | (entry[1] << 8) | entry[2];
  *out_sector = (entry[3] << 16) | (entry[4] << 8) | entry[5];

  return *out_sector > 0 && *out_length > 0;
}

/* Get number of entries in index file */
static int index_file_count(FILE *index_file) {
  if (!index_file)
    return 0;
  fseek(index_file, 0, SEEK_END);
  long size = ftell(index_file);
  return (int)(size / OSRS_INDEX_ENTRY_SIZE);
}

osrs_cache_t *osrs_cache_open(const char *path) {
  OSRS_INFO(OSRS_LOG_CAT_CACHE, "Opening cache at %s", path);

  osrs_cache_t *cache = calloc(1, sizeof(osrs_cache_t));
  if (!cache)
    return NULL;

  strncpy(cache->path, path, sizeof(cache->path) - 1);

  char filepath[4096];

  /* Open data file */
  snprintf(filepath, sizeof(filepath), "%s/main_file_cache.dat2", path);
  cache->data_file = fopen(filepath, "rb");
  if (!cache->data_file) {
    free(cache);
    return NULL;
  }

  /* Open master index (255) */
  snprintf(filepath, sizeof(filepath), "%s/main_file_cache.idx255", path);
  cache->index_255 = fopen(filepath, "rb");
  if (!cache->index_255) {
    fclose(cache->data_file);
    free(cache);
    return NULL;
  }

  /* Open other index files */
  for (int i = 0; i < OSRS_MAX_INDEXES; i++) {
    snprintf(filepath, sizeof(filepath), "%s/main_file_cache.idx%d", path, i);
    cache->index_files[i] = fopen(filepath, "rb");
    /* NULL is fine — index may not exist */
  }

  return cache;
}

void osrs_cache_close(osrs_cache_t *cache) {
  if (!cache)
    return;

  if (cache->data_file)
    fclose(cache->data_file);
  if (cache->index_255)
    fclose(cache->index_255);

  for (int i = 0; i < OSRS_MAX_INDEXES; i++) {
    if (cache->index_files[i]) {
      fclose(cache->index_files[i]);
    }
  }

  if (cache->indexes) {
    for (int i = 0; i < cache->index_count; i++) {
      osrs_index_free(&cache->indexes[i]);
    }
    free(cache->indexes);
  }

  free(cache);
}

uint8_t *osrs_cache_read_index(osrs_cache_t *cache, int index_id,
                               size_t *out_len) {
  if (!cache || !cache->index_255)
    return NULL;

  int sector, length;
  if (!index_file_read(cache->index_255, index_id, &sector, &length)) {
    return NULL;
  }

  uint8_t *data = malloc((size_t)length);
  if (!data)
    return NULL;

  if (!data_file_read(cache, 255, index_id, sector, length, data)) {
    free(data);
    return NULL;
  }

  *out_len = (size_t)length;
  return data;
}

/* Read archive data */
uint8_t *osrs_cache_read_archive(osrs_cache_t *cache, int index_id,
                                 int archive_id, size_t *out_len) {
  OSRS_TRACE(OSRS_LOG_CAT_CACHE, "Reading archive index=%d archive=%d",
             index_id, archive_id);

  if (!cache || index_id < 0 || index_id >= OSRS_MAX_INDEXES)
    return NULL;
  if (!cache->index_files[index_id])
    return NULL;

  int sector, length;
  if (!index_file_read(cache->index_files[index_id], archive_id, &sector,
                       &length)) {
    return NULL;
  }

  uint8_t *data = malloc((size_t)length);
  if (!data)
    return NULL;

  if (!data_file_read(cache, index_id, archive_id, sector, length, data)) {
    free(data);
    return NULL;
  }

  *out_len = (size_t)length;
  return data;
}

/* Read and decompress an archive, splitting into individual files */
osrs_archive_files_t *osrs_cache_read_archive_files(osrs_cache_t *cache,
                                                    int index_id,
                                                    int archive_id,
                                                    const uint32_t *xtea_key) {
  if (!cache || index_id < 0 || index_id >= OSRS_MAX_INDEXES)
    return NULL;

  osrs_index_t *index = osrs_cache_get_index(cache, index_id);
  if (!index)
    return NULL;

  /* Find archive metadata */
  osrs_archive_t *archive = NULL;
  for (int i = 0; i < index->archive_count; i++) {
    if (index->archives[i].id == archive_id) {
      archive = &index->archives[i];
      break;
    }
  }
  if (!archive || archive->file_count <= 0)
    return NULL;

  OSRS_DEBUG(OSRS_LOG_CAT_CACHE, "Loading archive files count=%d",
             archive->file_count);

  /* Read raw archive data */
  size_t raw_len;
  uint8_t *raw_data =
      osrs_cache_read_archive(cache, index_id, archive_id, &raw_len);
  if (!raw_data)
    return NULL;

  /* Decompress container */
  osrs_container_t *container =
      osrs_container_decompress(raw_data, raw_len, xtea_key);
  free(raw_data);
  if (!container)
    return NULL;

  osrs_archive_files_t *result = calloc(1, sizeof(osrs_archive_files_t));
  if (!result) {
    osrs_container_free(container);
    return NULL;
  }

  result->files =
      calloc((size_t)archive->file_count, sizeof(osrs_archive_file_t));
  if (!result->files) {
    free(result);
    osrs_container_free(container);
    return NULL;
  }
  result->count = archive->file_count;

  /* Initialize file IDs */
  for (int i = 0; i < archive->file_count; i++) {
    result->files[i].id = archive->file_ids[i];
  }

  /* Single file: entire data is the file */
  if (archive->file_count == 1) {
    result->files[0].data = malloc(container->data_len);
    if (result->files[0].data) {
      memcpy(result->files[0].data, container->data, container->data_len);
      result->files[0].len = container->data_len;
    }
    osrs_container_free(container);
    return result;
  }

  /* Multi-file: read chunk info from end of data */
  osrs_stream_t s;
  osrs_stream_init(&s, container->data, container->data_len);

  /* Read chunk count from last byte */
  s.pos = s.len - 1;
  int chunks = osrs_stream_u8(&s);

  /* Read chunk size deltas */
  s.pos = s.len - 1 - chunks * archive->file_count * 4;

  int **chunk_sizes = calloc((size_t)archive->file_count, sizeof(int *));
  size_t *file_sizes = calloc((size_t)archive->file_count, sizeof(size_t));
  if (!chunk_sizes || !file_sizes) {
    free(chunk_sizes);
    free(file_sizes);
    for (int i = 0; i < result->count; i++)
      free(result->files[i].data);
    free(result->files);
    free(result);
    osrs_container_free(container);
    return NULL;
  }

  for (int i = 0; i < archive->file_count; i++) {
    chunk_sizes[i] = calloc((size_t)chunks, sizeof(int));
  }

  for (int c = 0; c < chunks; c++) {
    int chunk_size = 0;
    for (int i = 0; i < archive->file_count; i++) {
      int delta = osrs_stream_i32(&s);
      chunk_size += delta;
      chunk_sizes[i][c] = chunk_size;
      file_sizes[i] += (size_t)chunk_size;
    }
  }

  /* Allocate file buffers */
  for (int i = 0; i < archive->file_count; i++) {
    if (file_sizes[i] > 0) {
      result->files[i].data = malloc(file_sizes[i]);
      if (result->files[i].data)
        result->files[i].len = file_sizes[i];
    }
  }

  /* Read file data from beginning of stream */
  s.pos = 0;
  size_t *file_offsets = calloc((size_t)archive->file_count, sizeof(size_t));

  for (int c = 0; c < chunks; c++) {
    for (int i = 0; i < archive->file_count; i++) {
      size_t sz = (size_t)chunk_sizes[i][c];
      if (result->files[i].data &&
          file_offsets[i] + sz <= result->files[i].len) {
        osrs_stream_bytes(&s, result->files[i].data + file_offsets[i], sz);
        file_offsets[i] += sz;
      } else {
        osrs_stream_skip(&s, sz);
      }
    }
  }

  /* Cleanup */
  free(file_offsets);
  for (int i = 0; i < archive->file_count; i++)
    free(chunk_sizes[i]);
  free(chunk_sizes);
  free(file_sizes);
  osrs_container_free(container);

  return result;
}

/* Free archive files */
void osrs_archive_files_free(osrs_archive_files_t *files) {
  if (!files)
    return;
  for (int i = 0; i < files->count; i++)
    free(files->files[i].data);
  free(files->files);
  free(files);
}

uint8_t *osrs_cache_read_archive_file(osrs_cache_t *cache, int index_id,
                                      int archive_id, int file_id,
                                      size_t *out_len) {
  if (out_len)
    *out_len = 0;
  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(cache, index_id, archive_id, NULL);
  if (!files)
    return NULL;
  uint8_t *out = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == file_id) {
      out = malloc(files->files[i].len);
      if (out) {
        memcpy(out, files->files[i].data, files->files[i].len);
        if (out_len)
          *out_len = files->files[i].len;
      }
      break;
    }
  }
  osrs_archive_files_free(files);
  return out;
}

/* Parse index data (from index 255 container) */
static bool parse_index_data(osrs_cache_t *cache, int index_id,
                             const uint8_t *data, size_t len) {
  osrs_stream_t s;
  osrs_stream_init(&s, data, len);

  osrs_index_t *index = &cache->indexes[cache->index_count];
  memset(index, 0, sizeof(*index));
  index->id = index_id;

  /* Read protocol */
  index->protocol = osrs_stream_u8(&s);

  if (index->protocol < 5 || index->protocol > 7) {
    return false;
  }

  /* Read revision */
  if (index->protocol >= 6) {
    index->revision = osrs_stream_i32(&s);
  }

  /* Read flags */
  uint8_t flags = osrs_stream_u8(&s);
  index->named = (flags & 0x01) != 0;
  index->sized = (flags & 0x04) != 0;

  /* Read archive count */
  int archive_count;
  if (index->protocol >= 7) {
    archive_count = (int)osrs_stream_usmart(&s);
  } else {
    archive_count = osrs_stream_u16(&s);
  }

  index->archives = calloc((size_t)archive_count, sizeof(osrs_archive_t));
  if (!index->archives)
    return false;

  /* Read archive IDs */
  int prev_id = 0;
  for (int i = 0; i < archive_count; i++) {
    int delta;
    if (index->protocol >= 7) {
      delta = (int)osrs_stream_usmart(&s);
    } else {
      delta = osrs_stream_u16(&s);
    }
    index->archives[i].id = prev_id + delta;
    prev_id = index->archives[i].id;
  }

  /* Read name hashes */
  if (index->named) {
    for (int i = 0; i < archive_count; i++) {
      index->archives[i].name_hash = osrs_stream_i32(&s);
    }
  }

  /* Read CRCs */
  for (int i = 0; i < archive_count; i++) {
    index->archives[i].crc = osrs_stream_i32(&s);
  }

  /* Read sizes (if sized flag set) */
  if (index->sized) {
    for (int i = 0; i < archive_count; i++) {
      /* compressed size and decompressed size - skip for now */
      osrs_stream_skip(&s, 4);
      osrs_stream_skip(&s, 4);
    }
  }

  /* Read revisions */
  for (int i = 0; i < archive_count; i++) {
    index->archives[i].revision = osrs_stream_i32(&s);
  }

  /* Read file counts */
  for (int i = 0; i < archive_count; i++) {
    if (index->protocol >= 7) {
      index->archives[i].file_count = (int)osrs_stream_usmart(&s);
    } else {
      index->archives[i].file_count = osrs_stream_u16(&s);
    }
  }

  /* Read file IDs */
  for (int i = 0; i < archive_count; i++) {
    int file_count = index->archives[i].file_count;
    index->archives[i].file_ids = calloc((size_t)file_count, sizeof(int));
    if (!index->archives[i].file_ids)
      return false;

    int prev_file_id = 0;
    for (int j = 0; j < file_count; j++) {
      int delta;
      if (index->protocol >= 7) {
        delta = (int)osrs_stream_usmart(&s);
      } else {
        delta = osrs_stream_u16(&s);
      }
      index->archives[i].file_ids[j] = prev_file_id + delta;
      prev_file_id = index->archives[i].file_ids[j];
    }
  }

  index->archive_count = archive_count;
  cache->index_count++;
  return true;
}

bool osrs_cache_load_indexes(osrs_cache_t *cache) {
  if (!cache || !cache->index_255)
    return false;

  int index_count = index_file_count(cache->index_255);
  if (index_count <= 0)
    return false;

  OSRS_INFO(OSRS_LOG_CAT_CACHE, "Loading %d indexes", index_count);

  cache->indexes = calloc((size_t)index_count, sizeof(osrs_index_t));
  if (!cache->indexes)
    return false;

  for (int i = 0; i < index_count; i++) {
    size_t len;
    uint8_t *data = osrs_cache_read_index(cache, i, &len);
    if (!data)
      continue;

    /* Decompress container */
    osrs_container_t *container = osrs_container_decompress(data, len, NULL);
    free(data);

    if (!container)
      continue;

    /* Parse index data */
    if (!parse_index_data(cache, i, container->data, container->data_len)) {
      osrs_container_free(container);
      continue;
    }

    OSRS_DEBUG(OSRS_LOG_CAT_CACHE, "Loaded index %d", i);

    /* Store CRC */
    osrs_index_t *index = &cache->indexes[cache->index_count - 1];
    index->crc = (int)container->crc;
    index->compression = container->compression;

    osrs_container_free(container);
  }

  return cache->index_count > 0;
}

osrs_index_t *osrs_cache_get_index(osrs_cache_t *cache, int index_id) {
  if (!cache || !cache->indexes)
    return NULL;
  for (int i = 0; i < cache->index_count; i++) {
    if (cache->indexes[i].id == index_id) {
      return &cache->indexes[i];
    }
  }
  return NULL;
}

osrs_archive_t *osrs_index_find_archive_by_hash(osrs_index_t *index,
                                                int name_hash) {
  if (!index || !index->archives)
    return NULL;
  for (int i = 0; i < index->archive_count; i++) {
    if (index->archives[i].name_hash == name_hash) {
      return &index->archives[i];
    }
  }
  return NULL;
}

void osrs_index_free(osrs_index_t *index) {
  if (!index)
    return;
  if (index->archives) {
    for (int i = 0; i < index->archive_count; i++) {
      free(index->archives[i].file_ids);
    }
    free(index->archives);
  }
}

osrs_container_t *osrs_container_decompress(const uint8_t *data, size_t len,
                                            const uint32_t *xtea_key) {
  if (!data || len < 5)
    return NULL;

  osrs_stream_t s;
  osrs_stream_init(&s, data, len);

  int compression = osrs_stream_u8(&s);
  int compressed_len = osrs_stream_i32(&s);

  OSRS_DEBUG(OSRS_LOG_CAT_CACHE,
             "Decompressing container compression=%d raw=%zu compressed=%d",
             compression, len, compressed_len);

  if (compressed_len < 0 || (size_t)compressed_len > len)
    return NULL;

  /* Calculate CRC */
  osrs_crc32_t crc_ctx;
  osrs_crc32_init(&crc_ctx);
  osrs_crc32_update(&crc_ctx, data, 5); /* compression + length */

  uint8_t *decrypted = NULL;
  size_t decrypted_len = 0;

  if (compression == OSRS_COMPRESSION_NONE) {
    decrypted_len = (size_t)compressed_len;
    decrypted = malloc(decrypted_len);
    if (!decrypted)
      return NULL;
    osrs_stream_bytes(&s, decrypted, decrypted_len);

    osrs_crc32_update(&crc_ctx, decrypted, decrypted_len);

    if (xtea_key) {
      osrs_xtea_t xtea;
      osrs_xtea_init_u32(&xtea, xtea_key);
      osrs_xtea_decrypt(&xtea, decrypted, decrypted_len);
    }

  } else if (compression == OSRS_COMPRESSION_BZ2 ||
             compression == OSRS_COMPRESSION_GZ) {
    /* Read encrypted data (includes 4-byte decompressed length) */
    size_t enc_len = (size_t)compressed_len + 4;
    uint8_t *enc_data = malloc(enc_len);
    if (!enc_data)
      return NULL;
    osrs_stream_bytes(&s, enc_data, enc_len);

    osrs_crc32_update(&crc_ctx, enc_data, enc_len);

    if (xtea_key) {
      osrs_xtea_t xtea;
      osrs_xtea_init_u32(&xtea, xtea_key);
      osrs_xtea_decrypt(&xtea, enc_data, enc_len);
    }

    /* Read decompressed length */
    osrs_stream_t enc_stream;
    osrs_stream_init(&enc_stream, enc_data, enc_len);
    int decompressed_len = osrs_stream_i32(&enc_stream);

    decrypted = malloc((size_t)decompressed_len);
    if (!decrypted) {
      free(enc_data);
      return NULL;
    }
    decrypted_len = (size_t)decompressed_len;

    /* Decompress */
    size_t out_len = decrypted_len;
    bool ok;
    if (compression == OSRS_COMPRESSION_BZ2) {
      /* OSRS cache stores BZip2 data without the BZh header; prepend one */
      size_t bz2_raw_len = osrs_stream_remaining(&enc_stream);
      uint8_t *bz2_data = malloc(bz2_raw_len + 4);
      if (!bz2_data) {
        free(enc_data);
        free(decrypted);
        return NULL;
      }
      memcpy(bz2_data, "BZh1", 4);
      memcpy(bz2_data + 4, enc_stream.data + enc_stream.pos, bz2_raw_len);
      ok = osrs_bzip2_decompress_oneshot(bz2_data, bz2_raw_len + 4, decrypted,
                                         &out_len);
      free(bz2_data);
    } else {
      ok = osrs_gzip_decompress_oneshot(enc_stream.data + enc_stream.pos,
                                        osrs_stream_remaining(&enc_stream),
                                        decrypted, &out_len);
    }

    free(enc_data);

    if (!ok || out_len != decrypted_len) {
      OSRS_WARN(OSRS_LOG_CAT_CACHE,
                "Container decompression failed: compression=%d "
                "expected=%zu got=%zu",
                compression, decrypted_len, out_len);
      free(decrypted);
      return NULL;
    }
  } else {
    OSRS_WARN(OSRS_LOG_CAT_CACHE,
              "Container decompression failed: unknown compression=%d",
              compression);
    return NULL;
  }

  /* Read revision */
  int revision = -1;
  if (osrs_stream_remaining(&s) >= 4) {
    revision = osrs_stream_i32(&s);
  } else if (osrs_stream_remaining(&s) >= 2) {
    revision = osrs_stream_u16(&s);
  }

  osrs_container_t *container = malloc(sizeof(osrs_container_t));
  if (!container) {
    free(decrypted);
    return NULL;
  }

  container->data = decrypted;
  container->data_len = decrypted_len;
  container->compression = compression;
  container->revision = revision;
  container->crc = osrs_crc32_final(&crc_ctx);

  OSRS_DEBUG(OSRS_LOG_CAT_CACHE,
             "Container decompressed: compression=%d raw=%zu decompressed=%zu",
             compression, len, decrypted_len);

  return container;
}

void osrs_container_free(osrs_container_t *container) {
  if (!container)
    return;
  free(container->data);
  free(container);
}

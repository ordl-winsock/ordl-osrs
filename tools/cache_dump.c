/*
 * tools/cache_dump.c — Dump OSRS cache index information
 * Pure C23, zero external dependencies.
 */

#include "osrs/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s <cache_path> [index_id]\n", prog);
  fprintf(stderr, "  cache_path: Path to OSRS cache directory\n");
  fprintf(stderr, "  index_id:   Optional index ID to dump details\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  const char *cache_path = argv[1];
  int target_index = -1;
  if (argc >= 3) {
    target_index = atoi(argv[2]);
  }

  printf("Opening OSRS cache at: %s\n", cache_path);

  osrs_cache_t *cache = osrs_cache_open(cache_path);
  if (!cache) {
    fprintf(stderr, "Failed to open cache\n");
    return 1;
  }

  printf("Loading indexes...\n");
  if (!osrs_cache_load_indexes(cache)) {
    fprintf(stderr, "Failed to load indexes\n");
    osrs_cache_close(cache);
    return 1;
  }

  printf("Loaded %d indexes\n\n", cache->index_count);

  for (int i = 0; i < cache->index_count; i++) {
    osrs_index_t *idx = &cache->indexes[i];
    printf("Index %d: protocol=%d revision=%d archives=%d named=%s crc=0x%08X "
           "compression=%d\n",
           idx->id, idx->protocol, idx->revision, idx->archive_count,
           idx->named ? "yes" : "no", idx->crc, idx->compression);

    if (target_index >= 0 && idx->id == target_index) {
      printf("  Archives:\n");
      for (int j = 0; j < idx->archive_count && j < 20; j++) {
        osrs_archive_t *arch = &idx->archives[j];
        printf("    [%d] id=%d name_hash=0x%08X crc=0x%08X files=%d\n", j,
               arch->id, arch->name_hash, arch->crc, arch->file_count);
      }
      if (idx->archive_count > 20) {
        printf("    ... and %d more\n", idx->archive_count - 20);
      }
    }
  }

  /* Test reading an archive */
  if (target_index >= 0) {
    osrs_index_t *idx = osrs_cache_get_index(cache, target_index);
    if (idx && idx->archive_count > 0) {
      int archive_id = idx->archives[0].id;
      printf("\nReading archive %d from index %d...\n", archive_id,
             target_index);

      size_t len;
      uint8_t *data =
          osrs_cache_read_archive(cache, target_index, archive_id, &len);
      if (data) {
        printf("  Read %zu bytes (raw)\n", len);

        /* Try to decompress */
        osrs_container_t *container =
            osrs_container_decompress(data, len, NULL);
        if (container) {
          printf("  Decompressed: %zu bytes, compression=%d, revision=%d, "
                 "crc=0x%08X\n",
                 container->data_len, container->compression,
                 container->revision, container->crc);
          printf("  First 64 bytes: ");
          for (size_t i = 0; i < container->data_len && i < 64; i++) {
            printf("%02X ", container->data[i]);
          }
          printf("\n");
          osrs_container_free(container);
        } else {
          printf("  Failed to decompress container\n");
        }
        free(data);
      } else {
        printf("  Failed to read archive\n");
      }
    }
  }

  osrs_cache_close(cache);
  return 0;
}

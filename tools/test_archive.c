/* Quick test: read archive 12850 from index 5 */
#include "osrs/cache.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <cache_path>\n", argv[0]);
    return 1;
  }
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache) {
    printf("Failed to open cache\n");
    return 1;
  }
  if (!osrs_cache_load_indexes(cache)) {
    printf("Failed to load indexes\n");
    return 1;
  }

  int archive_id = 12850;
  size_t len;
  uint8_t *data = osrs_cache_read_archive(cache, 5, archive_id, &len);
  if (!data) {
    printf("Failed to read archive %d from index 5\n", archive_id);
    return 1;
  }

  printf("Read %zu bytes for archive %d\n", len, archive_id);

  osrs_container_t *container = osrs_container_decompress(data, len, NULL);
  if (container) {
    printf("Decompressed: %zu bytes, compression=%d, revision=%d, crc=0x%08X\n",
           container->data_len, container->compression, container->revision,
           container->crc);
    printf("First 32 bytes: ");
    for (size_t i = 0; i < container->data_len && i < 32; i++) {
      printf("%02X ", container->data[i]);
    }
    printf("\n");

    osrs_archive_files_t *files =
        osrs_cache_read_archive_files(cache, 5, archive_id, NULL);
    if (files) {
      printf("Split into %d files\n", files->count);
      for (int i = 0; i < files->count; i++) {
        printf("  File %d: %zu bytes\n", i, files->files[i].len);
      }
      osrs_archive_files_free(files);
    } else {
      printf("Failed to split archive files\n");
    }

    osrs_container_free(container);
  } else {
    printf("Failed to decompress container\n");
  }

  free(data);
  osrs_cache_close(cache);
  return 0;
}

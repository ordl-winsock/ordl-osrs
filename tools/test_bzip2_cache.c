/*
 * tools/test_bzip2_cache.c — Test BZip2 decompression with real cache data
 */

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
    fprintf(stderr, "Failed to open cache\n");
    return 1;
  }

  if (!osrs_cache_load_indexes(cache)) {
    fprintf(stderr, "Failed to load indexes\n");
    osrs_cache_close(cache);
    return 1;
  }

  /* Read index 0, archive 0 */
  size_t raw_len;
  uint8_t *raw = osrs_cache_read_archive(cache, 0, 0, &raw_len);
  if (!raw) {
    fprintf(stderr, "Failed to read archive 0 from index 0\n");
    osrs_cache_close(cache);
    return 1;
  }

  printf("Read %zu bytes raw data\n", raw_len);
  printf("First 16 bytes: ");
  for (size_t i = 0; i < 16 && i < raw_len; i++)
    printf("%02X ", raw[i]);
  printf("\n");

  /* Try to decompress */
  osrs_container_t *container = osrs_container_decompress(raw, raw_len, NULL);
  if (container) {
    printf("SUCCESS: Decompressed %zu bytes\n", container->data_len);
    printf("First 64 bytes: ");
    for (size_t i = 0; i < 64 && i < container->data_len; i++)
      printf("%02X ", container->data[i]);
    printf("\n");
    osrs_container_free(container);
  } else {
    printf("FAILED to decompress\n");
  }

  free(raw);
  osrs_cache_close(cache);
  return container ? 0 : 1;
}

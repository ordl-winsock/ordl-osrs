/*
 * tools/test_bzip2_bulk.c — Bulk-test BZip2 decompression against live cache.
 */

#include "osrs/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  int total = 0, ok = 0, fail = 0;

  for (int idx_id = 0; idx_id < cache->index_count; idx_id++) {
    osrs_index_t *idx = osrs_cache_get_index(cache, idx_id);
    if (!idx)
      continue;

    for (int a = 0; a < idx->archive_count && a < 20; a++) {
      int archive_id = idx->archives[a].id;
      size_t len;
      uint8_t *data = osrs_cache_read_archive(cache, idx_id, archive_id, &len);
      if (!data)
        continue;

      osrs_container_t *container = osrs_container_decompress(data, len, NULL);
      if (container) {
        ok++;
        osrs_container_free(container);
      } else {
        fail++;
        printf("FAIL: index=%d archive=%d len=%zu\n", idx_id, archive_id, len);
      }
      free(data);
      total++;
    }
  }

  printf("Total: %d  OK: %d  Fail: %d\n", total, ok, fail);
  osrs_cache_close(cache);
  return fail > 0 ? 1 : 0;
}

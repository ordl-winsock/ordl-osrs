/* Quick tool to list all archive IDs in a cache index */
#include "osrs/cache.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <cache_path> <index_id>\n", argv[0]);
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

  int target = atoi(argv[2]);
  osrs_index_t *idx = osrs_cache_get_index(cache, target);
  if (!idx) {
    printf("Index %d not found\n", target);
    return 1;
  }

  printf("Index %d: %d archives\n", target, idx->archive_count);
  for (int i = 0; i < idx->archive_count; i++) {
    printf("  [%d] id=%d\n", i, idx->archives[i].id);
  }

  osrs_cache_close(cache);
  return 0;
}

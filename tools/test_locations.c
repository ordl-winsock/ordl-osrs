/* Quick test: parse locations from archive 12850 */
#include "osrs/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int id, x, y, z, type, orientation;
} loc_t;

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
  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(cache, 5, archive_id, NULL);
  if (!files) {
    printf("Failed to read archive %d\n", archive_id);
    return 1;
  }

  printf("Archive %d: %d files\n", archive_id, files->count);
  for (int i = 0; i < files->count; i++) {
    printf("  File %d: %zu bytes\n", i, files->files[i].len);
  }

  if (files->count > 1) {
    /* Try to parse locations manually */
    const uint8_t *data = files->files[1].data;
    size_t len = files->files[1].len;
    size_t pos = 0;

    int object_id = -1;
    int loc_count = 0;
    while (pos < len) {
      /* Simple smart read */
      int id_delta = data[pos];
      if (id_delta == 0) {
        pos++;
        break;
      }
      if (id_delta < 128) {
        pos++;
      } else {
        if (pos + 1 >= len)
          break;
        id_delta = ((id_delta & 0x7F) << 8) | data[pos + 1];
        pos += 2;
      }
      object_id += id_delta;

      while (pos < len) {
        int pos_delta = data[pos];
        if (pos_delta == 0) {
          pos++;
          break;
        }
        if (pos_delta < 128) {
          pos++;
        } else {
          if (pos + 1 >= len)
            break;
          pos_delta = ((pos_delta & 0x7F) << 8) | data[pos + 1];
          pos += 2;
        }
        if (pos >= len)
          break;
        uint8_t attr = data[pos++];
        loc_count++;
      }
    }
    printf("Parsed %d locations from file 1\n", loc_count);
  }

  osrs_archive_files_free(files);
  osrs_cache_close(cache);
  return 0;
}

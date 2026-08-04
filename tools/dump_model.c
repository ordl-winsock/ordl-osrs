/* tools/dump_model.c - dump raw decompressed model bytes for offline analysis
 */
#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <cache_dir> <model_id> <out.bin>\n", argv[0]);
    return 1;
  }
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  size_t len = 0;
  uint8_t *data = osrs_cache_read_archive(cache, 7, atoi(argv[2]), &len);
  if (!data)
    return 1;
  osrs_container_t *c = osrs_container_decompress(data, len, NULL);
  free(data);
  if (!c)
    return 1;
  FILE *f = fopen(argv[3], "wb");
  fwrite(c->data, 1, c->data_len, f);
  fclose(f);
  printf("wrote %zu bytes\n", c->data_len);
  osrs_container_free(c);
  osrs_cache_close(cache);
  return 0;
}

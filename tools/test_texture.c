/* tools/test_texture.c - verify texture pixel pipeline */
#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 3)
    return 1;
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  osrs_cache_load_indexes(cache);
  osrs_config_t *config = osrs_config_init(cache);

  for (int i = 2; i < argc; i++) {
    int tid = atoi(argv[i]);
    osrs_texture_t *tex = osrs_config_load_texture(config, tid);
    if (!tex) {
      printf("texture %d: def not found\n", tid);
      continue;
    }
    printf("texture %d: fileId=%d avgHSL=%04x opaque=%d\n", tid, tex->file_id,
           tex->average_rgb, tex->is_opaque);
    uint32_t *px = osrs_config_texture_pixels(config, tid);
    if (px) {
      char path[64];
      snprintf(path, sizeof(path), "/tmp/texture_%d.ppm", tid);
      FILE *f = fopen(path, "wb");
      fprintf(f, "P6\n128 128\n255\n");
      for (int j = 0; j < 128 * 128; j++)
        fputc((px[j] >> 16) & 0xFF, f), fputc((px[j] >> 8) & 0xFF, f),
            fputc(px[j] & 0xFF, f);
      fclose(f);
      printf("  wrote %s\n", path);
      free(px);
    } else {
      printf("  pixels failed\n");
    }
    osrs_texture_free(tex);
  }
  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}

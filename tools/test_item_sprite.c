/* tools/test_item_sprite.c - render item sprites to PPM for verification */
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/item_sprite.h"
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

  int n = argc - 2;
  int cols = n < 8 ? n : 8;
  int rows = (n + cols - 1) / cols;
  int cell = 40;
  uint32_t *out = calloc((size_t)cols * cell * rows * cell, sizeof(uint32_t));

  for (int i = 2; i < argc; i++) {
    int id = atoi(argv[i]);
    uint32_t buf[36 * 32];
    bool ok = osrs_item_sprite_render(config, id, buf, 36, 32);
    int cx = (i - 2) % cols, cy = (i - 2) / cols;
    printf("item %5d: %s\n", id, ok ? "ok" : "FAIL");
    if (ok) {
      /* center into cell */
      for (int y = 0; y < 32; y++)
        for (int x = 0; x < 36; x++)
          out[(cy * cell + y + 4) * (cols * cell) + cx * cell + x + 2] =
              buf[y * 36 + x];
    }
  }

  FILE *f = fopen("/tmp/item_sprites.ppm", "wb");
  fprintf(f, "P6\n%d %d\n255\n", cols * cell, rows * cell);
  for (int i = 0; i < cols * cell * rows * cell; i++)
    fputc((out[i] >> 16) & 0xFF, f), fputc((out[i] >> 8) & 0xFF, f),
        fputc(out[i] & 0xFF, f);
  fclose(f);
  printf("wrote /tmp/item_sprites.ppm\n");
  free(out);
  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}

/* tools/dump_defs.c - dump underlay/overlay def colors for given ids */
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/render_utils.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 2)
    return 1;
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  osrs_cache_load_indexes(cache);
  osrs_config_t *config = osrs_config_init(cache);

  printf("== underlays ==\n");
  for (int i = 2; i < argc; i++) {
    int id = atoi(argv[i]);
    osrs_underlay_def_t *d = osrs_config_load_underlay(config, id);
    if (d) {
      int h, s, l, m;
      osrs_rgb_to_hsl_full(d->color, &h, &s, &l, &m);
      printf("underlay %3d: color=%06x hue=%d sat=%d light=%d mult=%d\n", id,
             d->color, h, s, l, m);
      osrs_underlay_def_free(d);
    } else {
      printf("underlay %3d: NOT FOUND\n", id);
    }
  }
  printf("== overlays ==\n");
  for (int i = 2; i < argc; i++) {
    int id = atoi(argv[i]);
    osrs_overlay_def_t *d = osrs_config_load_overlay(config, id);
    if (d) {
      printf("overlay %3d: color=%06x texture=%d secondary=%06x hide=%d\n", id,
             d->color, d->texture, d->secondary_color, d->hide_underlay);
      osrs_overlay_def_free(d);
    } else {
      printf("overlay %3d: NOT FOUND\n", id);
    }
  }
  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}

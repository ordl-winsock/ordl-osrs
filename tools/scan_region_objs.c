#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/map.h"
#include "osrs/model.h"
#include "osrs/render_utils.h"
#include <stdio.h>
#include <stdlib.h>

/* Dump each location object in a region with its def, model, and lit color. */
int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <cache> <rx> <ry>\n", argv[0]);
    return 1;
  }
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  osrs_cache_load_indexes(cache);
  osrs_config_t *config = osrs_config_init(cache);
  osrs_map_t *map = osrs_map_init(cache);
  int rx = atoi(argv[2]), ry = atoi(argv[3]);
  osrs_region_t *r = osrs_map_load_region(map, rx, ry, NULL);
  if (!r) {
    fprintf(stderr, "region load failed\n");
    return 1;
  }
  printf("region %d,%d: %d locations\n", rx, ry, r->location_count);

  /* Track unique object ids to avoid spam */
  int seen[65536];
  int seen_count = 0;
  for (int i = 0; i < r->location_count; i++) {
    osrs_location_t *loc = &r->locations[i];
    if (loc->plane != 0)
      continue;
    int dup = 0;
    for (int j = 0; j < seen_count; j++)
      if (seen[j] == loc->object_id) {
        dup = 1;
        break;
      }
    if (dup)
      continue;
    seen[seen_count++] = loc->object_id;

    osrs_object_def_t *def = osrs_config_load_object(config, loc->object_id);
    if (!def || def->model_count == 0) {
      if (def)
        osrs_object_def_free(def);
      continue;
    }
    int mid = def->model_ids[0];
    osrs_archive_files_t *files =
        osrs_cache_read_archive_files(cache, 7, mid, NULL);
    if (!files || files->count < 1) {
      osrs_object_def_free(def);
      continue;
    }
    osrs_model_t *m =
        osrs_model_load(mid, files->files[0].data, files->files[0].len);
    if (!m) {
      osrs_archive_files_free(files);
      osrs_object_def_free(def);
      continue;
    }
    /* apply recolor */
    for (int rc = 0; rc < def->recolor_count; rc++)
      for (int f = 0; f < m->face_count; f++)
        if (m->face_color[f] == (uint16_t)def->recolor_src[rc])
          m->face_color[f] = (uint16_t)def->recolor_dst[rc];

    /* average hsl + the lit color at ambient 128 */
    long hue_sum = 0, sat_sum = 0, lum_sum = 0;
    for (int f = 0; f < m->face_count; f++) {
      hue_sum += (m->face_color[f] >> 10) & 63;
      sat_sum += (m->face_color[f] >> 7) & 7;
      lum_sum += m->face_color[f] & 127;
    }
    int fc = m->face_count > 0 ? m->face_count : 1;
    int avg_hue = (int)(hue_sum / fc), avg_sat = (int)(sat_sum / fc),
        avg_lum = (int)(lum_sum / fc);
    /* lit color at neutral shade (ambient 128 => shade 128) */
    int ambient = def->ambient + 128;
    uint32_t rgb = osrs_hsl_shade_to_rgb(m->face_color[0], ambient);
    printf("obj %5d '%-20s' mid=%5d faces=%3d hue=%2d sat=%d lum=%3d | "
           "ambient=%d | lit rgb=(%3d,%3d,%3d)%s\n",
           loc->object_id, def->name, mid, m->face_count, avg_hue, avg_sat,
           avg_lum, ambient, (int)((rgb >> 16) & 255), (int)((rgb >> 8) & 255),
           (int)(rgb & 255), avg_lum < 25 ? "  <-- DARK" : "");
    osrs_model_free(m);
    osrs_archive_files_free(files);
    osrs_object_def_free(def);
  }
  return 0;
}

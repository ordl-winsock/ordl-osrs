/* tools/dump_region.c - dump underlay/overlay/height stats for a region */
#include "osrs/cache.h"
#include "osrs/map.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <cache_dir> <rx> <ry>\n", argv[0]);
    return 1;
  }
  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  osrs_cache_load_indexes(cache);
  osrs_map_t *map = osrs_map_init(cache);
  osrs_region_t *r =
      osrs_map_load_region(map, atoi(argv[2]), atoi(argv[3]), NULL);
  if (!r) {
    printf("region load failed\n");
    return 1;
  }
  int under = 0, over = 0, nonzero_h = 0;
  int under_hist[8] = {0}, over_hist[8] = {0}, path_hist[16] = {0};
  int over_ids[32] = {0};
  int minh = 1 << 28, maxh = -(1 << 28);
  for (int p = 0; p < 4; p++)
    for (int x = 0; x < 64; x++)
      for (int z = 0; z < 64; z++) {
        osrs_tile_t *t = &r->tiles[p][x][z];
        if (t->underlay_id > 0) {
          under++;
          if (t->underlay_id < 8)
            under_hist[t->underlay_id]++;
        }
        if (t->overlay_id > 0) {
          over++;
          if (t->overlay_id < 8)
            over_hist[t->overlay_id]++;
          if (t->overlay_id < 32)
            over_ids[t->overlay_id]++;
          if (t->overlay_path < 16)
            path_hist[t->overlay_path]++;
        }
        if (t->height != 0)
          nonzero_h++;
        if (t->height < minh)
          minh = t->height;
        if (t->height > maxh)
          maxh = t->height;
      }
  printf("region (%s,%s): locations=%d\n", argv[2], argv[3], r->location_count);
  printf("  underlay tiles: %d (ids:", under);
  for (int i = 0; i < 8; i++)
    printf(" %d:%d", i, under_hist[i]);
  printf(")\n  overlay tiles: %d (ids:", over);
  for (int i = 0; i < 8; i++)
    printf(" %d:%d", i, over_hist[i]);
  printf(")\n  overlay paths:");
  for (int i = 0; i < 16; i++)
    if (path_hist[i])
      printf(" %d:%d", i, path_hist[i]);
  printf("\n  overlay ids (0-31):");
  for (int i = 0; i < 32; i++)
    if (over_ids[i])
      printf(" %d:%d", i, over_ids[i]);
  printf("\n  height: nonzero=%d min=%d max=%d\n", nonzero_h, minh, maxh);
  /* Sample of plane 0 tile ids around center */
  printf("  plane0 underlay ids [30..34]x[30..34]:\n");
  for (int z = 30; z <= 34; z++) {
    printf("   ");
    for (int x = 30; x <= 34; x++)
      printf(" %3d", r->tiles[0][x][z].underlay_id);
    printf("\n");
  }
  /* Find overlay id range */
  int omin = 1 << 28, omax = 0;
  for (int p = 0; p < 4; p++)
    for (int x = 0; x < 64; x++)
      for (int z = 0; z < 64; z++) {
        int o = r->tiles[p][x][z].overlay_id;
        if (o > 0) {
          if (o < omin)
            omin = o;
          if (o > omax)
            omax = o;
        }
      }
  printf("  overlay id range: %d..%d\n", omin, omax);
  /* Specific tile dump for a few coords (world tiles) */
  if (argc > 4) {
    int wx = atoi(argv[4]), wz = atoi(argv[5]);
    int lx = wx & 63, lz = wz & 63;
    osrs_tile_t *t = &r->tiles[0][lx][lz];
    printf("  tile (%d,%d) local(%d,%d): underlay=%d overlay=%d path=%d rot=%d "
           "height=%d settings=%d\n",
           wx, wz, lx, lz, t->underlay_id, t->overlay_id, t->overlay_path,
           t->overlay_rotation, t->height, t->settings);
    for (int dz = -3; dz <= 3; dz++) {
      printf("   ");
      for (int dx = -3; dx <= 3; dx++) {
        int ax = lx + dx, az = lz + dz;
        if (ax < 0 || ax > 63 || az < 0 || az > 63) {
          printf("   ---");
          continue;
        }
        osrs_tile_t *tt = &r->tiles[0][ax][az];
        printf(" %d/%d", tt->underlay_id, tt->overlay_id);
      }
      printf("\n");
    }
  }
  osrs_region_free(r);
  osrs_map_free(map);
  osrs_cache_close(cache);
  return 0;
}

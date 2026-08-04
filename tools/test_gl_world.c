/* tools/test_gl_world.c - standalone gl_world renderer test (no client) */
#include "forge/core.h"
#include "forge/platform.h"
#include "forge/renderer.h"
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/gl_world.h"
#include "osrs/map.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <cache_dir> <rx> <ry> [out.ppm]\n", argv[0]);
    return 1;
  }
  int rx = atoi(argv[2]);
  int ry = atoi(argv[3]);
  const char *out = argc > 4 ? argv[4] : "/tmp/gl_world_test.ppm";

  /* Mimic the client: create FORGE platform + renderer first */
  bool use_forge = getenv("GLW_FORGE") != NULL;
  fge_platform_t *platform = NULL;
  fge_renderer_t renderer;
  if (use_forge) {
    platform = fge_platform_create("glw test", 1280, 720, false);
    if (!platform) {
      printf("platform create failed\n");
      return 1;
    }
    if (!fge_renderer_init(&renderer, 1280, 720)) {
      printf("renderer init failed\n");
      return 1;
    }
    printf("forge platform + renderer created\n");
  }

  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache)
    return 1;
  osrs_cache_load_indexes(cache);
  osrs_config_t *config = osrs_config_init(cache);
  osrs_map_t *map = osrs_map_init(cache);

  osrs_region_t *region = osrs_map_load_region(map, rx, ry, NULL);
  if (!region) {
    printf("region load failed\n");
    return 1;
  }

  osrs_gl_world_t *gw = osrs_gl_world_create(1280, 720);
  if (!gw) {
    printf("gl_world create failed\n");
    return 1;
  }

  if (!getenv("GLW_NO_TERRAIN"))
    osrs_gl_world_upload_region(gw, region, rx, ry, config);
  if (!getenv("GLW_NO_MODELS"))
    osrs_gl_world_upload_region_models(gw, region, rx, ry, config);

  /* Optionally blank the terrain to isolate models */
  if (getenv("GLW_MODELS_ONLY")) {
    osrs_gl_world_delete_region(gw, rx, ry);
    /* re-upload models into a fresh region slot */
    osrs_gl_world_upload_region_models(gw, region, rx, ry, config);
  }

  osrs_iso_camera_t cam = {
      .x = 0,
      .y = 0,
      .zoom = getenv("GLW_ZOOM") ? (float)atof(getenv("GLW_ZOOM")) : 0.5f,
      .center_tile_x = getenv("GLW_CX") ? atoi(getenv("GLW_CX")) : rx * 64 + 32,
      .center_tile_z = getenv("GLW_CZ") ? atoi(getenv("GLW_CZ")) : ry * 64 + 32,
      .yaw = getenv("GLW_YAW") ? (float)atof(getenv("GLW_YAW"))
                               : OSRS_ISO_DEFAULT_YAW,
      .pitch = getenv("GLW_PITCH") ? (float)atof(getenv("GLW_PITCH"))
                                   : OSRS_ISO_DEFAULT_PITCH};

  static uint32_t pixels[1280 * 720];
  osrs_gl_world_begin(gw, &cam, 1280, 720);
  osrs_gl_world_render_region(gw, region, rx, ry);

  /* Draw a single object model at the camera center: GLW_OBJ=<model_id> */
  if (getenv("GLW_OBJ")) {
    int mid = atoi(getenv("GLW_OBJ"));
    float gh = 0.0f;
    int lx = cam.center_tile_x - rx * 64;
    int lz = cam.center_tile_z - ry * 64;
    if (lx >= 0 && lx < 64 && lz >= 0 && lz < 64)
      gh = (float)region->tiles[0][lx][lz].height;
    osrs_gl_world_draw_entity(gw, config, mid, 256, 768, NULL, NULL, 0,
                              (float)cam.center_tile_x + 0.5f, gh,
                              (float)cam.center_tile_z + 0.5f, 0.0f, 1.0f,
                              128.0f, 128.0f, 128.0f);
    /* GLW_OBJH: also draw at +height offset for projection measurement */
    if (getenv("GLW_OBJH")) {
      float ho = (float)atof(getenv("GLW_OBJH"));
      osrs_gl_world_draw_entity(gw, config, mid, 256, 768, NULL, NULL, 0,
                                (float)cam.center_tile_x + 0.5f, gh + ho,
                                (float)cam.center_tile_z + 0.5f, 0.0f, 1.0f,
                                128.0f, 128.0f, 128.0f);
    }
  }

  /* Spawn a test NPC model at region center if requested */
  if (getenv("GLW_NPC")) {
    int npc_model_ids[] = {211, 249, 315, 26634, 509, 28337, 185, 280, 179};
    /* Use the actual terrain height at the center tile */
    float gh = (float)region->tiles[0][32][32].height;
    for (int j = 0; j < 9; j++) {
      osrs_gl_world_draw_entity(gw, config, npc_model_ids[j], 64, 768, NULL,
                                NULL, 0, (float)(rx * 64 + 32) + 0.5f, gh,
                                (float)(ry * 64 + 32) + 0.5f, 0.0f, 1.0f,
                                128.0f, 128.0f, 128.0f);
    }
  }
  /* Spawn a test object model if requested */
  if (getenv("GLW_OBJ_OLD")) {
    float gh = (float)region->tiles[0][32][32].height;
    osrs_gl_world_draw_entity(
        gw, config, 60000, 64, 768, NULL, NULL, 0, (float)(rx * 64 + 32) + 0.5f,
        gh, (float)(ry * 64 + 32) + 0.5f, 0.0f, 1.0f, 128.0f, 128.0f, 128.0f);
  }

  osrs_gl_world_end(gw, pixels, 1280, 720);

  int nonzero = 0;
  unsigned long long checksum = 0;
  for (int i = 0; i < 1280 * 720; i++) {
    checksum = checksum * 31 + pixels[i];
    if ((pixels[i] & 0xFFFFFF) != 0x1D1010 && (pixels[i] & 0xFFFFFF) != 0)
      nonzero++;
  }
  printf("non-background pixels: %d / %d, checksum=%llx\n", nonzero, 1280 * 720,
         checksum);

  FILE *f = fopen(out, "wb");
  fprintf(f, "P6\n1280 720\n255\n");
  for (int i = 0; i < 1280 * 720; i++)
    fputc((pixels[i] >> 16) & 0xFF, f), fputc((pixels[i] >> 8) & 0xFF, f),
        fputc(pixels[i] & 0xFF, f);
  fclose(f);
  printf("wrote %s\n", out);

  osrs_gl_world_destroy(gw);
  osrs_region_free(region);
  osrs_map_free(map);
  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}

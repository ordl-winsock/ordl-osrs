/*
 * osrs/map.c - OSRS map/region loading implementation
 * Pure C23, zero external dependencies.
 */

#include "osrs/map.h"
#include "osrs/cache.h"
#include "osrs/log.h"
#include "osrs/render_utils.h"
#include <stdio.h>
#include <stdlib.h>

osrs_map_t *osrs_map_init(osrs_cache_t *cache) {
  if (!cache)
    return NULL;

  osrs_map_t *map = calloc(1, sizeof(osrs_map_t));
  if (!map)
    return NULL;

  map->cache = cache;
  return map;
}

void osrs_map_free(osrs_map_t *map) { free(map); }

static void parse_terrain(osrs_region_t *region, const uint8_t *data,
                          size_t len) {
  osrs_stream_t s;
  osrs_stream_init(&s, data, len);

  int underlay_count = 0;
  int overlay_count = 0;
  int8_t min_height = 0;
  int8_t max_height = 0;
  bool has_height = false;

  for (int z = 0; z < OSRS_REGION_PLANES; z++) {
    for (int x = 0; x < OSRS_REGION_SIZE; x++) {
      for (int y = 0; y < OSRS_REGION_SIZE; y++) {
        osrs_tile_t *tile = &region->tiles[z][x][y];

        while (1) {
          if (osrs_stream_remaining(&s) < 2)
            break;
          uint16_t attr = osrs_stream_u16(&s);
          if (attr == 0)
            break;

          if (attr == 1) {
            /* Explicit height */
            if (osrs_stream_remaining(&s) >= 1) {
              tile->height = (int8_t)osrs_stream_u8(&s) * 8;
              if (!has_height) {
                min_height = tile->height;
                max_height = tile->height;
                has_height = true;
              } else {
                if (tile->height < min_height)
                  min_height = tile->height;
                if (tile->height > max_height)
                  max_height = tile->height;
              }
            }
            break;
          } else if (attr >= 2 && attr <= 49) {
            /* Overlay.  The attribute byte encodes ONLY the shape
             * (path/rotation); the actual overlay id is a SEPARATE u16
             * read next (RuneLite MapLoader: tile.overlayId =
             * in.readShort()).  Using attr-2 as the id both assigns a
             * bogus id AND leaves the stream 2 bytes out of alignment,
             * corrupting every tile after the first overlay. */
            tile->overlay_path = (uint8_t)((attr - 2) / 4);
            tile->overlay_rotation = (uint8_t)((attr - 2) & 3);
            if (osrs_stream_remaining(&s) >= 2)
              tile->overlay_id = osrs_stream_u16(&s);
            overlay_count++;
          } else if (attr >= 50 && attr <= 81) {
            /* Settings/flags */
            tile->settings = (uint16_t)(attr - 49);
          } else {
            /* Underlay */
            tile->underlay_id = (uint16_t)(attr - 81);
            underlay_count++;
          }
        }
      }
    }
  }

  OSRS_DEBUG(OSRS_LOG_CAT_MAP,
             "terrain parsed: underlays=%d overlays=%d height_range=%d..%d",
             underlay_count, overlay_count, has_height ? (int)min_height : 0,
             has_height ? (int)max_height : 0);
}

static void add_location(osrs_region_t *region, int object_id, int type,
                         int orientation, int local_x, int local_y, int plane) {
  if (!region->locations) {
    region->location_capacity = 64;
    region->locations =
        calloc(region->location_capacity, sizeof(osrs_location_t));
    if (!region->locations)
      return;
  }

  if (region->location_count >= region->location_capacity) {
    region->location_capacity *= 2;
    osrs_location_t *new_locs = realloc(
        region->locations, region->location_capacity * sizeof(osrs_location_t));
    if (!new_locs)
      return;
    region->locations = new_locs;
  }

  osrs_location_t *loc = &region->locations[region->location_count++];
  loc->object_id = object_id;
  loc->type = type;
  loc->orientation = orientation;
  loc->local_x = local_x;
  loc->local_y = local_y;
  loc->plane = plane;
}

static void parse_locations(osrs_region_t *region, const uint8_t *data,
                            size_t len) {
  osrs_stream_t s;
  osrs_stream_init(&s, data, len);

  int object_id = -1;
  while (osrs_stream_remaining(&s) > 0) {
    int id_delta = (int)osrs_stream_uint_smart_short_compat(&s);
    if (id_delta == 0)
      break;
    object_id += id_delta;

    int pos_delta;
    int position = 0;
    while ((pos_delta = (int)osrs_stream_ushort_smart(&s)) != 0) {
      /* Position is a cumulative delta (sorted), not an absolute value. */
      position += pos_delta - 1;
      int local_y = position & 0x3F;
      int local_x = (position >> 6) & 0x3F;
      int plane = (position >> 12) & 3;

      if (osrs_stream_remaining(&s) < 1)
        break;
      uint8_t attr = osrs_stream_u8(&s);
      int type = attr >> 2;
      int orientation = attr & 3;

      add_location(region, object_id, type, orientation, local_x, local_y,
                   plane);
    }
  }

  OSRS_DEBUG(OSRS_LOG_CAT_MAP, "locations parsed: count=%d",
             region->location_count);
}

osrs_region_t *osrs_map_load_region(osrs_map_t *map, int region_x, int region_y,
                                    const uint32_t xtea_key[4]) {
  (void)xtea_key;
  if (!map || !map->cache) {
    OSRS_WARN(OSRS_LOG_CAT_MAP,
              "cannot load region (%d,%d): map or cache is null", region_x,
              region_y);
    return NULL;
  }

  OSRS_INFO(OSRS_LOG_CAT_MAP, "loading region (%d,%d)", region_x, region_y);

  osrs_region_t *region = calloc(1, sizeof(osrs_region_t));
  if (!region) {
    OSRS_WARN(OSRS_LOG_CAT_MAP, "cannot load region (%d,%d): allocation failed",
              region_x, region_y);
    return NULL;
  }

  region->region_x = region_x;
  region->region_y = region_y;
  region->region_id = osrs_region_id(region_x, region_y);

  osrs_index_t *map_index = osrs_cache_get_index(map->cache, 5);
  if (!map_index) {
    OSRS_WARN(OSRS_LOG_CAT_MAP,
              "cannot load region (%d,%d): cache index 5 missing", region_x,
              region_y);
    osrs_region_free(region);
    return NULL;
  }

  /* Try named archive lookup first (modern caches) */
  bool loaded_terrain = false;
  bool loaded_locations = false;

  char terrain_name[32];
  snprintf(terrain_name, sizeof(terrain_name), "m%d_%d", region_x, region_y);
  uint32_t terrain_hash = 0;
  for (const char *p = terrain_name; *p; p++) {
    terrain_hash = *p + ((terrain_hash << 5) - terrain_hash);
  }

  osrs_archive_t *terrain_arch =
      osrs_index_find_archive_by_hash(map_index, (int)terrain_hash);
  if (terrain_arch) {
    osrs_archive_files_t *files =
        osrs_cache_read_archive_files(map->cache, 5, terrain_arch->id, NULL);
    if (files && files->count > 0) {
      parse_terrain(region, files->files[0].data, files->files[0].len);
      loaded_terrain = true;
    }
    if (files)
      osrs_archive_files_free(files);
  }

  char locs_name[32];
  snprintf(locs_name, sizeof(locs_name), "l%d_%d", region_x, region_y);
  uint32_t locs_hash = 0;
  for (const char *p = locs_name; *p; p++) {
    locs_hash = *p + ((locs_hash << 5) - locs_hash);
  }

  osrs_archive_t *locs_arch =
      osrs_index_find_archive_by_hash(map_index, (int)locs_hash);
  if (locs_arch) {
    if (xtea_key) {
      OSRS_DEBUG(OSRS_LOG_CAT_MAP,
                 "using XTEA key for locations archive (%d,%d)", region_x,
                 region_y);
    }
    osrs_archive_files_t *files =
        osrs_cache_read_archive_files(map->cache, 5, locs_arch->id, xtea_key);
    if (files && files->count > 0) {
      parse_locations(region, files->files[0].data, files->files[0].len);
      loaded_locations = true;
    }
    if (files)
      osrs_archive_files_free(files);
  }

  /* Fallback: unnamed cache format (archive ID = region ID) */
  if (!loaded_terrain || !loaded_locations) {
    const uint32_t *fallback_key = !loaded_locations ? xtea_key : NULL;
    if (fallback_key) {
      OSRS_DEBUG(OSRS_LOG_CAT_MAP,
                 "using XTEA key for fallback archive (%d,%d)", region_x,
                 region_y);
    }
    osrs_archive_files_t *files = osrs_cache_read_archive_files(
        map->cache, 5, region->region_id, fallback_key);
    if (files) {
      if (!loaded_terrain && files->count > 0) {
        parse_terrain(region, files->files[0].data, files->files[0].len);
        loaded_terrain = true;
      }
      if (!loaded_locations && files->count > 1) {
        parse_locations(region, files->files[1].data, files->files[1].len);
        loaded_locations = true;
      }
      osrs_archive_files_free(files);
    }
  }

  return region;
}

osrs_region_t *osrs_map_load_region_by_id(osrs_map_t *map, int region_id,
                                          const uint32_t xtea_key[4]) {
  int rx = osrs_region_x_from_id(region_id);
  int ry = osrs_region_y_from_id(region_id);
  return osrs_map_load_region(map, rx, ry, xtea_key);
}

void osrs_region_free(osrs_region_t *region) {
  if (!region)
    return;
  free(region->locations);
  free(region);
}

/* -------------------------------------------------------------------------- */
/* Tile color pre-computation (blended terrain colors)                        */
/* -------------------------------------------------------------------------- */

void osrs_region_compute_colors(osrs_region_t *region, osrs_config_t *config) {
  if (!region || !config)
    return;

  for (int plane = 0; plane < OSRS_REGION_PLANES; plane++) {
    for (int x = 0; x < OSRS_REGION_SIZE; x++) {
      for (int z = 0; z < OSRS_REGION_SIZE; z++) {
        osrs_tile_t *tile = &region->tiles[plane][x][z];

        /* Base color */
        uint32_t c =
            osrs_terrain_color(tile->underlay_id, tile->overlay_id, config);

        /* Simple box-blend with cardinal neighbors */
        int r = (int)((c >> 16) & 0xFF);
        int g = (int)((c >> 8) & 0xFF);
        int b = (int)((c >> 0) & 0xFF);
        int w = 4; /* center weight = 4 */

        const int dx[4] = {0, 0, 1, -1};
        const int dz[4] = {1, -1, 0, 0};
        for (int n = 0; n < 4; n++) {
          int nx = x + dx[n];
          int nz = z + dz[n];
          if (nx < 0 || nx >= OSRS_REGION_SIZE || nz < 0 ||
              nz >= OSRS_REGION_SIZE)
            continue;
          osrs_tile_t *nb = &region->tiles[plane][nx][nz];
          uint32_t nc =
              osrs_terrain_color(nb->underlay_id, nb->overlay_id, config);
          r += (int)((nc >> 16) & 0xFF);
          g += (int)((nc >> 8) & 0xFF);
          b += (int)((nc >> 0) & 0xFF);
          w++;
        }

        r /= w;
        g /= w;
        b /= w;
        tile->render_color =
            0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
      }
    }
  }
}

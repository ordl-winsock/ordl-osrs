/*
 * osrs/map.h - OSRS map/region loading and rendering
 * Pure C23, zero external dependencies.
 *
 * Loads terrain and location data from cache index 5 (MAPS).
 * Terrain is stored in archives named m<rx>_<ry>.
 * Locations are stored in archives named l<rx>_<ry> (XTEA encrypted).
 */

#ifndef OSRS_MAP_H
#define OSRS_MAP_H

#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Region dimensions */
#define OSRS_REGION_SIZE 64
#define OSRS_REGION_PLANES 4
#define OSRS_TILE_SIZE 1

/* A single tile in the world */
typedef struct {
  int height;
  uint16_t settings;
  uint16_t overlay_id;
  uint8_t overlay_path;
  uint8_t overlay_rotation;
  uint16_t underlay_id;
  uint32_t render_color; /* Pre-computed blended terrain color */
} osrs_tile_t;

/* A location (object) placed in the world */
typedef struct {
  int object_id;
  int type;
  int orientation;
  int local_x;
  int local_y;
  int plane;
} osrs_location_t;

/* A 64x64 region */
typedef struct {
  int region_x;
  int region_y;
  int region_id;
  osrs_tile_t tiles[OSRS_REGION_PLANES][OSRS_REGION_SIZE][OSRS_REGION_SIZE];
  osrs_location_t *locations;
  int location_count;
  int location_capacity;
} osrs_region_t;

/* Map manager */
typedef struct {
  osrs_cache_t *cache;
} osrs_map_t;

/* Initialize map manager */
osrs_map_t *osrs_map_init(osrs_cache_t *cache);
void osrs_map_free(osrs_map_t *map);

/* Load a region by coordinates or ID */
osrs_region_t *osrs_map_load_region(osrs_map_t *map, int region_x, int region_y,
                                    const uint32_t xtea_key[4]);
osrs_region_t *osrs_map_load_region_by_id(osrs_map_t *map, int region_id,
                                          const uint32_t xtea_key[4]);

/* Free a region */
void osrs_region_free(osrs_region_t *region);

/* Pre-compute blended terrain colors for all tiles in a region.
 *  Call after terrain is parsed; uses config for underlay/overlay lookups. */
void osrs_region_compute_colors(osrs_region_t *region, osrs_config_t *config);

/* Get base world coordinates for a region */
static inline int osrs_region_base_x(int region_x) { return region_x << 6; }
static inline int osrs_region_base_y(int region_y) { return region_y << 6; }
static inline int osrs_region_id(int region_x, int region_y) {
  return (region_x << 8) | region_y;
}
static inline int osrs_region_x_from_id(int region_id) {
  return region_id >> 8;
}
static inline int osrs_region_y_from_id(int region_id) {
  return region_id & 0xFF;
}

#ifdef __cplusplus
}
#endif

#endif /* OSRS_MAP_H */

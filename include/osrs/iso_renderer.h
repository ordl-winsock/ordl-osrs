/*
 * osrs/iso_renderer.h - Isometric 3D renderer for OSRS world
 * Pure C23, zero external dependencies.
 *
 * Projects OSRS world tiles into an isometric (dimetric) view with
 * height support.  Each tile is drawn as a flat diamond (two triangles)
 * at its world height, giving the classic RuneScape look.
 */

#ifndef OSRS_ISO_RENDERER_H
#define OSRS_ISO_RENDERER_H

#include "forge/renderer.h"
#include "osrs/config.h"
#include "osrs/map.h"
#include "osrs/model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Isometric camera -------------------------------------------------------- */

typedef struct {
  float x, y;        /* Screen offset (pixels) */
  float zoom;        /* Zoom multiplier (1.0 = default) */
  int center_tile_x; /* World tile to centre on */
  int center_tile_z;
  float yaw;   /* Orbit angle around the vertical axis (radians).
                * Default OSRS_ISO_DEFAULT_YAW (~45 deg). */
  float pitch; /* Camera elevation angle (radians). 0 = side view,
                * PI/2 = top-down. Default OSRS_ISO_DEFAULT_PITCH. */
} osrs_iso_camera_t;

/* Default orbit angles matching the classic OSRS isometric view. */
#define OSRS_ISO_DEFAULT_YAW 0.7853982f /* 45 deg */
#define OSRS_ISO_DEFAULT_PITCH                                                 \
  0.7853982f /* 45 deg: the OSRS default camera pitch (256/2048 units).  The   \
              * steeper angle sees over low walls into pits/interiors the      \
              * way the real client does. */

/* Pixels-per-tile scale factor at zoom 1.0 (preserves the classic footprint).
 */
#define OSRS_ISO_ORBIT_SCALE 90.5f

/* Constants ---------------------------------------------------------------- */

/* Base tile size on screen at zoom 1.0.
 *  OSRS uses a 2:1 dimetric projection where tiles are 128×64 px. */
#define OSRS_ISO_TILE_W 128.0f
#define OSRS_ISO_TILE_H 64.0f

/* Height scale: each unit of terrain height shifts the tile up by this
 *  many screen pixels at zoom 1.0. */
#define OSRS_ISO_HEIGHT_SCALE 4.0f

/* Project world tile (plus height) to screen space.
 *  Returns false if the point is clearly off-screen (fast cull). */
bool osrs_iso_project(const osrs_iso_camera_t *cam, int screen_w, int screen_h,
                      int wx, int wz, int height, float *sx, float *sy);

/* Inverse: screen pixel -> nearest world tile (ignores height).
 *  Returns false if the screen point is outside the valid area. */
bool osrs_iso_screen_to_world(const osrs_iso_camera_t *cam, int screen_w,
                              int screen_h, float sx, float sy, int *wx,
                              int *wz);

/* Render a single region isometrically.  Tiles are drawn back-to-front
 *  (painter's algorithm) so nearer tiles correctly occlude farther ones.
 *  If config is non-NULL, underlay/overlay colours are looked up from the
 *  cache config definitions rather than using hard-coded defaults. */
void osrs_iso_render_region(const osrs_iso_camera_t *cam, fge_renderer_t *r,
                            osrs_region_t *region, int screen_w, int screen_h,
                            osrs_config_t *config);

/* Convenience: render a coloured diamond at a world tile position.
 *  Used for objects / NPCs / players that sit on top of terrain. */
void osrs_iso_render_marker(const osrs_iso_camera_t *cam, fge_renderer_t *r,
                            int screen_w, int screen_h, int wx, int wz,
                            int height, int size_px, uint32_t color);

/* Render a small vertical "wall" strip between two adjacent tile heights.
 *  Used to hide gaps between tiles of different heights. */
void osrs_iso_render_height_edge(const osrs_iso_camera_t *cam,
                                 fge_renderer_t *r, int screen_w, int screen_h,
                                 int wx, int wz, int h0, int h1,
                                 uint32_t color);

/* Render a 3D model at a world position with given orientation.
 *  Model vertices are projected into isometric space and drawn as
 *  flat-shaded triangles (painter's algorithm, back-to-front). */
void osrs_iso_render_model(const osrs_iso_camera_t *cam, fge_renderer_t *r,
                           int screen_w, int screen_h, int wx, int wz,
                           int height, osrs_model_t *model, int orientation);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_ISO_RENDERER_H */

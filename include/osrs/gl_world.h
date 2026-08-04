/*
 * osrs/gl_world.h — GPU-accelerated world renderer (OpenGL ES 2.0 / desktop GL)
 *
 * Renders OSRS terrain and models via GPU, then reads pixels back into a
 * CPU framebuffer so the existing software UI renderer can overlay on top.
 *
 * Uses EGL + offscreen surface (pbuffer) so there is no conflict with the
 * existing X11 software presentation path.
 */

#ifndef OSRS_GL_WORLD_H
#define OSRS_GL_WORLD_H

#include "osrs/config.h"
#include "osrs/iso_renderer.h"
#include "osrs/map.h"
#include "osrs/model.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct osrs_gl_world osrs_gl_world_t;

/* Create / destroy ----------------------------------------------------------
 */

osrs_gl_world_t *osrs_gl_world_create(int width, int height);
void osrs_gl_world_destroy(osrs_gl_world_t *gw);
bool osrs_gl_world_valid(const osrs_gl_world_t *gw);

/* Region management ---------------------------------------------------------
 */

/* Upload region terrain to GPU. Call once when a region is loaded.
 * The GL renderer keeps a cached VBO for the region.
 * config is needed for underlay/overlay definition lookups. */
void osrs_gl_world_upload_region_ex(osrs_gl_world_t *gw, osrs_region_t *region,
                                    osrs_region_t *adj_n, osrs_region_t *adj_e,
                                    osrs_region_t *adj_s, osrs_region_t *adj_w,
                                    int region_x, int region_y,
                                    osrs_config_t *config);
void osrs_gl_world_upload_region(osrs_gl_world_t *gw, osrs_region_t *region,
                                 int region_x, int region_y,
                                 osrs_config_t *config);

/* Delete a region's GPU data. Call when the region is unloaded. */
void osrs_gl_world_delete_region(osrs_gl_world_t *gw, int region_x,
                                 int region_y);

/* Upload region location models to GPU. Call after terrain upload.
 * Loads object definitions and model data from cache, creates shared VBOs. */
void osrs_gl_world_upload_region_models(osrs_gl_world_t *gw,
                                        osrs_region_t *region, int region_x,
                                        int region_y, osrs_config_t *config);

/* Frame rendering -----------------------------------------------------------
 */

/* Begin a frame. Sets up viewport, clears, binds camera. */
void osrs_gl_world_begin(osrs_gl_world_t *gw, const osrs_iso_camera_t *cam,
                         int screen_w, int screen_h);

/* Render one region. Must be called between begin/end. */
void osrs_gl_world_render_region(osrs_gl_world_t *gw, osrs_region_t *region,
                                 int region_x, int region_y);

/* End frame. Flushes GL, reads pixels into `out_pixels` (RGBA8888,
 * row-major, `screen_w * screen_h * 4` bytes), then unbinds FBO.
 * Returns true on success. */
bool osrs_gl_world_end(osrs_gl_world_t *gw, uint32_t *out_pixels, int screen_w,
                       int screen_h);

/* Entity model rendering (NPCs, players, ground items) ----------------------
 */

/* Draw one model at a world position with yaw rotation and scale.
 * Uses the shared lit-model cache. ambient/contrast from the definition.
 * recolor_src/dst/count: HSL recolors to apply (or count=0). */
void osrs_gl_world_draw_entity(osrs_gl_world_t *gw, osrs_config_t *config,
                               int model_id, int ambient, int contrast,
                               const int *recolor_src, const int *recolor_dst,
                               int recolor_count, float wx, float wy, float wz,
                               float rot_sin, float rot_cos, float scale_x,
                               float scale_y, float scale_z);

/* Draw one model with a skeletal animation frame applied.
 * packed_frame_id: (archive<<16)|file into cache indexes 0/1 (from a sequence
 * frameID). The animated VBO is cached per (model, frame). */
void osrs_gl_world_draw_animated(osrs_gl_world_t *gw, osrs_config_t *config,
                                 int model_id, int packed_frame_id, int ambient,
                                 int contrast, const int *recolor_src,
                                 const int *recolor_dst, int recolor_count,
                                 float wx, float wy, float wz, float rot_sin,
                                 float rot_cos, float scale_x, float scale_y,
                                 float scale_z);

/* Draw a prepared model directly (merged NPC model, animation already
 * applied). Builds a temp VBO, draws, deletes it. Model not freed. */
void osrs_gl_world_draw_model_direct(osrs_gl_world_t *gw, osrs_config_t *config,
                                     osrs_model_t *model, int ambient,
                                     int contrast, float wx, float wy, float wz,
                                     float rot_sin, float rot_cos,
                                     float scale_x, float scale_y,
                                     float scale_z);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_GL_WORLD_H */

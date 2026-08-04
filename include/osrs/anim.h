/*
 * osrs/anim.h - OSRS skeletal animation (framemaps, frames, vertex transform)
 * Pure C23, zero external dependencies.
 *
 * Pipeline (matches RuneLite cache module):
 *   sequence def -> frameIDs (packed: file = id>>16, archive = id&0xFFFF)
 *   frame  = cache index 0 (ANIMATIONS), archive + file
 *   framemap = cache index 1 (SKELETONS), archive = frame's first u16
 *   apply: per-bone transform (origin/translate/rotate/scale) to the model's
 *   vertices, grouped by the model's packed_vertex_groups labels.
 */

#ifndef OSRS_ANIM_H
#define OSRS_ANIM_H

#include "osrs/cache.h"
#include "osrs/model.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Framemap: the animation skeleton. Per-bone transform type + the list of
 * model vertex-group labels that bone affects. */
typedef struct {
  int id;
  int length; /* number of bones */
  int *types; /* per-bone: 0=origin,1=translate,2=rotate,3=scale,5=alpha */
  int **frame_maps; /* per-bone: array of vertex-group labels */
  int *frame_map_counts;
} osrs_framemap_t;

/* A single animation frame: per-bone transform values. */
typedef struct {
  int id;
  int framemap_id;
  int translator_count;
  int *index_frame_ids; /* per-entry: bone index into the framemap */
  int *translator_x;
  int *translator_y;
  int *translator_z;
} osrs_frame_t;

/* Load a framemap from cache index 1 (SKELETONS). */
osrs_framemap_t *osrs_framemap_load(osrs_cache_t *cache, int archive_id);
void osrs_framemap_free(osrs_framemap_t *fm);

/* Load a frame from cache index 0 (ANIMATIONS). archive_id + file_id come from
 * unpacking a sequence frameID (file = id>>16, archive = id&0xFFFF). Also
 * resolves and returns the frame's framemap via out_framemap (caller frees
 * both; *out_framemap may be NULL on failure). */
osrs_frame_t *osrs_frame_load(osrs_cache_t *cache, int archive_id, int file_id,
                              osrs_framemap_t **out_framemap);
void osrs_frame_free(osrs_frame_t *f);

/* Apply one animation frame to a model's vertices in place. Requires
 * model->packed_vertex_groups (models without it are left unchanged). The
 * model's normals are invalidated and should be recomputed before lighting. */
void osrs_model_apply_frame(osrs_model_t *model, const osrs_framemap_t *fm,
                            const osrs_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_ANIM_H */

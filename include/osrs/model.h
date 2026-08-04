/*
 * osrs/model.h - OSRS 3D model structures and loader
 * Pure C23, zero external dependencies.
 */

#ifndef OSRS_MODEL_H
#define OSRS_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A loaded 3D model from cache index 7 */
typedef struct {
  int id;

  /* Vertices */
  int vertex_count;
  int *vertex_x;
  int *vertex_y;
  int *vertex_z;

  /* Faces (triangles) */
  int face_count;
  uint16_t *face_a; /* vertex indices */
  uint16_t *face_b;
  uint16_t *face_c;
  uint16_t *face_color;      /* HSL16 colors */
  int8_t *face_render_type;  /* NULL or per-face: 0=gouraud, 1=flat */
  int8_t *face_priority;     /* NULL or per-face priority */
  int8_t *face_transparency; /* NULL or per-face alpha (0=opaque, -2=skip) */
  int16_t *face_texture;     /* NULL or per-face texture id (-1 = none) */
  int8_t *texture_coord;     /* NULL or per-face texface index (-1 = none) */
  int8_t global_priority;    /* used when face_priority == NULL */

  /* Texture UV-mapping triangles (per type-0 texture face) */
  int num_texture_faces;
  uint16_t *tex_idx1;
  uint16_t *tex_idx2;
  uint16_t *tex_idx3;

  /* Computed per-face UVs (osrs_model_compute_uvs): face_u[i][0..2] */
  float (*face_u)[3];
  float (*face_v)[3];

  /* Skeletal animation: per-vertex group label (packedVertexGroups), or NULL
   * if the model has no vertex bone info. vertex_group_max = highest label+1.
   * Used with a framemap to apply animation frames. */
  int *packed_vertex_groups;
  int vertex_group_max;

  /* Vertex normals (computed by osrs_model_compute_normals).
   * Unnormalized accumulated normals + magnitude, matching the OSRS
   * lighting pipeline (VertexNormal.java). */
  int *normal_x;
  int *normal_y;
  int *normal_z;
  int *normal_mag;

  /* Face normals (computed; used for flat faces). */
  int *face_normal_x;
  int *face_normal_y;
  int *face_normal_z;

  bool normals_computed;
} osrs_model_t;

/* Load a model from raw cache data.
 *  Auto-detects format (Type1/Type2/Type3/Old) from file footer. */
osrs_model_t *osrs_model_load(int model_id, const uint8_t *data, size_t len);

/* Free a model and all its arrays */
void osrs_model_free(osrs_model_t *model);

/* Compute face + vertex normals (idempotent). */
void osrs_model_compute_normals(osrs_model_t *model);

/* Compute per-face texture UVs from the texIndices mapping triangles
 * (ModelDefinition.computeTextureUVCoordinates). Idempotent. */
void osrs_model_compute_uvs(osrs_model_t *model);

/* Apply HSL recolor: replace all faces whose color matches src with dst. */
void osrs_model_recolor(osrs_model_t *model, int src, int dst);

/* Merge multiple models into one combined model (for skeletal animation of
 * multi-part NPCs/players). Combines vertices, faces (re-indexed), colors,
 * and packed_vertex_groups. Returns a new heap model (caller frees with
 * osrs_model_free). Parts must have face_color; other per-face streams are
 * dropped (animation only needs geometry+color). */
osrs_model_t *osrs_model_merge(osrs_model_t **parts, int count);

/* Deep-copy a model (for applying an animation frame without mutating the
 * cached base). Returns a new heap model. */
osrs_model_t *osrs_model_copy(const osrs_model_t *src);

/* Translate all vertices of a model by (tx, ty, tz). Modifies in-place. */
void osrs_model_translate(osrs_model_t *model, int tx, int ty, int tz);

/* OSRS model lighting: returns the lit ARGB color for one face corner.
 * corner: 0=A, 1=B, 2=C. ambient/contrast from the definition (object defs
 * use ambient+64, contrast*25+768; items use ambient+64, contrast+768).
 * Requires osrs_model_compute_normals() first. */
uint32_t osrs_model_light_vertex(const osrs_model_t *model, int face,
                                 int corner, int ambient, int contrast);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_MODEL_H */

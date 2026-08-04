/*
 * osrs/model.c - OSRS 3D model loader + normal/UV/lighting utilities
 * Pure C23, zero external dependencies.
 */

#include "osrs/model.h"
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/render_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Local byte-stream helpers (multiple parallel streams on same data)  */
/* ------------------------------------------------------------------ */

typedef struct {
  const uint8_t *data;
  size_t len;
  size_t pos;
} mem_stream_t;

static inline void ms_seek(mem_stream_t *s, size_t pos) { s->pos = pos; }

static inline uint8_t ms_u8(mem_stream_t *s) {
  if (s->pos >= s->len)
    return 0;
  return s->data[s->pos++];
}

static inline int8_t ms_i8(mem_stream_t *s) { return (int8_t)ms_u8(s); }

static inline uint16_t ms_u16(mem_stream_t *s) {
  uint8_t b0 = ms_u8(s);
  uint8_t b1 = ms_u8(s);
  return ((uint16_t)b0 << 8) | b1;
}

static inline int32_t ms_smart(mem_stream_t *s) {
  uint8_t b = ms_u8(s);
  if (b < 128)
    return (int32_t)b - 64;
  uint8_t b2 = ms_u8(s);
  return (int32_t)(((uint16_t)b << 8) | b2) - 0xC000;
}

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static osrs_model_t *alloc_model(int id) {
  osrs_model_t *m = calloc(1, sizeof(osrs_model_t));
  if (!m)
    return NULL;
  m->id = id;
  m->global_priority = 0;
  m->num_texture_faces = 0;
  m->normals_computed = false;
  return m;
}

/* ------------------------------------------------------------------ */
/* Type-1 decoder (footer: 0xFF 0xFF)  — 23-byte footer, RuneLite ref   */
/* ------------------------------------------------------------------ */

static osrs_model_t *decode_type1(int model_id, const uint8_t *data,
                                  size_t len) {
  if (len < 23)
    return NULL;

  mem_stream_t s[10];
  for (int i = 0; i < 10; i++) {
    s[i].data = data;
    s[i].len = len;
    s[i].pos = 0;
  }

  /* Footer (23 bytes) */
  ms_seek(&s[0], len - 23);
  int vcount = ms_u16(&s[0]);
  int fcount = ms_u16(&s[0]);
  int num_tex = ms_u8(&s[0]);
  int has_frt = ms_u8(&s[0]);
  int has_prio = ms_u8(&s[0]);
  int has_alpha = ms_u8(&s[0]);
  int has_packedTransparency = ms_u8(&s[0]); /* var15 */
  int has_faceTextures = ms_u8(&s[0]);       /* var16 */
  int has_packedVertexGroups = ms_u8(&s[0]); /* var17 */
  int vgroup_count = ms_u16(&s[0]);
  int pri_count = ms_u16(&s[0]);
  int tex_count = ms_u16(&s[0]);
  int face_count2 = ms_u16(&s[0]);
  int unknown_count = ms_u16(&s[0]);

  /* Count texture render types (needed for trailing texture-data size) */
  int tex_type0 = 0, tex_type1_3 = 0, tex_type2 = 0;
  if (num_tex > 0) {
    ms_seek(&s[0], 0);
    for (int i = 0; i < num_tex; i++) {
      uint8_t rt = ms_u8(&s[0]);
      if (rt == 0)
        tex_type0++;
      else if (rt >= 1 && rt <= 3)
        tex_type1_3++;
      else if (rt == 2)
        tex_type2++;
    }
  }

  /* ---- Section offsets (must match RuneLite decodeType1 exactly) ---- */
  size_t off = num_tex; /* texture render types */
  size_t off_vx = off;  /* vertex flags */
  off += (size_t)vcount;
  size_t off_frt = off; /* face render types */
  if (has_frt == 1)
    off += (size_t)fcount;
  size_t off_fidx_type = off; /* face index types */
  off += (size_t)fcount;
  size_t off_prio = off; /* face priorities */
  if (has_prio == 255)
    off += (size_t)fcount;
  size_t off_packedTransparency = off; /* packedTransparency */
  if (has_packedTransparency == 1)
    off += (size_t)fcount;
  size_t off_packedVertexGroups = off; /* packedVertexGroups */
  if (has_packedVertexGroups == 1)
    off += (size_t)vcount;
  size_t off_alpha = off; /* face alpha */
  if (has_alpha == 1)
    off += (size_t)fcount;
  size_t off_fidx = off; /* face index smart values */
  off += (size_t)face_count2;
  size_t off_faceTextures = off; /* face textures (u16) */
  if (has_faceTextures == 1)
    off += (size_t)fcount * 2;
  size_t off_textureCoords = off; /* texture coords */
  off += (size_t)unknown_count;
  size_t off_color = off; /* face colors (u16) */
  off += (size_t)fcount * 2;
  size_t off_vx_data = off; /* vertex X deltas */
  off += (size_t)vgroup_count;
  size_t off_vy_data = off; /* vertex Y deltas */
  off += (size_t)pri_count;
  size_t off_vz_data = off; /* vertex Z deltas */
  off += (size_t)tex_count;
  size_t off_tex_tri = off; /* texture triangles */
  off += (size_t)num_tex * 6;
  /* trailing texture data (type-dependent) — not read in basic decoder */
  off += (size_t)tex_type0 * 6;
  off += (size_t)tex_type1_3 * 6;
  off += (size_t)tex_type1_3 * 6;
  off += (size_t)tex_type1_3 * 2;
  off += (size_t)tex_type1_3;
  off += (size_t)tex_type1_3 * 2 + (size_t)tex_type2 * 2;
  (void)off; /* may be used for bounds checks later */

  osrs_model_t *m = alloc_model(model_id);
  if (!m)
    return NULL;

  m->vertex_count = vcount;
  m->face_count = fcount;
  m->num_texture_faces = num_tex;
  m->global_priority = (has_prio != 255) ? (int8_t)has_prio : 0;

  m->vertex_x = calloc((size_t)vcount, sizeof(int));
  m->vertex_y = calloc((size_t)vcount, sizeof(int));
  m->vertex_z = calloc((size_t)vcount, sizeof(int));
  m->face_a = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_b = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_c = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_color = calloc((size_t)fcount, sizeof(uint16_t));

  if (!m->vertex_x || !m->vertex_y || !m->vertex_z || !m->face_a ||
      !m->face_b || !m->face_c || !m->face_color) {
    osrs_model_free(m);
    return NULL;
  }

  if (has_frt == 1) {
    m->face_render_type = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_render_type) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_prio == 255) {
    m->face_priority = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_priority) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_alpha == 1) {
    m->face_transparency = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_transparency) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_faceTextures == 1) {
    m->face_texture = calloc((size_t)fcount, sizeof(int16_t));
    if (!m->face_texture) {
      osrs_model_free(m);
      return NULL;
    }
    if (num_tex > 0) {
      m->texture_coord = calloc((size_t)fcount, sizeof(int8_t));
      if (!m->texture_coord) {
        osrs_model_free(m);
        return NULL;
      }
    }
  }
  if (has_packedVertexGroups == 1) {
    m->packed_vertex_groups = calloc((size_t)vcount, sizeof(int));
    if (!m->packed_vertex_groups) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (num_tex > 0) {
    m->tex_idx1 = calloc((size_t)num_tex, sizeof(uint16_t));
    m->tex_idx2 = calloc((size_t)num_tex, sizeof(uint16_t));
    m->tex_idx3 = calloc((size_t)num_tex, sizeof(uint16_t));
    if (!m->tex_idx1 || !m->tex_idx2 || !m->tex_idx3) {
      osrs_model_free(m);
      return NULL;
    }
  }

  /* ---- Vertices ---- */
  ms_seek(&s[0], off_vx);      /* vertex flags */
  ms_seek(&s[1], off_vx_data); /* X deltas */
  ms_seek(&s[2], off_vy_data); /* Y deltas */
  ms_seek(&s[3], off_vz_data); /* Z deltas */
  ms_seek(&s[4], off_packedVertexGroups);
  int vx = 0, vy = 0, vz = 0;
  for (int i = 0; i < vcount; i++) {
    int flags = ms_u8(&s[0]);
    int dx = 0, dy = 0, dz = 0;
    if (flags & 1)
      dx = ms_smart(&s[1]);
    if (flags & 2)
      dy = ms_smart(&s[2]);
    if (flags & 4)
      dz = ms_smart(&s[3]);
    m->vertex_x[i] = vx + dx;
    m->vertex_y[i] = vy + dy;
    m->vertex_z[i] = vz + dz;
    vx = m->vertex_x[i];
    vy = m->vertex_y[i];
    vz = m->vertex_z[i];
    if (has_packedVertexGroups == 1)
      m->packed_vertex_groups[i] = ms_u8(&s[4]);
  }

  /* ---- Face colors and attributes ---- */
  ms_seek(&s[0], off_color);
  ms_seek(&s[1], off_frt);
  ms_seek(&s[2], off_prio);
  ms_seek(&s[3], off_alpha);
  ms_seek(&s[4], off_packedTransparency);
  ms_seek(&s[5], off_faceTextures);
  ms_seek(&s[6], off_textureCoords);
  for (int i = 0; i < fcount; i++) {
    m->face_color[i] = ms_u16(&s[0]);
    if (has_frt == 1)
      m->face_render_type[i] = ms_i8(&s[1]);
    if (has_prio == 255)
      m->face_priority[i] = ms_i8(&s[2]);
    if (has_alpha == 1)
      m->face_transparency[i] = ms_i8(&s[3]);
    if (has_packedTransparency == 1)
      (void)ms_u8(&s[4]);
    if (has_faceTextures == 1) {
      m->face_texture[i] = (int16_t)(ms_u16(&s[5]) - 1);
      if (m->texture_coord && m->face_texture[i] != -1)
        m->texture_coord[i] = (int8_t)(ms_u8(&s[6]) - 1);
    }
  }

  /* ---- Face indices ---- */
  ms_seek(&s[0], off_fidx);      /* smart values */
  ms_seek(&s[1], off_fidx_type); /* types */
  int fa = 0, fb = 0, fc = 0, flast = 0;
  for (int i = 0; i < fcount; i++) {
    int type = ms_u8(&s[1]);
    if (type == 1) {
      fa = flast + ms_smart(&s[0]);
      fb = fa + ms_smart(&s[0]);
      fc = fb + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 2) {
      fb = fc;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 3) {
      fa = fc;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 4) {
      int tmp = fa;
      fa = fb;
      fb = tmp;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    }
    m->face_a[i] = (uint16_t)fa;
    m->face_b[i] = (uint16_t)fb;
    m->face_c[i] = (uint16_t)fc;
  }

  /* ---- Texture triangles ---- */
  if (num_tex > 0) {
    ms_seek(&s[0], off_tex_tri);
    for (int i = 0; i < num_tex; i++) {
      m->tex_idx1[i] = ms_u16(&s[0]);
      m->tex_idx2[i] = ms_u16(&s[0]);
      m->tex_idx3[i] = ms_u16(&s[0]);
    }
  }

  return m;
}

/* ------------------------------------------------------------------ */
/* Type-2 decoder (footer: 0xFE 0xFF)  — 23-byte footer, RuneLite ref   */
/* ------------------------------------------------------------------ */

static osrs_model_t *decode_type2(int model_id, const uint8_t *data,
                                  size_t len) {
  if (len < 23)
    return NULL;

  mem_stream_t s[10];
  for (int i = 0; i < 10; i++) {
    s[i].data = data;
    s[i].len = len;
    s[i].pos = 0;
  }

  /* Footer (23 bytes) */
  ms_seek(&s[0], len - 23);
  int vcount = ms_u16(&s[0]);
  int fcount = ms_u16(&s[0]);
  int num_tex = ms_u8(&s[0]);
  int has_frt = ms_u8(&s[0]);
  int has_prio = ms_u8(&s[0]);
  int has_alpha = ms_u8(&s[0]);
  int has_packedTransparency = ms_u8(&s[0]); /* var15 */
  int has_packedVertexGroups = ms_u8(&s[0]); /* var16 */
  int has_animaya = ms_u8(&s[0]);            /* var17 */
  int vgroup_count = ms_u16(&s[0]);
  int pri_count = ms_u16(&s[0]);
  int tex_count = ms_u16(&s[0]);
  int face_count2 = ms_u16(&s[0]);
  int unknown_count = ms_u16(&s[0]);

  /* ---- Section offsets (must match RuneLite decodeType2 exactly) ---- */
  size_t off = 0;
  size_t off_vx = off; /* vertex flags */
  off += (size_t)vcount;
  size_t off_fidx_type = off; /* face index types */
  off += (size_t)fcount;
  size_t off_prio = off; /* face priorities */
  if (has_prio == 255)
    off += (size_t)fcount;
  size_t off_packedTransparency = off; /* packedTransparency */
  if (has_packedTransparency == 1)
    off += (size_t)fcount;
  size_t off_frt = off; /* face render types */
  if (has_frt == 1)
    off += (size_t)fcount;
  size_t off_packedVertexGroups = off; /* packedVertexGroups + animaya */
  off += (size_t)unknown_count;
  size_t off_alpha = off; /* face alpha */
  if (has_alpha == 1)
    off += (size_t)fcount;
  size_t off_fidx = off; /* face index smart values */
  off += (size_t)face_count2;
  size_t off_color = off; /* face colors (u16) */
  off += (size_t)fcount * 2;
  size_t off_tex_tri = off; /* texture triangles */
  off += (size_t)num_tex * 6;
  size_t off_vx_data = off; /* vertex X deltas */
  off += (size_t)vgroup_count;
  size_t off_vy_data = off; /* vertex Y deltas */
  off += (size_t)pri_count;
  size_t off_vz_data = off; /* vertex Z deltas */
  off += (size_t)tex_count;
  (void)off;

  osrs_model_t *m = alloc_model(model_id);
  if (!m)
    return NULL;

  m->vertex_count = vcount;
  m->face_count = fcount;
  m->num_texture_faces = num_tex;
  m->global_priority = (has_prio != 255) ? (int8_t)has_prio : 0;

  m->vertex_x = calloc((size_t)vcount, sizeof(int));
  m->vertex_y = calloc((size_t)vcount, sizeof(int));
  m->vertex_z = calloc((size_t)vcount, sizeof(int));
  m->face_a = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_b = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_c = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_color = calloc((size_t)fcount, sizeof(uint16_t));

  if (!m->vertex_x || !m->vertex_y || !m->vertex_z || !m->face_a ||
      !m->face_b || !m->face_c || !m->face_color) {
    osrs_model_free(m);
    return NULL;
  }

  /* In Type 2, has_frt controls renderType + textureCoord + faceTexture */
  if (has_frt == 1) {
    m->face_render_type = calloc((size_t)fcount, sizeof(int8_t));
    m->texture_coord = calloc((size_t)fcount, sizeof(int8_t));
    m->face_texture = calloc((size_t)fcount, sizeof(int16_t));
    if (!m->face_render_type || !m->texture_coord || !m->face_texture) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_prio == 255) {
    m->face_priority = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_priority) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_alpha == 1) {
    m->face_transparency = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_transparency) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_packedVertexGroups == 1) {
    m->packed_vertex_groups = calloc((size_t)vcount, sizeof(int));
    if (!m->packed_vertex_groups) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (num_tex > 0) {
    m->tex_idx1 = calloc((size_t)num_tex, sizeof(uint16_t));
    m->tex_idx2 = calloc((size_t)num_tex, sizeof(uint16_t));
    m->tex_idx3 = calloc((size_t)num_tex, sizeof(uint16_t));
    if (!m->tex_idx1 || !m->tex_idx2 || !m->tex_idx3) {
      osrs_model_free(m);
      return NULL;
    }
  }

  /* ---- Vertices ---- */
  ms_seek(&s[0], off_vx);      /* vertex flags */
  ms_seek(&s[1], off_vx_data); /* X deltas */
  ms_seek(&s[2], off_vy_data); /* Y deltas */
  ms_seek(&s[3], off_vz_data); /* Z deltas */
  ms_seek(&s[4], off_packedVertexGroups);
  int vx = 0, vy = 0, vz = 0;
  for (int i = 0; i < vcount; i++) {
    int flags = ms_u8(&s[0]);
    int dx = 0, dy = 0, dz = 0;
    if (flags & 1)
      dx = ms_smart(&s[1]);
    if (flags & 2)
      dy = ms_smart(&s[2]);
    if (flags & 4)
      dz = ms_smart(&s[3]);
    m->vertex_x[i] = vx + dx;
    m->vertex_y[i] = vy + dy;
    m->vertex_z[i] = vz + dz;
    vx = m->vertex_x[i];
    vy = m->vertex_y[i];
    vz = m->vertex_z[i];
    if (has_packedVertexGroups == 1)
      m->packed_vertex_groups[i] = ms_u8(&s[4]);
    if (has_animaya == 1) {
      int count = ms_u8(&s[4]);
      for (int j = 0; j < count; j++) {
        (void)ms_u8(&s[4]);
        (void)ms_u8(&s[4]);
      }
    }
  }

  /* ---- Face colors and attributes ---- */
  ms_seek(&s[0], off_color);
  ms_seek(&s[1], off_frt);
  ms_seek(&s[2], off_prio);
  ms_seek(&s[3], off_alpha);
  ms_seek(&s[4], off_packedTransparency);
  for (int i = 0; i < fcount; i++) {
    m->face_color[i] = ms_u16(&s[0]);
    if (has_frt == 1) {
      int8_t b = ms_i8(&s[1]);
      if ((b & 1) == 1)
        m->face_render_type[i] = 1;
      else
        m->face_render_type[i] = 0;
      if ((b & 2) == 2) {
        m->texture_coord[i] = (int8_t)(b >> 2);
        m->face_texture[i] = (int16_t)m->face_color[i];
        m->face_color[i] = 127;
      } else {
        m->texture_coord[i] = -1;
        m->face_texture[i] = -1;
      }
    }
    if (has_prio == 255)
      m->face_priority[i] = ms_i8(&s[2]);
    if (has_alpha == 1)
      m->face_transparency[i] = ms_i8(&s[3]);
    if (has_packedTransparency == 1)
      (void)ms_u8(&s[4]);
  }

  /* ---- Face indices ---- */
  ms_seek(&s[0], off_fidx);      /* smart values */
  ms_seek(&s[1], off_fidx_type); /* types */
  int fa = 0, fb = 0, fc = 0, flast = 0;
  for (int i = 0; i < fcount; i++) {
    int type = ms_u8(&s[1]);
    if (type == 1) {
      fa = flast + ms_smart(&s[0]);
      fb = fa + ms_smart(&s[0]);
      fc = fb + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 2) {
      fb = fc;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 3) {
      fa = fc;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 4) {
      int tmp = fa;
      fa = fb;
      fb = tmp;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    }
    m->face_a[i] = (uint16_t)fa;
    m->face_b[i] = (uint16_t)fb;
    m->face_c[i] = (uint16_t)fc;
  }

  /* ---- Texture triangles ---- */
  if (num_tex > 0) {
    ms_seek(&s[0], off_tex_tri);
    for (int i = 0; i < num_tex; i++) {
      m->tex_idx1[i] = ms_u16(&s[0]);
      m->tex_idx2[i] = ms_u16(&s[0]);
      m->tex_idx3[i] = ms_u16(&s[0]);
    }
  }

  return m;
}

/* ------------------------------------------------------------------ */
/* Type-3 decoder (footer: 0xFD 0xFF)                                   */
/* ------------------------------------------------------------------ */

static osrs_model_t *decode_type3(int model_id, const uint8_t *data,
                                  size_t len) {
  if (len < 26)
    return NULL;

  mem_stream_t s[10];
  for (int i = 0; i < 10; i++) {
    s[i].data = data;
    s[i].len = len;
    s[i].pos = 0;
  }

  /* Footer (26 bytes) — type-3 has extra fields vs type-2 */
  ms_seek(&s[0], len - 26);
  int vcount = ms_u16(&s[0]);
  int fcount = ms_u16(&s[0]);
  int num_tex = ms_u8(&s[0]);
  int has_frt = ms_u8(&s[0]);
  int has_prio = ms_u8(&s[0]);
  int has_alpha = ms_u8(&s[0]);
  int has_pt = ms_u8(&s[0]);
  int has_faceTextures = ms_u8(&s[0]);
  int has_pvg = ms_u8(&s[0]);
  int has_animaya = ms_u8(&s[0]);
  int vgroup_count = ms_u16(&s[0]);
  int pri_count = ms_u16(&s[0]);
  int tex_count = ms_u16(&s[0]);
  int face_count2 = ms_u16(&s[0]);
  int unknown_count = ms_u16(&s[0]);
  int extra_count = ms_u16(&s[0]);

  /* ---- Section offsets (must match RuneLite decodeType3 exactly) ---- */
  size_t off = (size_t)num_tex;
  size_t off_vx = off; /* vertex flags */
  off += (size_t)vcount;
  size_t off_fidx_type = off; /* face index types */
  off += (size_t)fcount;
  size_t off_prio = off;
  if (has_prio == 255)
    off += (size_t)fcount;
  size_t off_pt = off;
  if (has_pt == 1)
    off += (size_t)fcount;
  size_t off_frt = off;
  if (has_frt == 1)
    off += (size_t)fcount;
  size_t off_pvg_animaya = off;
  off += (size_t)extra_count;
  size_t off_alpha = off;
  if (has_alpha == 1)
    off += (size_t)fcount;
  size_t off_fidx = off;
  off += (size_t)face_count2;
  size_t off_faceTextures = off;
  if (has_faceTextures == 1)
    off += (size_t)fcount * 2;
  size_t off_textureCoords = off;
  off += (size_t)unknown_count;
  size_t off_color = off;
  off += (size_t)fcount * 2;
  size_t off_vx_data = off;
  off += (size_t)vgroup_count;
  size_t off_vy_data = off;
  off += (size_t)pri_count;
  size_t off_vz_data = off;
  off += (size_t)tex_count;
  size_t off_tex_tri = off;
  (void)off_tex_tri;
  (void)off;

  osrs_model_t *m = alloc_model(model_id);
  if (!m)
    return NULL;

  m->vertex_count = vcount;
  m->face_count = fcount;
  m->num_texture_faces = num_tex;
  m->global_priority = (has_prio != 255) ? (int8_t)has_prio : 0;

  m->vertex_x = calloc((size_t)vcount, sizeof(int));
  m->vertex_y = calloc((size_t)vcount, sizeof(int));
  m->vertex_z = calloc((size_t)vcount, sizeof(int));
  m->face_a = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_b = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_c = calloc((size_t)fcount, sizeof(uint16_t));
  m->face_color = calloc((size_t)fcount, sizeof(uint16_t));

  if (!m->vertex_x || !m->vertex_y || !m->vertex_z || !m->face_a ||
      !m->face_b || !m->face_c || !m->face_color) {
    osrs_model_free(m);
    return NULL;
  }

  if (has_frt == 1) {
    m->face_render_type = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_render_type) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_prio == 255) {
    m->face_priority = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_priority) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_alpha == 1) {
    m->face_transparency = calloc((size_t)fcount, sizeof(int8_t));
    if (!m->face_transparency) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (has_faceTextures == 1) {
    m->face_texture = calloc((size_t)fcount, sizeof(int16_t));
    if (!m->face_texture) {
      osrs_model_free(m);
      return NULL;
    }
    if (num_tex > 0) {
      m->texture_coord = calloc((size_t)fcount, sizeof(int8_t));
      if (!m->texture_coord) {
        osrs_model_free(m);
        return NULL;
      }
    }
  }
  if (has_pvg == 1) {
    m->packed_vertex_groups = calloc((size_t)vcount, sizeof(int));
    if (!m->packed_vertex_groups) {
      osrs_model_free(m);
      return NULL;
    }
  }
  if (num_tex > 0) {
    m->tex_idx1 = calloc((size_t)num_tex, sizeof(uint16_t));
    m->tex_idx2 = calloc((size_t)num_tex, sizeof(uint16_t));
    m->tex_idx3 = calloc((size_t)num_tex, sizeof(uint16_t));
    if (!m->tex_idx1 || !m->tex_idx2 || !m->tex_idx3) {
      osrs_model_free(m);
      return NULL;
    }
  }

  /* ---- Vertices ---- */
  ms_seek(&s[0], off_vx);          /* vertex flags */
  ms_seek(&s[1], off_vx_data);     /* X deltas */
  ms_seek(&s[2], off_vy_data);     /* Y deltas */
  ms_seek(&s[3], off_vz_data);     /* Z deltas */
  ms_seek(&s[4], off_pvg_animaya); /* packedVertexGroups / animaya */
  int vx = 0, vy = 0, vz = 0;
  for (int i = 0; i < vcount; i++) {
    int flags = ms_u8(&s[0]);
    int dx = 0, dy = 0, dz = 0;
    if (flags & 1)
      dx = ms_smart(&s[1]);
    if (flags & 2)
      dy = ms_smart(&s[2]);
    if (flags & 4)
      dz = ms_smart(&s[3]);
    m->vertex_x[i] = vx + dx;
    m->vertex_y[i] = vy + dy;
    m->vertex_z[i] = vz + dz;
    vx = m->vertex_x[i];
    vy = m->vertex_y[i];
    vz = m->vertex_z[i];
    if (has_pvg == 1)
      m->packed_vertex_groups[i] = ms_u8(&s[4]);
  }

  /* Animaya groups — skipped (not stored in C struct) */
  if (has_animaya == 1) {
    for (int i = 0; i < vcount; i++) {
      int count = ms_u8(&s[4]);
      for (int j = 0; j < count; j++) {
        (void)ms_u8(&s[4]);
        (void)ms_u8(&s[4]);
      }
    }
  }

  /* ---- Face colors and attributes ---- */
  ms_seek(&s[0], off_color);
  ms_seek(&s[1], off_frt);
  ms_seek(&s[2], off_prio);
  ms_seek(&s[3], off_alpha);
  ms_seek(&s[4], off_pt);
  ms_seek(&s[5], off_faceTextures);
  ms_seek(&s[6], off_textureCoords);
  for (int i = 0; i < fcount; i++) {
    m->face_color[i] = ms_u16(&s[0]);
    if (has_frt == 1)
      m->face_render_type[i] = ms_i8(&s[1]);
    if (has_prio == 255)
      m->face_priority[i] = ms_i8(&s[2]);
    if (has_alpha == 1)
      m->face_transparency[i] = ms_i8(&s[3]);
    if (has_pt == 1)
      (void)ms_u8(&s[4]);
    if (has_faceTextures == 1) {
      m->face_texture[i] = (int16_t)(ms_u16(&s[5]) - 1);
      if (m->texture_coord && m->face_texture[i] != -1)
        m->texture_coord[i] = (int8_t)(ms_u8(&s[6]) - 1);
    }
  }

  /* ---- Face indices ---- */
  ms_seek(&s[0], off_fidx);      /* smart values */
  ms_seek(&s[1], off_fidx_type); /* types */
  int fa = 0, fb = 0, fc = 0, flast = 0;
  for (int i = 0; i < fcount; i++) {
    int type = ms_u8(&s[1]);
    if (type == 1) {
      fa = flast + ms_smart(&s[0]);
      fb = fa + ms_smart(&s[0]);
      fc = fb + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 2) {
      fb = fc;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 3) {
      fa = fc;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    } else if (type == 4) {
      int tmp = fa;
      fa = fb;
      fb = tmp;
      fc = flast + ms_smart(&s[0]);
      flast = fc;
    }
    m->face_a[i] = (uint16_t)fa;
    m->face_b[i] = (uint16_t)fb;
    m->face_c[i] = (uint16_t)fc;
  }

  /* ---- Texture triangles (simplified: assume all type 0) ---- */
  if (num_tex > 0) {
    ms_seek(&s[0], off_tex_tri);
    for (int i = 0; i < num_tex; i++) {
      m->tex_idx1[i] = ms_u16(&s[0]);
      m->tex_idx2[i] = ms_u16(&s[0]);
      m->tex_idx3[i] = ms_u16(&s[0]);
    }
  }

  return m;
}

/* ------------------------------------------------------------------ */
/* Old format decoder                                                   */
/* ------------------------------------------------------------------ */

static osrs_model_t *decode_old_format(int model_id, const uint8_t *data,
                                       size_t len) {
  (void)len;
  /* Old format is extremely rare; return NULL for now */
  (void)model_id;
  (void)data;
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

osrs_model_t *osrs_model_load(int model_id, const uint8_t *data, size_t len) {
  if (!data || len < 2)
    return NULL;

  uint8_t fb1 = data[len - 2];
  uint8_t fb2 = data[len - 1];

  if (fb2 == 0xFD && fb1 == 0xFF) {
    return decode_type3(model_id, data, len);
  } else if (fb2 == 0xFE && fb1 == 0xFF) {
    return decode_type2(model_id, data, len);
  } else if (fb2 == 0xFF && fb1 == 0xFF) {
    return decode_type1(model_id, data, len);
  } else {
    return decode_old_format(model_id, data, len);
  }
}

void osrs_model_free(osrs_model_t *model) {
  if (!model)
    return;
  free(model->vertex_x);
  free(model->vertex_y);
  free(model->vertex_z);
  free(model->face_a);
  free(model->face_b);
  free(model->face_c);
  free(model->face_color);
  free(model->face_render_type);
  free(model->face_priority);
  free(model->face_transparency);
  free(model->face_texture);
  free(model->texture_coord);
  free(model->tex_idx1);
  free(model->tex_idx2);
  free(model->tex_idx3);
  free(model->face_u);
  free(model->face_v);
  free(model->packed_vertex_groups);
  free(model->normal_x);
  free(model->normal_y);
  free(model->normal_z);
  free(model->normal_mag);
  free(model->face_normal_x);
  free(model->face_normal_y);
  free(model->face_normal_z);
  free(model);
}

/* ------------------------------------------------------------------ */
/* Normal computation                                                   */
/* ------------------------------------------------------------------ */

void osrs_model_compute_normals(osrs_model_t *model) {
  if (!model || model->normals_computed)
    return;

  int vc = model->vertex_count;
  int fc = model->face_count;

  model->normal_x = calloc((size_t)vc, sizeof(int));
  model->normal_y = calloc((size_t)vc, sizeof(int));
  model->normal_z = calloc((size_t)vc, sizeof(int));
  model->normal_mag = calloc((size_t)vc, sizeof(int));

  if (!model->normal_x || !model->normal_y || !model->normal_z ||
      !model->normal_mag) {
    free(model->normal_x);
    free(model->normal_y);
    free(model->normal_z);
    free(model->normal_mag);
    model->normal_x = model->normal_y = model->normal_z = model->normal_mag =
        NULL;
    return;
  }

  for (int i = 0; i < fc; i++) {
    int a = model->face_a[i];
    int b = model->face_b[i];
    int c = model->face_c[i];
    if (a >= model->vertex_count || b >= model->vertex_count ||
        c >= model->vertex_count)
      continue;

    int x1 = model->vertex_x[b] - model->vertex_x[a];
    int y1 = model->vertex_y[b] - model->vertex_y[a];
    int z1 = model->vertex_z[b] - model->vertex_z[a];

    int x2 = model->vertex_x[c] - model->vertex_x[a];
    int y2 = model->vertex_y[c] - model->vertex_y[a];
    int z2 = model->vertex_z[c] - model->vertex_z[a];

    int nx = y1 * z2 - y2 * z1;
    int ny = z1 * x2 - z2 * x1;
    int nz = x1 * y2 - x2 * y1;

    while (nx > 8192 || ny > 8192 || nz > 8192 || nx < -8192 || ny < -8192 ||
           nz < -8192) {
      nx >>= 1;
      ny >>= 1;
      nz >>= 1;
    }

    int len = (int)sqrt((double)(nx * nx + ny * ny + nz * nz));
    if (len <= 0)
      len = 1;

    nx = nx * 256 / len;
    ny = ny * 256 / len;
    nz = nz * 256 / len;

    int8_t rt = model->face_render_type ? model->face_render_type[i] : 0;

    if (rt == 0) {
      model->normal_x[a] += nx;
      model->normal_y[a] += ny;
      model->normal_z[a] += nz;
      model->normal_mag[a]++;

      model->normal_x[b] += nx;
      model->normal_y[b] += ny;
      model->normal_z[b] += nz;
      model->normal_mag[b]++;

      model->normal_x[c] += nx;
      model->normal_y[c] += ny;
      model->normal_z[c] += nz;
      model->normal_mag[c]++;
    } else if (rt == 1) {
      if (!model->face_normal_x) {
        model->face_normal_x = calloc((size_t)fc, sizeof(int));
        model->face_normal_y = calloc((size_t)fc, sizeof(int));
        model->face_normal_z = calloc((size_t)fc, sizeof(int));
        if (!model->face_normal_x || !model->face_normal_y ||
            !model->face_normal_z) {
          free(model->face_normal_x);
          free(model->face_normal_y);
          free(model->face_normal_z);
          model->face_normal_x = model->face_normal_y = model->face_normal_z =
              NULL;
          continue;
        }
      }
      model->face_normal_x[i] = nx;
      model->face_normal_y[i] = ny;
      model->face_normal_z[i] = nz;
    }
  }

  /* Normalize per-vertex normals to length 256 and store the actual
   * vector magnitude (RuneLite: VertexNormal.x = nx*256/magnitude). */
  for (int v = 0; v < vc; v++) {
    int nx = model->normal_x[v];
    int ny = model->normal_y[v];
    int nz = model->normal_z[v];
    int mag = (int)sqrt((double)(nx * nx + ny * ny + nz * nz));
    if (mag > 0) {
      model->normal_x[v] = nx * 256 / mag;
      model->normal_y[v] = ny * 256 / mag;
      model->normal_z[v] = nz * 256 / mag;
      model->normal_mag[v] = mag;
    } else {
      model->normal_x[v] = 0;
      model->normal_y[v] = 0;
      model->normal_z[v] = 0;
      model->normal_mag[v] = 1;
    }
  }

  model->normals_computed = true;
}

/* ------------------------------------------------------------------ */
/* Texture UV computation                                               */
/* ------------------------------------------------------------------ */

void osrs_model_compute_uvs(osrs_model_t *model) {
  if (!model || model->face_u || model->face_v)
    return;

  int fc = model->face_count;
  model->face_u = calloc((size_t)fc, sizeof(float[3]));
  model->face_v = calloc((size_t)fc, sizeof(float[3]));
  if (!model->face_u || !model->face_v)
    return;

  if (!model->face_texture)
    return;

  for (int i = 0; i < fc; i++) {
    if (model->face_texture[i] == -1)
      continue;

    float u0, u1, u2, v0, v1, v2;

    if (model->texture_coord && model->texture_coord[i] != -1) {
      int tface = model->texture_coord[i] & 0xFF;
      if (tface < 0 || tface >= model->num_texture_faces)
        continue;
      int ta = model->face_a[i];
      int tb = model->face_b[i];
      int tc = model->face_c[i];
      if (ta >= model->vertex_count || tb >= model->vertex_count ||
          tc >= model->vertex_count)
        continue;

      int texA = model->tex_idx1[tface];
      int texB = model->tex_idx2[tface];
      int texC = model->tex_idx3[tface];
      if (texA < 0 || texA >= model->vertex_count || texB < 0 ||
          texB >= model->vertex_count || texC < 0 ||
          texC >= model->vertex_count)
        continue;

      float v1x = (float)model->vertex_x[texA];
      float v1y = (float)model->vertex_y[texA];
      float v1z = (float)model->vertex_z[texA];

      float v2x = (float)model->vertex_x[texB] - v1x;
      float v2y = (float)model->vertex_y[texB] - v1y;
      float v2z = (float)model->vertex_z[texB] - v1z;

      float v3x = (float)model->vertex_x[texC] - v1x;
      float v3y = (float)model->vertex_y[texC] - v1y;
      float v3z = (float)model->vertex_z[texC] - v1z;

      float v4x = (float)model->vertex_x[ta] - v1x;
      float v4y = (float)model->vertex_y[ta] - v1y;
      float v4z = (float)model->vertex_z[ta] - v1z;

      float v5x = (float)model->vertex_x[tb] - v1x;
      float v5y = (float)model->vertex_y[tb] - v1y;
      float v5z = (float)model->vertex_z[tb] - v1z;

      float v6x = (float)model->vertex_x[tc] - v1x;
      float v6y = (float)model->vertex_y[tc] - v1y;
      float v6z = (float)model->vertex_z[tc] - v1z;

      float v7x = v2y * v3z - v2z * v3y;
      float v7y = v2z * v3x - v2x * v3z;
      float v7z = v2x * v3y - v2y * v3x;

      float v8x = v3y * v7z - v3z * v7y;
      float v8y = v3z * v7x - v3x * v7z;
      float v8z = v3x * v7y - v3y * v7x;

      float f = 1.0f / (v8x * v2x + v8y * v2y + v8z * v2z);

      u0 = (v8x * v4x + v8y * v4y + v8z * v4z) * f;
      u1 = (v8x * v5x + v8y * v5y + v8z * v5z) * f;
      u2 = (v8x * v6x + v8y * v6y + v8z * v6z) * f;

      v8x = v2y * v7z - v2z * v7y;
      v8y = v2z * v7x - v2x * v7z;
      v8z = v2x * v7y - v2y * v7x;

      f = 1.0f / (v8x * v3x + v8y * v3y + v8z * v3z);

      v0 = (v8x * v4x + v8y * v4y + v8z * v4z) * f;
      v1 = (v8x * v5x + v8y * v5y + v8z * v5z) * f;
      v2 = (v8x * v6x + v8y * v6y + v8z * v6z) * f;
    } else {
      u0 = 0.0f;
      v0 = 0.0f;
      u1 = 1.0f;
      v1 = 0.0f;
      u2 = 0.0f;
      v2 = 1.0f;
    }

    model->face_u[i][0] = u0;
    model->face_u[i][1] = u1;
    model->face_u[i][2] = u2;
    model->face_v[i][0] = v0;
    model->face_v[i][1] = v1;
    model->face_v[i][2] = v2;
  }
}

/* ------------------------------------------------------------------ */
/* Recolor                                                              */
/* ------------------------------------------------------------------ */

void osrs_model_recolor(osrs_model_t *model, int src, int dst) {
  if (!model || !model->face_color)
    return;
  for (int i = 0; i < model->face_count; i++) {
    if (model->face_color[i] == (uint16_t)src)
      model->face_color[i] = (uint16_t)dst;
  }
}

/* ------------------------------------------------------------------ */
/* Lighting                                                             */
/* ------------------------------------------------------------------ */

uint32_t osrs_model_light_vertex(const osrs_model_t *model, int face,
                                 int corner, int ambient, int contrast) {
  /* OSRS scene light direction (ItemSpriteFactory.light: -50, -10, -50) */
  const int LX = -50, LY = -10, LZ = -50;
  static const double LIGHT_MAG =
      71.41369429002447; /* sqrt(50^2 + 10^2 + 50^2) */

  if (!model || face < 0 || face >= model->face_count)
    return 0xFFFFFFFF;

  int var7 = (int)(LIGHT_MAG * (double)contrast) >> 8;
  if (var7 < 1)
    var7 = 1;

  int rtype = model->face_render_type ? model->face_render_type[face] : 0;
  int shade;

  if (rtype == 1 && model->face_normal_x) {
    /* Flat face: use the face normal (256-length), divisor var7*3/2 */
    int fnx = model->face_normal_x[face];
    int fny = model->face_normal_y[face];
    int fnz = model->face_normal_z[face];
    int div = var7 / 2 + var7;
    shade = (LY * fny + LZ * fnz + LX * fnx) / div + ambient;
  } else if (rtype == 0 && model->normal_x) {
    /* Gouraud: use per-vertex normal */
    int v;
    if (corner == 0)
      v = model->face_a[face];
    else if (corner == 1)
      v = model->face_b[face];
    else
      v = model->face_c[face];
    if (v >= model->vertex_count)
      return osrs_hsl_shade_to_rgb(model->face_color[face], ambient);
    int nx = model->normal_x[v];
    int ny = model->normal_y[v];
    int nz = model->normal_z[v];
    int mag = model->normal_mag[v];
    if (mag < 1)
      mag = 1;
    shade = (LY * ny + LZ * nz + LX * nx) / (var7 * mag) + ambient;
  } else {
    shade = ambient;
  }

  return osrs_hsl_shade_to_rgb(model->face_color[face], shade);
}

/* ------------------------------------------------------------------ */
/* Merge multiple models into one                                       */
/* ------------------------------------------------------------------ */

osrs_model_t *osrs_model_merge(osrs_model_t **parts, int count) {
  if (!parts || count <= 0)
    return NULL;

  int total_v = 0, total_f = 0;
  for (int i = 0; i < count; i++) {
    if (!parts[i])
      continue;
    total_v += parts[i]->vertex_count;
    total_f += parts[i]->face_count;
  }

  if (total_v == 0 || total_f == 0)
    return NULL;

  osrs_model_t *m = alloc_model(0);
  if (!m)
    return NULL;

  m->vertex_count = total_v;
  m->face_count = total_f;

  m->vertex_x = calloc((size_t)total_v, sizeof(int));
  m->vertex_y = calloc((size_t)total_v, sizeof(int));
  m->vertex_z = calloc((size_t)total_v, sizeof(int));
  m->face_a = calloc((size_t)total_f, sizeof(uint16_t));
  m->face_b = calloc((size_t)total_f, sizeof(uint16_t));
  m->face_c = calloc((size_t)total_f, sizeof(uint16_t));
  m->face_color = calloc((size_t)total_f, sizeof(uint16_t));

  /* Determine if merged model needs optional per-vertex / per-face arrays. */
  bool need_groups = false, need_frt = false, need_fp = false;
  bool need_ft = false, need_ftrans = false, need_txc = false;
  for (int i = 0; i < count; i++) {
    osrs_model_t *p = parts[i];
    if (!p)
      continue;
    if (p->packed_vertex_groups)
      need_groups = true;
    if (p->face_render_type)
      need_frt = true;
    if (p->face_priority)
      need_fp = true;
    if (p->face_transparency)
      need_ftrans = true;
    if (p->face_texture)
      need_ft = true;
    if (p->texture_coord)
      need_txc = true;
  }

  if (need_groups)
    m->packed_vertex_groups = calloc((size_t)total_v, sizeof(int));
  if (need_frt)
    m->face_render_type = calloc((size_t)total_f, sizeof(int8_t));
  if (need_fp)
    m->face_priority = calloc((size_t)total_f, sizeof(int8_t));
  if (need_ftrans)
    m->face_transparency = calloc((size_t)total_f, sizeof(int8_t));
  if (need_ft)
    m->face_texture = calloc((size_t)total_f, sizeof(int16_t));
  if (need_txc)
    m->texture_coord = calloc((size_t)total_f, sizeof(int8_t));

  if (!m->vertex_x || !m->vertex_y || !m->vertex_z || !m->face_a ||
      !m->face_b || !m->face_c || !m->face_color ||
      (need_groups && !m->packed_vertex_groups) ||
      (need_frt && !m->face_render_type) || (need_fp && !m->face_priority) ||
      (need_ftrans && !m->face_transparency) || (need_ft && !m->face_texture) ||
      (need_txc && !m->texture_coord)) {
    osrs_model_free(m);
    return NULL;
  }

  int voff = 0, foff = 0;
  int max_group = 0;
  for (int i = 0; i < count; i++) {
    osrs_model_t *p = parts[i];
    if (!p)
      continue;
    if (p->packed_vertex_groups) {
      for (int v = 0; v < p->vertex_count; v++) {
        if (p->packed_vertex_groups[v] > max_group)
          max_group = p->packed_vertex_groups[v];
      }
    }
  }
  m->vertex_group_max = max_group + 1;

  for (int i = 0; i < count; i++) {
    osrs_model_t *p = parts[i];
    if (!p)
      continue;

    for (int v = 0; v < p->vertex_count; v++) {
      m->vertex_x[voff + v] = p->vertex_x[v];
      m->vertex_y[voff + v] = p->vertex_y[v];
      m->vertex_z[voff + v] = p->vertex_z[v];
      if (m->packed_vertex_groups && p->packed_vertex_groups)
        m->packed_vertex_groups[voff + v] = p->packed_vertex_groups[v];
    }

    for (int f = 0; f < p->face_count; f++) {
      m->face_a[foff + f] = (uint16_t)(voff + p->face_a[f]);
      m->face_b[foff + f] = (uint16_t)(voff + p->face_b[f]);
      m->face_c[foff + f] = (uint16_t)(voff + p->face_c[f]);
      m->face_color[foff + f] = p->face_color[f];
      if (m->face_render_type)
        m->face_render_type[foff + f] =
            p->face_render_type ? p->face_render_type[f] : 0;
      if (m->face_priority)
        m->face_priority[foff + f] =
            p->face_priority ? p->face_priority[f] : p->global_priority;
      if (m->face_transparency)
        m->face_transparency[foff + f] =
            p->face_transparency ? p->face_transparency[f] : 0;
      if (m->face_texture)
        m->face_texture[foff + f] = p->face_texture ? p->face_texture[f] : -1;
      if (m->texture_coord)
        m->texture_coord[foff + f] =
            p->texture_coord ? p->texture_coord[f] : -1;
    }

    voff += p->vertex_count;
    foff += p->face_count;
  }

  return m;
}

/* ------------------------------------------------------------------ */
/* Deep copy                                                            */
/* ------------------------------------------------------------------ */

osrs_model_t *osrs_model_copy(const osrs_model_t *src) {
  if (!src)
    return NULL;

  osrs_model_t *m = alloc_model(src->id);
  if (!m)
    return NULL;

  m->vertex_count = src->vertex_count;
  m->face_count = src->face_count;
  m->num_texture_faces = src->num_texture_faces;
  m->global_priority = src->global_priority;
  m->normals_computed = src->normals_computed;

#define COPY_ARR(FIELD, TYPE, COUNT)                                           \
  do {                                                                         \
    if (src->FIELD && (COUNT) > 0) {                                           \
      m->FIELD = calloc((size_t)(COUNT), sizeof(TYPE));                        \
      if (m->FIELD)                                                            \
        memcpy(m->FIELD, src->FIELD, (size_t)(COUNT) * sizeof(TYPE));          \
    }                                                                          \
  } while (0)

  COPY_ARR(vertex_x, int, src->vertex_count);
  COPY_ARR(vertex_y, int, src->vertex_count);
  COPY_ARR(vertex_z, int, src->vertex_count);
  COPY_ARR(face_a, uint16_t, src->face_count);
  COPY_ARR(face_b, uint16_t, src->face_count);
  COPY_ARR(face_c, uint16_t, src->face_count);
  COPY_ARR(face_color, uint16_t, src->face_count);
  COPY_ARR(face_render_type, int8_t, src->face_count);
  COPY_ARR(face_priority, int8_t, src->face_count);
  COPY_ARR(face_transparency, int8_t, src->face_count);
  COPY_ARR(face_texture, int16_t, src->face_count);
  COPY_ARR(texture_coord, int8_t, src->face_count);
  COPY_ARR(tex_idx1, uint16_t, src->num_texture_faces);
  COPY_ARR(tex_idx2, uint16_t, src->num_texture_faces);
  COPY_ARR(tex_idx3, uint16_t, src->num_texture_faces);
  COPY_ARR(packed_vertex_groups, int, src->vertex_count);
  COPY_ARR(normal_x, int, src->vertex_count);
  COPY_ARR(normal_y, int, src->vertex_count);
  COPY_ARR(normal_z, int, src->vertex_count);
  COPY_ARR(normal_mag, int, src->vertex_count);
  COPY_ARR(face_normal_x, int, src->face_count);
  COPY_ARR(face_normal_y, int, src->face_count);
  COPY_ARR(face_normal_z, int, src->face_count);

#undef COPY_ARR

  return m;
}

/* ------------------------------------------------------------------ */
/* Translate                                                            */
/* ------------------------------------------------------------------ */

void osrs_model_translate(osrs_model_t *model, int tx, int ty, int tz) {
  if (!model)
    return;
  for (int i = 0; i < model->vertex_count; i++) {
    model->vertex_x[i] += tx;
    model->vertex_y[i] += ty;
    model->vertex_z[i] += tz;
  }
}

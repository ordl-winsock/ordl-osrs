/*
 * osrs/anim.c - OSRS skeletal animation loaders + per-frame vertex transform.
 */

#include "osrs/anim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Cache indexes (RuneLite IndexType). */
#define OSRS_IDX_ANIMATIONS 0 /* frames */
#define OSRS_IDX_SKELETONS 1  /* framemaps */

/* CircularAngle: 2048-entry fixed-point sin/cos (65536 = 1.0). */
static int anim_sin[2048];
static int anim_cos[2048];
static bool anim_tables_ready = false;

static void anim_init_tables(void) {
  if (anim_tables_ready)
    return;
  for (int i = 0; i < 2048; i++) {
    anim_sin[i] = (int)(65536.0 * sin((double)i * M_PI / 1024.0));
    anim_cos[i] = (int)(65536.0 * cos((double)i * M_PI / 1024.0));
  }
  anim_tables_ready = true;
}

/* Stream helper. */
typedef struct {
  const uint8_t *data;
  size_t len;
  size_t pos;
} astream_t;

static void as_init(astream_t *s, const uint8_t *data, size_t len) {
  s->data = data;
  s->len = len;
  s->pos = 0;
}
static uint8_t as_u8(astream_t *s) {
  return (s->pos < s->len) ? s->data[s->pos++] : 0;
}
static uint16_t as_u16(astream_t *s) {
  return (uint16_t)((as_u8(s) << 8) | as_u8(s));
}
/* Signed smart (matches RuneLite InputStream.readShortSmart: b<128 -> b-64,
 * else (b<<8|next) - 0xC000, i.e. ((b-128)<<8|next) - 16384). */
static int as_smart_s(astream_t *s) {
  uint8_t b = as_u8(s);
  if (b < 128)
    return (int)b - 64;
  return (int)(((uint16_t)((b - 128) << 8) | as_u8(s))) - 16384;
}

/* Framemap --------------------------------------------------------------- */

osrs_framemap_t *osrs_framemap_load(osrs_cache_t *cache, int archive_id) {
  if (!cache || archive_id < 0)
    return NULL;
  osrs_archive_files_t *files = osrs_cache_read_archive_files(
      cache, OSRS_IDX_SKELETONS, archive_id, NULL);
  if (!files || files->count < 1) {
    if (files)
      osrs_archive_files_free(files);
    return NULL;
  }
  const uint8_t *data = files->files[0].data;
  size_t len = files->files[0].len;

  osrs_framemap_t *fm = calloc(1, sizeof(*fm));
  if (!fm) {
    osrs_archive_files_free(files);
    return NULL;
  }
  fm->id = archive_id;

  astream_t s;
  as_init(&s, data, len);
  fm->length = as_u8(&s);
  if (fm->length <= 0 || fm->length > 500) {
    free(fm);
    osrs_archive_files_free(files);
    return NULL;
  }
  fm->types = calloc((size_t)fm->length, sizeof(int));
  fm->frame_maps = calloc((size_t)fm->length, sizeof(int *));
  fm->frame_map_counts = calloc((size_t)fm->length, sizeof(int));
  if (!fm->types || !fm->frame_maps || !fm->frame_map_counts) {
    osrs_framemap_free(fm);
    osrs_archive_files_free(files);
    return NULL;
  }
  /* RuneLite FramemapLoader layout: length, then ALL types, then ALL counts,
   * then ALL labels (in bone order). Not interleaved count+labels per bone. */
  for (int i = 0; i < fm->length; i++)
    fm->types[i] = as_u8(&s);
  for (int i = 0; i < fm->length; i++) {
    int n = as_u8(&s);
    fm->frame_map_counts[i] = n;
    fm->frame_maps[i] = calloc((size_t)(n > 0 ? n : 1), sizeof(int));
    if (!fm->frame_maps[i]) {
      osrs_framemap_free(fm);
      osrs_archive_files_free(files);
      return NULL;
    }
  }
  for (int i = 0; i < fm->length; i++) {
    for (int j = 0; j < fm->frame_map_counts[i]; j++)
      fm->frame_maps[i][j] = as_u8(&s);
  }

  osrs_archive_files_free(files);
  return fm;
}

void osrs_framemap_free(osrs_framemap_t *fm) {
  if (!fm)
    return;
  if (fm->frame_maps) {
    for (int i = 0; i < fm->length; i++)
      free(fm->frame_maps[i]);
  }
  free(fm->frame_maps);
  free(fm->frame_map_counts);
  free(fm->types);
  free(fm);
}

/* Frame ------------------------------------------------------------------ */

osrs_frame_t *osrs_frame_load(osrs_cache_t *cache, int archive_id, int file_id,
                              osrs_framemap_t **out_framemap) {
  if (out_framemap)
    *out_framemap = NULL;
  if (!cache || archive_id < 0 || file_id < 0)
    return NULL;

  size_t flen = 0;
  uint8_t *data = osrs_cache_read_archive_file(cache, OSRS_IDX_ANIMATIONS,
                                               archive_id, file_id, &flen);
  if (!data || flen < 4) {
    free(data);
    return NULL;
  }

  /* First u16 = framemap archive id (SKELETONS). */
  int framemap_id = (data[0] << 8) | data[1];
  osrs_framemap_t *fm = osrs_framemap_load(cache, framemap_id);
  if (!fm) {
    free(data);
    return NULL;
  }

  osrs_frame_t *f = calloc(1, sizeof(*f));
  if (!f) {
    osrs_framemap_free(fm);
    free(data);
    return NULL;
  }
  f->id = file_id;
  f->framemap_id = framemap_id;

  astream_t in, ds;
  as_init(&in, data, flen);
  as_init(&ds, data, flen);

  (void)as_u16(&in);       /* framemapArchiveIndex */
  int length = as_u8(&in); /* group flag bytes follow */
  ds.pos = 3 + (size_t)length;

  int idx_ids[500], tx[500], ty[500], tz[500];
  int last_i = -1, index = 0;
  for (int i = 0; i < length && index < 500; i++) {
    int var9 = as_u8(&in);
    if (var9 <= 0)
      continue;

    if (i < fm->length && fm->types[i] != 0) {
      for (int v10 = i - 1; v10 > last_i; v10--) {
        if (v10 >= 0 && v10 < fm->length && fm->types[v10] == 0) {
          idx_ids[index] = v10;
          tx[index] = 0;
          ty[index] = 0;
          tz[index] = 0;
          index++;
          break;
        }
      }
    }

    idx_ids[index] = i;
    int var11 = (i < fm->length && fm->types[i] == 3) ? 128 : 0;
    tx[index] = (var9 & 1) ? as_smart_s(&ds) : var11;
    ty[index] = (var9 & 2) ? as_smart_s(&ds) : var11;
    tz[index] = (var9 & 4) ? as_smart_s(&ds) : var11;
    last_i = i;
    index++;
  }

  f->translator_count = index;
  f->index_frame_ids = calloc((size_t)index, sizeof(int));
  f->translator_x = calloc((size_t)index, sizeof(int));
  f->translator_y = calloc((size_t)index, sizeof(int));
  f->translator_z = calloc((size_t)index, sizeof(int));
  for (int i = 0; i < index; i++) {
    f->index_frame_ids[i] = idx_ids[i];
    f->translator_x[i] = tx[i];
    f->translator_y[i] = ty[i];
    f->translator_z[i] = tz[i];
  }

  free(data);
  if (out_framemap)
    *out_framemap = fm;
  else
    osrs_framemap_free(fm);
  return f;
}

void osrs_frame_free(osrs_frame_t *f) {
  if (!f)
    return;
  free(f->index_frame_ids);
  free(f->translator_x);
  free(f->translator_y);
  free(f->translator_z);
  free(f);
}

/* Frame application ------------------------------------------------------ */

void osrs_model_apply_frame(osrs_model_t *m, const osrs_framemap_t *fm,
                            const osrs_frame_t *frame) {
  if (!m || !fm || !frame || !m->packed_vertex_groups)
    return;
  anim_init_tables();

  int anim_ox = 0, anim_oy = 0, anim_oz = 0;
  int vc = m->vertex_count;

  for (int t = 0; t < frame->translator_count; t++) {
    int gid = frame->index_frame_ids[t];
    if (gid < 0 || gid >= fm->length)
      continue;
    int type = fm->types[gid];
    int nlabels = fm->frame_map_counts[gid];
    const int *labels = fm->frame_maps[gid];
    int dx = frame->translator_x[t];
    int dy = frame->translator_y[t];
    int dz = frame->translator_z[t];

    if (type == 0) {
      /* ORIGIN: centroid of affected vertices + (dx,dy,dz) */
      long cx = 0, cy = 0, cz = 0;
      int count = 0;
      for (int li = 0; li < nlabels; li++) {
        int label = labels[li];
        for (int v = 0; v < vc; v++) {
          if (m->packed_vertex_groups[v] == label) {
            cx += m->vertex_x[v];
            cy += m->vertex_y[v];
            cz += m->vertex_z[v];
            count++;
          }
        }
      }
      if (count > 0) {
        anim_ox = dx + (int)(cx / count);
        anim_oy = dy + (int)(cy / count);
        anim_oz = dz + (int)(cz / count);
      } else {
        anim_ox = dx;
        anim_oy = dy;
        anim_oz = dz;
      }
    } else if (type == 1) {
      /* TRANSLATE */
      for (int li = 0; li < nlabels; li++) {
        int label = labels[li];
        for (int v = 0; v < vc; v++) {
          if (m->packed_vertex_groups[v] == label) {
            m->vertex_x[v] += dx;
            m->vertex_y[v] += dy;
            m->vertex_z[v] += dz;
          }
        }
      }
    } else if (type == 2) {
      /* ROTATE about animOffset (roll/pitch/yaw, CircularAngle) */
      int ax = (dx & 255) * 8, ay = (dy & 255) * 8, az = (dz & 255) * 8;
      for (int li = 0; li < nlabels; li++) {
        int label = labels[li];
        for (int v = 0; v < vc; v++) {
          if (m->packed_vertex_groups[v] != label)
            continue;
          int vx = m->vertex_x[v] - anim_ox;
          int vy = m->vertex_y[v] - anim_oy;
          int vz = m->vertex_z[v] - anim_oz;
          if (az != 0) { /* roll (Z) */
            int sn = anim_sin[az], cs = anim_cos[az];
            int nx = (sn * vy + cs * vx) >> 16;
            vy = (cs * vy - sn * vx) >> 16;
            vx = nx;
          }
          if (ax != 0) { /* pitch (X) */
            int sn = anim_sin[ax], cs = anim_cos[ax];
            int ny = (cs * vy - sn * vz) >> 16;
            vz = (sn * vy + cs * vz) >> 16;
            vy = ny;
          }
          if (ay != 0) { /* yaw (Y) */
            int sn = anim_sin[ay], cs = anim_cos[ay];
            int nx = (sn * vz + cs * vx) >> 16;
            vz = (cs * vz - sn * vx) >> 16;
            vx = nx;
          }
          m->vertex_x[v] = vx + anim_ox;
          m->vertex_y[v] = vy + anim_oy;
          m->vertex_z[v] = vz + anim_oz;
        }
      }
    } else if (type == 3) {
      /* SCALE about animOffset (128 = 1.0) */
      for (int li = 0; li < nlabels; li++) {
        int label = labels[li];
        for (int v = 0; v < vc; v++) {
          if (m->packed_vertex_groups[v] != label)
            continue;
          int vx = m->vertex_x[v] - anim_ox;
          int vy = m->vertex_y[v] - anim_oy;
          int vz = m->vertex_z[v] - anim_oz;
          vx = dx * vx / 128;
          vy = dy * vy / 128;
          vz = dz * vz / 128;
          m->vertex_x[v] = vx + anim_ox;
          m->vertex_y[v] = vy + anim_oy;
          m->vertex_z[v] = vz + anim_oz;
        }
      }
    }
    /* type 5 (alpha): no vertex transform */
  }

  /* Vertices changed: normals must be recomputed before lighting. */
  m->normals_computed = false;
}

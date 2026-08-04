/*
 * osrs/item_sprite.c — Software renderer for OSRS inventory item sprites.
 *
 * Replicates the official client's item sprite pipeline
 * (ItemSpriteFactory + Model.projectAndDraw + Graphics3D):
 *   1. Load item def -> inventory model, apply recolors, light it
 *      (ambient+64, contrast+768, light dir -50,-10,-50).
 *   2. Rotate vertices by zan2d, (pitch 0), yan2d; translate by offsets.
 *   3. Apply xan2d orientation; perspective divide with focal 512.
 *   4. Painter-sort faces by average viewport depth; rasterize with
 *      gouraud shading (lit HSL palette) and textures.
 */

#include "osrs/item_sprite.h"
#include "osrs/model.h"
#include "osrs/render_utils.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* 2048-entry sine/cosine tables (scaled by 65536), built lazily. */
static int osrs_sin[2048];
static int osrs_cos[2048];
static bool tables_ready = false;

static void build_tables(void) {
  for (int i = 0; i < 2048; i++) {
    osrs_sin[i] = (int)(65536.0 * sin((double)i * M_PI / 1024.0));
    osrs_cos[i] = (int)(65536.0 * cos((double)i * M_PI / 1024.0));
  }
  tables_ready = true;
}

typedef struct {
  uint32_t *pixels;
  int w, h;
  int cx, cy; /* projection center */
  int zoom;   /* focal length (512) */
} sprite_raster_t;

/* Gouraud-shaded triangle rasterization (no texture). */
static void draw_triangle_gouraud(sprite_raster_t *r, int x0, int y0, int x1,
                                  int y1, int x2, int y2, uint32_t c0,
                                  uint32_t c1, uint32_t c2) {
  /* Sort by y */
  if (y0 > y1) {
    int t;
    t = y0;
    y0 = y1;
    y1 = t;
    t = x0;
    x0 = x1;
    x1 = t;
    t = c0;
    c0 = c1;
    c1 = t;
  }
  if (y1 > y2) {
    int t;
    t = y1;
    y1 = y2;
    y2 = t;
    t = x1;
    x1 = x2;
    x2 = t;
    t = c1;
    c1 = c2;
    c2 = t;
  }
  if (y0 > y1) {
    int t;
    t = y0;
    y0 = y1;
    y1 = t;
    t = x0;
    x0 = x1;
    x1 = t;
    t = c0;
    c0 = c1;
    c1 = t;
  }

  if (y2 == y0)
    return;

  for (int y = y0; y <= y2; y++) {
    if (y < 0 || y >= r->h)
      continue;

    /* Compute x range for this scanline */
    float xa, xb;
    uint32_t ca, cb;
    if (y < y1) {
      float t1 = (y1 != y0) ? (float)(y - y0) / (float)(y1 - y0) : 0.0f;
      float t2 = (y2 != y0) ? (float)(y - y0) / (float)(y2 - y0) : 0.0f;
      xa = (float)x0 + t1 * (float)(x1 - x0);
      xb = (float)x0 + t2 * (float)(x2 - x0);
      int r0 =
          (int)(c0 & 0xFF) + (int)(t1 * ((int)(c1 & 0xFF) - (int)(c0 & 0xFF)));
      int g0 = (int)((c0 >> 8) & 0xFF) +
               (int)(t1 * ((int)((c1 >> 8) & 0xFF) - (int)((c0 >> 8) & 0xFF)));
      int b0 =
          (int)((c0 >> 16) & 0xFF) +
          (int)(t1 * ((int)((c1 >> 16) & 0xFF) - (int)((c0 >> 16) & 0xFF)));
      int r2 =
          (int)(c0 & 0xFF) + (int)(t2 * ((int)(c2 & 0xFF) - (int)(c0 & 0xFF)));
      int g2 = (int)((c0 >> 8) & 0xFF) +
               (int)(t2 * ((int)((c2 >> 8) & 0xFF) - (int)((c0 >> 8) & 0xFF)));
      int b2 =
          (int)((c0 >> 16) & 0xFF) +
          (int)(t2 * ((int)((c2 >> 16) & 0xFF) - (int)((c0 >> 16) & 0xFF)));
      ca = 0xFF000000u | ((uint32_t)b0 << 16) | ((uint32_t)g0 << 8) |
           (uint32_t)r0;
      cb = 0xFF000000u | ((uint32_t)b2 << 16) | ((uint32_t)g2 << 8) |
           (uint32_t)r2;
    } else {
      float t1 = (y2 != y1) ? (float)(y - y1) / (float)(y2 - y1) : 0.0f;
      float t2 = (y2 != y0) ? (float)(y - y0) / (float)(y2 - y0) : 0.0f;
      xa = (float)x1 + t1 * (float)(x2 - x1);
      xb = (float)x0 + t2 * (float)(x2 - x0);
      int r1 =
          (int)(c1 & 0xFF) + (int)(t1 * ((int)(c2 & 0xFF) - (int)(c1 & 0xFF)));
      int g1 = (int)((c1 >> 8) & 0xFF) +
               (int)(t1 * ((int)((c2 >> 8) & 0xFF) - (int)((c1 >> 8) & 0xFF)));
      int b1 =
          (int)((c1 >> 16) & 0xFF) +
          (int)(t1 * ((int)((c2 >> 16) & 0xFF) - (int)((c1 >> 16) & 0xFF)));
      int r2 =
          (int)(c0 & 0xFF) + (int)(t2 * ((int)(c2 & 0xFF) - (int)(c0 & 0xFF)));
      int g2 = (int)((c0 >> 8) & 0xFF) +
               (int)(t2 * ((int)((c2 >> 8) & 0xFF) - (int)((c0 >> 8) & 0xFF)));
      int b2 =
          (int)((c0 >> 16) & 0xFF) +
          (int)(t2 * ((int)((c2 >> 16) & 0xFF) - (int)((c0 >> 16) & 0xFF)));
      ca = 0xFF000000u | ((uint32_t)b1 << 16) | ((uint32_t)g1 << 8) |
           (uint32_t)r1;
      cb = 0xFF000000u | ((uint32_t)b2 << 16) | ((uint32_t)g2 << 8) |
           (uint32_t)r2;
    }

    if (xa > xb) {
      float t = xa;
      xa = xb;
      xb = t;
      uint32_t tc = ca;
      ca = cb;
      cb = tc;
    }
    int xi0 = (int)(xa + 0.5f);
    int xi1 = (int)(xb + 0.5f);
    if (xi0 < 0)
      xi0 = 0;
    if (xi1 >= r->w)
      xi1 = r->w - 1;

    for (int x = xi0; x <= xi1; x++) {
      float t = (xi1 != xi0) ? (float)(x - xi0) / (float)(xb - xa) : 0.0f;
      int rr =
          (int)(ca & 0xFF) + (int)(t * ((int)(cb & 0xFF) - (int)(ca & 0xFF)));
      int gg = (int)((ca >> 8) & 0xFF) +
               (int)(t * ((int)((cb >> 8) & 0xFF) - (int)((ca >> 8) & 0xFF)));
      int bb = (int)((ca >> 16) & 0xFF) +
               (int)(t * ((int)((cb >> 16) & 0xFF) - (int)((ca >> 16) & 0xFF)));
      if (rr < 0)
        rr = 0;
      if (rr > 255)
        rr = 255;
      if (gg < 0)
        gg = 0;
      if (gg > 255)
        gg = 255;
      if (bb < 0)
        bb = 0;
      if (bb > 255)
        bb = 255;
      r->pixels[y * r->w + x] = 0xFF000000u | ((uint32_t)bb << 16) |
                                ((uint32_t)gg << 8) | (uint32_t)rr;
    }
  }
}

/* Textured triangle (nearest-neighbor sample, modulated by vertex shade). */
static void draw_triangle_textured(sprite_raster_t *r, int x0, int y0, int x1,
                                   int y1, int x2, int y2, uint32_t c0,
                                   uint32_t c1, uint32_t c2, float u0, float v0,
                                   float u1, float v1, float u2, float v2,
                                   const uint32_t *tex) {
  /* Perspective-incorrect affine mapping is sufficient for item sprites. */
  int minx = x0 < x1 ? x0 : x1;
  if (x2 < minx)
    minx = x2;
  int maxx = x0 > x1 ? x0 : x1;
  if (x2 > maxx)
    maxx = x2;
  int miny = y0 < y1 ? y0 : y1;
  if (y2 < miny)
    miny = y2;
  int maxy = y0 > y1 ? y0 : y1;
  if (y2 > maxy)
    maxy = y2;

  int d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
  if (d == 0)
    return;

  if (minx < 0)
    minx = 0;
  if (miny < 0)
    miny = 0;
  if (maxx >= r->w)
    maxx = r->w - 1;
  if (maxy >= r->h)
    maxy = r->h - 1;

  for (int y = miny; y <= maxy; y++) {
    for (int x = minx; x <= maxx; x++) {
      int w0 = (y1 - y2) * (x - x2) + (x2 - x1) * (y - y2);
      int w1 = (y2 - y0) * (x - x2) + (x0 - x2) * (y - y2);
      int w2 = d - w0 - w1;
      if (w0 < 0 || w1 < 0 || w2 < 0)
        continue;
      float f0 = (float)w0 / (float)d;
      float f1 = (float)w1 / (float)d;
      float f2 = (float)w2 / (float)d;
      float u = f0 * u0 + f1 * u1 + f2 * u2;
      float v = f0 * v0 + f1 * v1 + f2 * v2;
      int tx = (int)(u * 127.0f) & 0x7F;
      int ty = (int)(v * 127.0f) & 0x7F;
      uint32_t tc = tex[ty * 128 + tx];

      int rr = (int)((f0 * (float)(c0 & 0xFF) + f1 * (float)(c1 & 0xFF) +
                      f2 * (float)(c2 & 0xFF)) *
                     (float)(tc & 0xFF) / 255.0f);
      int gg = (int)((f0 * (float)((c0 >> 8) & 0xFF) +
                      f1 * (float)((c1 >> 8) & 0xFF) +
                      f2 * (float)((c2 >> 8) & 0xFF)) *
                     (float)((tc >> 8) & 0xFF) / 255.0f);
      int bb = (int)((f0 * (float)((c0 >> 16) & 0xFF) +
                      f1 * (float)((c1 >> 16) & 0xFF) +
                      f2 * (float)((c2 >> 16) & 0xFF)) *
                     (float)((tc >> 16) & 0xFF) / 255.0f);
      if (rr > 255)
        rr = 255;
      if (gg > 255)
        gg = 255;
      if (bb > 255)
        bb = 255;
      r->pixels[y * r->w + x] = 0xFF000000u | ((uint32_t)bb << 16) |
                                ((uint32_t)gg << 8) | (uint32_t)rr;
    }
  }
}

/* Texture pixel cache for item rendering */
#define ITEM_TEX_CACHE 64
static struct {
  int id;
  uint32_t *pixels;
} item_tex_cache[ITEM_TEX_CACHE];
static int item_tex_cache_count = 0;

static const uint32_t *item_get_texture(osrs_config_t *config, int tex_id) {
  for (int i = 0; i < item_tex_cache_count; i++) {
    if (item_tex_cache[i].id == tex_id)
      return item_tex_cache[i].pixels;
  }
  uint32_t *px = osrs_config_texture_pixels(config, tex_id);
  if (!px)
    return NULL;
  if (item_tex_cache_count < ITEM_TEX_CACHE) {
    item_tex_cache[item_tex_cache_count].id = tex_id;
    item_tex_cache[item_tex_cache_count].pixels = px;
    item_tex_cache_count++;
  } else {
    /* Evict slot 0 */
    free(item_tex_cache[0].pixels);
    for (int i = 1; i < ITEM_TEX_CACHE; i++)
      item_tex_cache[i - 1] = item_tex_cache[i];
    item_tex_cache[ITEM_TEX_CACHE - 1].id = tex_id;
    item_tex_cache[ITEM_TEX_CACHE - 1].pixels = px;
  }
  return px;
}

bool osrs_item_sprite_render(osrs_config_t *config, int item_id,
                             uint32_t *out_pixels, int w, int h) {
  if (!config || !out_pixels || w <= 0 || h <= 0)
    return false;
  if (!tables_ready)
    build_tables();

  memset(out_pixels, 0, (size_t)w * (size_t)h * sizeof(uint32_t));

  osrs_item_def_t *item = osrs_config_load_item(config, item_id);
  if (!item)
    return false;

  int model_id = item->inventory_model_id;
  int zoom2d = item->model_zoom;
  int xan = item->model_rotation_x & 2047;
  int yan = item->model_rotation_y & 2047;
  int zan = item->model_rotation_z & 2047;
  int xoff = item->model_offset_x;
  int yoff = item->model_offset_y;
  int ambient = OSRS_LIGHT_ENTITY_AMBIENT(item->ambient);
  int contrast = OSRS_LIGHT_ENTITY_CONTRAST(item->contrast);
  int recolor_src[8], recolor_dst[8], recolor_count;
  recolor_count = item->recolor_count;
  memcpy(recolor_src, item->recolor_src, sizeof(recolor_src));
  memcpy(recolor_dst, item->recolor_dst, sizeof(recolor_dst));
  osrs_item_def_free(item);

  if (model_id < 0 || zoom2d == 0) {
    return false;
  }

  /* Load model */
  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(config->cache, 7, model_id, NULL);
  if (!files || files->count < 1) {
    if (files)
      osrs_archive_files_free(files);
    return false;
  }
  osrs_model_t *model =
      osrs_model_load(model_id, files->files[0].data, files->files[0].len);
  osrs_archive_files_free(files);
  if (!model)
    return false;

  for (int i = 0; i < recolor_count; i++)
    osrs_model_recolor(model, recolor_src[i], recolor_dst[i]);

  osrs_model_compute_normals(model);
  osrs_model_compute_uvs(model);

  /* Model height (for centering) */
  int model_height = 0;
  for (int i = 0; i < model->vertex_count; i++) {
    if (-model->vertex_y[i] > model_height)
      model_height = -model->vertex_y[i];
  }

  /* Rotation + projection (per Model.projectAndDraw) */
  int n = model->vertex_count;
  int *sx = malloc((size_t)n * sizeof(int));
  int *sy = malloc((size_t)n * sizeof(int));
  int *sz = malloc((size_t)n * sizeof(int));
  if (!sx || !sy || !sz) {
    free(sx);
    free(sy);
    free(sz);
    osrs_model_free(model);
    return false;
  }

  sprite_raster_t r = {out_pixels, w, h, w / 2, h / 2, 512};

  int sinX = osrs_sin[xan];
  int cosX = osrs_cos[xan];
  int var17 = (int)(((int64_t)zoom2d * sinX) >> 16);
  int var18 = (int)(((int64_t)zoom2d * cosX) >> 16);
  int xOffset = xoff;
  int yOffset = model_height / 2 + var17 + yoff;
  int zOffset = var18 + yoff;
  int zRelated = (sinX * yOffset + cosX * zOffset) >> 16;

  for (int i = 0; i < n; i++) {
    int x = model->vertex_x[i];
    int y = model->vertex_y[i];
    int z = model->vertex_z[i];
    if (zan != 0) {
      int sinZ = osrs_sin[zan];
      int cosZ = osrs_cos[zan];
      int tmp = (y * sinZ + x * cosZ) >> 16;
      y = (y * cosZ - x * sinZ) >> 16;
      x = tmp;
    }
    if (yan != 0) {
      int sinY = osrs_sin[yan];
      int cosY = osrs_cos[yan];
      int tmp = (z * sinY + x * cosY) >> 16;
      z = (z * cosY - x * sinY) >> 16;
      x = tmp;
    }
    x += xOffset;
    y += yOffset;
    z += zOffset;
    int tmp = (y * cosX - z * sinX) >> 16;
    z = (y * sinX + z * cosX) >> 16;
    if (z < 1)
      z = 1;
    sz[i] = z - zRelated;
    sx[i] = x * r.zoom / z + r.cx;
    sy[i] = tmp * r.zoom / z + r.cy;
  }

  /* Painter's sort: far to near by average viewport depth */
  int fc = model->face_count;
  int *order = malloc((size_t)fc * sizeof(int));
  float *depth = malloc((size_t)fc * sizeof(float));
  if (!order || !depth) {
    free(order);
    free(depth);
    free(sx);
    free(sy);
    free(sz);
    osrs_model_free(model);
    return false;
  }
  for (int i = 0; i < fc; i++) {
    order[i] = i;
    depth[i] = (float)(sz[model->face_a[i]] + sz[model->face_b[i]] +
                       sz[model->face_c[i]]) /
               3.0f;
  }
  /* Insertion sort (faces are small) */
  for (int i = 1; i < fc; i++) {
    int o = order[i];
    float d = depth[i];
    int j = i - 1;
    while (j >= 0 && depth[j] < d) { /* far (larger z) first */
      depth[j + 1] = depth[j];
      order[j + 1] = order[j];
      j--;
    }
    depth[j + 1] = d;
    order[j + 1] = o;
  }

  /* Rasterize */
  for (int oi = 0; oi < fc; oi++) {
    int i = order[oi];
    if (model->face_transparency && model->face_transparency[i] == -2)
      continue;
    int a = model->face_a[i];
    int b = model->face_b[i];
    int c = model->face_c[i];
    if (a >= n || b >= n || c >= n)
      continue;

    uint32_t c0 = osrs_model_light_vertex(model, i, 0, ambient, contrast);
    uint32_t c1 = osrs_model_light_vertex(model, i, 1, ambient, contrast);
    uint32_t c2 = osrs_model_light_vertex(model, i, 2, ambient, contrast);

    int tex = model->face_texture ? model->face_texture[i] : -1;
    if (tex >= 0 && model->face_u) {
      const uint32_t *texpx = item_get_texture(config, tex);
      if (texpx) {
        draw_triangle_textured(&r, sx[a], sy[a], sx[b], sy[b], sx[c], sy[c], c0,
                               c1, c2, model->face_u[i][0], model->face_v[i][0],
                               model->face_u[i][1], model->face_v[i][1],
                               model->face_u[i][2], model->face_v[i][2], texpx);
        continue;
      }
    }

    draw_triangle_gouraud(&r, sx[a], sy[a], sx[b], sy[b], sx[c], sy[c], c0, c1,
                          c2);
  }

  free(order);
  free(depth);
  free(sx);
  free(sy);
  free(sz);
  osrs_model_free(model);
  return true;
}

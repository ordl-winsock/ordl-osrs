/*
 * osrs/iso_renderer.c - Isometric 3D renderer for OSRS world
 * Pure C23, zero external dependencies.
 */

#include "osrs/iso_renderer.h"
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/log.h"
#include "osrs/model.h"
#include "osrs/render_utils.h"
#include <math.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/* Simple static caches (LRU) — kept for future 3D model rendering            */
/* -------------------------------------------------------------------------- */

#if 0
#define CACHE_SIZE 64

static struct {
  int model_id;
  osrs_model_t *model;
} model_cache[CACHE_SIZE];
static int model_cache_count = 0;

static struct {
  int object_id;
  osrs_object_def_t *def;
} obj_cache[CACHE_SIZE];
static int obj_cache_count = 0;

static osrs_object_def_t *load_object_def(osrs_config_t *config,
                                          int object_id) {
  if (!config || object_id < 0)
    return NULL;

  /* Check cache */
  for (int i = 0; i < obj_cache_count; i++) {
    if (obj_cache[i].object_id == object_id)
      return obj_cache[i].def;
  }

  osrs_object_def_t *def = osrs_config_load_object(config, object_id);
  if (!def)
    return NULL;

  /* Insert into cache */
  if (obj_cache_count < CACHE_SIZE) {
    obj_cache[obj_cache_count].object_id = object_id;
    obj_cache[obj_cache_count].def = def;
    obj_cache_count++;
  } else {
    osrs_object_def_free(obj_cache[0].def);
    for (int i = 1; i < CACHE_SIZE; i++)
      obj_cache[i - 1] = obj_cache[i];
    obj_cache[CACHE_SIZE - 1].object_id = object_id;
    obj_cache[CACHE_SIZE - 1].def = def;
  }

  return def;
}

static osrs_model_t *load_model_from_cache(osrs_config_t *config,
                                           int model_id) {
  if (!config || !config->cache || model_id < 0)
    return NULL;

  /* Check cache */
  for (int i = 0; i < model_cache_count; i++) {
    if (model_cache[i].model_id == model_id)
      return model_cache[i].model;
  }

  /* Load from cache index 7 */
  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(config->cache, 7, model_id, NULL);
  if (!files || files->count == 0) {
    if (files)
      osrs_archive_files_free(files);
    return NULL;
  }

  osrs_model_t *model =
      osrs_model_load(model_id, files->files[0].data, files->files[0].len);
  osrs_archive_files_free(files);

  if (!model)
    return NULL;

  /* Insert into cache */
  if (model_cache_count < CACHE_SIZE) {
    model_cache[model_cache_count].model_id = model_id;
    model_cache[model_cache_count].model = model;
    model_cache_count++;
  } else {
    osrs_model_free(model_cache[0].model);
    for (int i = 1; i < CACHE_SIZE; i++)
      model_cache[i - 1] = model_cache[i];
    model_cache[CACHE_SIZE - 1].model_id = model_id;
    model_cache[CACHE_SIZE - 1].model = model;
  }

  OSRS_INFO(OSRS_LOG_CAT_CACHE, "Loaded model %d (%d verts, %d faces)",
            model_id, model->vertex_count, model->face_count);

  return model;
}
#endif

/* -------------------------------------------------------------------------- */
/* Projection math                                                            */
/* -------------------------------------------------------------------------- */

bool osrs_iso_project(const osrs_iso_camera_t *cam, int screen_w, int screen_h,
                      int wx, int wz, int height, float *sx, float *sy) {
  /* Matches the GPU world shader; the -45 degree yaw offset aligns the view
   * with the real OSRS default camera (north up-screen, east right-screen). */
  float yaw_delta = cam->yaw - OSRS_ISO_DEFAULT_YAW - (float)M_PI_4;
  float cy = cosf(yaw_delta), syw = sinf(yaw_delta);
  float cp = cosf(cam->pitch), sp = sinf(cam->pitch);

  float dx = (float)(wx - cam->center_tile_x);
  float dz = (float)(wz - cam->center_tile_z);
  float h = (float)height / 128.0f;

  float rdx = dx * cy - dz * syw;
  float rdz = dx * syw + dz * cy;
  float fwd = rdx + rdz;
  /* Matches the GPU shader's from-above view: north is up-screen, taller
   * geometry rises up-screen. */
  float vert = (-fwd * sp - h * cp) * 71.55f * cam->zoom;

  *sx = cam->x + (rdx - rdz) * 64.0f * cam->zoom + screen_w * 0.5f;
  *sy = cam->y + vert + screen_h * 0.5f;

  /* Fast off-screen cull: reject if > one tile size outside bounds */
  float margin = 128.0f * cam->zoom;
  return !(*sx < -margin || *sx >= screen_w + margin || *sy < -margin ||
           *sy >= screen_h + margin);
}

bool osrs_iso_screen_to_world(const osrs_iso_camera_t *cam, int screen_w,
                              int screen_h, float sx, float sy, int *wx,
                              int *wz) {
  float yaw_delta = cam->yaw - OSRS_ISO_DEFAULT_YAW - (float)M_PI_4;
  float cy = cosf(yaw_delta), syw = sinf(yaw_delta);
  float sp = sinf(cam->pitch);

  /* Reverse the planar projection (ground plane, h = 0). */
  float px = sx - cam->x - screen_w * 0.5f;
  float py = sy - cam->y - screen_h * 0.5f;

  float rdz_minus = px / (64.0f * cam->zoom);  /* rdx - rdz */
  float fwd = -py / (sp * 71.55f * cam->zoom); /* rdx + rdz */
  float rdx = (rdz_minus + fwd) * 0.5f;
  float rdz = (fwd - rdz_minus) * 0.5f;

  /* Inverse yaw pre-rotation. */
  float dx = rdx * cy + rdz * syw;
  float dz = -rdx * syw + rdz * cy;

  *wx = cam->center_tile_x + (int)roundf(dx);
  *wz = cam->center_tile_z + (int)roundf(dz);
  return true;
}

/* -------------------------------------------------------------------------- */
/* Diamond helpers                                                            */
/* -------------------------------------------------------------------------- */

/* Fill a flat-coloured diamond given its centre and half-size vectors.
 *  Drawn as two triangles: (top, right, bottom) and (top, bottom, left). */
static void draw_diamond(fge_framebuffer_t *fb, float cx, float cy, float hw,
                         float hh, uint32_t color) {
  fge_vec2_t top = fge_v2(cx, cy - hh);
  fge_vec2_t right = fge_v2(cx + hw, cy);
  fge_vec2_t bottom = fge_v2(cx, cy + hh);
  fge_vec2_t left = fge_v2(cx - hw, cy);

  fge_draw_triangle(fb, top, right, bottom, color);
  fge_draw_triangle(fb, top, bottom, left, color);
}

/* Darken an RGBA colour by a fixed amount.
 *  amt > 0 darkens, amt < 0 brightens. */
static uint32_t darken(uint32_t c, int amt) {
  int r = (int)((c >> 16) & 0xFF) - amt;
  int g = (int)((c >> 8) & 0xFF) - amt;
  int b = (int)((c >> 0) & 0xFF) - amt;
  if (r < 0)
    r = 0;
  else if (r > 255)
    r = 255;
  if (g < 0)
    g = 0;
  else if (g > 255)
    g = 255;
  if (b < 0)
    b = 0;
  else if (b > 255)
    b = 255;
  return (c & 0xFF000000) | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
         (uint32_t)b;
}

/* -------------------------------------------------------------------------- */
/* Region rendering                                                           */
/* -------------------------------------------------------------------------- */

void osrs_iso_render_region(const osrs_iso_camera_t *cam, fge_renderer_t *r,
                            osrs_region_t *region, int screen_w, int screen_h,
                            osrs_config_t *config) {
  (void)config; /* colors are pre-computed in tile->render_color */
  if (!region)
    return;

  float tw = OSRS_ISO_TILE_W * cam->zoom;
  float th = OSRS_ISO_TILE_H * cam->zoom;
  float hw = tw * 0.5f; /* half width  of diamond */
  float hh = th * 0.5f; /* half height of diamond */

  int base_x = region->region_x * OSRS_REGION_SIZE;
  int base_z = region->region_y * OSRS_REGION_SIZE;

  /* --- Terrain tiles (plane 0 only for now) --- */

  /* Painter's algorithm: draw from back (farthest NE) to front (nearest SW).
   *  In the from-above view, "back" means larger (x + z). */
  for (int sum = OSRS_REGION_SIZE * 2 - 2; sum >= 0; sum--) {
    for (int lx = 0; lx < OSRS_REGION_SIZE; lx++) {
      int lz = sum - lx;
      if (lz < 0 || lz >= OSRS_REGION_SIZE)
        continue;

      osrs_tile_t *tile = &region->tiles[0][lx][lz];

      int wx = base_x + lx;
      int wz = base_z + lz;

      float sx, sy;
      if (!osrs_iso_project(cam, screen_w, screen_h, wx, wz, tile->height, &sx,
                            &sy))
        continue;

      uint32_t color = tile->render_color;

      /* Height shading: subtle darkening per height unit */
      if (tile->height != 0)
        color = darken(color, tile->height / 4);

      /* Overlay makes the tile slightly brighter */
      if (tile->overlay_id != 0)
        color = darken(color, -8);

      /* Slightly oversize diamonds for seamless tile edges */
      draw_diamond(&r->fb, sx, sy, hw * 1.05f, hh * 1.05f, color);
    }
  }

  /* --- Locations (objects) --- */
  for (int i = 0; i < region->location_count; i++) {
    osrs_location_t *loc = &region->locations[i];
    if (loc->plane != 0)
      continue;

    int wx = base_x + loc->local_x;
    int wz = base_z + loc->local_y;

    /* 3D model rendering disabled — too slow for software renderer */
    (void)config;
#if 0
    /* Try to load and render 3D model */
    if (config) {
      /* First check if object is on-screen before loading anything */
      float sx, sy;
      if (!osrs_iso_project(cam, screen_w, screen_h, wx, wz, 0, &sx, &sy))
        continue;

      static int model_attempts = 0;
      if (++model_attempts <= 5) {
        OSRS_INFO(OSRS_LOG_CAT_RENDER,
                  "Object %d type=%d: trying to load model", loc->object_id,
                  loc->type);
      }

      osrs_object_def_t *obj_def = load_object_def(config, loc->object_id);
      if (obj_def) {
        if (obj_def->model_count > 0) {
          if (model_attempts <= 5) {
            OSRS_INFO(OSRS_LOG_CAT_RENDER, "Object %d has model_id=%d",
                      loc->object_id, obj_def->model_ids[0]);
          }
          osrs_model_t *model =
              load_model_from_cache(config, obj_def->model_ids[0]);
          if (model) {
            osrs_iso_render_model(cam, r, screen_w, screen_h, wx, wz, 0, model,
                                  loc->orientation);
            continue;
          } else if (model_attempts <= 5) {
            OSRS_INFO(OSRS_LOG_CAT_RENDER, "Object %d model load failed",
                      loc->object_id);
          }
        } else if (model_attempts <= 5) {
          OSRS_INFO(OSRS_LOG_CAT_RENDER, "Object %d has no models",
                    loc->object_id);
        }
      } else if (model_attempts <= 5) {
        OSRS_INFO(OSRS_LOG_CAT_RENDER, "Object %d def load failed",
                  loc->object_id);
      }
    }
#endif

    /* Fallback: simple shape rendering */
    float sx, sy;
    if (!osrs_iso_project(cam, screen_w, screen_h, wx, wz, 0, &sx, &sy))
      continue;

    /* Object rendering by type:
     *  0-3   : Walls → vertical rectangles
     *  4-8   : Decorative / interactive → larger diamonds
     *  9-11  : Diagonal walls → diagonal lines
     *  22    : Floor decoration → small dots (already on floor)
     *  other : Generic → medium diamond
     */
    int type = loc->type;
    if (type >= 0 && type <= 3) {
      /* Wall: tall vertical rectangle */
      float wall_w = hw * 0.25f;
      float wall_h = hh * 1.2f;
      uint32_t wall_color = 0xFF999999;
      fge_vec2_t v0 = fge_v2(sx - wall_w, sy);
      fge_vec2_t v1 = fge_v2(sx + wall_w, sy);
      fge_vec2_t v2 = fge_v2(sx + wall_w, sy - wall_h);
      fge_vec2_t v3 = fge_v2(sx - wall_w, sy - wall_h);
      fge_draw_triangle(&r->fb, v0, v1, v2, wall_color);
      fge_draw_triangle(&r->fb, v0, v2, v3, wall_color);
    } else if (type >= 4 && type <= 8) {
      /* Decorative / interactive: filled square */
      float sz = hw * 0.3f;
      uint32_t obj_color = 0xFF6688FF;
      fge_vec2_t v0 = fge_v2(sx - sz, sy - sz);
      fge_vec2_t v1 = fge_v2(sx + sz, sy - sz);
      fge_vec2_t v2 = fge_v2(sx + sz, sy + sz);
      fge_vec2_t v3 = fge_v2(sx - sz, sy + sz);
      fge_draw_triangle(&r->fb, v0, v1, v2, obj_color);
      fge_draw_triangle(&r->fb, v0, v2, v3, obj_color);
    } else if (type >= 9 && type <= 11) {
      /* Diagonal wall: X shape */
      uint32_t line_color = 0xFFCCCCCC;
      float dl = hw * 0.4f;
      fge_draw_line(&r->fb, fge_v2(sx - dl, sy - dl), fge_v2(sx + dl, sy + dl),
                    1.0f, line_color);
      fge_draw_line(&r->fb, fge_v2(sx - dl, sy + dl), fge_v2(sx + dl, sy - dl),
                    1.0f, line_color);
    } else if (type == 22) {
      /* Floor decoration: small subtle dot */
      float ms = hw * 0.15f;
      uint32_t obj_color = 0xFF44CC44;
      draw_diamond(&r->fb, sx, sy, ms, ms * 0.5f, obj_color);
    } else {
      /* Generic: medium diamond */
      float ms = hw * 0.3f;
      uint32_t obj_color = 0xFFFFAA44;
      draw_diamond(&r->fb, sx, sy - hh * 0.2f, ms, ms * 0.5f, obj_color);
    }
  }
}

/* -------------------------------------------------------------------------- */
/* Marker rendering (NPCs / players)                                          */
/* -------------------------------------------------------------------------- */

void osrs_iso_render_marker(const osrs_iso_camera_t *cam, fge_renderer_t *r,
                            int screen_w, int screen_h, int wx, int wz,
                            int height, int size_px, uint32_t color) {
  float sx, sy;
  if (!osrs_iso_project(cam, screen_w, screen_h, wx, wz, height, &sx, &sy))
    return;

  /* Marker sits slightly above the tile centre */
  float ms = size_px * cam->zoom * 0.5f;
  float my = sy - OSRS_ISO_TILE_H * cam->zoom * 0.25f;

  /* Draw a small filled circle via triangle fan approximation */
  fge_vec2_t centre = fge_v2(sx, my);
  int segs = 8;
  for (int i = 0; i < segs; i++) {
    float a0 = (float)(i) / segs * 6.2831853f - 1.5707963f;
    float a1 = (float)(i + 1) / segs * 6.2831853f - 1.5707963f;
    fge_vec2_t v0 = fge_v2(sx + cosf(a0) * ms, my + sinf(a0) * ms);
    fge_vec2_t v1 = fge_v2(sx + cosf(a1) * ms, my + sinf(a1) * ms);
    fge_draw_triangle(&r->fb, centre, v0, v1, color);
  }
}

/* -------------------------------------------------------------------------- */
/* Height edge filler                                                         */
/* -------------------------------------------------------------------------- */

void osrs_iso_render_height_edge(const osrs_iso_camera_t *cam,
                                 fge_renderer_t *r, int screen_w, int screen_h,
                                 int wx, int wz, int h0, int h1,
                                 uint32_t color) {
  (void)cam;
  (void)r;
  (void)screen_w;
  (void)screen_h;
  (void)wx;
  (void)wz;
  (void)h0;
  (void)h1;
  (void)color;
  /* Reserved for future full height-edge meshing. */
}

/* -------------------------------------------------------------------------- */
/* 3D Model rendering                                                         */
/* -------------------------------------------------------------------------- */

/* Simple struct for sorted faces */

/* HSL to RGBA (fast inline version) */
static uint32_t model_hsl_to_rgba(int hsl) {
  int hue = hsl & 0x3F;
  int sat = (hsl >> 6) & 0x07;
  int lum = (hsl >> 9) & 0x7F;
  if (lum == 0)
    return 0xFF000000;
  float h = hue * 360.0f / 64.0f;
  float s = sat / 7.0f;
  float l = lum / 127.0f;
  float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = l - c / 2.0f;
  float rr, gg, bb;
  if (h < 60) {
    rr = c;
    gg = x;
    bb = 0;
  } else if (h < 120) {
    rr = x;
    gg = c;
    bb = 0;
  } else if (h < 180) {
    rr = 0;
    gg = c;
    bb = x;
  } else if (h < 240) {
    rr = 0;
    gg = x;
    bb = c;
  } else if (h < 300) {
    rr = x;
    gg = 0;
    bb = c;
  } else {
    rr = c;
    gg = 0;
    bb = x;
  }
  uint8_t ru = (uint8_t)((rr + m) * 255.0f);
  uint8_t gu = (uint8_t)((gg + m) * 255.0f);
  uint8_t bu = (uint8_t)((bb + m) * 255.0f);
  return 0xFF000000 | ((uint32_t)ru << 16) | ((uint32_t)gu << 8) | (uint32_t)bu;
}

void osrs_iso_render_model(const osrs_iso_camera_t *cam, fge_renderer_t *r,
                           int screen_w, int screen_h, int wx, int wz,
                           int height, osrs_model_t *model, int orientation) {
  if (!model || model->face_count <= 0)
    return;

  float tw = OSRS_ISO_TILE_W * cam->zoom;
  float th = OSRS_ISO_TILE_H * cam->zoom;
  float hs = OSRS_ISO_HEIGHT_SCALE * cam->zoom;

  /* Rotation table for 8 orientations (0-7, each 45 degrees) */
  static const float sin_table[8] = {0.0f, 0.707107f,  1.0f,  0.707107f,
                                     0.0f, -0.707107f, -1.0f, -0.707107f};
  static const float cos_table[8] = {1.0f,  0.707107f,  0.0f, -0.707107f,
                                     -1.0f, -0.707107f, 0.0f, 0.707107f};
  float sori = sin_table[orientation & 7];
  float cori = cos_table[orientation & 7];

  /* Project all vertices to screen space */
  float proj_x[1024];
  float proj_y[1024];
  float *px = proj_x;
  float *py = proj_y;
  bool px_heap = false, py_heap = false;

  if (model->vertex_count > 1024) {
    px = malloc((size_t)model->vertex_count * sizeof(float));
    py = malloc((size_t)model->vertex_count * sizeof(float));
    if (!px || !py) {
      free(px);
      free(py);
      return;
    }
    px_heap = true;
    py_heap = true;
  }

  for (int i = 0; i < model->vertex_count; i++) {
    float vx = (float)model->vertex_x[i];
    float vz = (float)model->vertex_z[i];
    float rx = vx * cori - vz * sori;
    float rz = vx * sori + vz * cori;

    float world_x = (float)wx + rx * 0.01f;
    float world_z = (float)wz + rz * 0.01f;
    float world_y = (float)height + (float)model->vertex_y[i] * 0.01f;

    float dx = world_x - (float)cam->center_tile_x;
    float dz = world_z - (float)cam->center_tile_z;
    float px_ = (dx - dz) * tw * 0.5f;
    float py_ = (dx + dz) * th * 0.5f - world_y * hs;

    px[i] = cam->x + px_ + (float)screen_w * 0.5f;
    py[i] = cam->y + py_ + (float)screen_h * 0.5f;
  }

  /* Draw faces without depth sorting (fast but may have occlusion issues) */
  for (int i = 0; i < model->face_count; i++) {
    int a = model->face_a[i];
    int b = model->face_b[i];
    int fc = model->face_c[i];

    if (a >= model->vertex_count || b >= model->vertex_count ||
        fc >= model->vertex_count)
      continue;

    float ax = px[a], ay = py[a];
    float bx_ = px[b], by_ = py[b];
    float cx_ = px[fc], cy = py[fc];

    /* Backface cull */
    float cross = (bx_ - ax) * (cy - ay) - (by_ - ay) * (cx_ - ax);
    if (cross <= 0)
      continue;

    uint32_t color = model_hsl_to_rgba(model->face_color[i]);
    fge_draw_triangle(&r->fb, fge_v2(ax, ay), fge_v2(bx_, by_), fge_v2(cx_, cy),
                      color);
  }

  if (px_heap)
    free(px);
  if (py_heap)
    free(py);
}

/*
 * osrs/render_utils.c — Rendering utilities
 * Pure C23, zero external dependencies.
 */

#include "osrs/render_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool osrs_sprite_to_rgba(const osrs_sprite_t *sprite, uint32_t *out_pixels) {
  if (!sprite || !out_pixels || !sprite->pixels || sprite->width <= 0 ||
      sprite->height <= 0)
    return false;

  for (int y = 0; y < sprite->height; y++) {
    for (int x = 0; x < sprite->width; x++) {
      int idx = y * sprite->width + x;
      uint8_t pal_idx = sprite->pixels[idx];
      int pal_color = sprite->palette[pal_idx];

      /* OSRS palette is RGB888, alpha is implicit: index 0 = transparent */
      uint8_t r = (pal_color >> 16) & 0xFF;
      uint8_t g = (pal_color >> 8) & 0xFF;
      uint8_t b = pal_color & 0xFF;
      uint8_t a = (pal_idx == 0) ? 0x00 : 0xFF;

      out_pixels[idx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                        ((uint32_t)g << 8) | (uint32_t)b;
    }
  }

  return true;
}

/* JagexColor: exact OSRS HSL->RGB conversion (verified against
 * net.runelite.cache.models.JagexColor).
 *
 * HSL bit layout (16-bit): hue = bits 10-15 (0-63), sat = bits 7-9 (0-7),
 * luminance = bits 0-6 (0-127).
 *
 * Conversion uses half-step hue/saturation offsets and a brightness gamma
 * transform: channel' = (channel/256)^brightness * 256. */
#define OSRS_HSL_BRIGHTNESS_MODEL                                              \
  0.7                                /* world models (lift shadows for scene) */
#define OSRS_HSL_BRIGHTNESS_ITEM 0.6 /* item sprites */

uint32_t osrs_hsl_to_rgba_brightness(int hsl, double brightness) {
  double hue = (double)((hsl >> 10) & 63) / 64.0 + (0.5 / 64.0);
  double sat = (double)((hsl >> 7) & 7) / 8.0 + (0.5 / 8.0);
  double lum = (double)(hsl & 127) / 128.0;

  double chroma = (1.0 - fabs(2.0 * lum - 1.0)) * sat;
  double h6 = hue * 6.0;
  double x = chroma * (1.0 - fabs(fmod(h6, 2.0) - 1.0));
  double lightness = lum - chroma / 2.0;

  double r = lightness, g = lightness, b = lightness;
  switch ((int)h6) {
  case 0:
    r += chroma;
    g += x;
    break;
  case 1:
    g += chroma;
    r += x;
    break;
  case 2:
    g += chroma;
    b += x;
    break;
  case 3:
    b += chroma;
    g += x;
    break;
  case 4:
    b += chroma;
    r += x;
    break;
  default:
    r += chroma;
    b += x;
    break;
  }

  int ri = (int)(pow(r, brightness) * 256.0);
  int gi = (int)(pow(g, brightness) * 256.0);
  int bi = (int)(pow(b, brightness) * 256.0);
  if (ri > 255)
    ri = 255;
  if (gi > 255)
    gi = 255;
  if (bi > 255)
    bi = 255;

  int rgb = (ri << 16) | (gi << 8) | bi;
  if (rgb == 0)
    rgb = 1;
  return 0xFF000000u | (uint32_t)rgb;
}

uint32_t osrs_hsl_to_rgba(int hsl) {
  return osrs_hsl_to_rgba_brightness(hsl, OSRS_HSL_BRIGHTNESS_MODEL);
}

/* Precomputed palette: palette[hsl] = RGB with model brightness.
 * Built lazily on first use (65536 entries). */
static uint32_t osrs_hsl_palette[65536];
static bool osrs_hsl_palette_ready = false;

static void osrs_hsl_palette_build(void) {
  for (int i = 0; i < 65536; i++)
    osrs_hsl_palette[i] = osrs_hsl_to_rgba_brightness(i, 0.8) & 0x00FFFFFFu;
  osrs_hsl_palette_ready = true;
}

uint32_t osrs_hsl_palette_lookup(int hsl) {
  if (!osrs_hsl_palette_ready)
    osrs_hsl_palette_build();
  return 0xFF000000u | osrs_hsl_palette[hsl & 0xFFFF];
}

/* Apply OSRS lighting shade to an HSL color, returning RGB from the palette.
 * shade scales the luminance: lum' = clamp(lum * shade >> 7, 2, 126).
 * (method2608 + bound2to126 from ItemSpriteFactory.java) */
uint32_t osrs_hsl_shade_to_rgb(int hsl, int shade) {
  int lum = ((hsl & 127) * shade) >> 7;
  if (lum < 2)
    lum = 2;
  else if (lum > 126)
    lum = 126;
  int lit = (hsl & 0xFF80) | lum;
  return osrs_hsl_palette_lookup(lit);
}

/* Decompose an RGB888 color into the OSRS "full" HSL components (8-bit each)
 * plus the hue multiplier, per UnderlayDefinition.calculateHsl(). */
void osrs_rgb_to_hsl_full(int rgb, int *out_hue, int *out_sat, int *out_light,
                          int *out_hue_mult) {
  double r = (double)((rgb >> 16) & 255) / 256.0;
  double g = (double)((rgb >> 8) & 255) / 256.0;
  double b = (double)(rgb & 255) / 256.0;

  double min = r;
  if (g < min)
    min = g;
  if (b < min)
    min = b;
  double max = r;
  if (g > max)
    max = g;
  if (b > max)
    max = b;

  double hue = 0.0, sat = 0.0;
  double light = (max + min) / 2.0;
  if (min != max) {
    if (light < 0.5)
      sat = (max - min) / (max + min);
    else
      sat = (max - min) / (2.0 - max - min);

    if (r == max)
      hue = (g - b) / (max - min);
    else if (g == max)
      hue = 2.0 + (b - r) / (max - min);
    else if (b == max)
      hue = 4.0 + (r - g) / (max - min);
  }
  hue /= 6.0;

  int sat_i = (int)(sat * 256.0);
  int light_i = (int)(light * 256.0);
  if (sat_i < 0)
    sat_i = 0;
  else if (sat_i > 255)
    sat_i = 255;
  if (light_i < 0)
    light_i = 0;
  else if (light_i > 255)
    light_i = 255;

  int mult;
  if (light > 0.5)
    mult = (int)(sat * (1.0 - light) * 512.0);
  else
    mult = (int)(sat * light * 512.0);
  if (mult < 1)
    mult = 1;

  *out_hue = (int)((double)mult * hue);
  *out_sat = sat_i;
  *out_light = light_i;
  *out_hue_mult = mult;
}

/* Convert full-range HSL (8-bit hue/sat/light) to RGB888.
 * Per JagexColor.HSLtoRGBFull (no brightness adjustment). */
uint32_t osrs_hsl_full_to_rgb(int hue, int sat, int light) {
  double h = (double)(hue & 0xFF) / 256.0;
  double s = (double)(sat & 0xFF) / 256.0;
  double l = (double)(light & 0xFF) / 256.0;

  double chroma = (1.0 - fabs(2.0 * l - 1.0)) * s;
  double h6 = h * 6.0;
  double x = chroma * (1.0 - fabs(fmod(h6, 2.0) - 1.0));
  double lightness = l - chroma / 2.0;

  double r = lightness, g = lightness, b = lightness;
  switch ((int)h6) {
  case 0:
    r += chroma;
    g += x;
    break;
  case 1:
    g += chroma;
    r += x;
    break;
  case 2:
    g += chroma;
    b += x;
    break;
  case 3:
    b += chroma;
    g += x;
    break;
  case 4:
    b += chroma;
    r += x;
    break;
  default:
    r += chroma;
    b += x;
    break;
  }

  int ri = (int)(r * 256.0) & 255;
  int gi = (int)(g * 256.0) & 255;
  int bi = (int)(b * 256.0) & 255;
  int rgb = (ri << 16) | (gi << 8) | bi;
  if (rgb == 0)
    rgb = 1;
  return 0xFF000000u | (uint32_t)rgb;
}

uint32_t osrs_terrain_color_default(uint16_t underlay_id, uint16_t overlay_id) {
  uint16_t oid = overlay_id & 0x7FFF;
  uint16_t uid = underlay_id & 0x7FFF;

  if (oid != 0) {
    switch (oid) {
    case 1:
      return 0xFF4A8C4A; /* Grass */
    case 2:
      return 0xFF3A7C3A; /* Dark grass */
    case 3:
      return 0xFF6B8E6B; /* Light grass */
    case 4:
      return 0xFF8B7355; /* Dirt path */
    case 5:
      return 0xFF5C4033; /* Dark dirt */
    case 6:
      return 0xFF808080; /* Stone path */
    case 7:
      return 0xFF606060; /* Dark stone */
    case 8:
      return 0xFF4A6B8A; /* Water */
    case 9:
      return 0xFF3A5C7A; /* Deep water */
    case 10:
      return 0xFF8B4513; /* Wooden floor */
    case 11:
      return 0xFFA0522D; /* Light wood */
    case 12:
      return 0xFF654321; /* Dark wood */
    case 13:
      return 0xFFCD853F; /* Sand */
    case 14:
      return 0xFFD2691E; /* Light sand */
    case 15:
      return 0xFF8B6914; /* Dark sand */
    case 16:
      return 0xFF2F4F4F; /* Slate */
    case 17:
      return 0xFF708090; /* Light slate */
    case 18:
      return 0xFF556B2F; /* Swamp */
    case 19:
      return 0xFF6B8E23; /* Light swamp */
    case 20:
      return 0xFF800000; /* Red carpet */
    case 21:
      return 0xFF8B0000; /* Dark red */
    case 22:
      return 0xFF191970; /* Blue carpet */
    case 23:
      return 0xFF000080; /* Dark blue */
    case 24:
      return 0xFF006400; /* Green carpet */
    case 25:
      return 0xFF228B22; /* Light green */
    case 26:
      return 0xFF808000; /* Yellow carpet */
    case 27:
      return 0xFF8B8B00; /* Olive */
    case 28:
      return 0xFF4B0082; /* Purple carpet */
    case 29:
      return 0xFF483D8B; /* Dark purple */
    case 30:
      return 0xFF8B008B; /* Magenta */
    case 31:
      return 0xFF800080; /* Purple */
    default:
      return 0xFF505050; /* Unknown overlay */
    }
  }

  switch (uid) {
  case 0:
    return 0xFF3A5C3A; /* Deep grass */
  case 1:
    return 0xFF4A6B4A; /* Grass */
  case 2:
    return 0xFF5C7A5C; /* Light grass */
  case 3:
    return 0xFF6B8E6B; /* Lighter grass */
  case 4:
    return 0xFF8B7355; /* Dirt */
  case 5:
    return 0xFF5C4033; /* Dark dirt */
  case 6:
    return 0xFF808080; /* Stone */
  case 7:
    return 0xFF606060; /* Dark stone */
  case 8:
    return 0xFF4A6B8A; /* Water edge */
  case 9:
    return 0xFF3A5C7A; /* Deep water edge */
  case 10:
    return 0xFF8B4513; /* Wood */
  case 11:
    return 0xFFA0522D; /* Light wood */
  case 12:
    return 0xFF654321; /* Dark wood */
  case 13:
    return 0xFFCD853F; /* Sand */
  case 14:
    return 0xFFD2691E; /* Light sand */
  case 15:
    return 0xFF8B6914; /* Dark sand */
  case 16:
    return 0xFF2F4F4F; /* Slate */
  case 17:
    return 0xFF708090; /* Light slate */
  case 18:
    return 0xFF556B2F; /* Swamp */
  case 19:
    return 0xFF6B8E23; /* Light swamp */
  case 20:
    return 0xFF3A5C3A; /* More grass variants... */
  case 21:
    return 0xFF4A6B4A;
  case 22:
    return 0xFF5C7A5C;
  case 23:
    return 0xFF6B8E6B;
  case 24:
    return 0xFF7A9E7A;
  case 25:
    return 0xFF8BAE8B;
  case 26:
    return 0xFF4A5C3A;
  case 27:
    return 0xFF5C6B4A;
  case 28:
    return 0xFF6B7A5C;
  case 29:
    return 0xFF7A8E6B;
  case 30:
    return 0xFF8B9E7A;
  case 31:
    return 0xFF9EAE8B;
  default:
    return 0xFF3A5C3A; /* Default deep grass */
  }
}

/* -------------------------------------------------------------------------- */
/* Config-backed terrain colour lookup                                        */
/* -------------------------------------------------------------------------- */

#define TC_CACHE_SIZE 512

typedef struct {
  uint16_t underlay_id;
  uint16_t overlay_id;
  uint32_t color;
  bool valid;
} tc_entry_t;

uint32_t osrs_terrain_color(uint16_t underlay_id, uint16_t overlay_id,
                            osrs_config_t *config) {
  if (!config)
    return osrs_terrain_color_default(underlay_id, overlay_id);

  static tc_entry_t cache[TC_CACHE_SIZE];
  static bool init = false;
  if (!init) {
    memset(cache, 0, sizeof(cache));
    init = true;
  }

  /* Simple hash for cache lookup */
  uint32_t h = ((uint32_t)underlay_id << 16) | overlay_id;
  int idx = (int)(h % TC_CACHE_SIZE);

  if (cache[idx].valid && cache[idx].underlay_id == underlay_id &&
      cache[idx].overlay_id == overlay_id) {
    return cache[idx].color;
  }

  uint32_t color = 0;

  if (overlay_id != 0) {
    osrs_overlay_def_t *od = osrs_config_load_overlay(config, overlay_id);
    if (od) {
      if (od->color != 0)
        color = 0xFF000000 | (uint32_t)od->color;
      osrs_overlay_def_free(od);
    }
  }

  if (color == 0 && underlay_id != 0) {
    osrs_underlay_def_t *ud = osrs_config_load_underlay(config, underlay_id);
    if (ud) {
      if (ud->color != 0)
        color = 0xFF000000 | (uint32_t)ud->color;
      osrs_underlay_def_free(ud);
    }
  }

  if (color == 0)
    color = osrs_terrain_color_default(underlay_id, overlay_id);

  cache[idx].underlay_id = underlay_id;
  cache[idx].overlay_id = overlay_id;
  cache[idx].color = color;
  cache[idx].valid = true;

  return color;
}

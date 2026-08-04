/*
 * osrs/render_utils.h — Utilities for rendering OSRS data
 * Pure C23, zero external dependencies.
 */

#ifndef OSRS_RENDER_UTILS_H
#define OSRS_RENDER_UTILS_H

#include "osrs/config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OSRS lighting constants (deob verified).
 * Object defs store contrast pre-multiplied by 25 in the config loader.
 * NPC/item defs store raw contrast. Both use the same render-time formula. */
#define OSRS_LIGHT_AMBIENT_OFFSET 64
#define OSRS_LIGHT_CONTRAST_OFFSET 768

#define OSRS_LIGHT_OBJECT_AMBIENT(amb) ((amb) + OSRS_LIGHT_AMBIENT_OFFSET)
#define OSRS_LIGHT_OBJECT_CONTRAST(cont) ((cont) + OSRS_LIGHT_CONTRAST_OFFSET)
#define OSRS_LIGHT_ENTITY_AMBIENT(amb) ((amb) + OSRS_LIGHT_AMBIENT_OFFSET)
#define OSRS_LIGHT_ENTITY_CONTRAST(cont) ((cont) + OSRS_LIGHT_CONTRAST_OFFSET)

/* Convert an OSRS palette-indexed sprite to RGBA8888 pixels.
 * out_pixels must be pre-allocated with width * height * 4 bytes.
 * Returns true on success. */
bool osrs_sprite_to_rgba(const osrs_sprite_t *sprite, uint32_t *out_pixels);

/* Convert OSRS HSL color to RGBA8888.
 * OSRS HSL format: hue (6 bits) << 10 | sat (3 bits) << 7 | lum (7 bits).
 * Uses the exact JagexColor algorithm with brightness 0.8 (world models). */
uint32_t osrs_hsl_to_rgba(int hsl);

/* Same, with explicit brightness (0.6 = item sprites, 0.8 = world models). */
uint32_t osrs_hsl_to_rgba_brightness(int hsl, double brightness);

/* Palette lookup: HSL16 -> ARGB8888 with 0.8 brightness (65536-entry table). */
uint32_t osrs_hsl_palette_lookup(int hsl);

/* Apply OSRS lighting shade (0-255ish) to an HSL color -> shaded ARGB8888.
 * Scales luminance by shade/128, clamps to [2,126], palette lookup. */
uint32_t osrs_hsl_shade_to_rgb(int hsl, int shade);

/* Decompose RGB888 into OSRS full HSL (8-bit hue/sat/light + hue multiplier),
 * per UnderlayDefinition.calculateHsl(). */
void osrs_rgb_to_hsl_full(int rgb, int *out_hue, int *out_sat, int *out_light,
                          int *out_hue_mult);

/* Convert full-range HSL (8-bit components) to ARGB8888. */
uint32_t osrs_hsl_full_to_rgb(int hue, int sat, int light);

/* Predefined terrain colors for common underlay/overlay IDs.
 * These are approximations used when sprite data is unavailable. */
uint32_t osrs_terrain_color_default(uint16_t underlay_id, uint16_t overlay_id);

/* Look up terrain color from cache config definitions (underlay/overlay).
 * Falls back to osrs_terrain_color_default if config is NULL or definition
 * cannot be loaded.  Results are cached internally. */
uint32_t osrs_terrain_color(uint16_t underlay_id, uint16_t overlay_id,
                            osrs_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_RENDER_UTILS_H */

/*
 * osrs/item_sprite.h — Software renderer for OSRS inventory item sprites.
 * Renders an item's inventory model to a small ARGB image, matching the
 * official client's item sprite pipeline (rotation, perspective, gouraud
 * shading, textures).
 */

#ifndef OSRS_ITEM_SPRITE_H
#define OSRS_ITEM_SPRITE_H

#include "osrs/config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Render an item's inventory model into out_pixels (ARGB8888, w*h).
 * Returns true on success. out_pixels is cleared to transparent first. */
bool osrs_item_sprite_render(osrs_config_t *config, int item_id,
                             uint32_t *out_pixels, int w, int h);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_ITEM_SPRITE_H */

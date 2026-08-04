/*
 * osrs/config.c — OSRS config definition loaders implementation
 * Pure C23, zero external dependencies.
 */

#include "osrs/config.h"
#include "osrs/xtea.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

osrs_config_t *osrs_config_init(osrs_cache_t *cache) {
  if (!cache)
    return NULL;

  osrs_config_t *config = calloc(1, sizeof(osrs_config_t));
  if (!config)
    return NULL;

  config->cache = cache;

  /* Get commonly used indexes */
  config->config_index = osrs_cache_get_index(cache, 2);  /* Config */
  config->model_index = osrs_cache_get_index(cache, 7);   /* Models */
  config->sprite_index = osrs_cache_get_index(cache, 8);  /* Sprites */
  config->texture_index = osrs_cache_get_index(cache, 9); /* Textures */
  config->midi_index = osrs_cache_get_index(cache, 11);   /* MIDI */
  config->binary_index = osrs_cache_get_index(cache, 12); /* Binary */
  config->clientscript_index =
      osrs_cache_get_index(cache, 13);                        /* ClientScript */
  config->font_index = osrs_cache_get_index(cache, 14);       /* Fonts */
  config->vorbis_index = osrs_cache_get_index(cache, 15);     /* Vorbis */
  config->worldmap_index = osrs_cache_get_index(cache, 16);   /* WorldMap */
  config->world_index = osrs_cache_get_index(cache, 17);      /* World */
  config->region_index = osrs_cache_get_index(cache, 18);     /* Regions */
  config->maplabel_index = osrs_cache_get_index(cache, 19);   /* MapLabels */
  config->interface_index = osrs_cache_get_index(cache, 20);  /* Interfaces */
  config->jingled_index = osrs_cache_get_index(cache, 21);    /* Jingled */
  config->worldmapv2_index = osrs_cache_get_index(cache, 22); /* WorldMapV2 */

  return config;
}

void osrs_config_free(osrs_config_t *config) {
  if (!config)
    return;
  if (config->object_archive)
    osrs_archive_files_free(config->object_archive);
  if (config->npc_archive)
    osrs_archive_files_free(config->npc_archive);
  if (config->item_archive)
    osrs_archive_files_free(config->item_archive);
  if (config->underlay_archive)
    osrs_archive_files_free(config->underlay_archive);
  if (config->overlay_archive)
    osrs_archive_files_free(config->overlay_archive);
  if (config->kit_archive)
    osrs_archive_files_free(config->kit_archive);
  free(config);
}

/* Get a memoized config archive (lazy load on first use).
 * The returned archive is owned by the config — callers must NOT free it. */
static osrs_archive_files_t *config_get_archive(osrs_config_t *config,
                                                int archive_id) {
  osrs_archive_files_t **slot = NULL;
  switch (archive_id) {
  case OSRS_CONFIG_OBJECT:
    slot = &config->object_archive;
    break;
  case OSRS_CONFIG_NPC:
    slot = &config->npc_archive;
    break;
  case OSRS_CONFIG_ITEM:
    slot = &config->item_archive;
    break;
  case OSRS_CONFIG_UNDERLAY:
    slot = &config->underlay_archive;
    break;
  case OSRS_CONFIG_OVERLAY:
    slot = &config->overlay_archive;
    break;
  case OSRS_CONFIG_IDENTKIT:
    slot = &config->kit_archive;
    break;
  default:
    return NULL;
  }
  if (!*slot)
    *slot = osrs_cache_read_archive_files(config->cache, 2, archive_id, NULL);
  return *slot;
}

/* Load object definition */
osrs_object_def_t *osrs_config_load_object(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  /* Object definitions are in archive 6 of index 2 (memoized) */
  osrs_archive_files_t *files = config_get_archive(config, OSRS_CONFIG_OBJECT);
  if (!files)
    return NULL;

  /* Find file with matching ID */
  osrs_object_def_t *def = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == id) {
      osrs_archive_file_t *file = &files->files[i];
      def = calloc(1, sizeof(osrs_object_def_t));
      if (!def)
        break;
      def->id = id;

      osrs_stream_t s;
      osrs_stream_init(&s, file->data, file->len);

      /* Default values (RuneLite ObjectDefinition defaults) */
      def->blocks_projectile = true;
      def->interact_type = -1;
      def->animation_id = -1;
      def->varbit_id = -1;
      def->varp_id = -1;
      def->decor_displacement = 16;
      def->model_size_x = 128;
      def->model_size_y = 128;
      def->model_size_height = 128;
      def->size_x = 1;
      def->size_y = 1;

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;

        switch (opcode) {
        case 1: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          def->has_model_types = true;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_u16(&s);
            int mtype = osrs_stream_u8(&s);
            if (j < 16) {
              def->model_ids[j] = mid;
              def->model_types[j] = mtype;
            }
          }
          break;
        }
        case 2:
          osrs_stream_string(&s, def->name, sizeof(def->name));
          break;
        case 5: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_u16(&s);
            if (j < 16)
              def->model_ids[j] = mid;
          }
          break;
        }
        case 6: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          def->has_model_types = true;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_i32(&s);
            int mtype = osrs_stream_u8(&s);
            if (j < 16) {
              def->model_ids[j] = mid;
              def->model_types[j] = mtype;
            }
          }
          break;
        }
        case 7: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_i32(&s);
            if (j < 16)
              def->model_ids[j] = mid;
          }
          break;
        }
        case 14:
          def->size_x = osrs_stream_u8(&s);
          break;
        case 15:
          def->size_y = osrs_stream_u8(&s);
          break;
        case 17:
          def->interact_type = 0;
          def->blocks_projectile = false;
          break;
        case 18:
          def->blocks_projectile = false;
          break;
        case 19:
          def->wall_or_door = osrs_stream_u8(&s);
          break;
        case 21:
          def->contoured_ground = true;
          break;
        case 22:
          def->merge_normals = true;
          break;
        case 23:
          def->model_clipped = true;
          break;
        case 24:
          def->animation_id = osrs_stream_u16(&s);
          if (def->animation_id == 0xFFFF)
            def->animation_id = -1;
          break;
        case 27:
          def->interact_type = 1;
          break;
        case 28:
          def->decor_displacement = osrs_stream_u8(&s);
          break;
        case 29:
          def->ambient = (int8_t)osrs_stream_u8(&s);
          break;
        case 39:
          def->contrast = (int8_t)osrs_stream_u8(&s) * 25;
          break;
        case 30:
        case 31:
        case 32:
        case 33:
        case 34: {
          char action[64] = {0};
          osrs_stream_string(&s, action, sizeof(action));
          if (strcasecmp(action, "Hidden") != 0)
            memcpy(def->actions[opcode - 30], action, 64);
          break;
        }
        case 40: {
          int count = osrs_stream_u8(&s);
          def->recolor_count = count;
          for (int j = 0; j < count; j++) {
            int src = osrs_stream_u16(&s);
            int dst = osrs_stream_u16(&s);
            if (j < 8) {
              def->recolor_src[j] = src;
              def->recolor_dst[j] = dst;
            }
          }
          break;
        }
        case 41: {
          int count = osrs_stream_u8(&s);
          def->retexture_count = count;
          for (int j = 0; j < count; j++) {
            int src = osrs_stream_u16(&s);
            int dst = osrs_stream_u16(&s);
            if (j < 8) {
              def->retexture_src[j] = src;
              def->retexture_dst[j] = dst;
            }
          }
          break;
        }
        case 61:
          def->category = osrs_stream_u16(&s);
          break;
        case 62:
          def->rotated = true;
          break;
        case 64:
          /* shadow = false */
          break;
        case 65:
          def->model_size_x = osrs_stream_u16(&s);
          break;
        case 66:
          def->model_size_height = osrs_stream_u16(&s);
          break;
        case 67:
          def->model_size_y = osrs_stream_u16(&s);
          break;
        case 68:
          def->map_scene_id = osrs_stream_u16(&s);
          break;
        case 69:
          def->blocking_mask = (int8_t)osrs_stream_u8(&s);
          break;
        case 70:
          def->offset_x = osrs_stream_u16(&s);
          break;
        case 71:
          def->offset_height = osrs_stream_u16(&s);
          break;
        case 72:
          def->offset_y = osrs_stream_u16(&s);
          break;
        case 73:
          def->obstructs_ground = true;
          break;
        case 74:
          def->hollow = true;
          break;
        case 75:
          def->supports_items = osrs_stream_u8(&s);
          break;
        case 77: {
          def->varbit_id = osrs_stream_u16(&s);
          if (def->varbit_id == 0xFFFF)
            def->varbit_id = -1;
          def->varp_id = osrs_stream_u16(&s);
          if (def->varp_id == 0xFFFF)
            def->varp_id = -1;
          int len = osrs_stream_u8(&s);
          def->transform_count = len + 1;
          for (int j = 0; j <= len; j++) {
            int v = osrs_stream_u16(&s);
            if (j < 16) {
              def->transform_ids[j] = (v == 0xFFFF) ? -1 : v;
            }
          }
          break;
        }
        case 78:
          def->ambient_sound_id = osrs_stream_u16(&s);
          def->ambient_sound_distance = osrs_stream_u8(&s);
          osrs_stream_skip(&s, 1); /* rev220 retain */
          break;
        case 79: {
          /* ambientSoundChangeTicksMin/Max u16, distance u8, retain u8,
           * count u8 + ids u16 each */
          osrs_stream_skip(&s, 4);
          osrs_stream_skip(&s, 1);
          osrs_stream_skip(&s, 1);
          int len = osrs_stream_u8(&s);
          osrs_stream_skip(&s, (size_t)len * 2);
          break;
        }
        case 81:
          def->contoured_ground = true;
          osrs_stream_skip(&s, 1); /* value * 256 */
          break;
        case 82:
          def->map_area_id = osrs_stream_u16(&s);
          break;
        case 89:
        case 90:
        case 94:
          /* flags: randomizeAnimStart / deferAnimChange / unknown1 */
          break;
        case 91:
          osrs_stream_skip(&s, 1); /* soundDistanceFadeCurve */
          break;
        case 92: {
          def->varbit_id = osrs_stream_u16(&s);
          if (def->varbit_id == 0xFFFF)
            def->varbit_id = -1;
          def->varp_id = osrs_stream_u16(&s);
          if (def->varp_id == 0xFFFF)
            def->varp_id = -1;
          int fallback = osrs_stream_u16(&s);
          if (fallback == 0xFFFF)
            fallback = -1;
          int len = osrs_stream_u8(&s);
          def->transform_count = len + 1;
          for (int j = 0; j <= len; j++) {
            int v = osrs_stream_u16(&s);
            if (j < 16)
              def->transform_ids[j] = (v == 0xFFFF) ? -1 : v;
          }
          if (def->transform_count > 16)
            def->transform_count = 16;
          if (def->transform_count < 16)
            def->transform_ids[def->transform_count] = fallback;
          break;
        }
        case 93:
          osrs_stream_skip(&s, 6); /* fade in/out curves + durations */
          break;
        case 95:
          osrs_stream_skip(&s, 1); /* soundVisibility */
          break;
        case 96:
          osrs_stream_skip(&s, 1); /* raise */
          break;
        case 100: {
          /* entity sub-op: index u8 + subID u8 + string */
          osrs_stream_skip(&s, 2);
          while (osrs_stream_u8(&s) != 0)
            ;
          break;
        }
        case 101: {
          /* conditional op: index u8 + varp u16 + varb u16 + min u32 + max u32
           * + string */
          osrs_stream_skip(&s, 9);
          while (osrs_stream_u8(&s) != 0)
            ;
          break;
        }
        case 102: {
          /* conditional sub-op: index u8 + subID u16 + varp u16 + varb u16 +
           * min u32 + max u32 + string */
          osrs_stream_skip(&s, 11);
          while (osrs_stream_u8(&s) != 0)
            ;
          break;
        }
        case 249: {
          /* params: u8 count; per entry u8 type + u24 id + value
           * (type 1 = string, type 2 = u64, else u32) */
          int len = osrs_stream_u8(&s);
          for (int j = 0; j < len; j++) {
            int type = osrs_stream_u8(&s);
            osrs_stream_skip(&s, 3); /* param id u24 */
            if (type == 1) {
              while (osrs_stream_u8(&s) != 0)
                ;
            } else if (type == 2) {
              osrs_stream_skip(&s, 8);
            } else {
              osrs_stream_skip(&s, 4);
            }
          }
          break;
        }
        default:
          /* Truly unknown opcode: bail out to avoid misalignment garbage */
          break;
        }
      }
      break;
    }
  }

  return def;
}

/* Load underlay definition */
osrs_underlay_def_t *osrs_config_load_underlay(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  osrs_archive_files_t *files =
      config_get_archive(config, OSRS_CONFIG_UNDERLAY);
  if (!files)
    return NULL;

  osrs_underlay_def_t *def = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == id) {
      osrs_archive_file_t *file = &files->files[i];
      def = calloc(1, sizeof(osrs_underlay_def_t));
      if (!def)
        break;
      def->id = id;

      osrs_stream_t s;
      osrs_stream_init(&s, file->data, file->len);

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;
        if (opcode == 1) {
          /* 24-bit RGB color: RR GG BB */
          def->color = ((int)osrs_stream_u8(&s) << 16) |
                       ((int)osrs_stream_u8(&s) << 8) | (int)osrs_stream_u8(&s);
        }
        /* Other opcodes ignored for now */
      }
      break;
    }
  }

  return def;
}

void osrs_underlay_def_free(osrs_underlay_def_t *def) { free(def); }

/* Load overlay definition */
osrs_overlay_def_t *osrs_config_load_overlay(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  osrs_archive_files_t *files = config_get_archive(config, OSRS_CONFIG_OVERLAY);
  if (!files)
    return NULL;

  osrs_overlay_def_t *def = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == id) {
      osrs_archive_file_t *file = &files->files[i];
      def = calloc(1, sizeof(osrs_overlay_def_t));
      if (!def)
        break;
      def->id = id;

      osrs_stream_t s;
      osrs_stream_init(&s, file->data, file->len);

      /* Defaults (RuneLite OverlayDefinition) */
      def->texture = -1;
      def->secondary_color = -1;
      def->hide_underlay = true;

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;
        switch (opcode) {
        case 1:
          /* rgbColor u24 */
          def->color = ((int)osrs_stream_u8(&s) << 16) |
                       ((int)osrs_stream_u8(&s) << 8) | (int)osrs_stream_u8(&s);
          break;
        case 2:
          /* texture id (index 9) */
          def->texture = osrs_stream_u8(&s);
          break;
        case 5:
          /* hideUnderlay = false */
          def->hide_underlay = false;
          break;
        case 7:
          /* secondaryRgbColor u24 */
          def->secondary_color = ((int)osrs_stream_u8(&s) << 16) |
                                 ((int)osrs_stream_u8(&s) << 8) |
                                 (int)osrs_stream_u8(&s);
          break;
          /* Other opcodes ignored for now */
        }
      }
      break;
    }
  }

  return def;
}

void osrs_overlay_def_free(osrs_overlay_def_t *def) { free(def); }

/* Load NPC definition */
osrs_npc_def_t *osrs_config_load_npc(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  osrs_archive_files_t *files = config_get_archive(config, OSRS_CONFIG_NPC);
  if (!files)
    return NULL;

  osrs_npc_def_t *def = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == id) {
      osrs_archive_file_t *file = &files->files[i];
      def = calloc(1, sizeof(osrs_npc_def_t));
      if (!def)
        break;
      def->id = id;

      osrs_stream_t s;
      osrs_stream_init(&s, file->data, file->len);

      /* Default values */
      def->is_visible = true;
      def->is_minimap_visible = true;
      def->combat_level = -1;
      def->size = 1;
      def->width_scale = 128;
      def->height_scale = 128;

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;

        switch (opcode) {
        case 1: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_u16(&s);
            if (j < 16)
              def->model_ids[j] = mid;
          }
          break;
        }
        case 2:
          osrs_stream_string(&s, def->name, sizeof(def->name));
          break;
        case 12:
          def->size = osrs_stream_u8(&s);
          break;
        case 13:
          def->stand_anim = osrs_stream_u16(&s);
          break;
        case 14:
          def->walk_anim = osrs_stream_u16(&s);
          break;
        case 15:
          def->turn_left_anim = osrs_stream_u16(&s);
          break;
        case 16:
          def->turn_right_anim = osrs_stream_u16(&s);
          break;
        case 17:
          def->walk_anim = osrs_stream_u16(&s);
          def->turn_anim = osrs_stream_u16(&s);
          def->turn_left_anim = osrs_stream_u16(&s);
          def->turn_right_anim = osrs_stream_u16(&s);
          break;
        case 18:
          osrs_stream_u16(&s); /* category - no field */
          break;
        case 30:
        case 31:
        case 32:
        case 33:
        case 34: {
          char action[64];
          osrs_stream_string(&s, action, sizeof(action));
          if (strcmp(action, "Hidden") != 0) {
            strncpy(def->actions[opcode - 30], action,
                    sizeof(def->actions[0]) - 1);
            def->actions[opcode - 30][sizeof(def->actions[0]) - 1] = '\0';
          }
          break;
        }
        case 40: {
          int count = osrs_stream_u8(&s);
          def->recolor_count = count;
          for (int j = 0; j < count; j++) {
            int src = osrs_stream_u16(&s);
            int dst = osrs_stream_u16(&s);
            if (j < 8) {
              def->recolor_src[j] = src;
              def->recolor_dst[j] = dst;
            }
          }
          break;
        }
        case 41: {
          int count = osrs_stream_u8(&s);
          def->retexture_count = count;
          for (int j = 0; j < count; j++) {
            int src = osrs_stream_u16(&s);
            int dst = osrs_stream_u16(&s);
            if (j < 8) {
              def->retexture_src[j] = src;
              def->retexture_dst[j] = dst;
            }
          }
          break;
        }
        case 60: {
          int count = osrs_stream_u8(&s);
          def->chathead_count = count < 8 ? count : 8;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_u16(&s);
            if (j < 8)
              def->chathead_ids[j] = mid;
          }
          break;
        }
        case 61: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_i32(&s);
            if (j < 16)
              def->model_ids[j] = mid;
          }
          break;
        }
        case 62: {
          int count = osrs_stream_u8(&s);
          def->chathead_count = count < 8 ? count : 8;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_i32(&s);
            if (j < 8)
              def->chathead_ids[j] = mid;
          }
          break;
        }
        case 74:
          def->attack = osrs_stream_u16(&s);
          break;
        case 75:
          def->strength = osrs_stream_u16(&s);
          break;
        case 76:
          def->defence = osrs_stream_u16(&s);
          break;
        case 77:
          def->ranged = osrs_stream_u16(&s);
          break;
        case 78:
          def->magic = osrs_stream_u16(&s); /* stats[4] */
          break;
        case 79:
          def->hitpoints = osrs_stream_u16(&s); /* stats[5] */
          break;
        case 93:
          def->is_minimap_visible = false;
          break;
        case 95:
          def->combat_level = osrs_stream_u16(&s);
          break;
        case 97:
          def->width_scale = osrs_stream_u16(&s);
          break;
        case 98:
          def->height_scale = osrs_stream_u16(&s);
          break;
        case 99:
          /* renderPriority = 1 - no field */
          break;
        case 100:
          def->ambient = osrs_stream_i8(&s);
          break;
        case 101:
          def->contrast = osrs_stream_i8(&s);
          break;
        case 102: {
          /* headIcons - complex variable-length, read and discard */
          int bitfield = osrs_stream_u8(&s);
          int len = 0;
          for (int var5 = bitfield; var5 != 0; var5 >>= 1) {
            ++len;
          }
          for (int k = 0; k < len; k++) {
            if ((bitfield & (1 << k)) != 0) {
              osrs_stream_usmart(&s); /* bigSmart2 */
              if (s.pos < s.len && s.data[s.pos] < 128) {
                osrs_stream_u8(&s); /* unsignedShortSmartMinusOne approx */
              } else {
                osrs_stream_u16(&s);
              }
            }
          }
          break;
        }
        case 103:
          osrs_stream_u16(&s); /* rotationSpeed - no field */
          break;
        case 106: {
          int varbit = osrs_stream_u16(&s);
          def->varbit_id = (varbit == 0xFFFF) ? -1 : varbit;
          int varp = osrs_stream_u16(&s);
          def->varp_id = (varp == 0xFFFF) ? -1 : varp;
          int length = osrs_stream_u8(&s);
          def->transform_count = length + 2;
          for (int j = 0; j <= length && j < 16; j++) {
            int tid = osrs_stream_u16(&s);
            def->transform_ids[j] = (tid == 0xFFFF) ? -1 : tid;
          }
          break;
        }
        case 107:
          def->is_interactive = false;
          break;
        case 109:
          /* rotationFlag = false - no field */
          break;
        case 111:
          /* isFollower / renderPriority - no field */
          break;
        case 114:
          osrs_stream_u16(&s); /* runAnimation - no field */
          break;
        case 115:
          osrs_stream_u16(&s); /* runAnimation */
          osrs_stream_u16(&s); /* runRotate180Animation */
          osrs_stream_u16(&s); /* runRotateLeftAnimation */
          osrs_stream_u16(&s); /* runRotateRightAnimation */
          break;
        case 116:
          osrs_stream_u16(&s); /* crawlAnimation - no field */
          break;
        case 117:
          osrs_stream_u16(&s); /* crawlAnimation */
          osrs_stream_u16(&s); /* crawlRotate180Animation */
          osrs_stream_u16(&s); /* crawlRotateLeftAnimation */
          osrs_stream_u16(&s); /* crawlRotateRightAnimation */
          break;
        case 118: {
          int varbit = osrs_stream_u16(&s);
          def->varbit_id = (varbit == 0xFFFF) ? -1 : varbit;
          int varp = osrs_stream_u16(&s);
          def->varp_id = (varp == 0xFFFF) ? -1 : varp;
          int var = osrs_stream_u16(&s);
          if (var == 0xFFFF)
            var = -1;
          int length = osrs_stream_u8(&s);
          def->transform_count = length + 2;
          for (int j = 0; j <= length && j < 16; j++) {
            int tid = osrs_stream_u16(&s);
            def->transform_ids[j] = (tid == 0xFFFF) ? -1 : tid;
          }
          if (def->transform_count < 16)
            def->transform_ids[def->transform_count - 1] = var;
          break;
        }
        case 122:
          /* isFollower = true - no field */
          break;
        case 123:
          /* lowPriorityFollowerOps - no field */
          break;
        case 124:
          osrs_stream_u16(&s); /* height - no field */
          break;
        case 126:
          osrs_stream_u16(&s); /* footprintSize - no field */
          break;
        case 129:
        case 130:
        case 145:
          /* boolean flags with no data */
          break;
        case 146:
          osrs_stream_u16(&s); /* overlapTintHSL - no field */
          break;
        case 147:
          /* zbuf = false - no field */
          break;
        case 249: {
          int length = osrs_stream_u8(&s);
          for (int j = 0; j < length; j++) {
            int isString = osrs_stream_u8(&s);
            (void)osrs_stream_u8(&s);  /* key high byte */
            (void)osrs_stream_u16(&s); /* key low bytes */
            if (isString) {
              char str[256];
              osrs_stream_string(&s, str, sizeof(str));
            } else {
              osrs_stream_i32(&s);
            }
          }
          break;
        }
        case 251:
        case 252:
        case 253:
          /* entity ops sub/conditional - complex, skip */
          break;
        default:
          break;
        }
      }
      break;
    }
  }

  return def;
}

/* Load item definition */
osrs_item_def_t *osrs_config_load_item(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  osrs_archive_files_t *files = config_get_archive(config, OSRS_CONFIG_ITEM);
  if (!files)
    return NULL;

  osrs_item_def_t *def = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == id) {
      osrs_archive_file_t *file = &files->files[i];
      def = calloc(1, sizeof(osrs_item_def_t));
      if (!def)
        break;
      def->id = id;
      def->is_tradeable = true;
      def->value = 1;
      def->model_zoom = 2000; /* RuneLite ItemDefinition default */

      osrs_stream_t s;
      osrs_stream_init(&s, file->data, file->len);

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;

        switch (opcode) {
        case 1:
          def->inventory_model_id = osrs_stream_u16(&s);
          break;
        case 2:
          osrs_stream_string(&s, def->name, sizeof(def->name));
          break;
        case 3:
          osrs_stream_string(&s, def->examine, sizeof(def->examine));
          break;
        case 4:
          def->model_zoom = osrs_stream_u16(&s);
          break;
        case 5:
          def->model_rotation_x = osrs_stream_u16(&s);
          break;
        case 6:
          def->model_rotation_y = osrs_stream_u16(&s);
          break;
        case 7:
          def->model_offset_x = osrs_stream_u16(&s);
          if (def->model_offset_x > 32767)
            def->model_offset_x -= 65536;
          break;
        case 8:
          def->model_offset_y = osrs_stream_u16(&s);
          if (def->model_offset_y > 32767)
            def->model_offset_y -= 65536;
          break;
        case 9: {
          char unknown1[64];
          osrs_stream_string(&s, unknown1, sizeof(unknown1));
          break;
        }
        case 11:
          def->is_stackable = true;
          break;
        case 12:
          def->value = osrs_stream_i32(&s);
          break;
        case 13:
          def->equip_slot = osrs_stream_i8(&s);
          break;
        case 14:
          osrs_stream_i8(&s);
          break;
        case 15:
          def->is_tradeable = false;
          break;
        case 16:
          def->is_members = true;
          break;
        case 23:
          def->male_model_id = osrs_stream_u16(&s);
          def->male_model_offset = osrs_stream_u8(&s);
          break;
        case 24:
          def->male_model1_id = osrs_stream_u16(&s);
          break;
        case 25:
          def->female_model_id = osrs_stream_u16(&s);
          def->female_model_offset = osrs_stream_u8(&s);
          break;
        case 26:
          def->female_model1_id = osrs_stream_u16(&s);
          break;
        case 27:
          osrs_stream_i8(&s);
          break;
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
          osrs_stream_string(&s, def->ground_actions[opcode - 30],
                             sizeof(def->ground_actions[0]));
          break;
        case 35:
        case 36:
        case 37:
        case 38:
        case 39:
          osrs_stream_string(&s, def->inventory_actions[opcode - 35],
                             sizeof(def->inventory_actions[0]));
          break;
        case 40: {
          int count = osrs_stream_u8(&s);
          def->recolor_count = count;
          for (int j = 0; j < count; j++) {
            int src = osrs_stream_u16(&s);
            int dst = osrs_stream_u16(&s);
            if (j < 8) {
              def->recolor_src[j] = src;
              def->recolor_dst[j] = dst;
            }
          }
          break;
        }
        case 41: {
          int count = osrs_stream_u8(&s);
          def->retexture_count = count;
          for (int j = 0; j < count; j++) {
            int src = osrs_stream_u16(&s);
            int dst = osrs_stream_u16(&s);
            if (j < 8) {
              def->retexture_src[j] = src;
              def->retexture_dst[j] = dst;
            }
          }
          break;
        }
        case 42:
          def->shift_click_index = osrs_stream_i8(&s);
          break;
        case 43: {
          int op_id = osrs_stream_u8(&s);
          (void)op_id;
          while (true) {
            int subop_id = osrs_stream_u8(&s) - 1;
            if (subop_id == -1)
              break;
            char subop_str[256];
            osrs_stream_string(&s, subop_str, sizeof(subop_str));
          }
          break;
        }
        case 44:
          def->inventory_model_id = osrs_stream_i32(&s);
          break;
        case 45:
          def->male_model_id = osrs_stream_i32(&s);
          def->male_model_offset = osrs_stream_u8(&s);
          break;
        case 46:
          def->male_model1_id = osrs_stream_i32(&s);
          break;
        case 47:
          def->male_model2_id = osrs_stream_i32(&s);
          break;
        case 48:
          def->female_model_id = osrs_stream_i32(&s);
          def->female_model_offset = osrs_stream_u8(&s);
          break;
        case 49:
          def->female_model1_id = osrs_stream_i32(&s);
          break;
        case 50:
          def->female_model2_id = osrs_stream_i32(&s);
          break;
        case 51:
          def->male_chathead_id = osrs_stream_i32(&s);
          break;
        case 52:
          def->male_head_model2_id = osrs_stream_i32(&s);
          break;
        case 53:
          def->female_chathead_id = osrs_stream_i32(&s);
          break;
        case 54:
          def->female_head_model2_id = osrs_stream_i32(&s);
          break;
        case 65:
          /* geTradeable = true */
          break;
        case 75:
          osrs_stream_i16(&s);
          break;
        case 78:
          def->male_model2_id = osrs_stream_u16(&s);
          break;
        case 79:
          def->female_model2_id = osrs_stream_u16(&s);
          break;
        case 90:
          def->male_chathead_id = osrs_stream_u16(&s);
          break;
        case 91:
          def->female_chathead_id = osrs_stream_u16(&s);
          break;
        case 92:
          def->male_head_model2_id = osrs_stream_u16(&s);
          break;
        case 93:
          def->female_head_model2_id = osrs_stream_u16(&s);
          break;
        case 94:
          osrs_stream_u16(&s);
          break;
        case 95:
          def->model_rotation_z = osrs_stream_u16(&s);
          break;
        case 97:
          def->noted_id = osrs_stream_u16(&s);
          break;
        case 98:
          def->noted_template_id = osrs_stream_u16(&s);
          break;
        case 100:
        case 101:
        case 102:
        case 103:
        case 104:
        case 105:
        case 106:
        case 107:
        case 108:
        case 109:
          osrs_stream_u16(&s);
          osrs_stream_u16(&s);
          break;
        case 110:
          osrs_stream_u16(&s);
          break;
        case 111:
          osrs_stream_u16(&s);
          break;
        case 112:
          osrs_stream_u16(&s);
          break;
        case 113:
          def->ambient = osrs_stream_i8(&s);
          break;
        case 114:
          def->contrast = osrs_stream_i8(&s);
          break;
        case 115:
          osrs_stream_u8(&s);
          break;
        case 139:
          osrs_stream_u16(&s);
          break;
        case 140:
          osrs_stream_u16(&s);
          break;
        case 148:
          def->placeholder_id = osrs_stream_u16(&s);
          break;
        case 149:
          def->placeholder_template_id = osrs_stream_u16(&s);
          break;
        case 160:
          def->is_stackable = true;
          break;
        case 200: {
          osrs_stream_u8(&s);
          osrs_stream_u8(&s);
          char subop_str[256];
          osrs_stream_string(&s, subop_str, sizeof(subop_str));
          break;
        }
        case 201: {
          osrs_stream_u8(&s);
          osrs_stream_u16(&s);
          osrs_stream_u16(&s);
          osrs_stream_i32(&s);
          osrs_stream_i32(&s);
          char cond_str[256];
          osrs_stream_string(&s, cond_str, sizeof(cond_str));
          break;
        }
        case 202: {
          osrs_stream_u8(&s);
          osrs_stream_u16(&s);
          osrs_stream_u16(&s);
          osrs_stream_u16(&s);
          osrs_stream_i32(&s);
          osrs_stream_i32(&s);
          char cond_str[256];
          osrs_stream_string(&s, cond_str, sizeof(cond_str));
          break;
        }
        case 249: {
          int length = osrs_stream_u8(&s);
          for (int j = 0; j < length; j++) {
            int is_string = osrs_stream_u8(&s);
            int key = (osrs_stream_u8(&s) << 16) | osrs_stream_u16(&s);
            (void)key;
            if (is_string == 1) {
              char param_str[256];
              osrs_stream_string(&s, param_str, sizeof(param_str));
            } else if (is_string == 2) {
              osrs_stream_u64(&s);
            } else {
              osrs_stream_i32(&s);
            }
          }
          break;
        }
        default:
          break;
        }
      }
      break;
    }
  }

  return def;
}

/* Load sequence definition.
 * The archive holds every sequence delta-encoded; we must parse (and skip)
 * each def's opcodes to keep the stream aligned, storing only the target.
 * Opcode table matches RuneLite SequenceLoader (rev 239 / non-rev226). */
osrs_sequence_def_t *osrs_config_load_sequence(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  osrs_archive_files_t *files = osrs_cache_read_archive_files(
      config->cache, 2, OSRS_CONFIG_SEQUENCE, NULL);
  if (!files)
    return NULL;

  osrs_sequence_def_t *def = NULL;
  osrs_stream_t s;
  bool target = false;
  for (int fi = 0; fi < files->count && !def; fi++) {
    if (files->files[fi].id != id)
      continue;
    osrs_archive_file_t *file = &files->files[fi];
    def = calloc(1, sizeof(osrs_sequence_def_t));
    if (!def)
      break;
    def->id = id;
    target = true;
    osrs_stream_init(&s, file->data, file->len);

    /* Parse this def's opcodes. */
    for (;;) {
      if (osrs_stream_remaining(&s) < 1)
        break;
      int opcode = osrs_stream_u8(&s);
      if (opcode == 0)
        break;
      switch (opcode) {
      case 1: { /* frames: count, all delays, all id-low16, all id-high16 */
        int count = osrs_stream_u16(&s);
        if (target) {
          def->frame_count = count;
          def->frame_delays = calloc((size_t)count, sizeof(int));
          def->frame_ids = calloc((size_t)count, sizeof(int));
        }
        for (int i = 0; i < count; i++) {
          int v = osrs_stream_u16(&s);
          if (target)
            def->frame_delays[i] = v;
        }
        for (int i = 0; i < count; i++) {
          int v = osrs_stream_u16(&s);
          if (target)
            def->frame_ids[i] = v;
        }
        for (int i = 0; i < count; i++) {
          int v = osrs_stream_u16(&s);
          if (target)
            def->frame_ids[i] += v << 16;
        }
        break;
      }
      case 2: /* frameStep */
        (void)osrs_stream_u16(&s);
        break;
      case 3: { /* interleaveLeave: count bytes */
        int n = osrs_stream_u8(&s);
        for (int i = 0; i < n; i++)
          (void)osrs_stream_u8(&s);
        break;
      }
      case 4: /* stretches: no data */
        break;
      case 5: /* forcedPriority */
        (void)osrs_stream_u8(&s);
        break;
      case 6: /* leftHandItem */
      case 7: /* rightHandItem */
        (void)osrs_stream_u16(&s);
        break;
      case 8: /* maxLoops */
        (void)osrs_stream_u8(&s);
        break;
      case 9: /* precedenceAnimating */
        (void)osrs_stream_u8(&s);
        break;
      case 10: /* priority */
        if (target)
          def->priority = osrs_stream_u8(&s);
        else
          (void)osrs_stream_u8(&s);
        break;
      case 11: /* replyMode */
        (void)osrs_stream_u8(&s);
        break;
      case 12: { /* chatFrameIds: count, low16s, high16s */
        int n = osrs_stream_u8(&s);
        for (int i = 0; i < n * 2; i++)
          (void)osrs_stream_u16(&s);
        break;
      }
      case 13: /* animMayaID (int) */
        (void)osrs_stream_u32(&s);
        break;
      case 14: { /* frameSounds: u16 count x (frame u16 + 3-byte sound) */
        int n = osrs_stream_u16(&s);
        for (int i = 0; i < n * 5; i++)
          (void)osrs_stream_u8(&s);
        break;
      }
      case 15: /* animMayaStart + animMayaEnd */
        (void)osrs_stream_u16(&s);
        (void)osrs_stream_u16(&s);
        break;
      case 16: /* verticalOffset (1 byte) */
        (void)osrs_stream_u8(&s);
        break;
      case 17: { /* animMayaMasks: count bytes */
        int n = osrs_stream_u8(&s);
        for (int i = 0; i < n; i++)
          (void)osrs_stream_u8(&s);
        break;
      }
      case 18: { /* debugName: null-terminated string */
        while (osrs_stream_remaining(&s) > 0 && osrs_stream_u8(&s) != 0)
          ;
        break;
      }
      case 19: /* soundsCrossWorldView: no data */
        break;
      default:
        /* Unknown opcode: cannot skip safely; stop parsing this def. */
        opcode = 0;
        break;
      }
      if (opcode == 0)
        break;
    }

    if (target && def)
      break; /* found and parsed the target file */
  }

  osrs_archive_files_free(files);
  return def;
}

/* Load spotanim definition */
osrs_spotanim_def_t *osrs_config_load_spotanim(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  size_t len;
  uint8_t *data =
      osrs_cache_read_archive(config->cache, 2, OSRS_CONFIG_SPOTANIM, &len);
  if (!data)
    return NULL;

  osrs_container_t *container = osrs_container_decompress(data, len, NULL);
  free(data);
  if (!container)
    return NULL;

  osrs_stream_t s;
  osrs_stream_init(&s, container->data, container->data_len);

  osrs_spotanim_def_t *def = NULL;
  int current_id = 0;

  while (osrs_stream_remaining(&s) > 0) {
    int delta = osrs_stream_u16(&s);
    if (delta == 0)
      break;
    current_id += delta;

    if (current_id == id) {
      def = calloc(1, sizeof(osrs_spotanim_def_t));
      if (!def)
        break;

      def->id = current_id;

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;

        switch (opcode) {
        case 1:
          def->model_id = osrs_stream_u16(&s);
          break;
        case 2:
          def->animation_id = osrs_stream_u16(&s);
          break;
        case 3:
          def->rotation = osrs_stream_u16(&s);
          break;
        case 4:
          def->model_scale_x = osrs_stream_u16(&s);
          break;
        case 5:
          def->model_scale_y = osrs_stream_u16(&s);
          break;
        case 6:
          def->ambient = osrs_stream_u8(&s);
          break;
        case 7:
          def->contrast = osrs_stream_u8(&s);
          break;
        case 8:
          def->recolor_count = osrs_stream_u8(&s);
          for (int i = 0; i < def->recolor_count && i < 8; i++) {
            def->recolor_src[i] = osrs_stream_u16(&s);
            def->recolor_dst[i] = osrs_stream_u16(&s);
          }
          break;
        case 9:
          def->retexture_count = osrs_stream_u8(&s);
          for (int i = 0; i < def->retexture_count && i < 8; i++) {
            def->retexture_src[i] = osrs_stream_u16(&s);
            def->retexture_dst[i] = osrs_stream_u16(&s);
          }
          break;
        case 10:
          def->unknown_1 = osrs_stream_u16(&s);
          break;
        case 11:
          def->unknown_2 = osrs_stream_u16(&s);
          break;
        default:
          break;
        }
      }
      break;
    }
  }

  osrs_container_free(container);
  return def;
}

/* Free definitions */
void osrs_object_def_free(osrs_object_def_t *def) {
  if (!def)
    return;
  free(def);
}

void osrs_npc_def_free(osrs_npc_def_t *def) {
  if (!def)
    return;
  free(def);
}

/* Load kit (ident) definition — index 2 archive 3, file = kit id */
osrs_kit_def_t *osrs_config_load_kit(osrs_config_t *config, int id) {
  if (!config || !config->config_index)
    return NULL;

  osrs_archive_files_t *files =
      config_get_archive(config, OSRS_CONFIG_IDENTKIT);
  if (!files)
    return NULL;

  osrs_kit_def_t *def = NULL;
  for (int i = 0; i < files->count; i++) {
    if (files->files[i].id == id) {
      osrs_archive_file_t *file = &files->files[i];
      def = calloc(1, sizeof(osrs_kit_def_t));
      if (!def)
        break;
      def->id = id;
      def->body_part_id = -1;

      osrs_stream_t s;
      osrs_stream_init(&s, file->data, file->len);

      while (1) {
        int opcode = osrs_stream_u8(&s);
        if (opcode == 0)
          break;
        switch (opcode) {
        case 1:
          def->body_part_id = osrs_stream_u8(&s);
          break;
        case 2: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_u16(&s);
            if (j < 16)
              def->model_ids[j] = mid;
          }
          break;
        }
        case 3:
          def->non_selectable = true;
          break;
        case 5: {
          int count = osrs_stream_u8(&s);
          def->model_count = count < 16 ? count : 16;
          for (int j = 0; j < count; j++) {
            int mid = osrs_stream_i32(&s);
            if (j < 16)
              def->model_ids[j] = mid;
          }
          break;
        }
        case 40: {
          int count = osrs_stream_u8(&s);
          def->recolor_count = count;
          for (int j = 0; j < count; j++) {
            int src = (int16_t)osrs_stream_u16(&s);
            int dst = (int16_t)osrs_stream_u16(&s);
            if (j < 8) {
              def->recolor_src[j] = src;
              def->recolor_dst[j] = dst;
            }
          }
          break;
        }
        case 41: {
          int count = osrs_stream_u8(&s);
          for (int j = 0; j < count; j++) {
            osrs_stream_u16(&s);
            osrs_stream_u16(&s);
          }
          break;
        }
        default:
          /* chathead models (60-79) and others: skip conservatively */
          if (opcode >= 60 && opcode <= 69)
            osrs_stream_skip(&s, 2);
          else if (opcode >= 70 && opcode <= 79)
            osrs_stream_skip(&s, 4);
          break;
        }
      }
      break;
    }
  }

  return def;
}

void osrs_kit_def_free(osrs_kit_def_t *def) {
  if (!def)
    return;
  free(def);
}

void osrs_item_def_free(osrs_item_def_t *def) {
  if (!def)
    return;
  free(def);
}

void osrs_sequence_def_free(osrs_sequence_def_t *def) {
  if (!def)
    return;
  free(def->frame_ids);
  free(def->frame_delays);
  free(def);
}

void osrs_spotanim_def_free(osrs_spotanim_def_t *def) {
  if (!def)
    return;
  free(def);
}

/* Load model data */
uint8_t *osrs_config_load_model(osrs_config_t *config, int model_id,
                                size_t *out_len) {
  if (!config || !config->model_index)
    return NULL;

  /* Models are in archive 0 of index 7 */
  return osrs_cache_read_archive(config->cache, 7, model_id, out_len);
}

/* Load map data (with XTEA decryption) */
uint8_t *osrs_config_load_map(osrs_config_t *config, int region_id,
                              const uint32_t xtea_key[4], size_t *out_len) {
  if (!config || !config->region_index)
    return NULL;

  /* Map data is in index 5, archive = region_id */
  size_t len;
  uint8_t *data = osrs_cache_read_archive(config->cache, 5, region_id, &len);
  if (!data)
    return NULL;

  /* Decompress container with XTEA key */
  osrs_container_t *container = osrs_container_decompress(data, len, xtea_key);
  free(data);
  if (!container)
    return NULL;

  *out_len = container->data_len;
  uint8_t *result = container->data;
  free(container);
  return result;
}

/* Load landscape data (with XTEA decryption) */
uint8_t *osrs_config_load_landscape(osrs_config_t *config, int region_id,
                                    const uint32_t xtea_key[4],
                                    size_t *out_len) {
  if (!config || !config->region_index)
    return NULL;

  /* Landscape data is in index 5, archive = region_id */
  size_t len;
  uint8_t *data = osrs_cache_read_archive(config->cache, 5, region_id, &len);
  if (!data)
    return NULL;

  /* Decompress container with XTEA key */
  osrs_container_t *container = osrs_container_decompress(data, len, xtea_key);
  free(data);
  if (!container)
    return NULL;

  *out_len = container->data_len;
  uint8_t *result = container->data;
  free(container);
  return result;
}

uint32_t *osrs_config_texture_pixels(osrs_config_t *config, int texture_id) {
  osrs_texture_t *tex = osrs_config_load_texture(config, texture_id);
  if (!tex || tex->file_id < 0) {
    if (tex)
      osrs_texture_free(tex);
    return NULL;
  }
  int file_id = tex->file_id;
  osrs_texture_free(tex);

  osrs_sprite_t *spr = osrs_config_load_sprite(config, file_id, 0);
  if (!spr || !spr->pixels || spr->width <= 0 || spr->height <= 0) {
    if (spr)
      osrs_sprite_free(spr);
    return NULL;
  }

  uint32_t *rgb = malloc(128 * 128 * sizeof(uint32_t));
  if (!rgb) {
    osrs_sprite_free(spr);
    return NULL;
  }

  const double BRIGHT = 0.8;
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) {
      int sx = x * spr->width / 128;
      int sy = y * spr->height / 128;
      int idx = spr->pixels[sy * spr->width + sx] & 0xFF;
      int c = spr->palette[idx];
      int r = (int)(pow((double)((c >> 16) & 0xFF) / 256.0, BRIGHT) * 256.0);
      int g = (int)(pow((double)((c >> 8) & 0xFF) / 256.0, BRIGHT) * 256.0);
      int b = (int)(pow((double)(c & 0xFF) / 256.0, BRIGHT) * 256.0);
      if (r > 255)
        r = 255;
      if (g > 255)
        g = 255;
      if (b > 255)
        b = 255;
      rgb[y * 128 + x] =
          0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
  }

  osrs_sprite_free(spr);
  return rgb;
}

/* Load sprite (index 8, archive = sprite id, file_id = frame index).
 * Trailer-based format (RuneLite SpriteLoader):
 *   [pixel data...][palette u24 x (paletteLength-1)][header: maxW u16,
 *   maxH u16, paletteLen-1 u8, per-sprite offsetX/Y/W/H u16 x count]
 *   [spriteCount u16 at len-2]
 * Per-sprite pixel data: flags u8 (bit0=vertical scan, bit1=has alpha),
 * then width*height palette indices, then optional alpha bytes. */
osrs_sprite_t *osrs_config_load_sprite(osrs_config_t *config, int archive_id,
                                       int file_id) {
  if (!config || !config->sprite_index)
    return NULL;

  size_t len;
  uint8_t *data = osrs_cache_read_archive(config->cache, 8, archive_id, &len);
  if (!data)
    return NULL;

  osrs_container_t *container = osrs_container_decompress(data, len, NULL);
  free(data);
  if (!container)
    return NULL;

  const uint8_t *d = container->data;
  size_t n = container->data_len;
  if (n < 9) {
    osrs_container_free(container);
    return NULL;
  }

  int sprite_count = (d[n - 2] << 8) | d[n - 1];
  if (sprite_count < 1 || file_id >= sprite_count) {
    osrs_container_free(container);
    return NULL;
  }

  /* Header block at len-7-count*8 */
  size_t hdr = n - 7 - (size_t)sprite_count * 8;
  int max_w = (d[hdr] << 8) | d[hdr + 1];
  int max_h = (d[hdr + 2] << 8) | d[hdr + 3];
  int palette_len = d[hdr + 4] + 1;

  const uint8_t *offx_p = d + hdr + 5;
  const uint8_t *offy_p = offx_p + (size_t)sprite_count * 2;
  const uint8_t *width_p = offy_p + (size_t)sprite_count * 2;
  const uint8_t *height_p = width_p + (size_t)sprite_count * 2;

  /* Palette at hdr - (palette_len-1)*3 */
  size_t pal_off = hdr - (size_t)(palette_len - 1) * 3;
  int palette[256] = {0};
  for (int i = 1; i < palette_len && i < 256; i++) {
    const uint8_t *p = d + pal_off + (size_t)(i - 1) * 3;
    palette[i] = (p[0] << 16) | (p[1] << 8) | p[2];
    if (palette[i] == 0)
      palette[i] = 1;
  }

  /* Pixel data starts at offset 0, sequential per sprite */
  size_t pos = 0;
  osrs_sprite_t *sprite = NULL;
  for (int i = 0; i < sprite_count; i++) {
    int w = (width_p[i * 2] << 8) | width_p[i * 2 + 1];
    int h = (height_p[i * 2] << 8) | height_p[i * 2 + 1];
    int ox = (offx_p[i * 2] << 8) | offx_p[i * 2 + 1];
    int oy = (offy_p[i * 2] << 8) | offy_p[i * 2 + 1];
    int dim = w * h;
    if (dim < 0 || pos + 1 > n)
      break;

    int flags = d[pos++];
    bool vertical = (flags & 1) != 0;
    bool has_alpha = (flags & 2) != 0;

    if (i == file_id) {
      sprite = calloc(1, sizeof(osrs_sprite_t));
      if (!sprite)
        break;
      sprite->id = archive_id;
      sprite->width = w;
      sprite->height = h;
      sprite->offset_x = ox;
      sprite->offset_y = oy;
      sprite->pixel_count = dim;
      memcpy(sprite->palette, palette, sizeof(palette));
      sprite->pixels = malloc((size_t)dim);
      if (!sprite->pixels) {
        free(sprite);
        sprite = NULL;
        break;
      }
      /* Read palette indices */
      if (!vertical) {
        for (int j = 0; j < dim; j++)
          sprite->pixels[j] = d[pos + j];
        pos += (size_t)dim;
      } else {
        int k = 0;
        for (int x = 0; x < w; x++)
          for (int y = 0; y < h; y++)
            sprite->pixels[w * y + x] = d[pos + k++];
        pos += (size_t)dim;
      }
      /* Alpha bytes (skip; we treat index 0 as transparent) */
      if (has_alpha)
        pos += (size_t)dim;
      (void)max_w;
      (void)max_h;
    } else {
      /* Skip this sprite's pixel data */
      pos += (size_t)dim;
      if (has_alpha)
        pos += (size_t)dim;
    }
  }

  osrs_container_free(container);
  return sprite;
}

void osrs_sprite_free(osrs_sprite_t *sprite) {
  if (!sprite)
    return;
  free(sprite->pixels);
  free(sprite);
}

/* Load texture definition (index 9, archive 0, file = texture id).
 * Rev233 layout: fileId u16 (sprite archive in index 8), missingColor u16
 * (average color, packed HSL16), field1778 u8, animationDirection u8,
 * animationSpeed u8.  Pixels come from the referenced sprite (128x128). */
osrs_texture_t *osrs_config_load_texture(osrs_config_t *config, int id) {
  if (!config || !config->texture_index)
    return NULL;

  size_t len;
  uint8_t *data = osrs_cache_read_archive_file(config->cache, 9, 0, id, &len);
  if (!data) {
    /* Fallback: try as a plain archive (older layout) */
    data = osrs_cache_read_archive(config->cache, 9, id, &len);
    if (!data)
      return NULL;
  }

  osrs_texture_t *texture = calloc(1, sizeof(osrs_texture_t));
  if (!texture) {
    free(data);
    return NULL;
  }

  if (len >= 6) {
    texture->id = id;
    texture->file_id = (int)((data[0] << 8) | data[1]);
    texture->average_rgb = (int)((data[2] << 8) | data[3]); /* HSL16 */
    texture->is_opaque = data[4] == 1;
    texture->is_mipmap = false;
    texture->width = 128;
    texture->height = 128;
  } else {
    texture->id = id;
    texture->file_id = -1;
  }

  free(data);
  return texture;
}

void osrs_texture_free(osrs_texture_t *texture) {
  if (!texture)
    return;
  free(texture->pixels);
  free(texture);
}

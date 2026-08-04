/*
 * tools/config_dump.c — Dump OSRS config definitions
 * Pure C23, zero external dependencies.
 */

#include "osrs/cache.h"
#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s <cache_path> <type> <id>\n", prog);
  fprintf(stderr, "  type: object, npc, item, sequence, spotanim, model, "
                  "sprite, texture, map\n");
  fprintf(stderr, "  id:   definition ID (or region ID for map)\n");
}

int main(int argc, char **argv) {
  if (argc < 4) {
    print_usage(argv[0]);
    return 1;
  }

  const char *cache_path = argv[1];
  const char *type = argv[2];
  int id = atoi(argv[3]);

  printf("Opening OSRS cache at: %s\n", cache_path);

  osrs_cache_t *cache = osrs_cache_open(cache_path);
  if (!cache) {
    fprintf(stderr, "Failed to open cache\n");
    return 1;
  }

  printf("Loading indexes...\n");
  if (!osrs_cache_load_indexes(cache)) {
    fprintf(stderr, "Failed to load indexes\n");
    osrs_cache_close(cache);
    return 1;
  }

  printf("Loaded %d indexes\n\n", cache->index_count);

  osrs_config_t *config = osrs_config_init(cache);
  if (!config) {
    fprintf(stderr, "Failed to init config\n");
    osrs_cache_close(cache);
    return 1;
  }

  if (strcmp(type, "object") == 0) {
    osrs_object_def_t *def = osrs_config_load_object(config, id);
    if (def) {
      printf("Object %d:\n", def->id);
      printf("  Name: %s\n", def->name);
      printf("  Size: %dx%d\n", def->size_x, def->size_y);
      printf("  Interact type: %d\n", def->interact_type);
      printf("  Blocks projectile: %s\n",
             def->blocks_projectile ? "yes" : "no");
      printf("  Animation ID: %d\n", def->animation_id);
      printf("  Actions: ");
      for (int i = 0; i < 5; i++) {
        if (def->actions[i][0])
          printf("[%d]%s ", i, def->actions[i]);
      }
      printf("\n");
      printf("  Model count: %d\n", def->model_count);
      printf("  Recolor count: %d\n", def->recolor_count);
      printf("  Retexture count: %d\n", def->retexture_count);
      printf("  Category: %d\n", def->category);
      osrs_object_def_free(def);
    } else {
      printf("Object %d not found\n", id);
    }
  } else if (strcmp(type, "npc") == 0) {
    osrs_npc_def_t *def = osrs_config_load_npc(config, id);
    if (def) {
      printf("NPC %d:\n", def->id);
      printf("  Name: %s\n", def->name);
      printf("  Combat level: %d\n", def->combat_level);
      printf("  Size: %d\n", def->size);
      printf("  Hitpoints: %d\n", def->hitpoints);
      printf("  Attack: %d\n", def->attack);
      printf("  Strength: %d\n", def->strength);
      printf("  Defence: %d\n", def->defence);
      printf("  Ranged: %d\n", def->ranged);
      printf("  Magic: %d\n", def->magic);
      printf("  Attack bonus: %d\n", def->attack_bonus);
      printf("  Strength bonus: %d\n", def->strength_bonus);
      printf("  Attack speed: %d\n", def->attack_speed);
      printf("  Slayer level: %d\n", def->slayer_level);
      printf("  Is aggressive: %s\n", def->is_aggressive ? "yes" : "no");
      printf("  Is dragon: %s\n", def->is_dragon ? "yes" : "no");
      printf("  Is demon: %s\n", def->is_demon ? "yes" : "no");
      printf("  Is undead: %s\n", def->is_undead ? "yes" : "no");
      printf("  Actions: ");
      for (int i = 0; i < 5; i++) {
        if (def->actions[i][0])
          printf("%s ", def->actions[i]);
      }
      printf("\n");
      printf("  Model count: %d\n", def->model_count);
      printf("  Transform count: %d\n", def->transform_count);
      osrs_npc_def_free(def);
    } else {
      printf("NPC %d not found\n", id);
    }
  } else if (strcmp(type, "item") == 0) {
    osrs_item_def_t *def = osrs_config_load_item(config, id);
    if (def) {
      printf("Item %d:\n", def->id);
      printf("  Name: %s\n", def->name);
      printf("  Examine: %s\n", def->examine);
      printf("  Value: %d\n", def->value);
      printf("  High alch: %d\n", def->high_alch);
      printf("  Low alch: %d\n", def->low_alch);
      printf("  Is members: %s\n", def->is_members ? "yes" : "no");
      printf("  Is stackable: %s\n", def->is_stackable ? "yes" : "no");
      printf("  Is tradeable: %s\n", def->is_tradeable ? "yes" : "no");
      printf("  Is equipable: %s\n", def->is_equipable ? "yes" : "no");
      printf("  Equip slot: %d\n", def->equip_slot);
      printf("  Attack stab/slash/crush: %d/%d/%d\n", def->attack_stab,
             def->attack_slash, def->attack_crush);
      printf("  Attack magic/ranged: %d/%d\n", def->attack_magic,
             def->attack_ranged);
      printf("  Defence stab/slash/crush: %d/%d/%d\n", def->defence_stab,
             def->defence_slash, def->defence_crush);
      printf("  Defence magic/ranged: %d/%d\n", def->defence_magic,
             def->defence_ranged);
      printf("  Strength bonus: %d\n", def->strength_bonus);
      printf("  Ranged strength: %d\n", def->ranged_strength);
      printf("  Magic damage: %d\n", def->magic_damage);
      printf("  Prayer bonus: %d\n", def->prayer_bonus);
      printf("  Attack speed: %d\n", def->attack_speed);
      printf("  Model ID: %d\n", def->model_id);
      printf("  Inventory model ID: %d\n", def->inventory_model_id);
      osrs_item_def_free(def);
    } else {
      printf("Item %d not found\n", id);
    }
  } else if (strcmp(type, "sequence") == 0) {
    osrs_sequence_def_t *def = osrs_config_load_sequence(config, id);
    if (def) {
      printf("Sequence %d:\n", def->id);
      printf("  Frame count: %d\n", def->frame_count);
      printf("  Loop count: %d\n", def->loop_count);
      printf("  Priority: %d\n", def->priority);
      printf("  Left hand item: %d\n", def->left_hand_item);
      printf("  Right hand item: %d\n", def->right_hand_item);
      printf("  Max loops: %d\n", def->max_loops);
      printf("  Animating precedence: %d\n", def->animating_precedence);
      printf("  Walking precedence: %d\n", def->walking_precedence);
      printf("  Replay mode: %d\n", def->replay_mode);
      printf("  Interface ID: %d\n", def->interface_id);
      osrs_sequence_def_free(def);
    } else {
      printf("Sequence %d not found\n", id);
    }
  } else if (strcmp(type, "spotanim") == 0) {
    osrs_spotanim_def_t *def = osrs_config_load_spotanim(config, id);
    if (def) {
      printf("Spotanim %d:\n", def->id);
      printf("  Model ID: %d\n", def->model_id);
      printf("  Animation ID: %d\n", def->animation_id);
      printf("  Rotation: %d\n", def->rotation);
      printf("  Model scale: %dx%d\n", def->model_scale_x, def->model_scale_y);
      printf("  Ambient: %d\n", def->ambient);
      printf("  Contrast: %d\n", def->contrast);
      osrs_spotanim_def_free(def);
    } else {
      printf("Spotanim %d not found\n", id);
    }
  } else if (strcmp(type, "model") == 0) {
    size_t len;
    uint8_t *data = osrs_config_load_model(config, id, &len);
    if (data) {
      printf("Model %d: %zu bytes\n", id, len);
      printf("First 64 bytes: ");
      for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02X ", data[i]);
      }
      printf("\n");
      free(data);
    } else {
      printf("Model %d not found\n", id);
    }
  } else if (strcmp(type, "sprite") == 0) {
    int file_id = (argc >= 5) ? atoi(argv[4]) : 0;
    osrs_sprite_t *sprite = osrs_config_load_sprite(config, id, file_id);
    if (sprite) {
      printf("Sprite %d/%d:\n", id, file_id);
      printf("  Size: %dx%d\n", sprite->width, sprite->height);
      printf("  Offset: %d,%d\n", sprite->offset_x, sprite->offset_y);
      printf("  Pixel count: %d\n", sprite->pixel_count);
      osrs_sprite_free(sprite);
    } else {
      printf("Sprite %d/%d not found\n", id, file_id);
    }
  } else if (strcmp(type, "texture") == 0) {
    osrs_texture_t *texture = osrs_config_load_texture(config, id);
    if (texture) {
      printf("Texture %d:\n", texture->id);
      printf("  File ID: %d\n", texture->file_id);
      printf("  Size: %dx%d\n", texture->width, texture->height);
      printf("  Is opaque: %s\n", texture->is_opaque ? "yes" : "no");
      printf("  Is mipmap: %s\n", texture->is_mipmap ? "yes" : "no");
      printf("  Average RGB: %06X\n", texture->average_rgb);
      osrs_texture_free(texture);
    } else {
      printf("Texture %d not found\n", id);
    }
  } else if (strcmp(type, "map") == 0) {
    /* Map loading requires XTEA keys */
    printf("Map loading requires XTEA keys (not implemented in this tool)\n");
  } else {
    fprintf(stderr, "Unknown type: %s\n", type);
    print_usage(argv[0]);
    osrs_config_free(config);
    osrs_cache_close(cache);
    return 1;
  }

  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}

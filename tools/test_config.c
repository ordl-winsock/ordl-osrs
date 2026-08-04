/*
 * tools/test_config.c — Test config definition loaders
 */

#include "osrs/config.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <cache_path>\n", argv[0]);
    return 1;
  }

  osrs_cache_t *cache = osrs_cache_open(argv[1]);
  if (!cache) {
    fprintf(stderr, "Failed to open cache\n");
    return 1;
  }

  if (!osrs_cache_load_indexes(cache)) {
    fprintf(stderr, "Failed to load indexes\n");
    osrs_cache_close(cache);
    return 1;
  }

  osrs_config_t *config = osrs_config_init(cache);
  if (!config) {
    fprintf(stderr, "Failed to init config\n");
    osrs_cache_close(cache);
    return 1;
  }

  /* Test object definitions */
  printf("=== Object Definitions ===\n");
  for (int i = 0; i < 5; i++) {
    osrs_object_def_t *obj = osrs_config_load_object(config, i);
    if (obj) {
      printf("Object %d: name='%s' size=%dx%d interact=%d\n", obj->id,
             obj->name, obj->size_x, obj->size_y, obj->interact_type);
      osrs_object_def_free(obj);
    }
  }

  /* Test NPC definitions */
  printf("\n=== NPC Definitions ===\n");
  for (int i = 0; i < 5; i++) {
    osrs_npc_def_t *npc = osrs_config_load_npc(config, i);
    if (npc) {
      printf("NPC %d: name='%s' level=%d size=%d\n", npc->id, npc->name,
             npc->combat_level, npc->size);
      osrs_npc_def_free(npc);
    }
  }

  /* Test item definitions */
  printf("\n=== Item Definitions ===\n");
  for (int i = 0; i < 5; i++) {
    osrs_item_def_t *item = osrs_config_load_item(config, i);
    if (item) {
      printf("Item %d: name='%s' value=%d members=%d\n", item->id, item->name,
             item->value, item->is_members);
      osrs_item_def_free(item);
    }
  }

  /* Test underlay */
  printf("\n=== Underlay Definitions ===\n");
  for (int i = 0; i < 5; i++) {
    osrs_underlay_def_t *u = osrs_config_load_underlay(config, i);
    if (u) {
      printf("Underlay %d: color=0x%06X\n", u->id, u->color);
      osrs_underlay_def_free(u);
    }
  }

  /* Test overlay */
  printf("\n=== Overlay Definitions ===\n");
  for (int i = 0; i < 5; i++) {
    osrs_overlay_def_t *o = osrs_config_load_overlay(config, i);
    if (o) {
      printf("Overlay %d: color=0x%06X hide=%d\n", o->id, o->color,
             o->hide_underlay);
      osrs_overlay_def_free(o);
    }
  }

  osrs_config_free(config);
  osrs_cache_close(cache);
  return 0;
}

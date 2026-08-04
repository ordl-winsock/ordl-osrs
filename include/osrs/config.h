/*
 * osrs/config.h — OSRS config definition loaders
 * Pure C23, zero external dependencies.
 *
 * Loads game configuration data from cache archives:
 * objects, NPCs, items, sprites, textures, animations, etc.
 */

#ifndef OSRS_CONFIG_H
#define OSRS_CONFIG_H

#include "osrs/cache.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config archive IDs (from index 2) */
#define OSRS_CONFIG_UNDERLAY 1
#define OSRS_CONFIG_IDENTKIT 3
#define OSRS_CONFIG_OVERLAY 4
#define OSRS_CONFIG_INV 5
#define OSRS_CONFIG_OBJECT 6
#define OSRS_CONFIG_ENUM 8
#define OSRS_CONFIG_NPC 9
#define OSRS_CONFIG_ITEM 10
#define OSRS_CONFIG_PARAM 11
#define OSRS_CONFIG_SEQUENCE 12
#define OSRS_CONFIG_SPOTANIM 13
#define OSRS_CONFIG_VARBIT 14
#define OSRS_CONFIG_VARPLAYER 16
#define OSRS_CONFIG_AREA 17
#define OSRS_CONFIG_STRUCT 18
#define OSRS_CONFIG_DBROW 19
#define OSRS_CONFIG_DBTABLE 20
#define OSRS_CONFIG_DBINDEX 21
#define OSRS_CONFIG_WORLDMAP 22
#define OSRS_CONFIG_WORLD 23
#define OSRS_CONFIG_REGION 24
#define OSRS_CONFIG_MAPLABEL 25
#define OSRS_CONFIG_INTERFACE 26
#define OSRS_CONFIG_MIDI 27
#define OSRS_CONFIG_MODEL 28
#define OSRS_CONFIG_SPRITE 29
#define OSRS_CONFIG_TEXTURE 30
#define OSRS_CONFIG_BINARY 31
#define OSRS_CONFIG_JINGLED 32
#define OSRS_CONFIG_CLIENTSCRIPT 33
#define OSRS_CONFIG_FONT 34
#define OSRS_CONFIG_VORBIS 35
#define OSRS_CONFIG_WORLDMAPV2 36

/* Object definition */
typedef struct {
  int id;
  char name[64];
  int size_x;
  int size_y;
  int interact_type;
  bool blocks_projectile;
  bool contoured_ground;
  int decor_displacement;
  int animation_id;
  int wall_or_door;
  int ambient;
  int contrast;
  char actions[5][64];
  int model_ids[16];
  int model_types[16];
  int model_count;
  bool has_model_types;
  int recolor_src[8];
  int recolor_dst[8];
  int recolor_count;
  int retexture_src[8];
  int retexture_dst[8];
  int retexture_count;
  int model_size_x;
  int model_size_height;
  int model_size_y;
  int offset_x;
  int offset_height;
  int offset_y;
  int map_scene_id;
  int map_area_id;
  int category;
  int varbit_id;
  int varp_id;
  int transform_ids[16];
  int transform_count;
  int ambient_sound_id;
  int ambient_sound_distance;
  bool rotated;
  bool merge_normals;
  bool model_clipped;
  bool obstructs_ground;
  bool hollow;
  int blocking_mask;
  int supports_items;
} osrs_object_def_t;

/* Underlay definition */
typedef struct {
  int id;
  int color;
} osrs_underlay_def_t;

/* Overlay definition */
typedef struct {
  int id;
  int color;           /* rgbColor u24 (0 = none) */
  int texture;         /* texture id into index 9 (-1 = none) */
  int secondary_color; /* secondaryRgbColor u24 (-1 = none) */
  bool hide_underlay;  /* default true; opcode 5 clears */
} osrs_overlay_def_t;

/* NPC definition */
typedef struct {
  int id;
  char name[64];
  int combat_level;
  int size;
  int stand_anim;
  int walk_anim;
  int turn_anim;
  int turn_left_anim;
  int turn_right_anim;
  int attack_anim;
  int defend_anim;
  int death_anim;
  int prayer_anim;
  int hitpoints;
  int attack;
  int strength;
  int defence;
  int ranged;
  int magic;
  int attack_bonus;
  int strength_bonus;
  int defence_bonus;
  int ranged_bonus;
  int magic_bonus;
  int stab_defence;
  int slash_defence;
  int crush_defence;
  int magic_defence;
  int ranged_defence;
  int attack_speed;
  int slayer_level;
  int slayer_xp;
  bool is_aggressive;
  bool is_poisonous;
  bool is_venomous;
  bool is_immune_poison;
  bool is_immune_venom;
  bool is_immune_cannon;
  bool is_immune_thrall;
  bool is_dragon;
  bool is_demon;
  bool is_undead;
  char actions[5][64];
  int model_ids[16];
  int model_count;
  int recolor_src[8];
  int recolor_dst[8];
  int recolor_count;
  int retexture_src[8];
  int retexture_dst[8];
  int retexture_count;
  int head_model_ids[8];
  int head_model_count;
  int chathead_ids[8];
  int chathead_count;
  int transform_ids[16];
  int transform_count;
  int varp_id;
  int varbit_id;
  bool is_interactive;
  bool is_visible;
  bool is_minimap_visible;
  int ambient_sound_id;
  int ambient_sound_distance;
  int ambient_sound_change;
  bool has_sound;
  int walk_priority;
  int death_priority;
  int respawn_time;
  int unknown_1;
  int unknown_2;
  int ambient;
  int contrast;
  int width_scale;
  int height_scale;
} osrs_npc_def_t;

/* Item definition */
typedef struct {
  int id;
  char name[64];
  char examine[256];
  int value;
  int high_alch;
  int low_alch;
  bool is_members;
  bool is_stackable;
  bool is_noted;
  bool is_noteable;
  bool is_tradeable;
  bool is_equipable;
  bool is_two_handed;
  bool is_full_body;
  bool is_full_head;
  bool is_full_mask;
  int equip_slot;
  int attack_stab;
  int attack_slash;
  int attack_crush;
  int attack_magic;
  int attack_ranged;
  int defence_stab;
  int defence_slash;
  int defence_crush;
  int defence_magic;
  int defence_ranged;
  int strength_bonus;
  int ranged_strength;
  int magic_damage;
  int prayer_bonus;
  int attack_speed;
  int model_id;
  int model_zoom;
  int model_offset_x;
  int model_offset_y;
  int model_rotation_x;
  int model_rotation_y;
  int model_rotation_z;
  int inventory_model_id;
  int male_model_id;
  int male_model_offset;
  int male_model1_id;
  int male_model2_id;
  int female_model_id;
  int female_model_offset;
  int female_model1_id;
  int female_model2_id;
  int male_chathead_id;
  int female_chathead_id;
  int male_head_model2_id;
  int female_head_model2_id;
  int recolor_src[8];
  int recolor_dst[8];
  int recolor_count;
  int retexture_src[8];
  int retexture_dst[8];
  int retexture_count;
  char ground_actions[5][64];
  char inventory_actions[5][64];
  int shift_click_index;
  int ambient;
  int contrast;
  int noted_id;
  int noted_template_id;
  int placeholder_id;
  int placeholder_template_id;
  int unknown_1;
  int unknown_2;
} osrs_item_def_t;

/* Sprite definition */
typedef struct {
  int id;
  int width;
  int height;
  int offset_x;
  int offset_y;
  int palette[256];
  uint8_t *pixels;
  int pixel_count;
} osrs_sprite_t;

/* Kit (ident) definition — base body parts for player appearance */
typedef struct {
  int id;
  int body_part_id; /* which body slot (0=hair,1=jaw,2=torso,3=arms,
                       4=hands,5=legs,6=feet) */
  int model_ids[16];
  int model_count;
  int recolor_src[8];
  int recolor_dst[8];
  int recolor_count;
  bool non_selectable;
} osrs_kit_def_t;

/* Texture definition */
typedef struct {
  int id;
  int file_id;
  int width;
  int height;
  bool is_opaque;
  bool is_mipmap;
  uint8_t *pixels;
  int pixel_count;
  int average_rgb;
} osrs_texture_t;

/* Animation (Sequence) definition */
typedef struct {
  int id;
  int frame_count;
  int *frame_ids;
  int *frame_delays;
  int loop_count;
  int priority;
  int left_hand_item;
  int right_hand_item;
  int max_loops;
  int animating_precedence;
  int walking_precedence;
  int replay_mode;
  int interface_id;
  int unknown_1;
  int unknown_2;
} osrs_sequence_def_t;

/* Spot animation definition */
typedef struct {
  int id;
  int model_id;
  int animation_id;
  int rotation;
  int model_scale_x;
  int model_scale_y;
  int ambient;
  int contrast;
  int recolor_src[8];
  int recolor_dst[8];
  int recolor_count;
  int retexture_src[8];
  int retexture_dst[8];
  int retexture_count;
  int unknown_1;
  int unknown_2;
} osrs_spotanim_def_t;

/* Config manager */
typedef struct {
  osrs_cache_t *cache;
  osrs_index_t *config_index;
  osrs_index_t *model_index;
  osrs_index_t *sprite_index;
  osrs_index_t *texture_index;
  osrs_index_t *midi_index;
  osrs_index_t *binary_index;
  osrs_index_t *clientscript_index;
  osrs_index_t *font_index;
  osrs_index_t *vorbis_index;
  osrs_index_t *worldmap_index;
  osrs_index_t *world_index;
  osrs_index_t *region_index;
  osrs_index_t *maplabel_index;
  osrs_index_t *interface_index;
  osrs_index_t *jingled_index;
  osrs_index_t *worldmapv2_index;

  /* Memoized split archives (lazy-loaded on first definition request).
   * Avoids re-reading + re-decompressing the whole config archive per
   * definition lookup. Freed by osrs_config_free(). */
  osrs_archive_files_t *object_archive;
  osrs_archive_files_t *npc_archive;
  osrs_archive_files_t *item_archive;
  osrs_archive_files_t *underlay_archive;
  osrs_archive_files_t *overlay_archive;
  osrs_archive_files_t *kit_archive;
} osrs_config_t;

/* Initialize config manager */
osrs_config_t *osrs_config_init(osrs_cache_t *cache);
void osrs_config_free(osrs_config_t *config);

/* Load definitions */
osrs_object_def_t *osrs_config_load_object(osrs_config_t *config, int id);
osrs_underlay_def_t *osrs_config_load_underlay(osrs_config_t *config, int id);
osrs_overlay_def_t *osrs_config_load_overlay(osrs_config_t *config, int id);
osrs_npc_def_t *osrs_config_load_npc(osrs_config_t *config, int id);
osrs_item_def_t *osrs_config_load_item(osrs_config_t *config, int id);
osrs_kit_def_t *osrs_config_load_kit(osrs_config_t *config, int id);
osrs_sequence_def_t *osrs_config_load_sequence(osrs_config_t *config, int id);
osrs_spotanim_def_t *osrs_config_load_spotanim(osrs_config_t *config, int id);

/* Free definitions */
void osrs_object_def_free(osrs_object_def_t *def);
void osrs_underlay_def_free(osrs_underlay_def_t *def);
void osrs_overlay_def_free(osrs_overlay_def_t *def);
void osrs_npc_def_free(osrs_npc_def_t *def);
void osrs_item_def_free(osrs_item_def_t *def);
void osrs_kit_def_free(osrs_kit_def_t *def);
void osrs_sequence_def_free(osrs_sequence_def_t *def);
void osrs_spotanim_def_free(osrs_spotanim_def_t *def);

/* Load sprites */
osrs_sprite_t *osrs_config_load_sprite(osrs_config_t *config, int archive_id,
                                       int file_id);
void osrs_sprite_free(osrs_sprite_t *sprite);

/* Load textures */
osrs_texture_t *osrs_config_load_texture(osrs_config_t *config, int id);
void osrs_texture_free(osrs_texture_t *texture);

/* Load texture pixels as 128x128 ARGB8888 (brightness-adjusted, resized).
 * Pipeline: texture def (index 9) -> sprite (index 8, frame 0) -> palette.
 * Returns malloc'd 128*128 uint32 array (caller frees), or NULL. */
uint32_t *osrs_config_texture_pixels(osrs_config_t *config, int texture_id);

/* Load model data */
uint8_t *osrs_config_load_model(osrs_config_t *config, int model_id,
                                size_t *out_len);

/* Load map data (with XTEA decryption) */
uint8_t *osrs_config_load_map(osrs_config_t *config, int region_id,
                              const uint32_t xtea_key[4], size_t *out_len);

/* Load landscape data (with XTEA decryption) */
uint8_t *osrs_config_load_landscape(osrs_config_t *config, int region_id,
                                    const uint32_t xtea_key[4],
                                    size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_CONFIG_H */

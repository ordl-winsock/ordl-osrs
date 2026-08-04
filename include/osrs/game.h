/*
 * osrs/game.h — OSRS game session
 * Pure C23, zero external dependencies.
 *
 * Ties together: TCP transport, login handshake, game packet
 * dispatch, player/world state.
 */

#ifndef OSRS_GAME_H
#define OSRS_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "osrs/auth.h"
#include "osrs/net.h"
#include "osrs/playerinfo.h"
#include "osrs/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSRS_GAME_CHAT_MAX 32
#define OSRS_GAME_CHAT_LEN 256
#define OSRS_GAME_SKILLS 23
#define OSRS_GAME_NPC_MAX 4096
#define OSRS_GAME_INV_SIZE 28

typedef struct {
  int id;
  int x, z;
  int plane;
  int orientation; /* facing angle (2048 units/rev) */
  bool active;
  bool visible;
  char name[64];
  int combat_level;
  /* Skeletal animation state */
  int anim_seq_id;    /* current sequence id (-1 = none) */
  int anim_frame_idx; /* current frame index in the sequence */
  double anim_timer;  /* seconds accumulated toward the next frame */
} osrs_npc_t;

typedef struct {
  int id;
  int quantity;
} osrs_inv_item_t;

typedef struct {
  int id;
  int x, z;
  int plane;
  int quantity;
  bool active;
} osrs_ground_item_t;

#define OSRS_GAME_GROUND_ITEM_MAX 1024

typedef enum {
  OSRS_GAME_DISCONNECTED = 0,
  OSRS_GAME_CONNECTING,
  OSRS_GAME_LOGGING_IN,
  OSRS_GAME_IN_GAME,
  OSRS_GAME_FAILED,
} osrs_game_state_t;

typedef struct {
  int type;
  char name[64];
  char text[OSRS_GAME_CHAT_LEN];
} osrs_chat_msg_t;

typedef struct {
  /* Transport + protocol */
  osrs_net_t net;
  osrs_protocol_t proto;
  osrs_game_state_t state;
  int fail_reason; /* login response code on failure */

  /* Player state */
  int player_x, player_z; /* absolute tile coords */
  int player_plane;
  int zone_x, zone_z; /* current zone (8-tile units) */
  int world_area;
  bool map_ready;

  /* Player info (GPI) — local + nearby players */
  osrs_playerinfo_t pi;

  /* Stats */
  int levels[OSRS_GAME_SKILLS];
  int real_levels[OSRS_GAME_SKILLS];
  uint32_t xp[OSRS_GAME_SKILLS];
  int run_energy;
  int run_weight;

  /* Varp */
  int varps[4096];

  /* Chat ring buffer */
  osrs_chat_msg_t chat[OSRS_GAME_CHAT_MAX];
  int chat_head;
  int chat_count;

  /* NPC tracking */
  osrs_npc_t npcs[OSRS_GAME_NPC_MAX];
  int npc_count;
  int npc_indices[512]; /* high-res server index list (display order) */
  int npc_indices_count;
  int npc_origin_x, npc_origin_z; /* from SET_NPC_UPDATE_ORIGIN */

  /* Inventory */
  osrs_inv_item_t inventory[OSRS_GAME_INV_SIZE];
  int inv_count;

  /* Ground items */
  osrs_ground_item_t ground_items[OSRS_GAME_GROUND_ITEM_MAX];
  int ground_item_count;

  /* Ping tracking */
  uint32_t ping_v1, ping_v2;
  bool ping_pending;

  /* Timing */
  double keepalive_timer;
  double connected_time;
  double detect_modified_timer;

  /* Interface state (bank, GE, etc.) */
  int open_interface;
  int open_sub_interface;
  bool interface_open;

  /* Callbacks (optional, set by client) */
  void (*on_state_change)(osrs_game_state_t state, void *userdata);
  void (*on_map_load)(int base_x, int base_z, void *userdata);
  void (*on_map_keys)(const uint32_t keys[][4], int key_count, void *userdata);
  void (*on_chat)(const osrs_chat_msg_t *msg, void *userdata);
  void *userdata;
} osrs_game_t;

/* Initialize game session */
void osrs_game_init(osrs_game_t *game);

/* Connect and log in. cache_crcs: 23 index CRCs from the local cache.
 * auth: credentials (method + username + secret). secret is the password
 * (legacy) or session token (jagex).
 * Returns true if the login process started OK (state machine runs via
 * osrs_game_update). */
bool osrs_game_connect(osrs_game_t *game, const char *host, uint16_t port,
                       const osrs_auth_t *auth, const uint32_t cache_crcs[23],
                       int timeout_ms);

/* Process network I/O and packets. Call every frame. */
void osrs_game_update(osrs_game_t *game, double dt);

/* Disconnect */
void osrs_game_disconnect(osrs_game_t *game);

/* --- Actions --- */
void osrs_game_move_click(osrs_game_t *game, int x, int z, bool run);
void osrs_game_send_window_status(osrs_game_t *game, int mode, int w, int h);
void osrs_game_send_public_chat(osrs_game_t *game, const char *msg);
void osrs_game_send_cheat(osrs_game_t *game, const char *command);
void osrs_game_map_build_complete(osrs_game_t *game);
void osrs_game_interact_object(osrs_game_t *game, int object_id, int x, int z);
void osrs_game_interact_npc(osrs_game_t *game, int npc_index);
void osrs_game_interact_player(osrs_game_t *game, int player_index);

/* Helpers */
const char *osrs_login_response_str(int code);

/* Absolute tile -> region coords */
static inline int osrs_tile_to_region_x(int x) { return x >> 6; }
static inline int osrs_tile_to_region_y(int y) { return y >> 6; }

#ifdef __cplusplus
}
#endif

#endif /* OSRS_GAME_H */

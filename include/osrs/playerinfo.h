/*
 * osrs/playerinfo.h — OSRS rev-239 player info (GPI) decoder
 * Pure C23, zero external dependencies.
 *
 * Decodes the per-cycle PLAYER_INFO packet: high/low resolution player
 * movement, high<->low resolution transitions and extended info blocks
 * (appearance, move speed, etc.). Bit layout verified against RSProt
 * osrs-239 (blurite/rsprot) and the official client.
 */

#ifndef OSRS_PLAYERINFO_H
#define OSRS_PLAYERINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSRS_PI_CAPACITY 2048
/* High-resolution view distance in tiles (Chebyshev). */
#define OSRS_PI_VIEW_DISTANCE 15

/* Extended-info mask bits (client wire values). */
#define OSRS_EI_FACE 0x1
#define OSRS_EI_PLAYER_RESET 0x2
#define OSRS_EI_SAY 0x4
#define OSRS_EI_EXT_SHORT 0x8
#define OSRS_EI_APPEARANCE 0x20
#define OSRS_EI_SEQUENCE 0x40
#define OSRS_EI_CHAT 0x100
#define OSRS_EI_TINTING 0x200
#define OSRS_EI_MOVE_SPEED 0x400
#define OSRS_EI_EXT_MEDIUM 0x800
#define OSRS_EI_TEMP_MOVE_SPEED 0x1000
#define OSRS_EI_EXACT_MOVE 0x4000
#define OSRS_EI_HEADBARS 0x10000
#define OSRS_EI_SPOTANIM 0x20000
#define OSRS_EI_HITMARKS 0x40000
#define OSRS_EI_FREEZE 0x80000
#define OSRS_EI_TRANSPARENCY 0x100000
#define OSRS_EI_UNKNOWN_10 0x10
#define OSRS_EI_UNKNOWN_80 0x80

typedef struct {
  bool active;         /* slot currently tracks a logged-in player */
  bool high_res;       /* in high-resolution (visible) set */
  bool was_stationary; /* stationary last cycle (pass ordering) */
  bool is_stationary;  /* stationary this cycle (becomes was_ next) */
  bool has_extinfo;    /* extended info block present this cycle */
  int x, z, level;     /* absolute tile coords + plane */

  /* Appearance (extended info) */
  bool appearance_valid;
  char name[13];
  int combat_level;
  int body_type;       /* 0 male, 1 female */
  int skull_icon;      /* -1 none */
  int overhead_icon;   /* -1 none */
  int transformed_npc; /* >=0 if transmogged into an NPC */
  int equipment[12];
  int ident_kits[12]; /* base body kits, -1 = none */
  int colours[5];

  /* Movement */
  int move_speed;
  bool teleported; /* set when a teleport occurred this cycle */
} osrs_player_t;

typedef struct {
  osrs_player_t players[OSRS_PI_CAPACITY];
  int local_index; /* our own player slot */
  int view_distance;

  /* Decode scratch: frozen start-of-cycle resolution lists. Each entry
   * snapshots (index, was_stationary) so later passes aren't affected by
   * this cycle's mutations. */
  int filtered[OSRS_PI_CAPACITY];
  int extinfo_order[OSRS_PI_CAPACITY];
  int extinfo_count;
  int snap_index[2][OSRS_PI_CAPACITY]; /* [0]=low [1]=high */
  bool snap_was[2][OSRS_PI_CAPACITY];
  int snap_count[2];

  /* Local player absolute position (from login seed + movement) */
  int local_x, local_z, local_level;
  bool local_pos_valid;
} osrs_playerinfo_t;

void osrs_pi_init(osrs_playerinfo_t *pi);

/* Parse the RebuildLogin GPI init block (footer of the login rebuild
 * packet): 30-bit local coord followed by 18-bit low-res positions for
 * every other slot. Sets the local absolute position. */
bool osrs_pi_seed_login(osrs_playerinfo_t *pi, int local_index,
                        const uint8_t *data, size_t len);

/* Decode one PLAYER_INFO packet. */
bool osrs_pi_decode(osrs_playerinfo_t *pi, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_PLAYERINFO_H */

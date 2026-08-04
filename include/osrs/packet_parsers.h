/*
 * osrs/packet_parsers.h - Incoming game packet parsers
 * Pure C23, zero external dependencies.
 */

#ifndef OSRS_PACKET_PARSERS_H
#define OSRS_PACKET_PARSERS_H

#include "osrs/protocol.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* REBUILD_NORMAL */
typedef struct {
  int zone_x, zone_z;
  int world_area;
  int key_count;
  uint32_t keys[9][4]; /* XTEA keys for up to 9 mapsquares */
} osrs_rebuild_normal_t;

bool osrs_parse_rebuild_normal(osrs_packet_t *pkt, osrs_rebuild_normal_t *out);

/* PING */
typedef struct {
  uint32_t value1, value2;
} osrs_ping_t;

bool osrs_parse_ping(osrs_packet_t *pkt, osrs_ping_t *out);
size_t osrs_build_ping_reply(osrs_packet_t *p, uint32_t v1, uint32_t v2,
                             int fps);

/* MESSAGE_GAME */
typedef struct {
  int type;
  bool has_name;
  char name[64];
  char text[256];
} osrs_message_game_t;

bool osrs_parse_message_game(osrs_packet_t *pkt, osrs_message_game_t *out);

/* UPDATE_STAT */
typedef struct {
  int skill_id;
  int level;
  int real_level;
  uint32_t experience;
} osrs_update_stat_t;

bool osrs_parse_update_stat(osrs_packet_t *pkt, osrs_update_stat_t *out);

/* VARP */
bool osrs_parse_varp_small(osrs_packet_t *pkt, int *id, int *value);
bool osrs_parse_varp_large(osrs_packet_t *pkt, int *id, int32_t *value);

/* Interface */
bool osrs_parse_if_opentop(osrs_packet_t *pkt, int *iface);
bool osrs_parse_if_opensub(osrs_packet_t *pkt, int *iface, int *component,
                           int *type);
bool osrs_parse_if_closesub(osrs_packet_t *pkt, int *component);

/* Misc */
bool osrs_parse_u16_packet(osrs_packet_t *pkt, int *value);
size_t osrs_build_detect_modified_client(osrs_packet_t *p, uint32_t hash);

/* Stream helpers from cache.h */
#include "osrs/cache.h"

/* Additional helpers */
uint8_t osrs_g1(osrs_packet_t *pkt);
size_t osrs_build_message_public(osrs_packet_t *p, const char *msg,
                                 int color_effect, int move_effect);
size_t osrs_build_client_cheat(osrs_packet_t *p, const char *command);
size_t osrs_build_oploc(osrs_packet_t *p, int opcode, int object_id, int x,
                        int z);
size_t osrs_build_opnpc(osrs_packet_t *p, int opcode, int npc_index);
size_t osrs_build_opplayer(osrs_packet_t *p, int opcode, int player_index);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_PACKET_PARSERS_H */

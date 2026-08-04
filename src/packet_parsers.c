/*
 * osrs/packet_parsers.c - Incoming game packet parsers
 * Pure C23, zero external dependencies.
 */

#include "osrs/packet_parsers.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* REBUILD_NORMAL / REBUILD_REGION                                            */
/* -------------------------------------------------------------------------- */

bool osrs_parse_rebuild_normal(osrs_packet_t *pkt, osrs_rebuild_normal_t *out) {
  /* REBUILD_NORMAL_V2 (rev 239): packet = [gpi init block][6-byte footer]
   * The 6-byte footer is at the END of the packet:
   *   p2Alt1(worldArea) = [low][high]
   *   p2Alt2(zoneZ)     = [high][low+128]
   *   p2Alt2(zoneX)     = [high][low+128]
   * The gpi init block (if present) is the first pkt->len-6 bytes. */
  if (pkt->len < 6)
    return false;

  const uint8_t *f = pkt->data + pkt->len - 6;

  /* p2Alt1: [low][high] -> low | (high << 8) */
  uint16_t world_area = f[0] | ((uint16_t)f[1] << 8);

  /* p2Alt2: [high][low+128] -> (high << 8) | ((low+128 - 128) & 0xFF) */
  uint16_t zone_z = ((uint16_t)f[2] << 8) | (((uint16_t)f[3] - 128) & 0xFF);
  uint16_t zone_x = ((uint16_t)f[4] << 8) | (((uint16_t)f[5] - 128) & 0xFF);

  out->zone_x = (int)zone_x;
  out->zone_z = (int)zone_z;
  out->world_area = (int)world_area;
  out->key_count = 0;
  pkt->pos = pkt->len; /* consumed entire packet */
  return true;
}

/* -------------------------------------------------------------------------- */
/* PING                                                                       */
/* -------------------------------------------------------------------------- */

bool osrs_parse_ping(osrs_packet_t *pkt, osrs_ping_t *out) {
  if (pkt->len < 8)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  out->value1 = osrs_stream_u32(&s);
  out->value2 = osrs_stream_u32(&s);
  pkt->pos = s.pos;
  return true;
}

static void pkt_p1(osrs_packet_t *p, uint8_t v) {
  if (p->len < sizeof(p->data))
    p->data[p->len++] = v;
}

static void pkt_p2(osrs_packet_t *p, uint16_t v) {
  pkt_p1(p, (uint8_t)(v >> 8));
  pkt_p1(p, (uint8_t)v);
}

static void pkt_p4(osrs_packet_t *p, uint32_t v) {
  pkt_p2(p, (uint16_t)(v >> 16));
  pkt_p2(p, (uint16_t)v);
}

size_t osrs_build_ping_reply(osrs_packet_t *p, uint32_t v1, uint32_t v2,
                             int fps) {
  p->len = 0;
  p->pos = 0;
  pkt_p4(p, v1);
  pkt_p4(p, v2);
  pkt_p1(p, (uint8_t)fps);
  return p->len;
}

/* -------------------------------------------------------------------------- */
/* MESSAGE_GAME                                                               */
/* -------------------------------------------------------------------------- */

bool osrs_parse_message_game(osrs_packet_t *pkt, osrs_message_game_t *out) {
  if (pkt->len < 1)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  out->type = (int)osrs_stream_u8(&s);
  out->has_name = false;
  out->name[0] = '\0';
  out->text[0] = '\0';

  /* Read name if present (type-dependent) */
  if (out->type >= 2 && out->type <= 10) {
    if (osrs_stream_remaining(&s) > 0) {
      osrs_stream_string(&s, out->name, sizeof(out->name));
      out->has_name = true;
    }
  }
  /* Read text */
  if (osrs_stream_remaining(&s) > 0) {
    osrs_stream_string(&s, out->text, sizeof(out->text));
  }
  pkt->pos = s.pos;
  return true;
}

/* -------------------------------------------------------------------------- */
/* UPDATE_STAT                                                                */
/* -------------------------------------------------------------------------- */

bool osrs_parse_update_stat(osrs_packet_t *pkt, osrs_update_stat_t *out) {
  if (pkt->len < 7)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  out->skill_id = (int)osrs_stream_u8(&s);
  out->level = (int)osrs_stream_u8(&s);
  out->real_level = (int)osrs_stream_u8(&s);
  out->experience = osrs_stream_u32(&s);
  pkt->pos = s.pos;
  return true;
}

/* -------------------------------------------------------------------------- */
/* VARP                                                                       */
/* -------------------------------------------------------------------------- */

bool osrs_parse_varp_small(osrs_packet_t *pkt, int *id, int *value) {
  if (pkt->len < 3)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  *id = (int)osrs_stream_u16(&s);
  *value = (int)osrs_stream_u8(&s);
  pkt->pos = s.pos;
  return true;
}

bool osrs_parse_varp_large(osrs_packet_t *pkt, int *id, int32_t *value) {
  if (pkt->len < 6)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  *id = (int)osrs_stream_u16(&s);
  *value = (int32_t)osrs_stream_u32(&s);
  pkt->pos = s.pos;
  return true;
}

/* -------------------------------------------------------------------------- */
/* Interface                                                                  */
/* -------------------------------------------------------------------------- */

bool osrs_parse_if_opentop(osrs_packet_t *pkt, int *iface) {
  if (pkt->len < 2)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  *iface = (int)osrs_stream_u16(&s);
  pkt->pos = s.pos;
  return true;
}

bool osrs_parse_if_opensub(osrs_packet_t *pkt, int *iface, int *component,
                           int *type) {
  if (pkt->len < 7)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  *iface = (int)osrs_stream_u32(&s);
  *component = (int)osrs_stream_u16(&s);
  *type = (int)osrs_stream_u8(&s);
  pkt->pos = s.pos;
  return true;
}

bool osrs_parse_if_closesub(osrs_packet_t *pkt, int *component) {
  if (pkt->len < 4)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  *component = (int)osrs_stream_u32(&s);
  pkt->pos = s.pos;
  return true;
}

/* -------------------------------------------------------------------------- */
/* Misc                                                                       */
/* -------------------------------------------------------------------------- */

bool osrs_parse_u16_packet(osrs_packet_t *pkt, int *value) {
  if (pkt->len < 2)
    return false;
  osrs_stream_t s;
  osrs_stream_init(&s, pkt->data, pkt->len);
  *value = (int)osrs_stream_u16(&s);
  pkt->pos = s.pos;
  return true;
}

size_t osrs_build_detect_modified_client(osrs_packet_t *p, uint32_t hash) {
  p->len = 0;
  p->pos = 0;
  pkt_p4(p, hash);
  return p->len;
}

/* -------------------------------------------------------------------------- */
/* Additional helpers needed by game.c                                        */
/* -------------------------------------------------------------------------- */

uint8_t osrs_g1(osrs_packet_t *pkt) {
  if (pkt->pos < pkt->len)
    return pkt->data[pkt->pos++];
  return 0;
}

size_t osrs_build_message_public(osrs_packet_t *p, const char *msg,
                                 int color_effect, int move_effect) {
  p->len = 0;
  p->pos = 0;
  size_t msg_len = strlen(msg);
  if (msg_len > 200)
    msg_len = 200;
  pkt_p1(p, (uint8_t)color_effect);
  pkt_p1(p, (uint8_t)move_effect);
  memcpy(p->data + p->len, msg, msg_len);
  p->len += msg_len;
  return p->len;
}

size_t osrs_build_client_cheat(osrs_packet_t *p, const char *command) {
  p->len = 0;
  p->pos = 0;
  size_t n = strlen(command);
  if (n > 200)
    n = 200;
  memcpy(p->data, command, n);
  p->len = n;
  return p->len;
}

size_t osrs_build_oploc(osrs_packet_t *p, int opcode, int object_id, int x,
                        int z) {
  (void)opcode;
  p->len = 0;
  p->pos = 0;
  pkt_p2(p, (uint16_t)x);
  pkt_p2(p, (uint16_t)z);
  pkt_p2(p, (uint16_t)object_id);
  return p->len;
}

size_t osrs_build_opnpc(osrs_packet_t *p, int opcode, int npc_index) {
  (void)opcode;
  p->len = 0;
  p->pos = 0;
  pkt_p2(p, (uint16_t)npc_index);
  return p->len;
}

size_t osrs_build_opplayer(osrs_packet_t *p, int opcode, int player_index) {
  (void)opcode;
  p->len = 0;
  p->pos = 0;
  pkt_p2(p, (uint16_t)player_index);
  return p->len;
}

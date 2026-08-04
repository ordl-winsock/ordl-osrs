/*
 * osrs/game.c — OSRS game session implementation
 * Pure C23, zero external dependencies.
 */

#include "osrs/game.h"
#include "osrs/cache.h"
#include "osrs/log.h"
#include "osrs/packet_parsers.h"
#include "osrs/proofofwork.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void osrs_game_init(osrs_game_t *game) {
  memset(game, 0, sizeof(*game));
  game->state = OSRS_GAME_DISCONNECTED;
  game->net.fd = -1;
  osrs_protocol_init(&game->proto);
  for (int i = 0; i < OSRS_GAME_SKILLS; i++) {
    game->levels[i] = 1;
    game->real_levels[i] = 1;
  }
  OSRS_DEBUG(OSRS_LOG_CAT_GAME, "game init");
}

static const char *game_state_name(osrs_game_state_t state) {
  switch (state) {
  case OSRS_GAME_DISCONNECTED:
    return "DISCONNECTED";
  case OSRS_GAME_CONNECTING:
    return "CONNECTING";
  case OSRS_GAME_LOGGING_IN:
    return "LOGGING_IN";
  case OSRS_GAME_IN_GAME:
    return "IN_GAME";
  case OSRS_GAME_FAILED:
    return "FAILED";
  default:
    return "UNKNOWN";
  }
}

static void game_set_state(osrs_game_t *game, osrs_game_state_t state) {
  OSRS_INFO(OSRS_LOG_CAT_GAME, "state change: %s -> %s",
            game_state_name(game->state), game_state_name(state));
  game->state = state;
  if (game->on_state_change)
    game->on_state_change(state, game->userdata);
}

static uint32_t game_rand32(void) {
  /* xorshift seeded from time — fine for session seeds */
  static uint64_t s = 0;
  if (s == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    s = (uint64_t)ts.tv_nsec ^ ((uint64_t)ts.tv_sec << 21) ^ (uintptr_t)&ts;
  }
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  return (uint32_t)s;
}

bool osrs_game_connect(osrs_game_t *game, const char *host, uint16_t port,
                       const osrs_auth_t *auth, const uint32_t cache_crcs[23],
                       int timeout_ms) {
  osrs_game_disconnect(game);
  osrs_protocol_init(&game->proto);

  snprintf(game->proto.username, sizeof(game->proto.username), "%s",
           auth->username);
  snprintf(game->proto.secret, sizeof(game->proto.secret), "%s", auth->secret);
  game->proto.auth_type = (int)auth->method;

  if (cache_crcs)
    memcpy(game->proto.index_crcs, cache_crcs, sizeof(game->proto.index_crcs));

  const char *xs =
      getenv("OSRS_XTEA_SEED"); /* opt-in deterministic seed (testing) */
  unsigned long xv = xs ? strtoul(xs, NULL, 0) : 0;
  for (int i = 0; i < 4; i++) {
    if (xs)
      game->proto.xtea_seed[i] = (uint32_t)(xv * (uint64_t)(i + 1));
    else
      game->proto.xtea_seed[i] = game_rand32();
  }

  OSRS_INFO(OSRS_LOG_CAT_NET, "connecting to %s:%d (auth=%s)", host, port,
            osrs_auth_method_str(auth->method));

  if (!osrs_net_connect(&game->net, host, port, timeout_ms)) {
    game_set_state(game, OSRS_GAME_FAILED);
    game->fail_reason = -1;
    return false;
  }

  /* Send INIT_GAME_CONNECTION */
  uint8_t init_pkt[1];
  size_t n = osrs_login_build_init(init_pkt);
  if (!osrs_net_queue(&game->net, init_pkt, n)) {
    osrs_net_close(&game->net);
    game_set_state(game, OSRS_GAME_FAILED);
    return false;
  }

  game->proto.state = OSRS_CONN_INIT_SENT;
  game_set_state(game, OSRS_GAME_LOGGING_IN);
  return true;
}

void osrs_game_disconnect(osrs_game_t *game) {
  osrs_net_close(&game->net);
  if (game->state != OSRS_GAME_DISCONNECTED)
    game_set_state(game, OSRS_GAME_DISCONNECTED);
  game->proto.state = OSRS_CONN_DISCONNECTED;
  game->map_ready = false;
}

/* Queue a client packet (opcode + framing + ISAAC) */
static void game_send_packet(osrs_game_t *game, int opcode,
                             const uint8_t *payload, size_t payload_len) {
  uint8_t buf[8192 + 4];
  int n = osrs_game_write_packet(&game->proto, opcode, payload, payload_len,
                                 buf, sizeof(buf));
  if (n < 0) {
    OSRS_WARN(OSRS_LOG_CAT_PKT, "outgoing opcode=%d too large", opcode);
    return;
  }
  OSRS_DEBUG(OSRS_LOG_CAT_PKT, "outgoing opcode=%d len=%d", opcode, n);
  osrs_net_queue(&game->net, buf, (size_t)n);
  game->keepalive_timer = 0; /* any outgoing packet counts as activity */
}

static void game_push_chat(osrs_game_t *game, int type, const char *name,
                           const char *text) {
  osrs_chat_msg_t *m = &game->chat[game->chat_head];
  memset(m, 0, sizeof(*m));
  m->type = type;
  snprintf(m->name, sizeof(m->name), "%s", name ? name : "");
  snprintf(m->text, sizeof(m->text), "%s", text ? text : "");
  game->chat_head = (game->chat_head + 1) % OSRS_GAME_CHAT_MAX;
  if (game->chat_count < OSRS_GAME_CHAT_MAX)
    game->chat_count++;
  if (game->on_chat)
    game->on_chat(m, game->userdata);
}

static const char *opcode_name(int opcode) {
  switch (opcode) {
  case OSRS_IN_REBUILD_NORMAL_V2:
    return "REBUILD_NORMAL";
  case OSRS_IN_REBUILD_REGION_V2:
    return "REBUILD_REGION";
  case OSRS_IN_PLAYER_INFO:
    return "PLAYER_INFO";
  case OSRS_IN_NPC_INFO_SMALL_V5:
    return "NPC_INFO_SMALL";
  case OSRS_IN_NPC_INFO_LARGE_V5:
    return "NPC_INFO_LARGE";
  case OSRS_IN_IF_OPENTOP:
    return "IF_OPENTOP";
  case OSRS_IN_IF_OPENSUB:
    return "IF_OPENSUB";
  case OSRS_IN_IF_CLOSESUB:
    return "IF_CLOSESUB";
  case OSRS_IN_IF_SETTEXT:
    return "IF_SETTEXT";
  case OSRS_IN_IF_SETHIDE:
    return "IF_SETHIDE";
  case OSRS_IN_UPDATE_INV_FULL:
    return "UPDATE_INV_FULL";
  case OSRS_IN_UPDATE_INV_PARTIAL:
    return "UPDATE_INV_PARTIAL";
  case OSRS_IN_UPDATE_STAT_V2:
    return "UPDATE_STAT";
  case OSRS_IN_UPDATE_RUNENERGY:
    return "UPDATE_RUNENERGY";
  case OSRS_IN_UPDATE_RUNWEIGHT:
    return "UPDATE_RUNWEIGHT";
  case OSRS_IN_VARP_SMALL:
    return "VARP_SMALL";
  case OSRS_IN_VARP_LARGE:
    return "VARP_LARGE";
  case OSRS_IN_VARP_RESET:
    return "VARP_RESET";
  case OSRS_IN_MESSAGE_GAME:
    return "MESSAGE_GAME";
  case OSRS_IN_MESSAGE_PRIVATE:
    return "MESSAGE_PRIVATE";
  case OSRS_IN_CHAT_FILTER_SETTINGS:
    return "CHAT_FILTER_SETTINGS";
  case OSRS_IN_LOGOUT:
    return "LOGOUT";
  case OSRS_IN_LOGOUT_WITHREASON:
    return "LOGOUT_WITHREASON";
  case OSRS_IN_SEND_PING:
    return "SEND_PING";
  case OSRS_IN_SERVER_TICK_END:
    return "SERVER_TICK_END";
  case OSRS_IN_SET_MAP_FLAG_V2:
    return "SET_MAP_FLAG";
  case OSRS_IN_SET_PLAYER_OP:
    return "SET_PLAYER_OP";
  case OSRS_IN_UPDATE_ZONE_FULL_FOLLOWS:
    return "UPDATE_ZONE_FULL_FOLLOWS";
  case OSRS_IN_UPDATE_ZONE_PARTIAL_FOLLOWS:
    return "UPDATE_ZONE_PARTIAL_FOLLOWS";
  default:
    return NULL;
  }
}

/* -------------------------------------------------------------------------- */
/* NPC_INFO decoder (rev 239, NpcInfoSmallV5/LargeV5)                         */
/* -------------------------------------------------------------------------- */

/* MSB-first bit reader */
typedef struct {
  const uint8_t *data;
  size_t len;
  size_t bitpos; /* absolute bit position */
} npc_bitreader_t;

static void br_init(npc_bitreader_t *br, const uint8_t *data, size_t len) {
  br->data = data;
  br->len = len;
  br->bitpos = 0;
}

static int br_bits(npc_bitreader_t *br, int count) {
  int value = 0;
  for (int i = 0; i < count; i++) {
    size_t byte = br->bitpos >> 3;
    int bit = 7 - (int)(br->bitpos & 7);
    value <<= 1;
    if (byte < br->len)
      value |= (br->data[byte] >> bit) & 1;
    br->bitpos++;
  }
  return value;
}

static int br_bits_left(const npc_bitreader_t *br) {
  return (int)(br->len * 8 - br->bitpos);
}

/* 3-bit direction opcode -> (dx, dz) step */
static const int NPC_DIR_DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int NPC_DIR_DZ[8] = {1, 1, 1, 0, 0, -1, -1, -1};
/* direction opcode -> facing angle (2048 units/rev) */
static const int NPC_TURN_ANGLES[8] = {768,  1024, 1280, 512,
                                       1536, 256,  0,    1792};

static void game_npc_step(osrs_npc_t *npc, int dir) {
  npc->x += NPC_DIR_DX[dir & 7];
  npc->z += NPC_DIR_DZ[dir & 7];
  npc->orientation = NPC_TURN_ANGLES[dir & 7];
}

static void game_decode_npc_info(osrs_game_t *game, const osrs_packet_t *pkt,
                                 bool is_large) {
  npc_bitreader_t br;
  br_init(&br, pkt->data, pkt->len);

  int count = br_bits(&br, 8);

  /* Implicit trailing removal when the count shrinks */
  if (count < game->npc_indices_count) {
    for (int i = count; i < game->npc_indices_count; i++) {
      int sidx = game->npc_indices[i];
      if (sidx >= 0 && sidx < OSRS_GAME_NPC_MAX)
        game->npcs[sidx].active = false;
    }
    game->npc_indices_count = count;
  }
  if (count > 512)
    count = 512;

  /* High-resolution loop: movement / removal per existing slot */
  int out_idx = 0; /* compaction pointer for the indices list */
  for (int i = 0; i < count; i++) {
    if (i >= game->npc_indices_count)
      break;
    int sidx = game->npc_indices[i];
    osrs_npc_t *npc = NULL;
    if (sidx >= 0 && sidx < OSRS_GAME_NPC_MAX)
      npc = &game->npcs[sidx];

    int flag = br_bits(&br, 1);
    if (flag == 0) {
      /* no update */
      if (npc && npc->active)
        game->npc_indices[out_idx++] = sidx;
      continue;
    }
    int type = br_bits(&br, 2);
    if (type == 0) {
      /* extended info only (skipped) */
      if (npc && npc->active)
        game->npc_indices[out_idx++] = sidx;
    } else if (type == 1) {
      /* walk */
      int d1 = br_bits(&br, 3);
      if (npc && npc->active) {
        game_npc_step(npc, d1);
        game->npc_indices[out_idx++] = sidx;
      }
      (void)br_bits(&br, 1); /* ext flag */
    } else if (type == 2) {
      int mode = br_bits(&br, 1);
      if (mode == 1) {
        /* run: two steps */
        int d1 = br_bits(&br, 3);
        int d2 = br_bits(&br, 3);
        if (npc && npc->active) {
          game_npc_step(npc, d1);
          game_npc_step(npc, d2);
        }
      } else {
        /* crawl: one step */
        int d1 = br_bits(&br, 3);
        if (npc && npc->active)
          game_npc_step(npc, d1);
      }
      if (npc && npc->active)
        game->npc_indices[out_idx++] = sidx;
      (void)br_bits(&br, 1); /* ext flag */
    } else {
      /* remove */
      if (npc)
        npc->active = false;
    }
  }
  game->npc_indices_count = out_idx;

  /* Low-resolution spawn loop */
  int delta_bits = is_large ? 8 : 6;
  int delta_mod = is_large ? 256 : 64;
  while (br_bits_left(&br) >= 28) {
    int index = br_bits(&br, 16);
    if (index == 0xFFFF)
      break;
    if (br_bits(&br, 1)) /* spawnCycle flag */
      (void)br_bits(&br, 32);
    (void)br_bits(&br, 1); /* ext flag */
    int dx = br_bits(&br, delta_bits);
    int dir = br_bits(&br, 3);
    int dz = br_bits(&br, delta_bits);
    (void)br_bits(&br, 1); /* jump */
    int npc_id = br_bits(&br, 14);

    if (dx > (delta_mod / 2 - 1))
      dx -= delta_mod;
    if (dz > (delta_mod / 2 - 1))
      dz -= delta_mod;

    if (index < 0 || index >= OSRS_GAME_NPC_MAX)
      continue;

    /* Absolute position = scene base + origin + delta */
    int base_x = (game->zone_x - 6) * 8;
    int base_z = (game->zone_z - 6) * 8;
    osrs_npc_t *npc = &game->npcs[index];
    npc->id = npc_id;
    npc->x = base_x + game->npc_origin_x + dx;
    npc->z = base_z + game->npc_origin_z + dz;
    npc->plane = game->player_plane;
    npc->orientation = NPC_TURN_ANGLES[dir & 7];
    npc->active = true;
    npc->visible = true;
    npc->name[0] = '\0';
    npc->combat_level = 0;

    if (game->npc_indices_count < 512)
      game->npc_indices[game->npc_indices_count++] = index;
    if (index + 1 > game->npc_count)
      game->npc_count = index + 1;
  }

  /* Extended info blocks follow (byte-aligned) — skipped for now; they only
   * carry cosmetics (names, levels, animations) and the packet is
   * length-delimited. */
}

/* -------------------------------------------------------------------------- */
/* Ground item tracking */
/* -------------------------------------------------------------------------- */

static void game_add_ground_item(osrs_game_t *game, int id, int x, int z,
                                 int level, int quantity) {
  /* Update existing entry at the same spot if present */
  for (int i = 0; i < game->ground_item_count; i++) {
    osrs_ground_item_t *it = &game->ground_items[i];
    if (it->active && it->id == id && it->x == x && it->z == z &&
        it->plane == level) {
      it->quantity = quantity;
      return;
    }
  }
  /* Find a free slot */
  if (game->ground_item_count >= OSRS_GAME_GROUND_ITEM_MAX)
    return;
  osrs_ground_item_t *it = &game->ground_items[game->ground_item_count++];
  it->id = id;
  it->x = x;
  it->z = z;
  it->plane = level;
  it->quantity = quantity;
  it->active = true;
}

static void game_remove_ground_item(osrs_game_t *game, int id, int x, int z,
                                    int level) {
  for (int i = 0; i < game->ground_item_count; i++) {
    osrs_ground_item_t *it = &game->ground_items[i];
    if (it->active && it->id == id && it->x == x && it->z == z &&
        it->plane == level) {
      it->active = false;
      return;
    }
  }
}

/* Handle one decoded server packet */
static void game_handle_packet(osrs_game_t *game, int opcode,
                               osrs_packet_t *pkt) {
  const char *name = opcode_name(opcode);
  if (name)
    OSRS_INFO(OSRS_LOG_CAT_PKT, "opcode=%d name=%s", opcode, name);
  else
    OSRS_DEBUG(OSRS_LOG_CAT_PKT, "opcode=%d (unknown)", opcode);

  switch (opcode) {
  case OSRS_IN_REBUILD_NORMAL_V2: {
    osrs_rebuild_normal_t rb;
    if (osrs_parse_rebuild_normal(pkt, &rb)) {
      /* Sanity-check zone coords: valid OSRS map spans ~0-1600 zones.
       * A framing desync can produce garbage here; reject it rather than
       * teleport the view into the void. */
      if (rb.zone_x < 6 || rb.zone_x > 2600 || rb.zone_z < 6 ||
          rb.zone_z > 2600) {
        OSRS_WARN(OSRS_LOG_CAT_GAME,
                  "rejecting absurd rebuild base zone %d,%d (area %d) - "
                  "likely framing desync",
                  rb.zone_x, rb.zone_z, rb.world_area);
        break;
      }
      game->zone_x = rb.zone_x;
      game->zone_z = rb.zone_z;
      game->world_area = rb.world_area;
      game->player_plane = 0;
      /* Zone coords are in 8-tile units; scene is 13x13 zones centered
       * on the player. Base tile = (zone - 6) * 8. */
      int base_x = (rb.zone_x - 6) * 8;
      int base_z = (rb.zone_z - 6) * 8;
      if (game->on_map_load)
        game->on_map_load(base_x, base_z, game->userdata);
      game->map_ready = true;

      /* Pass XTEA keys to client for map decryption */
      if (rb.key_count > 0 && game->on_map_keys)
        game->on_map_keys(rb.keys, rb.key_count, game->userdata);

      /* A login rebuild carries the GPI init block at the START of the packet,
       * followed by a 6-byte footer (worldArea, zoneZ, zoneX) at the END.
       * A plain region rebuild is just the 6-byte footer. */
      size_t gpi_len = pkt->len > 6 ? pkt->len - 6 : 0;
      if (gpi_len > 0) {
        osrs_pi_seed_login(&game->pi, game->proto.player_index, pkt->data,
                           gpi_len);
        if (game->pi.local_pos_valid) {
          game->player_x = game->pi.local_x;
          game->player_z = game->pi.local_z;
          game->player_plane = game->pi.local_level;
          OSRS_DEBUG(OSRS_LOG_CAT_GAME,
                     "Seed login pos: %d,%d,%d (gpi_len=%zu)", game->player_x,
                     game->player_z, game->player_plane, gpi_len);
        }
      }

      /* Tell server we finished building the map */
      game_send_packet(game, OSRS_OUT_MAP_BUILD_COMPLETE, NULL, 0);
    }
    break;
  }

  case OSRS_IN_PLAYER_INFO: {
    if (osrs_pi_decode(&game->pi, pkt->data, pkt->len)) {
      if (game->pi.local_pos_valid) {
        game->player_x = game->pi.local_x;
        game->player_z = game->pi.local_z;
        game->player_plane = game->pi.local_level;
        OSRS_DEBUG(OSRS_LOG_CAT_GAME, "Player position updated: %d,%d,%d",
                   game->player_x, game->player_z, game->player_plane);
      }
    }
    break;
  }

  case OSRS_IN_SEND_PING: {
    osrs_ping_t ping;
    if (osrs_parse_ping(pkt, &ping)) {
      osrs_packet_t reply;
      osrs_build_ping_reply(&reply, ping.value1, ping.value2, 60);
      game_send_packet(game, OSRS_OUT_SEND_PING_REPLY, reply.data, reply.len);
    }
    break;
  }

  case OSRS_IN_MESSAGE_GAME: {
    osrs_message_game_t msg;
    if (osrs_parse_message_game(pkt, &msg)) {
      game_push_chat(game, msg.type, msg.has_name ? msg.name : "", msg.text);
    }
    break;
  }

  case OSRS_IN_UPDATE_STAT_V2: {
    osrs_update_stat_t st;
    if (osrs_parse_update_stat(pkt, &st) && st.skill_id >= 0 &&
        st.skill_id < OSRS_GAME_SKILLS) {
      game->levels[st.skill_id] = st.level;
      game->real_levels[st.skill_id] = st.real_level;
      game->xp[st.skill_id] = st.experience;
    }
    break;
  }

  case OSRS_IN_UPDATE_RUNENERGY: {
    int v;
    if (osrs_parse_u16_packet(pkt, &v))
      game->run_energy = v;
    break;
  }

  case OSRS_IN_UPDATE_RUNWEIGHT: {
    int v;
    if (osrs_parse_u16_packet(pkt, &v))
      game->run_weight = v;
    break;
  }

  case OSRS_IN_VARP_SMALL: {
    int id, value;
    if (osrs_parse_varp_small(pkt, &id, &value) && id >= 0 && id < 4096)
      game->varps[id] = value;
    break;
  }

  case OSRS_IN_VARP_LARGE: {
    int id;
    int32_t value;
    if (osrs_parse_varp_large(pkt, &id, &value) && id >= 0 && id < 4096)
      game->varps[id] = value;
    break;
  }

  case OSRS_IN_LOGOUT:
  case OSRS_IN_LOGOUT_WITHREASON: {
    int reason = 0;
    if (opcode == OSRS_IN_LOGOUT_WITHREASON && pkt->len >= 1)
      reason = (int)osrs_g1(pkt);
    game->npc_count = 0;
    memset(game->npcs, 0, sizeof(game->npcs));
    OSRS_INFO(OSRS_LOG_CAT_GAME, "Logged out by server (reason=%d)", reason);
    game_push_chat(game, 0, "", "Logged out by server.");
    osrs_game_disconnect(game);
    break;
  }

  case OSRS_IN_NPC_INFO_SMALL_V5:
  case OSRS_IN_NPC_INFO_LARGE_V5: {
    game_decode_npc_info(game, pkt, opcode == OSRS_IN_NPC_INFO_LARGE_V5);
    break;
  }

  case OSRS_IN_SET_NPC_UPDATE_ORIGIN: {
    if (pkt->len >= 2) {
      game->npc_origin_x = pkt->data[0];
      game->npc_origin_z = pkt->data[1];
    }
    break;
  }

  case OSRS_IN_OBJ_ADD_SPECIFIC: {
    /* 17 bytes: timeUntilPublic p2 | id p2Alt1 | timeUntilDespawn p2Alt1 |
     * neverPublic p1Alt3 | ownership p1 | opFlags p1Alt2 |
     * coordGrid.packed p4Alt2 | quantity p4Alt3 */
    if (pkt->len < 17)
      break;
    const uint8_t *d = pkt->data;
    int id = d[2] | (d[3] << 8); /* p2Alt1 = u16 LE */
    uint32_t packed = ((uint32_t)d[9] << 8) | (uint32_t)d[10] |
                      ((uint32_t)d[11] << 24) | ((uint32_t)d[12] << 16);
    uint32_t quantity = ((uint32_t)d[13] << 16) | ((uint32_t)d[14] << 24) |
                        (uint32_t)d[15] | ((uint32_t)d[16] << 8);
    int level = (int)(packed >> 28) & 3;
    int x = (int)(packed & 0x3FFF);
    int z = (int)((packed >> 14) & 0x3FFF);
    game_add_ground_item(game, id, x, z, level, (int)quantity);
    break;
  }

  case OSRS_IN_OBJ_DEL_SPECIFIC: {
    /* 10 bytes: id p2Alt2 | quantity p4 | coordGrid.packed p4 */
    if (pkt->len < 10)
      break;
    const uint8_t *d = pkt->data;
    int id = ((int)d[1] & 0x7F) | ((int)(d[0] & 0x7F) << 7); /* p2Alt2 */
    uint32_t packed = ((uint32_t)d[6] << 24) | ((uint32_t)d[7] << 16) |
                      ((uint32_t)d[8] << 8) | (uint32_t)d[9];
    int level = (int)(packed >> 28) & 3;
    int x = (int)(packed & 0x3FFF);
    int z = (int)((packed >> 14) & 0x3FFF);
    game_remove_ground_item(game, id, x, z, level);
    break;
  }

  case OSRS_IN_OBJ_COUNT_SPECIFIC: {
    /* 14 bytes: oldQuantity p4Alt1 | newQuantity p4 | coordGrid.packed p4 |
     * id p2 */
    if (pkt->len < 14)
      break;
    const uint8_t *d = pkt->data;
    uint32_t quantity = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) |
                        ((uint32_t)d[6] << 8) | (uint32_t)d[7];
    uint32_t packed = ((uint32_t)d[8] << 24) | ((uint32_t)d[9] << 16) |
                      ((uint32_t)d[10] << 8) | (uint32_t)d[11];
    int id = (d[12] << 8) | d[13];
    int level = (int)(packed >> 28) & 3;
    int x = (int)(packed & 0x3FFF);
    int z = (int)((packed >> 14) & 0x3FFF);
    game_add_ground_item(game, id, x, z, level, (int)quantity);
    break;
  }

  case OSRS_IN_UPDATE_INV_FULL: {
    osrs_stream_t s;
    osrs_stream_init(&s, pkt->data, pkt->len);
    int inv_id = osrs_stream_u16(&s);
    (void)inv_id;
    int count = osrs_stream_u16(&s);
    game->inv_count = 0;
    for (int i = 0; i < count && i < OSRS_GAME_INV_SIZE; i++) {
      if (osrs_stream_remaining(&s) < 4)
        break;
      game->inventory[i].id = osrs_stream_u16(&s);
      game->inventory[i].quantity = osrs_stream_u16(&s);
      game->inv_count++;
    }
    break;
  }

  case OSRS_IN_UPDATE_INV_PARTIAL: {
    osrs_stream_t s;
    osrs_stream_init(&s, pkt->data, pkt->len);
    int inv_id = osrs_stream_u16(&s);
    (void)inv_id;
    int count = osrs_stream_u16(&s);
    for (int i = 0; i < count; i++) {
      if (osrs_stream_remaining(&s) < 4)
        break;
      int slot = osrs_stream_u16(&s);
      int item_id = osrs_stream_u16(&s);
      if (slot >= 0 && slot < OSRS_GAME_INV_SIZE) {
        game->inventory[slot].id = item_id;
        if (osrs_stream_remaining(&s) >= 2)
          game->inventory[slot].quantity = osrs_stream_u16(&s);
      }
    }
    break;
  }

  case OSRS_IN_IF_OPENTOP: {
    int iface;
    if (osrs_parse_if_opentop(pkt, &iface)) {
      game->open_interface = iface;
      game->interface_open = true;
      OSRS_DEBUG(OSRS_LOG_CAT_UI, "Open top: %d", iface);
    }
    break;
  }

  case OSRS_IN_IF_OPENSUB: {
    int iface, component, type;
    if (osrs_parse_if_opensub(pkt, &iface, &component, &type)) {
      game->open_sub_interface = iface;
      game->interface_open = true;
      OSRS_DEBUG(OSRS_LOG_CAT_UI, "Open sub: %d on component %d (type %d)",
                 iface, component, type);
    }
    break;
  }

  case OSRS_IN_IF_CLOSESUB: {
    int component;
    if (osrs_parse_if_closesub(pkt, &component)) {
      game->open_sub_interface = 0;
      if (game->open_interface == 0)
        game->interface_open = false;
      OSRS_DEBUG(OSRS_LOG_CAT_UI, "Close sub on component %d", component);
    }
    break;
  }

  default:
    /* Packet consumed but not handled (interfaces, info blocks, etc.) */
    break;
  }
}

void osrs_game_update(osrs_game_t *game, double dt) {
  if (game->state == OSRS_GAME_DISCONNECTED || game->state == OSRS_GAME_FAILED)
    return;

  game->connected_time += dt;
  game->keepalive_timer += dt;
  game->detect_modified_timer += dt;

  OSRS_TRACE(OSRS_LOG_CAT_GAME, "update dt=%.4f keepalive=%.2f", dt,
             game->keepalive_timer);

  if (!osrs_net_poll(&game->net)) {
    if (game->state != OSRS_GAME_FAILED) {
      game_push_chat(game, 0, "", "Connection lost.");
      game_set_state(game, OSRS_GAME_FAILED);
    }
    return;
  }

  /* Keepalive: NO_TIMEOUT every ~5s of inactivity (matches official client) */
  if (game->proto.state == OSRS_CONN_GAME && game->keepalive_timer > 5.0) {
    game->keepalive_timer = 0;
    game_send_packet(game, OSRS_OUT_NO_TIMEOUT, NULL, 0);
  }

  /* DETECT_MODIFIED_CLIENT every ~30s (anti-tamper heartbeat) */
  if (game->proto.state == OSRS_CONN_GAME &&
      game->detect_modified_timer > 30.0) {
    game->detect_modified_timer = 0;
    osrs_packet_t p;
    osrs_build_detect_modified_client(&p, 0);
    game_send_packet(game, OSRS_OUT_DETECT_MODIFIED_CLIENT, p.data, p.len);
  }

  /* Process incoming bytes */
  for (;;) {
    size_t avail = osrs_net_available(&game->net);
    if (avail == 0)
      break;

    uint8_t buf[8192];
    size_t n_peek = avail > sizeof(buf) ? sizeof(buf) : avail;
    osrs_net_peek(&game->net, buf, n_peek);

    if (game->proto.state == OSRS_CONN_INIT_SENT) {
      size_t consumed = 0;
      int rc = osrs_login_process_init_response(&game->proto, buf, n_peek,
                                                &consumed);
      if (rc == 0)
        break; /* need more data */
      osrs_net_consume(&game->net, consumed);
      if (rc < 0) {
        game->fail_reason = -rc;
        game_set_state(game, OSRS_GAME_FAILED);
        return;
      }

      /* Got session id — send login block */
      uint8_t login_pkt[4096];
      size_t n = osrs_login_build_gamelogin(&game->proto, login_pkt);
      if (n == 0) {
        game_set_state(game, OSRS_GAME_FAILED);
        return;
      }
      fprintf(stderr, "[DEBUG] login packet (%zu bytes): ", n);
      for (size_t i = 0; i < n && i < 128; i++)
        fprintf(stderr, "%02x ", login_pkt[i]);
      fprintf(stderr, "\n");
      osrs_net_queue(&game->net, login_pkt, n);
      game->proto.state = OSRS_CONN_LOGIN_SENT;
      continue;
    }

    if (game->proto.state == OSRS_CONN_LOGIN_SENT) {
      size_t consumed = 0;
      int rc = osrs_login_parse_response(&game->proto, buf, n_peek, &consumed);
      if (rc < 0)
        break; /* need more data */
      OSRS_INFO(OSRS_LOG_CAT_LOGIN, "server response: %d", rc);
      osrs_net_consume(&game->net, consumed);
      if (rc == OSRS_LOGINRESP_PROOF_OF_WORK) {
        /* Solve the SHA256 hashcash challenge and reply. */
        game->proto.state = OSRS_CONN_POW;
        uint64_t result =
            osrs_pow_solve(game->proto.pow_version, game->proto.pow_difficulty,
                           game->proto.pow_salt, 0);
        uint8_t reply[16];
        size_t rlen = osrs_login_build_pow_reply(result, reply);
        osrs_net_queue(&game->net, reply, rlen);
        /* Stop reading: the reply must be flushed (next osrs_net_poll) before
         * we read the server's follow-up response. */
        game->proto.state = OSRS_CONN_LOGIN_SENT;
        break;
      }
      if (rc != OSRS_LOGINRESP_OK) {
        game->fail_reason = rc;
        game_set_state(game, OSRS_GAME_FAILED);
        return;
      }
      game_set_state(game, OSRS_GAME_IN_GAME);
      game->proto.state = OSRS_CONN_GAME;
      OSRS_INFO(OSRS_LOG_CAT_LOGIN,
                "OK! player_index=%d members=%d staffmod=%d playermod=%d "
                "account_hash=%016llx user_id=%llu user_hash=%016llx",
                game->proto.player_index, game->proto.members,
                game->proto.staff_mod_level, (int)game->proto.player_mod,
                (unsigned long long)game->proto.account_hash,
                (unsigned long long)game->proto.user_id,
                (unsigned long long)game->proto.user_hash);
      /* Send initial window status after login */
      osrs_game_send_window_status(game, game->proto.resizable ? 2 : 1,
                                   game->proto.width, game->proto.height);
      /* Tell server the client window has focus */
      {
        osrs_packet_t p;
        osrs_build_event_applet_focus(&p, true);
        game_send_packet(game, OSRS_OUT_EVENT_APPLET_FOCUS, p.data, p.len);
      }
      continue;
    }

    if (game->proto.state == OSRS_CONN_GAME) {
      /* Dump raw bytes for offline analysis */
      {
        static FILE *dump = NULL;
        if (!dump) {
          dump = fopen("/tmp/osrs_game_dump.bin", "wb");
        }
        if (dump) {
          fwrite(buf, 1, n_peek, dump);
          fflush(dump);
        }
      }

      osrs_packet_t pkt;
      size_t consumed = 0;
      int opcode =
          osrs_game_read_packet(&game->proto, buf, n_peek, &pkt, &consumed);
      if (opcode == -1)
        break; /* need more data */
      if (opcode == -2) {
        /* Unknown opcode — try to resync by skipping 1 byte */
        OSRS_WARN(OSRS_LOG_CAT_PROTO,
                  "unknown opcode, skipping 1 byte, avail=%zu first=", n_peek);
        for (size_t i = 0; i < n_peek && i < 8; i++)
          OSRS_WARN(OSRS_LOG_CAT_PROTO, " %02x", buf[i]);
        osrs_net_consume(&game->net, 1);
        continue;
      }
      OSRS_INFO(OSRS_LOG_CAT_PKT, "opcode=%d consumed=%zu", opcode, consumed);
      osrs_net_consume(&game->net, consumed);
      game_handle_packet(game, opcode, &pkt);
      continue;
    }

    break;
  }
}

void osrs_game_move_click(osrs_game_t *game, int x, int z, bool run) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  OSRS_DEBUG(OSRS_LOG_CAT_GAME, "move_click x=%d z=%d run=%d", x, z, (int)run);
  osrs_packet_t p;
  osrs_build_move_gameclick(&p, x, z, run);
  game_send_packet(game, OSRS_OUT_MOVE_GAMECLICK, p.data, p.len);
}

void osrs_game_send_window_status(osrs_game_t *game, int mode, int w, int h) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  osrs_packet_t p;
  osrs_build_window_status(&p, mode, w, h);
  game_send_packet(game, OSRS_OUT_WINDOW_STATUS, p.data, p.len);
}

void osrs_game_send_public_chat(osrs_game_t *game, const char *msg) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  osrs_packet_t p;
  osrs_build_message_public(&p, msg, 0, 0);
  game_send_packet(game, OSRS_OUT_MESSAGE_PUBLIC, p.data, p.len);
}

void osrs_game_send_cheat(osrs_game_t *game, const char *command) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  osrs_packet_t p;
  osrs_build_client_cheat(&p, command);
  game_send_packet(game, OSRS_OUT_CLIENT_CHEAT, p.data, p.len);
}

void osrs_game_map_build_complete(osrs_game_t *game) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  game_send_packet(game, OSRS_OUT_MAP_BUILD_COMPLETE, NULL, 0);
}

void osrs_game_interact_object(osrs_game_t *game, int object_id, int x, int z) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  OSRS_DEBUG(OSRS_LOG_CAT_GAME, "interact_object object_id=%d x=%d z=%d",
             object_id, x, z);
  osrs_packet_t p;
  osrs_build_oploc(&p, OSRS_OUT_OPLOC1_V2, object_id, x, z);
  game_send_packet(game, OSRS_OUT_OPLOC1_V2, p.data, p.len);
}

void osrs_game_interact_npc(osrs_game_t *game, int npc_index) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  OSRS_DEBUG(OSRS_LOG_CAT_GAME, "interact_npc npc_index=%d", npc_index);
  osrs_packet_t p;
  osrs_build_opnpc(&p, OSRS_OUT_OPNPC1_V2, npc_index);
  game_send_packet(game, OSRS_OUT_OPNPC1_V2, p.data, p.len);
}

void osrs_game_interact_player(osrs_game_t *game, int player_index) {
  if (game->proto.state != OSRS_CONN_GAME)
    return;
  OSRS_DEBUG(OSRS_LOG_CAT_GAME, "interact_player player_index=%d",
             player_index);
  osrs_packet_t p;
  osrs_build_opplayer(&p, OSRS_OUT_OPPLAYER1, player_index);
  game_send_packet(game, OSRS_OUT_OPPLAYER1, p.data, p.len);
}

const char *osrs_login_response_str(int code) {
  switch (code) {
  case OSRS_LOGINRESP_OK:
    return "OK";
  case OSRS_LOGINRESP_INVALID_CRED:
    return "Invalid username or password";
  case OSRS_LOGINRESP_BANNED:
    return "Account banned";
  case OSRS_LOGINRESP_DUPLICATE:
    return "Already logged in";
  case OSRS_LOGINRESP_OUT_OF_DATE:
    return "Client out of date";
  case OSRS_LOGINRESP_SERVER_FULL:
    return "World full";
  case OSRS_LOGINRESP_LOGINSERVER_OFFLINE:
    return "Login server offline";
  case OSRS_LOGINRESP_IP_LIMIT:
    return "IP limit reached";
  case OSRS_LOGINRESP_BAD_SESSION_ID:
    return "Bad session id";
  case OSRS_LOGINRESP_NEED_MEMBERS:
    return "Members world";
  case OSRS_LOGINRESP_TOO_MANY_ATTEMPTS:
    return "Too many attempts";
  case OSRS_LOGINRESP_LOCKED:
    return "Account locked";
  case OSRS_LOGINRESP_HOP_BLOCKED:
    return "World hop blocked";
  case OSRS_LOGINRESP_INVALID_LOGIN_PACKET:
    return "Invalid login packet";
  case OSRS_LOGINRESP_AUTHENTICATOR:
    return "Authenticator required";
  case -1:
    return "Connection failed";
  default:
    return "Unknown error";
  }
}

/*
 * tools/packet_decode.c - Decode OSRS packet dumps from libosrs_packets.so
 *
 * Usage: packet_decode <dump_file> <seed0> <seed1> <seed2> <seed3>
 *        packet_decode /tmp/osrs_packets.raw 0x12345678 0x9abcdef0 0xdeadbeef
 * 0xcafebabe
 *
 * Reads the framed binary dump produced by the LD_PRELOAD logger and applies
 * ISAAC decoding to recover server packet opcodes and payloads.
 */

#include "osrs/isaac.h"
#include "osrs/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PAYLOAD 65536

typedef struct {
  uint8_t dir;
  uint64_t ts;
  uint32_t len;
  uint8_t data[MAX_PAYLOAD];
} frame_t;

static const char *direction_str(uint8_t dir) {
  return dir == 0 ? "C->S" : "S->C";
}

static const char *server_pkt_name(int opcode) {
  switch (opcode) {
  case OSRS_IN_MIDI_SONG_STOP:
    return "MIDI_SONG_STOP";
  case OSRS_IN_NPC_INFO_LARGE_V5:
    return "NPC_INFO_LARGE_V5";
  case OSRS_IN_CAM_RESET:
    return "CAM_RESET";
  case OSRS_IN_PLAYER_INFO:
    return "PLAYER_INFO";
  case OSRS_IN_NPC_INFO_SMALL_V5:
    return "NPC_INFO_SMALL_V5";
  case OSRS_IN_MESSAGE_GAME:
    return "MESSAGE_GAME";
  case OSRS_IN_REBUILD_NORMAL_V2:
    return "REBUILD_NORMAL_V2";
  case OSRS_IN_REBUILD_REGION_V2:
    return "REBUILD_REGION_V2";
  case OSRS_IN_UPDATE_STAT_V2:
    return "UPDATE_STAT_V2";
  case OSRS_IN_UPDATE_INV_FULL:
    return "UPDATE_INV_FULL";
  case OSRS_IN_UPDATE_INV_PARTIAL:
    return "UPDATE_INV_PARTIAL";
  case OSRS_IN_IF_OPENTOP:
    return "IF_OPENTOP";
  case OSRS_IN_IF_OPENSUB:
    return "IF_OPENSUB";
  case OSRS_IN_IF_CLOSESUB:
    return "IF_CLOSESUB";
  case OSRS_IN_LOGOUT:
    return "LOGOUT";
  case OSRS_IN_LOGOUT_WITHREASON:
    return "LOGOUT_WITHREASON";
  case OSRS_IN_SET_MAP_FLAG_V2:
    return "SET_MAP_FLAG_V2";
  case OSRS_IN_UPDATE_RUNENERGY:
    return "UPDATE_RUNENERGY";
  case OSRS_IN_UPDATE_RUNWEIGHT:
    return "UPDATE_RUNWEIGHT";
  case OSRS_IN_SEND_PING:
    return "SEND_PING";
  case OSRS_IN_VARP_SMALL:
    return "VARP_SMALL";
  case OSRS_IN_VARP_LARGE:
    return "VARP_LARGE";
  case OSRS_IN_VARP_RESET:
    return "VARP_RESET";
  case OSRS_IN_VARP_SYNC:
    return "VARP_SYNC";
  case OSRS_IN_SERVER_TICK_END:
    return "SERVER_TICK_END";
  case OSRS_IN_UPDATE_ZONE_PARTIAL_FOLLOWS:
    return "UPDATE_ZONE_PARTIAL_FOLLOWS";
  case OSRS_IN_UPDATE_ZONE_FULL_FOLLOWS:
    return "UPDATE_ZONE_FULL_FOLLOWS";
  case OSRS_IN_UPDATE_ZONE_PARTIAL_ENCLOSED:
    return "UPDATE_ZONE_PARTIAL_ENCLOSED";
  default:
    return NULL;
  }
}

static const char *client_pkt_name(int opcode) {
  switch (opcode) {
  case OSRS_OUT_SEND_PING_REPLY:
    return "SEND_PING_REPLY";
  case OSRS_OUT_IDLE:
    return "IDLE";
  case OSRS_OUT_NO_TIMEOUT:
    return "NO_TIMEOUT";
  case OSRS_OUT_WINDOW_STATUS:
    return "WINDOW_STATUS";
  case OSRS_OUT_EVENT_APPLET_FOCUS:
    return "EVENT_APPLET_FOCUS";
  case OSRS_OUT_EVENT_MOUSE_CLICK_V2:
    return "EVENT_MOUSE_CLICK_V2";
  case OSRS_OUT_EVENT_MOUSE_MOVE:
    return "EVENT_MOUSE_MOVE";
  case OSRS_OUT_EVENT_KEYBOARD:
    return "EVENT_KEYBOARD";
  case OSRS_OUT_MOVE_GAMECLICK:
    return "MOVE_GAMECLICK";
  case OSRS_OUT_DETECT_MODIFIED_CLIENT:
    return "DETECT_MODIFIED_CLIENT";
  case OSRS_OUT_CLIENT_CHEAT:
    return "CLIENT_CHEAT";
  case OSRS_OUT_MAP_BUILD_COMPLETE:
    return "MAP_BUILD_COMPLETE";
  case OSRS_OUT_MESSAGE_PUBLIC:
    return "MESSAGE_PUBLIC";
  case OSRS_OUT_MESSAGE_PRIVATE:
    return "MESSAGE_PRIVATE";
  case OSRS_OUT_OPPLAYER1:
    return "OPPLAYER1";
  case OSRS_OUT_OPLOC1_V2:
    return "OPLOC1_V2";
  case OSRS_OUT_OPNPC1_V2:
    return "OPNPC1_V2";
  case OSRS_OUT_OPOBJ1_V2:
    return "OPOBJ1_V2";
  default:
    return NULL;
  }
}

typedef struct {
  osrs_isaac_t isaac;
  uint32_t peek_val;  /* next ISAAC value, or 0 if consumed */
  uint32_t peek2_val; /* second-next ISAAC value */
  bool has_peek;
  bool has_peek2;
} isaac_stream_t;

static void isaac_stream_init(isaac_stream_t *s, const uint32_t seed[4]) {
  osrs_isaac_init(&s->isaac, seed);
  s->has_peek = false;
  s->has_peek2 = false;
}

static uint32_t isaac_stream_next(isaac_stream_t *s) {
  if (s->has_peek) {
    uint32_t v = s->peek_val;
    s->has_peek = false;
    return v;
  }
  return osrs_isaac_next(&s->isaac);
}

static uint32_t isaac_stream_peek(isaac_stream_t *s) {
  if (!s->has_peek) {
    s->peek_val = osrs_isaac_next(&s->isaac);
    s->has_peek = true;
  }
  return s->peek_val;
}

static uint32_t isaac_stream_peek2(isaac_stream_t *s) {
  (void)isaac_stream_peek(s);
  if (!s->has_peek2) {
    s->peek2_val = osrs_isaac_next(&s->isaac);
    s->has_peek2 = true;
  }
  return s->peek2_val;
}

/* -------------------------------------------------------------------------- */
/* Packet decoder (matches osrs_game_read_packet exactly)                     */
/* -------------------------------------------------------------------------- */
static int decode_packet(isaac_stream_t *isaac, const uint8_t *data, size_t len,
                         int *opcode_out, size_t *payload_len_out,
                         size_t *consumed_out) {
  if (len < 1)
    return -1; /* need more */

  uint32_t isaac1 = isaac_stream_peek(isaac) & 0xFF;
  int var1 = (data[0] - (int)isaac1) & 0xFF;
  int opcode;
  size_t pos;

  if (var1 < 128) {
    opcode = var1;
    pos = 1;
  } else {
    if (len < 2)
      return -1;
    uint32_t isaac2 = isaac_stream_peek2(isaac) & 0xFF;
    int var2 = (data[1] - (int)isaac2) & 0xFF;
    opcode = ((var1 - 128) << 8) + var2;
    pos = 2;
  }

  int size = osrs_server_packet_sizes[opcode & 0xFF];
  if (size == OSRS_PKT_VAR_BYTE) {
    if (len < pos + 1)
      return -1;
    size = data[pos];
    pos += 1;
  } else if (size == OSRS_PKT_VAR_SHORT) {
    if (len < pos + 2)
      return -1;
    size = ((int)data[pos] << 8) | data[pos + 1];
    pos += 2;
  } else if (size < 0) {
    /* Unknown opcode */
    *opcode_out = opcode;
    *payload_len_out = 0;
    *consumed_out = pos;
    /* consume ISAAC so stream stays in sync */
    isaac_stream_next(isaac);
    if (var1 >= 128)
      isaac_stream_next(isaac);
    return 0;
  }

  size_t total = pos + (size_t)size;
  if (len < total)
    return -1;

  /* commit ISAAC */
  isaac_stream_next(isaac);
  if (var1 >= 128)
    isaac_stream_next(isaac);

  *opcode_out = opcode;
  *payload_len_out = (size_t)size;
  *consumed_out = total;
  return 0;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
  if (argc < 6) {
    fprintf(stderr,
            "Usage: %s <dump_file> <seed0> <seed1> <seed2> <seed3>\n"
            "  Seeds are the xtea_seed values from the login handshake.\n"
            "  For server packets: seeds = xtea_seed[i] + 50\n"
            "  For client packets: seeds = xtea_seed[i]\n",
            argv[0]);
    return 1;
  }

  const char *path = argv[1];
  uint32_t seed[4];
  for (int i = 0; i < 4; i++)
    seed[i] = (uint32_t)strtoul(argv[i + 2], NULL, 0);

  FILE *f = fopen(path, "rb");
  if (!f) {
    perror("fopen");
    return 1;
  }

  /* Verify header */
  uint8_t hdr[8];
  if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "OSRSPKT", 7) != 0) {
    fprintf(stderr, "Error: not a valid OSRS packet dump\n");
    fclose(f);
    return 1;
  }

  /* Initialize ISAAC for both directions */
  uint32_t server_seed[4];
  for (int i = 0; i < 4; i++)
    server_seed[i] = seed[i] + 50;

  isaac_stream_t isaac_server; /* S->C */
  isaac_stream_t isaac_client; /* C->S */
  isaac_stream_init(&isaac_server, server_seed);
  isaac_stream_init(&isaac_client, seed);

  printf("Dump: %s\n", path);
  printf("Client ISAAC seed: %08x %08x %08x %08x\n", seed[0], seed[1], seed[2],
         seed[3]);
  printf("Server ISAAC seed: %08x %08x %08x %08x\n", server_seed[0],
         server_seed[1], server_seed[2], server_seed[3]);
  printf("\n");

  /* Accumulator for each direction (packets may span multiple reads) */
  uint8_t s_accum[65536];
  uint8_t c_accum[65536];
  size_t s_accum_len = 0;
  size_t c_accum_len = 0;

  for (;;) {
    uint8_t dir;
    uint64_t ts;
    uint32_t len;
    if (fread(&dir, 1, 1, f) != 1)
      break;
    if (fread(&ts, 1, 8, f) != 8)
      break;
    if (fread(&len, 1, 4, f) != 4)
      break;

    uint8_t *accum = (dir == 0) ? c_accum : s_accum;
    size_t *accum_len = (dir == 0) ? &c_accum_len : &s_accum_len;
    isaac_stream_t *isaac = (dir == 0) ? &isaac_client : &isaac_server;

    if (*accum_len + len > sizeof(s_accum)) {
      fprintf(stderr, "Warning: accumulator overflow, flushing\n");
      *accum_len = 0;
      continue;
    }

    if (fread(accum + *accum_len, 1, len, f) != len)
      break;
    *accum_len += len;

    /* Decode as many packets as possible from accumulator */
    for (;;) {
      int opcode;
      size_t payload_len, consumed;
      int rc = decode_packet(isaac, accum, *accum_len, &opcode, &payload_len,
                             &consumed);
      if (rc < 0)
        break; /* need more data */

      const char *name =
          (dir == 0) ? client_pkt_name(opcode) : server_pkt_name(opcode);
      if (!name)
        name = "UNKNOWN";

      printf("[%s] opcode=%-3d %-30s len=%-5zu | ", direction_str(dir), opcode,
             name, payload_len);
      for (size_t i = 0; i < payload_len && i < 16; i++)
        printf("%02x ", accum[consumed - payload_len + i]);
      if (payload_len > 16)
        printf("...");
      printf("\n");

      /* Remove consumed bytes from accumulator */
      size_t remaining = *accum_len - consumed;
      if (remaining > 0)
        memmove(accum, accum + consumed, remaining);
      *accum_len = remaining;
    }
  }

  fclose(f);

  if (s_accum_len > 0 || c_accum_len > 0) {
    printf("\nRemaining unprocessed bytes: server=%zu client=%zu\n",
           s_accum_len, c_accum_len);
  }

  return 0;
}

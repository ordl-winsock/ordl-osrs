/*
 * osrs/playerinfo.c — OSRS rev-239 player info (GPI) decoder
 * Pure C23, zero external dependencies.
 */

#include "osrs/playerinfo.h"

#include <string.h>

#include "osrs/log.h"

/* -------------------------------------------------------------------------- */
/* Bit reader (MSB-first, matches JagByteBuf BitBuf)                          */
/* -------------------------------------------------------------------------- */

typedef struct {
  const uint8_t *data;
  size_t len;    /* in bytes */
  size_t bitpos; /* absolute bit position */
  bool overflow;
} br_t;

static void br_init(br_t *br, const uint8_t *data, size_t len) {
  br->data = data;
  br->len = len;
  br->bitpos = 0;
  br->overflow = false;
}

static uint32_t br_bits(br_t *br, int n) {
  if (n <= 0)
    return 0;
  uint32_t value = 0;
  for (int i = 0; i < n; i++) {
    size_t bytepos = br->bitpos >> 3;
    if (bytepos >= br->len) {
      br->overflow = true;
      return value;
    }
    int bit = 7 - (int)(br->bitpos & 7);
    value = (value << 1) | ((br->data[bytepos] >> bit) & 1u);
    br->bitpos++;
  }
  return value;
}

/* Align to the next byte boundary. */
static void br_align(br_t *br) { br->bitpos = (br->bitpos + 7u) & ~7u; }

static size_t br_byte_pos(const br_t *br) { return (br->bitpos + 7u) >> 3; }

/* Sign-extend an n-bit two's complement value. */
static int32_t sign_extend(uint32_t v, int n) {
  uint32_t signbit = 1u << (n - 1);
  if (v & signbit)
    return (int32_t)(v | ~((1u << n) - 1u));
  return (int32_t)v;
}

/* Single-cell movement deltas (3-bit opcode). */
static const int8_t k_single_dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int8_t k_single_dz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

/* Dual-cell movement deltas (4-bit opcode). */
static const int8_t k_dual_dx[16] = {-2, -1, 0, 1,  2,  -2, 2, -2,
                                     2,  -2, 2, -2, -1, 0,  1, 2};
static const int8_t k_dual_dz[16] = {-2, -2, -2, -2, -2, -1, -1, 0,
                                     0,  1,  1,  2,  2,  2,  2,  2};

/* -------------------------------------------------------------------------- */

void osrs_pi_init(osrs_playerinfo_t *pi) {
  memset(pi, 0, sizeof(*pi));
  pi->local_index = -1;
  pi->view_distance = OSRS_PI_VIEW_DISTANCE;
  for (int i = 0; i < OSRS_PI_CAPACITY; i++) {
    pi->players[i].skull_icon = -1;
    pi->players[i].overhead_icon = -1;
    pi->players[i].transformed_npc = -1;
  }
}

bool osrs_pi_seed_login(osrs_playerinfo_t *pi, int local_index,
                        const uint8_t *data, size_t len) {
  osrs_pi_init(pi);
  pi->local_index = local_index;

  br_t br;
  br_init(&br, data, len);

  /* 30-bit packed local coord: level(2) | x(14) | z(14) */
  uint32_t packed = br_bits(&br, 30);
  int level = (int)(packed >> 28) & 0x3;
  int x = (int)(packed >> 14) & 0x3FFF;
  int z = (int)(packed & 0x3FFF);
  if (br.overflow)
    return false;

  pi->local_x = x;
  pi->local_z = z;
  pi->local_level = level;
  pi->local_pos_valid = true;

  /* Local player is high-res slot 0. */
  osrs_player_t *self = &pi->players[local_index];
  self->active = true;
  self->high_res = true;
  self->was_stationary = false;
  self->x = x;
  self->z = z;
  self->level = level;

  /* Every other slot starts low-res with an 18-bit coarse position. */
  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    if (i == local_index)
      continue;
    uint32_t lp = br_bits(&br, 18);
    osrs_player_t *p = &pi->players[i];
    p->active = false; /* unknown until promoted */
    p->high_res = false;
    p->was_stationary = false;
    p->level = (int)(lp >> 16) & 0x3;
    /* Coarse block coords (x>>13, z>>13); refined on promotion. */
    p->x = ((int)(lp >> 8) & 0xFF) << 13;
    p->z = ((int)lp & 0xFF) << 13;
  }
  return !br.overflow;
}

/* -------------------------------------------------------------------------- */
/* Low-resolution movement buffer (shared by high->low + low-res updates)     */
/* -------------------------------------------------------------------------- */

static void pi_lowres_movement(osrs_playerinfo_t *pi, int idx, br_t *br,
                               int opcode) {
  osrs_player_t *p = &pi->players[idx];
  int level_delta = sign_extend(br_bits(br, 2), 2);
  p->level += level_delta;
  switch (opcode) {
  case 1:
    /* level change only */
    break;
  case 2: {
    int dir = (int)br_bits(br, 3);
    p->x += k_single_dx[dir];
    p->z += k_single_dz[dir];
    break;
  }
  case 3: {
    int dx = sign_extend(br_bits(br, 8), 8);
    int dz = sign_extend(br_bits(br, 8), 8);
    p->x += dx;
    p->z += dz;
    break;
  }
  default:
    break;
  }
}

/* -------------------------------------------------------------------------- */
/* High-resolution passes                                                     */
/* -------------------------------------------------------------------------- */

static void pi_highres_update(osrs_playerinfo_t *pi, int idx, br_t *br) {
  osrs_player_t *p = &pi->players[idx];
  int old_x = p->x;
  int old_z = p->z;
  int old_level = p->level;
  int extinfo = (int)br_bits(br, 1);
  int moveop = (int)br_bits(br, 2);

  if (moveop == 0 && extinfo == 0) {
    /* High -> low resolution removal. */
    int has_lowres_move = (int)br_bits(br, 1);
    if (has_lowres_move) {
      int opcode = (int)br_bits(br, 2);
      pi_lowres_movement(pi, idx, br, opcode);
    }
    p->high_res = false;
    p->has_extinfo = false;
    OSRS_DEBUG(OSRS_LOG_CAT_PI, "player %d removed from high-res", idx);
    return;
  }

  if (extinfo) {
    p->has_extinfo = true;
    pi->extinfo_order[pi->extinfo_count++] = idx;
  } else {
    p->has_extinfo = false;
  }

  switch (moveop) {
  case 0:
    /* Stationary this cycle (has update but no movement). */
    break;
  case 1: { /* walk */
    int dir = (int)br_bits(br, 3);
    p->x += k_single_dx[dir];
    p->z += k_single_dz[dir];
    OSRS_DEBUG(OSRS_LOG_CAT_PI, "player %d walk (%d,%d,%d) -> (%d,%d,%d)", idx,
               old_x, old_z, old_level, p->x, p->z, p->level);
    break;
  }
  case 2: { /* run */
    int dir = (int)br_bits(br, 4);
    p->x += k_dual_dx[dir];
    p->z += k_dual_dz[dir];
    OSRS_DEBUG(OSRS_LOG_CAT_PI, "player %d run (%d,%d,%d) -> (%d,%d,%d)", idx,
               old_x, old_z, old_level, p->x, p->z, p->level);
    break;
  }
  case 3: { /* teleport */
    int large = (int)br_bits(br, 1);
    int level_delta = sign_extend(br_bits(br, 2), 2);
    p->level += level_delta;
    if (large) {
      p->x += sign_extend(br_bits(br, 14), 14);
      p->z += sign_extend(br_bits(br, 14), 14);
    } else {
      p->x += sign_extend(br_bits(br, 5), 5);
      p->z += sign_extend(br_bits(br, 5), 5);
    }
    p->teleported = true;
    OSRS_DEBUG(OSRS_LOG_CAT_PI, "player %d teleport (%d,%d,%d) -> (%d,%d,%d)",
               idx, old_x, old_z, old_level, p->x, p->z, p->level);
    break;
  }
  }

  if (idx == pi->local_index) {
    pi->local_x = p->x;
    pi->local_z = p->z;
    pi->local_level = p->level;
    pi->local_pos_valid = true;
  }
}

/* Run one stationary-filtered pass over a frozen resolution snapshot.
 * which: 1 = high-res set, 0 = low-res set.
 * process_stationary: which was_stationary subset this pass handles. */
static void pi_pass(osrs_playerinfo_t *pi, br_t *br, int which,
                    bool process_stationary) {
  /* Build the filtered index list from the frozen snapshot, in slot order. */
  int n = 0;
  for (int i = 0; i < pi->snap_count[which]; i++) {
    if (pi->snap_was[which][i] != process_stationary)
      continue;
    pi->filtered[n++] = pi->snap_index[which][i];
  }

  int consumed = 0;
  while (consumed < n) {
    int update = (int)br_bits(br, 1);
    if (br->overflow)
      return;
    if (update == 0) {
      /* Skip run: run-length of consecutive no-update players. */
      int opcode = (int)br_bits(br, 2);
      int skip;
      switch (opcode) {
      case 0:
        skip = 1;
        break;
      case 1:
        skip = (int)br_bits(br, 5) + 1;
        break;
      case 2:
        skip = (int)br_bits(br, 8) + 1;
        break;
      default:
        skip = (int)br_bits(br, 11) + 1;
        break;
      }
      for (int k = 0; k < skip && consumed < n; k++) {
        pi->players[pi->filtered[consumed]].is_stationary = true;
        consumed++;
      }
    } else {
      int idx = pi->filtered[consumed];
      osrs_player_t *p = &pi->players[idx];
      p->is_stationary = false;
      p->active = true;
      if (which) {
        pi_highres_update(pi, idx, br);
      } else {
        /* Low-res: peek 2 bits; 0 = promotion, else movement opcode. */
        int v = (int)br_bits(br, 2);
        if (v == 0) {
          /* Promote to high resolution. */
          int has_lowres_move = (int)br_bits(br, 1);
          if (has_lowres_move) {
            int opcode = (int)br_bits(br, 2);
            pi_lowres_movement(pi, idx, br, opcode);
          }
          int x13 = (int)br_bits(br, 13);
          int z13 = (int)br_bits(br, 13);
          int extinfo = (int)br_bits(br, 1);
          /* Reconstruct absolute coords: snap the 13-bit value into the
           * 8192-block containing the local player (they're within the
           * view distance of us). */
          int base_x = pi->local_x & ~0x1FFF;
          int base_z = pi->local_z & ~0x1FFF;
          int ax = base_x | x13;
          int az = base_z | z13;
          if (ax - pi->local_x > 4096)
            ax -= 8192;
          else if (pi->local_x - ax > 4096)
            ax += 8192;
          if (az - pi->local_z > 4096)
            az -= 8192;
          else if (pi->local_z - az > 4096)
            az += 8192;
          p->x = ax;
          p->z = az;
          p->level = pi->local_level;
          p->high_res = true;
          p->is_stationary = true; /* encoder marks promoted stationary */
          OSRS_DEBUG(OSRS_LOG_CAT_PI, "player %d promoted to high-res", idx);
          if (extinfo) {
            p->has_extinfo = true;
            pi->extinfo_order[pi->extinfo_count++] = idx;
          } else {
            p->has_extinfo = false;
          }
        } else {
          pi_lowres_movement(pi, idx, br, v);
          p->is_stationary = false;
        }
      }
      consumed++;
    }
  }
  OSRS_TRACE(OSRS_LOG_CAT_PI, "pass %s %s: processed %d of %d players",
             which ? "high" : "low",
             process_stationary ? "stationary" : "non-stationary", consumed, n);
}

/* -------------------------------------------------------------------------- */
/* Extended info blocks                                                       */
/* -------------------------------------------------------------------------- */

typedef struct {
  const uint8_t *data;
  size_t len;
  size_t pos;
} bytes_t;

static uint32_t bg1(bytes_t *b) {
  if (b->pos >= b->len)
    return 0;
  return b->data[b->pos++];
}
static uint32_t bg1alt1(bytes_t *b) { return (bg1(b) - 128u) & 0xFFu; }
static uint32_t bg1alt2(bytes_t *b) { return (0u - bg1(b)) & 0xFFu; }
static uint32_t bg1alt3(bytes_t *b) { return (128u - bg1(b)) & 0xFFu; }
static uint32_t bg2(bytes_t *b) { return (bg1(b) << 8) | bg1(b); }
static uint32_t bg2alt1(bytes_t *b) { return bg1(b) | (bg1(b) << 8); }
static uint32_t bg2alt2(bytes_t *b) {
  return (bg1(b) << 8) | ((bg1(b) - 128u) & 0xFFu);
}
static uint32_t bg2alt3(bytes_t *b) {
  return ((bg1(b) - 128u) & 0xFFu) | (bg1(b) << 8);
}
static uint32_t bg4(bytes_t *b) {
  return (bg1(b) << 24) | (bg1(b) << 16) | (bg1(b) << 8) | bg1(b);
}
static uint32_t bgsmart1or2(bytes_t *b) {
  if (b->pos >= b->len)
    return 0;
  if (b->data[b->pos] < 128)
    return bg1(b);
  return bg2(b) - 32768u;
}
static uint32_t bgsmart2or4null(bytes_t *b) {
  if (b->pos >= b->len)
    return 0;
  if ((int8_t)b->data[b->pos] < 0) {
    /* 4-byte value (first byte has +128 marker) */
    if (b->pos + 4 > b->len)
      return 0;
    uint32_t v = (bg1(b) << 24) | (bg1(b) << 16) | (bg1(b) << 8) | bg1(b);
    return v & 0x7FFFFFFFu;
  }
  /* 2-byte value */
  if (b->pos + 2 > b->len)
    return 0;
  uint32_t v = (bg1(b) << 8) | bg1(b);
  if (v == 0x7FFF)
    return 0xFFFFFFFFu; /* -1 */
  return v;
}

static void bgstr(bytes_t *b, char *out, size_t max) {
  size_t i = 0;
  while (b->pos < b->len && i + 1 < max) {
    uint8_t c = b->data[b->pos++];
    if (c == 0)
      break;
    out[i++] = (char)c;
  }
  /* consume terminator if we stopped early due to max */
  if (i + 1 >= max) {
    while (b->pos < b->len && b->data[b->pos] != 0)
      b->pos++;
    if (b->pos < b->len)
      b->pos++;
  }
  out[i] = '\0';
}

static void pi_decode_appearance(osrs_playerinfo_t *pi, int idx, bytes_t *b) {
  osrs_player_t *p = &pi->players[idx];
  (void)pi;
  /* Length-prefixed: p1Alt3 length, then that many pdataAlt2 bytes (every
   * payload byte is written +128 on the wire; strip it here). */
  uint32_t blklen = bg1alt3(b);
  OSRS_INFO(OSRS_LOG_CAT_PI, "player %d appearance: blklen=%u pos=%zu len=%zu",
            idx, blklen, b->pos, b->len);
  if (blklen == 0 || b->pos + blklen > b->len) {
    /* malformed; skip what we can */
    OSRS_INFO(OSRS_LOG_CAT_PI,
              "player %d appearance: SKIP blklen=%u pos=%zu len=%zu", idx,
              blklen, b->pos, b->len);
    if (b->pos + blklen <= b->len)
      b->pos += blklen;
    return;
  }
  size_t end = b->pos + blklen;
  uint8_t raw[512];
  if (blklen > sizeof(raw)) {
    b->pos = end;
    return;
  }
  for (uint32_t i = 0; i < blklen; i++)
    raw[i] = (uint8_t)(b->data[b->pos + i] - 128u);
  bytes_t sub = {raw, blklen, 0};

  if (idx == 1) {
    char hex[128];
    size_t n = blklen < 32 ? blklen : 32;
    for (size_t i = 0; i < n; i++)
      snprintf(hex + i * 3, 4, "%02X ", raw[i]);
    hex[n * 3 - 1] = '\0';
    OSRS_DEBUG(OSRS_LOG_CAT_PI, "player %d appearance hex (len=%u): %s", idx,
               blklen, hex);
  }

  p->body_type = (int)bg1(&sub);
  p->skull_icon = (int)(int8_t)bg1(&sub);
  p->overhead_icon = (int)(int8_t)bg1(&sub);

  /* 12 wear slots (visible: equipment or ident kit), variable length:
   * a single 0 byte = empty; otherwise a big-endian short.
   * v == 0xFFFF on slot 0 = NPC transmog.
   * v >= 2048: worn item (v - 2048 = item id).
   * 256 <= v < 2048: ident-kit override ((v - 256) | 0x8000). */
  for (int i = 0; i < 12; i++) {
    p->equipment[i] = -1;
    p->ident_kits[i] = -1;
  }
  for (int i = 0; i < 12; i++) {
    uint32_t hi = bg1(&sub);
    if (hi == 0)
      continue;
    uint32_t v = (hi << 8) | bg1(&sub);
    if (i == 0 && v == 0xFFFF) {
      p->transformed_npc = (int)bg2(&sub);
      break;
    }
    if (v >= 2048) {
      p->equipment[i] = (int)(v - 2048);
    } else if (v >= 256) {
      p->equipment[i] = (int)((v - 256) | 0x8000);
    }
    if (i < 3)
      OSRS_DEBUG(OSRS_LOG_CAT_PI,
                 "player %d slot[%d]: hi=0x%02X v=0x%04X equip=%d", idx, i, hi,
                 v, p->equipment[i]);
  }

  /* Second 12 slots: base ident kits (rev 239+ format).
   * Same encoding: 0 = empty, otherwise v - 256 = kit id. */
  for (int i = 0; i < 12; i++) {
    uint32_t hi = bg1(&sub);
    if (hi == 0)
      continue;
    uint32_t v = (hi << 8) | bg1(&sub);
    if (v >= 256 && v < 2048) {
      p->ident_kits[i] = (int)(v - 256);
    }
  }

  /* Colours: 5 bytes. */
  for (int i = 0; i < 5; i++)
    p->colours[i] = (int)bg1(&sub);
  /* Base animation set: 7 shorts. */
  for (int i = 0; i < 7; i++)
    bg2(&sub);

  bgstr(&sub, p->name, sizeof(p->name));
  p->combat_level = (int)bg1(&sub);
  bg2(&sub); /* skill (total) level */
  bg1(&sub); /* hidden flag */
  /* Obj type customisations, name extras, pronoun: variable; skipped via
   * the block-length resync below. */
  p->appearance_valid = true;
  OSRS_INFO(
      OSRS_LOG_CAT_PI,
      "player %d appearance decoded: name=%s kits=%d,%d,%d equip=%d,%d,%d", idx,
      p->name, p->ident_kits[4], p->ident_kits[6], p->ident_kits[7],
      p->equipment[0], p->equipment[1], p->equipment[2]);

  b->pos = end; /* always resync to block end */
}

static void pi_decode_extinfo(osrs_playerinfo_t *pi, int idx, bytes_t *b) {
  osrs_player_t *p = &pi->players[idx];
  uint32_t flag = bg1(b);
  if (flag & OSRS_EI_EXT_SHORT)
    flag |= bg1(b) << 8;
  if (flag & OSRS_EI_EXT_MEDIUM)
    flag |= bg1(b) << 16;

  OSRS_INFO(OSRS_LOG_CAT_PI, "player %d extinfo flag=0x%X data_len=%zu", idx,
            flag, b->len);
  if (idx == pi->local_index && b->len > 0) {
    char hex[256];
    size_t n = b->len < 64 ? b->len : 64;
    for (size_t i = 0; i < n; i++)
      snprintf(hex + i * 3, 4, "%02X ", b->data[i]);
    hex[n * 3 - 1] = '\0';
    OSRS_INFO(OSRS_LOG_CAT_PI, "player %d extinfo hex: %s", idx, hex);
  }

  /* Known flag mask for the decoder's current revision.  Unknown flags
   * are treated as no-ops (0 bytes consumed) — this keeps the buffer
   * aligned for subsequent players when the server sends flags we don't
   * yet recognise. */
  const uint32_t known_mask =
      OSRS_EI_HITMARKS | OSRS_EI_PLAYER_RESET | OSRS_EI_FACE |
      OSRS_EI_MOVE_SPEED | OSRS_EI_SPOTANIM | OSRS_EI_CHAT |
      OSRS_EI_TRANSPARENCY | OSRS_EI_TINTING | OSRS_EI_HEADBARS | OSRS_EI_SAY |
      OSRS_EI_SEQUENCE | OSRS_EI_TEMP_MOVE_SPEED | OSRS_EI_APPEARANCE |
      OSRS_EI_EXACT_MOVE | OSRS_EI_FREEZE | OSRS_EI_UNKNOWN_80 |
      OSRS_EI_UNKNOWN_10 | OSRS_EI_EXT_SHORT | OSRS_EI_EXT_MEDIUM;
  uint32_t unknown = flag & ~known_mask;
  if (unknown) {
    OSRS_WARN(OSRS_LOG_CAT_PI,
              "player %d extinfo unknown flag bits 0x%X (flag=0x%X), "
              "treating as no-op",
              idx, unknown, flag);
    /* Don't consume any bytes for unknown flags; hope the server puts
     * empty/no-op blocks at the end of the mask. */
  }

  /* Blocks must be consumed in the exact client write order. */
  if (flag & OSRS_EI_HITMARKS) {
    uint32_t count = bg1alt3(b);
    for (uint32_t i = 0; i < count; i++) {
      bgsmart1or2(b); /* type */
      bgsmart1or2(b); /* value */
      bgsmart1or2(b); /* delay */
      bgsmart1or2(b); /* limit */
    }
  }
  if (flag & OSRS_EI_PLAYER_RESET)
    bg1alt2(b);
  if (flag & OSRS_EI_FACE) {
    uint32_t face_flag = bg1alt2(b);
    uint32_t kind = (face_flag >> 3) & 0x3;
    switch (kind) {
    case 0: {             /* Entity */
      bgsmart1or2(b);     /* entity type */
      bgsmart2or4null(b); /* index */
      bgsmart1or2(b);     /* fallback angle */
      break;
    }
    case 1: {         /* Loc */
      bgsmart1or2(b); /* x */
      bgsmart1or2(b); /* z */
      bgsmart1or2(b); /* size bitpacked */
      break;
    }
    case 2: {         /* Angle */
      bgsmart1or2(b); /* angle */
      break;
    }
    case 3: /* Reset */
    default:
      break;
    }
  }
  if (flag & OSRS_EI_MOVE_SPEED)
    p->move_speed = (int)(int8_t)(0 - (int)bg1(b));
  if (flag & OSRS_EI_SPOTANIM) {
    uint32_t count = bg1(b);
    for (uint32_t i = 0; i < count; i++) {
      bg1alt2(b); /* slot */
      bg2alt2(b); /* id */
      bg4(b);     /* delay|height */
    }
  }
  if (flag & OSRS_EI_CHAT) {
    uint32_t colour_effects = bg2alt2(b);
    uint32_t colour = colour_effects >> 8;
    bg1alt2(b); /* modicon */
    bg1alt1(b); /* autotyper */
    uint32_t hlen = bg1alt2(b);
    for (uint32_t i = 0; i < hlen; i++)
      bg1(b); /* huffman-compressed text */
    /* optional pattern bytes for colours 13..20 */
    if (colour >= 13 && colour <= 20) {
      uint32_t pattern_len = colour - 12;
      for (uint32_t i = 0; i < pattern_len; i++)
        bg1alt2(b);
    }
  }
  if (flag & OSRS_EI_TRANSPARENCY) {
    bg2alt2(b); /* start */
    bg2(b);     /* end */
    bg1alt3(b); /* startTransparency */
    bg1alt3(b); /* endTransparency */
    bg1alt3(b); /* useStartTransparency */
  }
  if (flag & OSRS_EI_TINTING) {
    bg2alt1(b);
    bg2(b);
    bg1alt2(b);
    bg1(b);
    bg1alt3(b);
    bg1alt3(b);
  }
  if (flag & OSRS_EI_HEADBARS) {
    uint32_t count = bg1alt1(b);
    for (uint32_t i = 0; i < count; i++) {
      bgsmart1or2(b); /* type */
      uint32_t end = bgsmart1or2(b);
      if (end != 0x7FFF) {
        bgsmart1or2(b); /* start */
        bg1alt1(b);     /* startFill */
        if (end > 0)
          bg1alt2(b); /* endFill */
      }
    }
  }
  if (flag & OSRS_EI_SAY) {
    char tmp[80];
    bgstr(b, tmp, sizeof(tmp));
  }
  if (flag & OSRS_EI_SEQUENCE) {
    bg2alt2(b); /* id */
    bg1(b);     /* delay */
  }
  if (flag & OSRS_EI_UNKNOWN_80) {
    /* Unknown flag 0x80 — server sends this but rsprot docs don't list it.
     * Consuming zero bytes for now; if misalignment shows up in logs we
     * can adjust the block size. */
  }
  if (flag & OSRS_EI_TEMP_MOVE_SPEED)
    bg1(b);
  if (flag & OSRS_EI_APPEARANCE)
    pi_decode_appearance(pi, idx, b);
  if (flag & OSRS_EI_EXACT_MOVE) {
    bg1(b);
    bg1alt1(b);
    bg1alt2(b);
    bg1alt3(b);
    bg2(b);
    bg2alt1(b);
    bg2alt3(b);
  }
  if (flag & OSRS_EI_FREEZE) {
    bg2alt3(b);
    bg2alt3(b);
    bg1alt1(b);
  }
}

/* -------------------------------------------------------------------------- */

bool osrs_pi_decode(osrs_playerinfo_t *pi, const uint8_t *data, size_t len) {
  OSRS_INFO(OSRS_LOG_CAT_PI, "decode start, len=%zu", len);
  pi->extinfo_count = 0;
  for (int i = 0; i < OSRS_PI_CAPACITY; i++)
    pi->players[i].teleported = false;

  /* Snapshot the start-of-cycle resolution lists. Pass membership and
   * stationary filtering read these frozen values; this cycle's updates
   * (promotions/removals) only take effect in the lists built next cycle. */
  pi->snap_count[0] = 0;
  pi->snap_count[1] = 0;
  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    osrs_player_t *p = &pi->players[i];
    int which = p->high_res ? 1 : 0;
    int n = pi->snap_count[which];
    pi->snap_index[which][n] = i;
    pi->snap_was[which][n] = p->was_stationary;
    pi->snap_count[which] = n + 1;
    p->is_stationary = false;
  }

  br_t br;
  br_init(&br, data, len);

  /* Four passes: high-res non-stationary, high-res stationary,
   * low-res stationary, low-res non-stationary. */
  pi_pass(pi, &br, 1, false);
  pi_pass(pi, &br, 1, true);
  pi_pass(pi, &br, 0, true);
  pi_pass(pi, &br, 0, false);

  if (br.overflow)
    return false;

  /* Extended info blocks follow, byte-aligned, in the order flagged. */
  br_align(&br);
  size_t off = br_byte_pos(&br);
  if (off > len)
    off = len;
  bytes_t b = {data + off, len - off, 0};
  for (int i = 0; i < pi->extinfo_count; i++) {
    if (b.pos >= b.len)
      break;
    pi_decode_extinfo(pi, pi->extinfo_order[i], &b);
  }

  /* Rotate stationary flags for the next cycle. */
  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    pi->players[i].was_stationary = pi->players[i].is_stationary;
    pi->players[i].is_stationary = false;
  }
  int high_count = 0, low_count = 0;
  for (int i = 0; i < OSRS_PI_CAPACITY; i++) {
    if (pi->players[i].high_res)
      high_count++;
    else if (pi->players[i].active)
      low_count++;
  }
  OSRS_INFO(OSRS_LOG_CAT_PI,
            "decode complete: local_idx=%d local=(%d,%d,%d) high=%d low=%d",
            pi->local_index, pi->local_x, pi->local_z, pi->local_level,
            high_count, low_count);
  return true;
}

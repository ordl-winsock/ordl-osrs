/*
 * tests/test_playerinfo.c — unit tests for the rev-239 player info decoder.
 * Encodes synthetic GPI bitstreams the way RSProt's encoder would, then
 * verifies osrs_pi_decode / osrs_pi_seed_login reproduce the right state.
 */

#include <stdio.h>
#include <string.h>

#include "osrs/playerinfo.h"

static int failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL: %s (line %d)\n", msg, __LINE__);                           \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/* --- Bit writer (MSB-first), mirrors RSProt pBits --- */
typedef struct {
  uint8_t buf[8192];
  size_t bitpos;
} bw_t;

static void bw_bits(bw_t *w, int n, uint32_t v) {
  for (int i = n - 1; i >= 0; i--) {
    size_t bytepos = w->bitpos >> 3;
    int bit = 7 - (int)(w->bitpos & 7);
    if ((v >> i) & 1u)
      w->buf[bytepos] |= (uint8_t)(1u << bit);
    w->bitpos++;
  }
}

static size_t bw_bytes(bw_t *w) { return (w->bitpos + 7) >> 3; }

/* pStationary run-length encoding (1 bit 0 + opcode + value = skips-1). */
static void bw_stationary(bw_t *w, int skips) {
  int count = skips - 1; /* written value is one less */
  bw_bits(w, 1, 0);
  if (count == 0) {
    bw_bits(w, 2, 0);
  } else if (count <= 0x1F) {
    bw_bits(w, 2, 1);
    bw_bits(w, 5, (uint32_t)count);
  } else if (count <= 0xFF) {
    bw_bits(w, 2, 2);
    bw_bits(w, 8, (uint32_t)count);
  } else {
    bw_bits(w, 2, 3);
    bw_bits(w, 11, (uint32_t)count);
  }
}

/* Build a login footer: 30-bit self + 18-bit x N low-res. */
static size_t build_login_footer(uint8_t *out, int local_index, int x, int z,
                                 int level) {
  bw_t w = {.bitpos = 0};
  memset(w.buf, 0, sizeof(w.buf));
  uint32_t packed = ((uint32_t)(level & 3) << 28) |
                    ((uint32_t)(x & 0x3FFF) << 14) | (uint32_t)(z & 0x3FFF);
  bw_bits(&w, 30, packed);
  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    if (i == local_index)
      continue;
    bw_bits(&w, 18, 0); /* all low-res at block 0,0,0 */
  }
  size_t n = bw_bytes(&w);
  memcpy(out, w.buf, n);
  return n;
}

static void test_seed_login(void) {
  uint8_t footer[8192];
  size_t n = build_login_footer(footer, 1, 3200, 3200, 0);

  osrs_playerinfo_t pi;
  bool ok = osrs_pi_seed_login(&pi, 1, footer, n);
  CHECK(ok, "seed_login returns true");
  CHECK(pi.local_pos_valid, "local pos valid after seed");
  CHECK(pi.local_x == 3200, "local_x == 3200");
  CHECK(pi.local_z == 3200, "local_z == 3200");
  CHECK(pi.local_level == 0, "local_level == 0");
  CHECK(pi.players[1].high_res, "self is high-res");
  CHECK(!pi.players[2].high_res, "other is low-res");
  printf("test_seed_login done\n");
}

/* A single PLAYER_INFO cycle: local player walks East, everyone else idle. */
static void test_local_walk(void) {
  osrs_playerinfo_t pi;
  uint8_t footer[8192];
  size_t n = build_login_footer(footer, 1, 3200, 3200, 0);
  osrs_pi_seed_login(&pi, 1, footer, n);

  bw_t w = {.bitpos = 0};
  memset(w.buf, 0, sizeof(w.buf));
  /* Pass 1: high-res non-stationary = self (index 1). Walk East. */
  bw_bits(&w, 1, 1); /* update */
  bw_bits(&w, 1, 0); /* no extinfo */
  bw_bits(&w, 2, 1); /* moveop walk */
  bw_bits(&w, 3, 4); /* dir E */
  /* Pass 2: high-res stationary = none. */
  /* Pass 3: low-res stationary = none. */
  /* Pass 4: low-res non-stationary = 2046 idle players, one big skip run. */
  bw_stationary(&w, 2046);

  size_t len = bw_bytes(&w);
  bool ok = osrs_pi_decode(&pi, w.buf, len);
  CHECK(ok, "decode returns true");
  CHECK(pi.local_x == 3201, "local_x incremented to 3201 after walk E");
  CHECK(pi.local_z == 3200, "local_z unchanged");
  printf("test_local_walk done (x=%d z=%d)\n", pi.local_x, pi.local_z);
}

/* Local player teleports by a large delta. */
static void test_local_teleport(void) {
  osrs_playerinfo_t pi;
  uint8_t footer[8192];
  size_t n = build_login_footer(footer, 1, 3200, 3200, 0);
  osrs_pi_seed_login(&pi, 1, footer, n);

  bw_t w = {.bitpos = 0};
  memset(w.buf, 0, sizeof(w.buf));
  /* Pass 1: self teleports +40 x, -20 z (large teleport). */
  bw_bits(&w, 1, 1); /* update */
  bw_bits(&w, 1, 0); /* no extinfo */
  bw_bits(&w, 2, 3); /* moveop teleport */
  bw_bits(&w, 1, 1); /* large */
  bw_bits(&w, 2, 0); /* level delta 0 */
  bw_bits(&w, 14, (uint32_t)(40 & 0x3FFF));
  bw_bits(&w, 14, (uint32_t)((-20) & 0x3FFF));
  bw_stationary(&w, 2046);

  size_t len = bw_bytes(&w);
  bool ok = osrs_pi_decode(&pi, w.buf, len);
  CHECK(ok, "decode true");
  CHECK(pi.local_x == 3240, "teleport x 3240");
  CHECK(pi.local_z == 3180, "teleport z 3180");
  printf("test_local_teleport done (x=%d z=%d)\n", pi.local_x, pi.local_z);
}

/* A low-res player is promoted to high-res with an appearance block. */
static void test_promotion_with_appearance(void) {
  osrs_playerinfo_t pi;
  uint8_t footer[8192];
  size_t n = build_login_footer(footer, 1, 3200, 3200, 0);
  osrs_pi_seed_login(&pi, 1, footer, n);

  /* Player 5 promoted to high-res at (3205, 3207), with appearance. */
  bw_t w = {.bitpos = 0};
  memset(w.buf, 0, sizeof(w.buf));
  /* Pass 1: self stationary (update=0 skip run of 1). */
  bw_stationary(&w, 1);
  /* Pass 2: none. Pass 3: none (low-res not stationary). */
  /* Pass 4: low-res non-stationary. Slot order: 2,3,4 skipped (3), then 5
   * promoted, then 6..2047 skipped (2042). */
  bw_stationary(&w, 3); /* slots 2,3,4 */
  /* slot 5: update=1, then v=0 (promotion) */
  bw_bits(&w, 1, 1); /* update */
  bw_bits(&w, 2, 0); /* v=0 -> promotion */
  bw_bits(&w, 1, 0); /* no lowres move */
  bw_bits(&w, 13, 3205 & 0x1FFF);
  bw_bits(&w, 13, 3207 & 0x1FFF);
  bw_bits(&w, 1, 1);       /* has extinfo */
  bw_stationary(&w, 2042); /* slots 6..2047 */

  /* Byte-align, then one appearance extended-info block for slot 5.
   * Rev 239 appearance payload: equipment/kit slots are single 0 bytes
   * when empty; every payload byte is written +128 (pdataAlt2). */
  uint8_t app[128];
  size_t ap = 0;
  app[ap++] = 0;   /* body type male */
  app[ap++] = 255; /* skull -1 */
  app[ap++] = 255; /* overhead -1 */
  for (int i = 0; i < 12; i++)
    app[ap++] = 0; /* equipment: 0 = empty */
  for (int i = 0; i < 12; i++)
    app[ap++] = 0; /* ident kits: 0 = none */
  for (int i = 0; i < 5; i++)
    app[ap++] = 0; /* colours */
  for (int i = 0; i < 7; i++) {
    app[ap++] = 0; /* base anim shorts */
    app[ap++] = 0;
  }
  const char *nm = "Bob";
  memcpy(app + ap, nm, 4);
  ap += 4;         /* "Bob\0" */
  app[ap++] = 42;  /* combat level */
  app[ap++] = 0;   /* skill level hi */
  app[ap++] = 100; /* skill level lo */
  app[ap++] = 0;   /* hidden */

  /* Assemble extended info section: mask=APPEARANCE(0x20), then block. */
  uint8_t *tail = w.buf + bw_bytes(&w);
  size_t tp = 0;
  tail[tp++] = 0x20;                     /* mask: appearance */
  tail[tp++] = (uint8_t)(128 - (int)ap); /* p1Alt3 length */
  for (size_t i = 0; i < ap; i++)
    tail[tp++] = (uint8_t)(app[i] + 128); /* pdataAlt2 payload */

  size_t len = bw_bytes(&w) + tp;
  bool ok = osrs_pi_decode(&pi, w.buf, len);
  CHECK(ok, "decode true");
  CHECK(pi.players[5].high_res, "player 5 promoted to high-res");
  CHECK(pi.players[5].x == 3205, "player 5 x == 3205");
  CHECK(pi.players[5].z == 3207, "player 5 z == 3207");
  CHECK(pi.players[5].appearance_valid, "player 5 appearance valid");
  CHECK(strcmp(pi.players[5].name, "Bob") == 0, "player 5 name == Bob");
  CHECK(pi.players[5].combat_level == 42, "player 5 combat == 42");
  printf("test_promotion_with_appearance done (p5: %d,%d '%s' lvl %d)\n",
         pi.players[5].x, pi.players[5].z, pi.players[5].name,
         pi.players[5].combat_level);
}

int main(void) {
  test_seed_login();
  test_local_walk();
  test_local_teleport();
  test_promotion_with_appearance();
  if (failures) {
    printf("\n%d FAILURES\n", failures);
    return 1;
  }
  printf("\nAll playerinfo tests passed.\n");
  return 0;
}

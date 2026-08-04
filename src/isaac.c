/*
 * osrs/isaac.c — ISAAC stream cipher implementation
 * Pure C23, zero external dependencies.
 *
 * Matches the OSRS client's IsaacCipher exactly (deob verified):
 *   - results/memory pools start ZEROED, seed words go into results[0..]
 *   - eight accumulators start at the golden ratio and are scrambled 4×
 *   - two seed-mixing passes, then the results pool is generated
 *   - results are consumed from rsl[255] DOWN to rsl[0]
 */

#include "osrs/isaac.h"

#include <string.h>

/* ISAAC mixing macro — standard Bob Jenkins mix(). */
#define ISAAC_MIX(a, b, c, d, e, f, g, h)                                      \
  do {                                                                         \
    a ^= b << 11;                                                              \
    d += a;                                                                    \
    b += c;                                                                    \
    b ^= c >> 2;                                                               \
    e += b;                                                                    \
    c += d;                                                                    \
    c ^= d << 8;                                                               \
    f += c;                                                                    \
    d += e;                                                                    \
    d ^= e >> 16;                                                              \
    g += d;                                                                    \
    e += f;                                                                    \
    e ^= f << 10;                                                              \
    h += e;                                                                    \
    f += g;                                                                    \
    f ^= g >> 4;                                                               \
    a += f;                                                                    \
    g += h;                                                                    \
    g ^= h << 8;                                                               \
    b += g;                                                                    \
    h += a;                                                                    \
    h ^= a >> 9;                                                               \
    c += h;                                                                    \
    a += b;                                                                    \
  } while (0)

void osrs_isaac_refill(osrs_isaac_t *ctx) {
  uint32_t a = ctx->a;
  uint32_t b = ctx->b;
  uint32_t c = ctx->c;
  uint32_t x, y;

  c = c + 1;
  b = b + c;

  for (int i = 0; i < OSRS_ISAAC_WORDS; i++) {
    x = ctx->mem[i];
    switch (i & 3) {
    case 0:
      a = a ^ (a << 13);
      break;
    case 1:
      a = a ^ (a >> 6);
      break;
    case 2:
      a = a ^ (a << 2);
      break;
    case 3:
      a = a ^ (a >> 16);
      break;
    }
    a = ctx->mem[(i + 128) & 0xFF] + a;
    y = ctx->mem[(x >> 2) & 0xFF] + a + b;
    ctx->mem[i] = y;
    b = ctx->mem[(y >> 10) & 0xFF] + x;
    ctx->rsl[i] = b;
  }

  ctx->a = a;
  ctx->b = b;
  ctx->c = c;
  ctx->idx = OSRS_ISAAC_WORDS;
}

void osrs_isaac_init_seed(osrs_isaac_t *ctx, const uint8_t *seed, size_t len) {
  uint32_t a = 0x9E3779B9u;
  uint32_t b = 0x9E3779B9u;
  uint32_t c = 0x9E3779B9u;
  uint32_t d = 0x9E3779B9u;
  uint32_t e = 0x9E3779B9u;
  uint32_t f = 0x9E3779B9u;
  uint32_t g = 0x9E3779B9u;
  uint32_t h = 0x9E3779B9u;

  /* mem and rsl start ZEROED (the client uses `new int[256]`). Only the seed
   * words are placed into rsl[0..]; everything else stays zero. */
  memset(ctx->mem, 0, sizeof(ctx->mem));
  memset(ctx->rsl, 0, sizeof(ctx->rsl));

  /* Fill in the seed (little-endian words) */
  size_t seed_words = len / 4;
  if (seed_words > OSRS_ISAAC_WORDS)
    seed_words = OSRS_ISAAC_WORDS;
  for (size_t i = 0; i < seed_words; i++) {
    ctx->rsl[i] = ((uint32_t)seed[i * 4]) | ((uint32_t)seed[i * 4 + 1] << 8) |
                  ((uint32_t)seed[i * 4 + 2] << 16) |
                  ((uint32_t)seed[i * 4 + 3] << 24);
  }

  /* Initialize state */
  ctx->a = ctx->b = ctx->c = 0;

  /* Initial scramble of the eight accumulators */
  for (int i = 0; i < 4; i++) {
    ISAAC_MIX(a, b, c, d, e, f, g, h);
  }

  /* Mix in the seed (first pass) */
  for (int i = 0; i < OSRS_ISAAC_WORDS; i += 8) {
    a += ctx->rsl[i];
    b += ctx->rsl[i + 1];
    c += ctx->rsl[i + 2];
    d += ctx->rsl[i + 3];
    e += ctx->rsl[i + 4];
    f += ctx->rsl[i + 5];
    g += ctx->rsl[i + 6];
    h += ctx->rsl[i + 7];
    ISAAC_MIX(a, b, c, d, e, f, g, h);
    ctx->mem[i] = a;
    ctx->mem[i + 1] = b;
    ctx->mem[i + 2] = c;
    ctx->mem[i + 3] = d;
    ctx->mem[i + 4] = e;
    ctx->mem[i + 5] = f;
    ctx->mem[i + 6] = g;
    ctx->mem[i + 7] = h;
  }

  /* Second pass — makes all of seed affect all of mem */
  for (int i = 0; i < OSRS_ISAAC_WORDS; i += 8) {
    a += ctx->mem[i];
    b += ctx->mem[i + 1];
    c += ctx->mem[i + 2];
    d += ctx->mem[i + 3];
    e += ctx->mem[i + 4];
    f += ctx->mem[i + 5];
    g += ctx->mem[i + 6];
    h += ctx->mem[i + 7];
    ISAAC_MIX(a, b, c, d, e, f, g, h);
    ctx->mem[i] = a;
    ctx->mem[i + 1] = b;
    ctx->mem[i + 2] = c;
    ctx->mem[i + 3] = d;
    ctx->mem[i + 4] = e;
    ctx->mem[i + 5] = f;
    ctx->mem[i + 6] = g;
    ctx->mem[i + 7] = h;
  }

  /* Fill results pool; consume from the top down */
  osrs_isaac_refill(ctx);
  ctx->cnt = OSRS_ISAAC_WORDS;
}

void osrs_isaac_init(osrs_isaac_t *ctx, const uint32_t seed[4]) {
  uint8_t seed_bytes[16];
  for (int i = 0; i < 4; i++) {
    seed_bytes[i * 4] = (uint8_t)(seed[i]);
    seed_bytes[i * 4 + 1] = (uint8_t)(seed[i] >> 8);
    seed_bytes[i * 4 + 2] = (uint8_t)(seed[i] >> 16);
    seed_bytes[i * 4 + 3] = (uint8_t)(seed[i] >> 24);
  }
  osrs_isaac_init_seed(ctx, seed_bytes, sizeof(seed_bytes));
}

uint32_t osrs_isaac_next(osrs_isaac_t *ctx) {
  /* Consume results from rsl[255] down to rsl[0] (matches the client's
   * nextInt(): results[--valuesRemaining]). */
  if (ctx->cnt == 0) {
    osrs_isaac_refill(ctx);
    ctx->cnt = OSRS_ISAAC_WORDS;
  }
  return ctx->rsl[--ctx->cnt];
}

uint32_t osrs_isaac_peek(const osrs_isaac_t *ctx) {
  /* Return the next value that osrs_isaac_next would produce, without
   * advancing the cipher. If the pool is exhausted, return 0 (caller must
   * ensure the pool is not empty before peeking). */
  if (ctx->cnt == 0)
    return 0;
  return ctx->rsl[ctx->cnt - 1];
}

uint32_t osrs_isaac_peek2(const osrs_isaac_t *ctx) {
  /* Return the second-next value that osrs_isaac_next would produce,
   * without advancing the cipher. If fewer than 2 values remain, return 0. */
  if (ctx->cnt < 2)
    return 0;
  return ctx->rsl[ctx->cnt - 2];
}

void osrs_isaac_bytes(osrs_isaac_t *ctx, uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i++) {
    out[i] = (uint8_t)(osrs_isaac_next(ctx) >> ((i & 3) * 8));
  }
}

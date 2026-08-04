/*
 * osrs/isaac.h — ISAAC stream cipher
 * Pure C23, zero external dependencies.
 *
 * ISAAC (Indirection, Shift, Accumulate, Add, and Count) is a
 * cryptographically secure PRNG and stream cipher by Bob Jenkins.
 * Used by OSRS for game packet opcode obfuscation.
 */

#ifndef OSRS_ISAAC_H
#define OSRS_ISAAC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSRS_ISAAC_WORDS 256

/* ISAAC context */
typedef struct {
  uint32_t mem[OSRS_ISAAC_WORDS]; /* Memory pool */
  uint32_t rsl[OSRS_ISAAC_WORDS]; /* Results pool */
  uint32_t cnt;                   /* Counter */
  uint32_t a, b, c;               /* Accumulator, last result, counter */
  uint32_t idx;                   /* Index into rsl */
} osrs_isaac_t;

/* Initialize ISAAC with seed (4 x uint32 from login handshake) */
void osrs_isaac_init(osrs_isaac_t *ctx, const uint32_t seed[4]);

/* Initialize ISAAC with arbitrary seed data */
void osrs_isaac_init_seed(osrs_isaac_t *ctx, const uint8_t *seed, size_t len);

/* Generate next 32-bit value */
uint32_t osrs_isaac_next(osrs_isaac_t *ctx);

/* Peek the next 32-bit value without consuming it */
uint32_t osrs_isaac_peek(const osrs_isaac_t *ctx);

/* Peek the value after next without consuming anything.
 * Returns 0 if there are fewer than 2 values remaining. */
uint32_t osrs_isaac_peek2(const osrs_isaac_t *ctx);

/* Generate bytes */
void osrs_isaac_bytes(osrs_isaac_t *ctx, uint8_t *out, size_t len);

/* Refill results pool (called automatically when needed) */
void osrs_isaac_refill(osrs_isaac_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_ISAAC_H */

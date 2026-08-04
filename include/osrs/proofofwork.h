/*
 * osrs/proofofwork.h — OSRS login proof-of-work (SHA256 hashcash) solver
 * Pure C23, zero external dependencies.
 *
 * When the login server responds PROOF_OF_WORK (69), it sends a challenge:
 *   challengeType.id(1) + version(1) + difficulty(1) + salt(jstr)
 * The client must find a `result` (uint64) such that
 *   SHA256( hex(version) + hex(difficulty) + salt + hex(result) )
 * has at least `difficulty` leading zero bits, and reply with p8(result).
 */

#ifndef OSRS_PROOF_OF_WORK_H
#define OSRS_PROOF_OF_WORK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Count leading zero bits across a 32-byte hash (MSB of byte 0 first). */
int osrs_pow_leading_zeros(const uint8_t hash[32]);

/* Solve a SHA256 proof-of-work challenge.
 * baseString = hex(version) + hex(difficulty) + salt (built by caller or
 * via osrs_pow_solve). Returns the result value that satisfies the
 * difficulty, starting the search at `start`. */
uint64_t osrs_pow_solve(int version, int difficulty, const char *salt,
                        uint64_t start);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_PROOF_OF_WORK_H */

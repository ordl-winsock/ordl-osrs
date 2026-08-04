/*
 * osrs/proofofwork.c — OSRS login proof-of-work (SHA256 hashcash) solver
 * Pure C23, zero external dependencies.
 */

#include "osrs/proofofwork.h"
#include "osrs/log.h"

#include <stdio.h>
#include <string.h>

#include "security/sha256.h"

int osrs_pow_leading_zeros(const uint8_t hash[32]) {
  int bits = 0;
  for (int i = 0; i < 32; i++) {
    uint8_t b = hash[i];
    if (b == 0) {
      bits += 8;
      continue;
    }
    /* count leading zeros in this byte */
    for (int j = 7; j >= 0; j--) {
      if (b & (1u << j))
        return bits;
      bits++;
    }
    return bits;
  }
  return bits;
}

uint64_t osrs_pow_solve(int version, int difficulty, const char *salt,
                        uint64_t start) {
  OSRS_INFO(OSRS_LOG_CAT_POW, "PoW solve start: version=%d difficulty=%d",
            version, difficulty);

  /* baseString = hex(version) + hex(difficulty) + salt */
  char base[1024];
  int baselen =
      snprintf(base, sizeof(base), "%x%x%s", version, difficulty, salt);

  char candidate[1200];
  memcpy(candidate, base, (size_t)baselen);

  uint8_t hash[32];
  uint64_t result = start;
  for (;; result++) {
    int hexlen =
        snprintf(candidate + baselen, sizeof(candidate) - (size_t)baselen,
                 "%llx", (unsigned long long)result);
    gc_sha256((const uint8_t *)candidate, (size_t)(baselen + hexlen), hash);
    if (osrs_pow_leading_zeros(hash) >= difficulty) {
      OSRS_INFO(OSRS_LOG_CAT_POW, "PoW solved: result=%llu (tried %llu hashes)",
                (unsigned long long)result,
                (unsigned long long)(result - start + 1));
      return result;
    }
  }
}

/*
 * osrs/rsa.c - RSA public-key encryption for OSRS login protocol
 * Pure C23, zero external dependencies.
 *
 * RSA-encrypts the login block with Jagex's public key:
 *   ciphertext = plaintext^e mod n
 */

#include "osrs/rsa.h"
#include "osrs/bignum.h"
#include "osrs/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OSRS RSA public key for the login block (GAMELOGIN), extracted from the
 * live gamepack. Modulus and exponent are big-endian hex; 1024-bit. */
static const char OSRS_RSA_MODULUS_HEX[] =
    "c4cc48b4f69a621564fe6227e5ee0d9a58642f25b2e29800d4529bdb92f693b"
    "226f06c62fa3d61ce8b578b77b0bb2a4074c05a4e3ff901917d2db94e76718f7"
    "12619ce0ec71239558f1753b28a0654a542375f6302df7c1e06d1df07cbc4297"
    "d792cba9df43ea09b2059c868eaffff0bad854574d270624794379cb5e8b061f3";

static const char OSRS_RSA_EXPONENT_HEX[] = "10001";

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static size_t hex_decode(const char *hex, uint8_t *out, size_t out_cap) {
  size_t hex_len = strlen(hex);
  size_t out_len = (hex_len + 1) / 2;
  if (out_len > out_cap)
    return 0;
  size_t i = 0;
  size_t pos = 0;
  /* Odd-length hex: the first nibble stands alone (e.g. "10001" = 0x10001). */
  if (hex_len & 1) {
    int lo = hex_digit(hex[0]);
    if (lo < 0)
      return 0;
    out[i++] = (uint8_t)lo;
    pos = 1;
  }
  for (; pos + 1 < hex_len; pos += 2) {
    int hi = hex_digit(hex[pos]);
    int lo = hex_digit(hex[pos + 1]);
    if (hi < 0 || lo < 0)
      return 0;
    out[i++] = (uint8_t)((hi << 4) | lo);
  }
  return i;
}

size_t osrs_rsa_encrypt_login(const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *out, size_t out_len) {
  uint8_t mod_bytes[256];
  uint8_t exp_bytes[8];
  /* Allow overriding the login RSA key (hex) for local protocol testing
   * against a private server with a known keypair. */
  const char *mod_hex = getenv("OSRS_RSA_MODULUS");
  const char *exp_hex = getenv("OSRS_RSA_EXPONENT");
  if (!mod_hex || !mod_hex[0])
    mod_hex = OSRS_RSA_MODULUS_HEX;
  if (!exp_hex || !exp_hex[0])
    exp_hex = OSRS_RSA_EXPONENT_HEX;
  size_t mod_len = hex_decode(mod_hex, mod_bytes, sizeof(mod_bytes));
  size_t exp_len = hex_decode(exp_hex, exp_bytes, sizeof(exp_bytes));
  if (mod_len == 0 || exp_len == 0) {
    OSRS_ERROR(OSRS_LOG_CAT_CRYPTO, "RSA key decode failed (mod=%zu exp=%zu)",
               mod_len, exp_len);
    return 0;
  }

  OSRS_DEBUG(OSRS_LOG_CAT_CRYPTO,
             "RSA encrypt: mod=%zuB exp=%zuB plaintext=%zuB", mod_len, exp_len,
             plaintext_len);

  if (plaintext_len > mod_len || out_len < mod_len)
    return 0;

  /* OSRS client does not pad the RSA plaintext - it treats the raw bytes
   * as a big-endian integer directly.  The plaintext must be <= modulus. */
  osrs_bn_t m, e, p, c;
  osrs_bn_from_bytes(&m, mod_bytes, mod_len);
  osrs_bn_from_bytes(&e, exp_bytes, exp_len);
  osrs_bn_from_bytes(&p, plaintext, plaintext_len);

  osrs_bn_modpow(&c, &p, &e, &m);

  /* Write the ciphertext and compute its natural length.
   * If the most-significant byte would have the high bit set,
   * Java's BigInteger (used by the server) interprets it as negative.
   * Prepend a 0x00 byte to force positivity, matching BigInteger.toByteArray().
   */
  osrs_bn_to_bytes(&c, out, mod_len);
  size_t ct_len = mod_len;
  while (ct_len > 1 && out[mod_len - ct_len] == 0)
    ct_len--;
  if (ct_len < mod_len)
    memmove(out, out + mod_len - ct_len, ct_len);
  if (out[0] >= 0x80) {
    memmove(out + 1, out, ct_len);
    out[0] = 0x00;
    ct_len++;
  }
  return ct_len;
}

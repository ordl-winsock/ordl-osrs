/*
 * osrs/rsa.h - RSA public-key encryption for OSRS login protocol
 * Pure C23, zero external dependencies.
 */

#ifndef OSRS_RSA_H
#define OSRS_RSA_H

#include <stddef.h>
#include <stdint.h>

/* RSA encrypt a block with the OSRS public key.
 * plaintext: raw bytes (must be <= modulus size - 1)
 * plaintext_len: length of plaintext
 * out: receives ciphertext (same size as modulus)
 * out_len: size of out buffer, must be >= modulus byte size
 * Returns number of bytes written, or 0 on failure.
 */
size_t osrs_rsa_encrypt_login(const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *out, size_t out_len);

#endif /* OSRS_RSA_H */

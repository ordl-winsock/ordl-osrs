/*
 * osrs/xtea.h — XTEA (eXtended TEA) block cipher
 * Pure C23, zero external dependencies.
 *
 * Used by OSRS for map/region file decryption.
 * 64-bit block size, 128-bit key, 32 rounds.
 */

#ifndef OSRS_XTEA_H
#define OSRS_XTEA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSRS_XTEA_KEY_SIZE   16  /* 128 bits */
#define OSRS_XTEA_BLOCK_SIZE 8   /* 64 bits  */
#define OSRS_XTEA_ROUNDS     32

/* XTEA context */
typedef struct {
    uint32_t key[4];  /* 128-bit key */
} osrs_xtea_t;

/* Initialize XTEA context with 128-bit key */
void osrs_xtea_init(osrs_xtea_t *ctx, const uint8_t key[OSRS_XTEA_KEY_SIZE]);

/* Initialize XTEA context with key as 4 x uint32 */
void osrs_xtea_init_u32(osrs_xtea_t *ctx, const uint32_t key[4]);

/* Encrypt 8-byte block */
void osrs_xtea_encrypt_block(const osrs_xtea_t *ctx, uint8_t block[OSRS_XTEA_BLOCK_SIZE]);

/* Decrypt 8-byte block */
void osrs_xtea_decrypt_block(const osrs_xtea_t *ctx, uint8_t block[OSRS_XTEA_BLOCK_SIZE]);

/* Encrypt data (length must be multiple of 8) */
void osrs_xtea_encrypt(const osrs_xtea_t *ctx, uint8_t *data, size_t len);

/* Decrypt data (length must be multiple of 8) */
void osrs_xtea_decrypt(const osrs_xtea_t *ctx, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_XTEA_H */

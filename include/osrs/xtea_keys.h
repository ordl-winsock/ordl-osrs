/*
 * osrs/xtea_keys.h - XTEA key store for map decryption
 * Pure C23, zero external dependencies.
 */

#ifndef OSRS_XTEA_KEYS_H
#define OSRS_XTEA_KEYS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t mapsquare; /* Region ID = (region_x << 8) | region_y */
  uint32_t key[4];    /* XTEA key */
} osrs_xtea_key_t;

typedef struct {
  osrs_xtea_key_t *keys;
  int count;
  int capacity;
} osrs_xtea_store_t;

/* Create empty key store */
osrs_xtea_store_t *osrs_xtea_store_create(void);

/* Destroy key store */
void osrs_xtea_store_destroy(osrs_xtea_store_t *store);

/* Load keys from binary file (format: count uint32le, then mapsquare uint32le +
 * 4x int32le) */
bool osrs_xtea_store_load_binary(osrs_xtea_store_t *store, const char *path);

/* Get key for a mapsquare (region_id), returns NULL if not found */
const uint32_t *osrs_xtea_store_get_key(const osrs_xtea_store_t *store,
                                        uint32_t mapsquare);

/* Get key for region coordinates, returns NULL if not found */
const uint32_t *
osrs_xtea_store_get_key_for_region(const osrs_xtea_store_t *store, int region_x,
                                   int region_y);

/* Add or replace a key for a mapsquare */
bool osrs_xtea_store_add_key(osrs_xtea_store_t *store, uint32_t mapsquare,
                             const uint32_t key[4]);

#endif /* OSRS_XTEA_KEYS_H */

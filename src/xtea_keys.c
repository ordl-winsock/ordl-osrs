/*
 * osrs/xtea_keys.c - XTEA key store implementation
 * Pure C23, zero external dependencies.
 */

#include "osrs/xtea_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

osrs_xtea_store_t *osrs_xtea_store_create(void) {
  osrs_xtea_store_t *store = calloc(1, sizeof(osrs_xtea_store_t));
  return store;
}

void osrs_xtea_store_destroy(osrs_xtea_store_t *store) {
  if (!store)
    return;
  free(store->keys);
  free(store);
}

bool osrs_xtea_store_load_binary(osrs_xtea_store_t *store, const char *path) {
  if (!store || !path)
    return false;

  FILE *f = fopen(path, "rb");
  if (!f)
    return false;

  uint32_t count;
  if (fread(&count, sizeof(count), 1, f) != 1) {
    fclose(f);
    return false;
  }

  store->keys = realloc(store->keys, (size_t)count * sizeof(osrs_xtea_key_t));
  if (!store->keys) {
    fclose(f);
    return false;
  }

  for (uint32_t i = 0; i < count; i++) {
    osrs_xtea_key_t *k = &store->keys[i];
    if (fread(&k->mapsquare, sizeof(k->mapsquare), 1, f) != 1)
      break;
    for (int j = 0; j < 4; j++) {
      int32_t key_part;
      if (fread(&key_part, sizeof(key_part), 1, f) != 1)
        break;
      k->key[j] = (uint32_t)key_part;
    }
  }

  store->count = (int)count;
  store->capacity = (int)count;
  fclose(f);
  return true;
}

const uint32_t *osrs_xtea_store_get_key(const osrs_xtea_store_t *store,
                                        uint32_t mapsquare) {
  if (!store || !store->keys)
    return NULL;

  for (int i = 0; i < store->count; i++) {
    if (store->keys[i].mapsquare == mapsquare)
      return store->keys[i].key;
  }
  return NULL;
}

const uint32_t *
osrs_xtea_store_get_key_for_region(const osrs_xtea_store_t *store, int region_x,
                                   int region_y) {
  uint32_t mapsquare = (uint32_t)((region_x << 8) | region_y);
  return osrs_xtea_store_get_key(store, mapsquare);
}

bool osrs_xtea_store_add_key(osrs_xtea_store_t *store, uint32_t mapsquare,
                             const uint32_t key[4]) {
  if (!store)
    return false;

  /* Check if key already exists for this mapsquare */
  for (int i = 0; i < store->count; i++) {
    if (store->keys[i].mapsquare == mapsquare) {
      memcpy(store->keys[i].key, key, 4 * sizeof(uint32_t));
      return true;
    }
  }

  /* Need to grow array */
  if (store->count >= store->capacity) {
    int new_cap = store->capacity == 0 ? 16 : store->capacity * 2;
    osrs_xtea_key_t *new_keys =
        realloc(store->keys, (size_t)new_cap * sizeof(osrs_xtea_key_t));
    if (!new_keys)
      return false;
    store->keys = new_keys;
    store->capacity = new_cap;
  }

  osrs_xtea_key_t *k = &store->keys[store->count++];
  k->mapsquare = mapsquare;
  memcpy(k->key, key, 4 * sizeof(uint32_t));
  return true;
}

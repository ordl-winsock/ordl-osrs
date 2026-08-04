/*
 * tools/stay_connected.c — Connect and stay connected, just processing packets.
 */
#include "osrs/auth.h"
#include "osrs/cache.h"
#include "osrs/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <cache_path> <host> [user] [pass]\n", argv[0]);
    return 1;
  }

  const char *cache_path = argv[1];
  const char *host = argv[2];
  const char *user = argc > 3 ? argv[3] : "fake_user_12345";
  const char *pass = argc > 4 ? argv[4] : "fake_pass_12345";

  osrs_auth_t auth;
  memset(&auth, 0, sizeof(auth));
  auth.method = OSRS_AUTH_LEGACY;
  snprintf(auth.username, sizeof(auth.username), "%s", user);
  snprintf(auth.secret, sizeof(auth.secret), "%s", pass);

  static char host_buf[512];
  uint16_t port = OSRS_GAME_PORT;
  snprintf(host_buf, sizeof(host_buf), "%s", host);
  char *colon = strrchr(host_buf, ':');
  if (colon) {
    *colon = '\0';
    int p = atoi(colon + 1);
    if (p > 0)
      port = (uint16_t)p;
  }

  printf("Loading cache from %s...\n", cache_path);
  osrs_cache_t *cache = osrs_cache_open(cache_path);
  if (!cache) {
    fprintf(stderr, "Failed to open cache\n");
    return 1;
  }
  osrs_cache_load_indexes(cache);
  printf("Loaded %d indexes\n", cache->index_count);

  uint32_t crcs[23] = {0};
  for (int i = 0; i < 23; i++) {
    osrs_index_t *idx = osrs_cache_get_index(cache, i);
    if (idx)
      crcs[i] = (uint32_t)idx->crc;
  }

  osrs_game_t game;
  osrs_game_init(&game);

  printf("Connecting to %s:%d as '%s'...\n", host_buf, (int)port,
         auth.username);
  if (!osrs_game_connect(&game, host_buf, port, &auth, crcs, 10000)) {
    fprintf(stderr, "Connection failed\n");
    osrs_cache_close(cache);
    return 1;
  }

  /* Run for 30 seconds, just processing packets */
  for (int i = 0; i < 3000; i++) {
    osrs_game_update(&game, 0.01);
    if (game.state == OSRS_GAME_FAILED) {
      printf("Login failed: [%d] %s\n", game.fail_reason,
             osrs_login_response_str(game.fail_reason));
      osrs_cache_close(cache);
      return 1;
    }
    if (game.state == OSRS_GAME_DISCONNECTED) {
      printf("Disconnected\n");
      osrs_cache_close(cache);
      return 0;
    }
    usleep(10000);
  }

  printf("Stayed connected for 30 seconds!\n");
  osrs_game_disconnect(&game);
  osrs_cache_close(cache);
  return 0;
}

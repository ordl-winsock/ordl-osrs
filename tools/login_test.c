/*
 * tools/login_test.c — Test the OSRS login handshake against a live server.
 *
 * Usage: login_test <cache_path> <host> [user] [pass]
 *
 * With fake credentials, a correct implementation should receive
 * login response 3 (invalid username or password), proving the full
 * RSA + XTEA + framing pipeline works end to end.
 */

#include "osrs/auth.h"
#include "osrs/cache.h"
#include "osrs/game.h"
#include "osrs/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  osrs_log_init();
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <cache> <host> [user] [pass]\n", argv[0]);
    return 1;
  }

  const char *cache_path = argv[1];
  const char *host = argv[2];
  const char *user = argc > 3 ? argv[3] : "fake_user_12345";
  const char *pass = argc > 4 ? argv[4] : "fake_pass_12345";
  const char *auth_name = argc > 5 ? argv[5] : "legacy";

  osrs_auth_t auth;
  memset(&auth, 0, sizeof(auth));
  auth.method = osrs_auth_method_from_string(auth_name);
  snprintf(auth.username, sizeof(auth.username), "%s", user);
  snprintf(auth.secret, sizeof(auth.secret), "%s", pass);

  /* Parse optional host:port (default OSRS_GAME_PORT). */
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

  /* Load cache for index CRCs */
  printf("Loading cache from %s...\n", cache_path);
  osrs_cache_t *cache = osrs_cache_open(cache_path);
  if (!cache) {
    fprintf(stderr, "Failed to open cache\n");
    return 1;
  }
  osrs_cache_load_indexes(cache);
  printf("Loaded %d indexes\n", cache->index_count);

  /* Collect 23 index CRCs */
  uint32_t crcs[23] = {0};
  for (int i = 0; i < 23; i++) {
    osrs_index_t *idx = osrs_cache_get_index(cache, i);
    if (idx)
      crcs[i] = (uint32_t)idx->crc;
  }
  printf("CRC[0..3]: %08x %08x %08x %08x\n", crcs[0], crcs[1], crcs[2],
         crcs[3]);

  /* Connect */
  osrs_game_t game;
  osrs_game_init(&game);

  printf("Connecting to %s:%d as '%s' (auth=%s)...\n", host_buf, (int)port,
         auth.username, osrs_auth_method_str(auth.method));
  if (!osrs_game_connect(&game, host_buf, port, &auth, crcs, 10000)) {
    fprintf(stderr, "FAILED: %s\n", osrs_login_response_str(game.fail_reason));
    return 1;
  }

  /* Run the login state machine for up to 30 seconds */
  for (int i = 0; i < 3000; i++) {
    osrs_game_update(&game, 0.01);
    if (game.state == OSRS_GAME_IN_GAME) {
      printf("SUCCESS: logged in! player_index=%d members=%d pos=%d,%d,%d\n",
             game.proto.player_index, game.proto.members, game.player_x,
             game.player_z, game.player_plane);

      /* Wait for the rebuild footer to seed the position.
       * Live servers (rev 239+) send a 30-bit packed coord in the
       * REBUILD_NORMAL header and may put coord=0 in the GPI init
       * footer; the real position arrives in the first PLAYER_INFO. */
      int sx = -1, sz = -1;
      for (int j = 0; j < 400; j++) {
        osrs_game_update(&game, 0.01);
        if (game.player_x != 0 || game.player_z != 0)
          break;
        usleep(10000);
      }
      sx = game.player_x;
      sz = game.player_z;
      printf("spawn: %d,%d level=%d\n", sx, sz, game.player_plane);

      /* Send a click-to-move far away (forces a server teleport). */
      osrs_game_move_click(&game, sx + 20, sz + 12, true);
      int moved_ok = 0;
      for (int j = 0; j < 400; j++) {
        osrs_game_update(&game, 0.01);
        if (game.player_x != sx || game.player_z != sz) {
          printf("MOVED: %d,%d -> %d,%d (tick %d)\n", sx, sz, game.player_x,
                 game.player_z, j);
          moved_ok = 1;
          break;
        }
        usleep(10000);
      }
      printf(moved_ok ? "MOVEMENT TEST PASS\n" : "MOVEMENT TEST FAIL\n");

      osrs_game_disconnect(&game);
      osrs_cache_close(cache);
      return moved_ok ? 0 : 2;
    }
    if (game.state == OSRS_GAME_FAILED) {
      printf("Login failed: [%d] %s\n", game.fail_reason,
             osrs_login_response_str(game.fail_reason));
      osrs_cache_close(cache);
      return (game.fail_reason == OSRS_LOGINRESP_INVALID_CRED ||
              game.fail_reason == OSRS_LOGINRESP_BAD_SESSION_ID)
                 ? 0
                 : 1;
    }
    usleep(10000);
  }

  fprintf(stderr, "TIMEOUT waiting for login response\n");
  osrs_cache_close(cache);
  return 1;
}

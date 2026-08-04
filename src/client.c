/*
 * src/client.c - OSRS Client Application
 * Pure C23, zero external dependencies.
 *
 * Built on FORGE engine. Features:
 *   - Windowed rendering via FORGE platform
 *   - Software renderer for map display
 *   - Cache loading and region rendering
 *   - Full FORGE logging and telemetry integration
 *   - Debug overlay with cache stats, FPS, entity info
 */

#include "forge/core.h"
#include "forge/log.h"
#include "forge/math.h"
#include "forge/platform.h"
#include "forge/renderer.h"
#include "forge/telemetry.h"
#include "forge/time.h"

#include "osrs/anim.h"
#include "osrs/cache.h"
#include "osrs/config.h"
#include "osrs/game.h"
#include "osrs/jagex.h"
#include "osrs/map.h"
#include "osrs/model.h"
#include "osrs/protocol.h"
#include "osrs/render_utils.h"
#include "osrs/xtea_keys.h"

#include "osrs/gl_world.h"
#include "osrs/iso_renderer.h"
#include "osrs/item_sprite.h"
#include "osrs/log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CLIENT_TITLE "ORDL OSRS Client"
#define CLIENT_WIDTH 1280
#define CLIENT_HEIGHT 720
#define CLIENT_TARGET_FPS 60.0
#define CLIENT_REGION_CACHE_SIZE 32
#define CLIENT_MAX_LOAD_RADIUS 3
#define CLIENT_PAN_LIMIT 40

/* Camera state: the camera centers on a world-tile focus point that tracks
 * the player; the arrow keys orbit (yaw) and tilt (pitch) the view. */
typedef struct {
  float x, y; /* legacy screen-pixel offset (kept at 0 in iso mode) */
  float zoom;
  float yaw;            /* orbit angle around the player (radians) */
  float pitch;          /* camera elevation (radians) */
  int focus_x, focus_z; /* world tile the camera centers on */
  int pan_tx, pan_tz;   /* pan offset from the player, in tiles */
  int last_player_x, last_player_z; /* for pan reset on movement */
  int target_region_x;
  int target_region_y;
} camera_t;

/* Client state */
typedef struct {
  /* FORGE */
  fge_platform_t *platform;
  fge_renderer_t renderer;
  fge_clock_t clock;
  fge_frame_time_t frame_time;

  /* OSRS */
  osrs_cache_t *cache;
  osrs_config_t *config;
  osrs_map_t *map;
  osrs_xtea_store_t *xtea_store;

  /* Online game session */
  osrs_game_t game;
  bool online;
  bool connect_requested;
  double reconnect_timer; /* auto-reconnect delay after a drop */

  /* Region cache - stores multiple loaded regions */
  struct {
    osrs_region_t *region;
    int region_x;
    int region_y;
    bool active;
  } region_cache[CLIENT_REGION_CACHE_SIZE];
  int region_cache_count;

  /* Camera */
  camera_t camera;

  /* GPU world renderer */
  osrs_gl_world_t *gl_world;

  /* Region tracking */
  int loaded_region_x;
  int loaded_region_y;

  /* Model cache */
  struct {
    int model_id;
    osrs_model_t *model;
  } model_cache[64];
  int model_cache_count;

  /* Stats */
  uint64_t frame_count;
  double avg_frame_time;
  int cache_objects_loaded;
  int cache_npcs_loaded;
  int cache_items_loaded;

  /* Object name cache for rendering */
  struct {
    int id;
    char name[64];
    bool loaded;
  } obj_name_cache[256];
  int obj_name_cache_count;

  /* Item sprite cache (item_id -> 36x32 ARGB sprite, 0 = failed) */
  struct {
    int id;
    uint32_t pixels[36 * 32];
    bool valid;
  } item_sprite_cache[128];
  int item_sprite_cache_count;

  /* Rendering mode */
  bool iso_mode;
} client_state_t;

static client_state_t g_client = {0};

/* -------------------------------------------------------------------------- */
/* Framebuffer dump (debug: OSRS_FRAME_DUMP=path_prefix enables, every 60th   */
/* frame writes <prefix>.ppm with the current framebuffer contents)           */
/* -------------------------------------------------------------------------- */

static void client_dump_frame(client_state_t *client) {
  static char dump_prefix[512] = {0};
  static bool dump_checked = false;
  static uint32_t dump_seq = 0;
  if (!dump_checked) {
    const char *env = getenv("OSRS_FRAME_DUMP");
    if (env && env[0])
      snprintf(dump_prefix, sizeof(dump_prefix), "%s", env);
    dump_checked = true;
  }
  if (!dump_prefix[0])
    return;

  const fge_framebuffer_t *fb = &client->renderer.fb;
  if (!fb->pixels)
    return;

  char path[600];
  snprintf(path, sizeof(path), "%s_%04u.ppm", dump_prefix, dump_seq++);
  FILE *f = fopen(path, "wb");
  if (!f)
    return;
  fprintf(f, "P6\n%d %d\n255\n", fb->width, fb->height);
  for (int y = 0; y < fb->height; y++) {
    for (int x = 0; x < fb->width; x++) {
      uint32_t px = fb->pixels[y * fb->stride_pixels + x];
      uint8_t rgb[3] = {(uint8_t)((px >> 16) & 0xFF),
                        (uint8_t)((px >> 8) & 0xFF), (uint8_t)(px & 0xFF)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

/* -------------------------------------------------------------------------- */
/* Cache loading & stats                                                      */
/* -------------------------------------------------------------------------- */

static bool client_load_cache(const char *cache_path) {
  OSRS_INFO(OSRS_LOG_CAT_CACHE, "Loading OSRS cache from: %s", cache_path);
  FGE_INFO(FGE_LOG_CAT_ASSET, "Loading OSRS cache from: %s", cache_path);

  g_client.cache = osrs_cache_open(cache_path);
  if (!g_client.cache) {
    OSRS_ERROR(OSRS_LOG_CAT_CACHE, "Failed to open cache at %s", cache_path);
    FGE_ERROR(FGE_LOG_CAT_ASSET, "Failed to open cache at %s", cache_path);
    return false;
  }

  if (!osrs_cache_load_indexes(g_client.cache)) {
    OSRS_ERROR(OSRS_LOG_CAT_CACHE, "Failed to load cache indexes");
    FGE_ERROR(FGE_LOG_CAT_ASSET, "Failed to load cache indexes");
    return false;
  }

  OSRS_INFO(OSRS_LOG_CAT_CACHE, "Loaded %d cache indexes",
            g_client.cache->index_count);
  FGE_INFO(FGE_LOG_CAT_ASSET, "Loaded %d cache indexes",
           g_client.cache->index_count);

  g_client.config = osrs_config_init(g_client.cache);
  if (!g_client.config) {
    OSRS_ERROR(OSRS_LOG_CAT_CONFIG, "Failed to init config manager");
    FGE_ERROR(FGE_LOG_CAT_ASSET, "Failed to init config manager");
    return false;
  }

  g_client.map = osrs_map_init(g_client.cache);
  if (!g_client.map) {
    OSRS_ERROR(OSRS_LOG_CAT_MAP, "Failed to init map manager");
    FGE_ERROR(FGE_LOG_CAT_ASSET, "Failed to init map manager");
    return false;
  }

  /* Quick cache verification: load some objects */
  OSRS_DEBUG(OSRS_LOG_CAT_CACHE, "Verifying cache by loading sample objects");
  for (int i = 1; i <= 10; i++) {
    osrs_object_def_t *obj = osrs_config_load_object(g_client.config, i);
    if (obj) {
      g_client.cache_objects_loaded++;
      osrs_object_def_free(obj);
    }
  }

  for (int i = 1; i <= 10; i++) {
    osrs_npc_def_t *npc = osrs_config_load_npc(g_client.config, i);
    if (npc) {
      g_client.cache_npcs_loaded++;
      osrs_npc_def_free(npc);
    }
  }

  for (int i = 1; i <= 10; i++) {
    osrs_item_def_t *item = osrs_config_load_item(g_client.config, i);
    if (item) {
      g_client.cache_items_loaded++;
      osrs_item_def_free(item);
    }
  }

  OSRS_INFO(OSRS_LOG_CAT_CACHE,
            "Cache verified: %d objects, %d NPCs, %d items loaded",
            g_client.cache_objects_loaded, g_client.cache_npcs_loaded,
            g_client.cache_items_loaded);
  FGE_INFO(FGE_LOG_CAT_ASSET,
           "Cache verified: %d objects, %d NPCs, %d items loaded",
           g_client.cache_objects_loaded, g_client.cache_npcs_loaded,
           g_client.cache_items_loaded);

  return true;
}

/* -------------------------------------------------------------------------- */
/* Region loading                                                             */
/* -------------------------------------------------------------------------- */

static osrs_region_t *client_find_region(int region_x, int region_y) {
  for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
    if (g_client.region_cache[i].active &&
        g_client.region_cache[i].region_x == region_x &&
        g_client.region_cache[i].region_y == region_y) {
      return g_client.region_cache[i].region;
    }
  }
  return NULL;
}

static void client_unload_region(int region_x, int region_y) {
  for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
    if (g_client.region_cache[i].active &&
        g_client.region_cache[i].region_x == region_x &&
        g_client.region_cache[i].region_y == region_y) {
      osrs_region_free(g_client.region_cache[i].region);
      g_client.region_cache[i].region = NULL;
      g_client.region_cache[i].active = false;
      g_client.region_cache_count--;
      if (g_client.gl_world)
        osrs_gl_world_delete_region(g_client.gl_world, region_x, region_y);
      FGE_INFO(FGE_LOG_CAT_ASSET, "Unloaded region %d,%d", region_x, region_y);
      return;
    }
  }
}

static void client_load_region(int region_x, int region_y) {
  if (client_find_region(region_x, region_y))
    return;

  /* Find a free slot */
  int slot = -1;
  for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
    if (!g_client.region_cache[i].active) {
      slot = i;
      break;
    }
  }

  /* If no free slot, evict the oldest */
  if (slot < 0) {
    slot = 0;
    osrs_region_free(g_client.region_cache[slot].region);
    g_client.region_cache[slot].active = false;
    g_client.region_cache_count--;
  }

  FGE_INFO(FGE_LOG_CAT_ASSET, "Loading region %d,%d", region_x, region_y);
  FGE_SCOPE(FGE_LOG_CAT_ASSET, "region_load");

  const uint32_t *xtea_key = NULL;
  if (g_client.xtea_store) {
    xtea_key = osrs_xtea_store_get_key_for_region(g_client.xtea_store, region_x,
                                                  region_y);
  }

  osrs_region_t *region =
      osrs_map_load_region(g_client.map, region_x, region_y, xtea_key);

  if (region) {
    g_client.region_cache[slot].region = region;
    g_client.region_cache[slot].region_x = region_x;
    g_client.region_cache[slot].region_y = region_y;
    g_client.region_cache[slot].active = true;
    g_client.region_cache_count++;
    osrs_region_compute_colors(region, g_client.config);
    if (g_client.gl_world) {
      osrs_region_t *adj_n = client_find_region(region_x, region_y - 1);
      osrs_region_t *adj_e = client_find_region(region_x + 1, region_y);
      osrs_region_t *adj_s = client_find_region(region_x, region_y + 1);
      osrs_region_t *adj_w = client_find_region(region_x - 1, region_y);
      osrs_gl_world_upload_region_ex(g_client.gl_world, region, adj_n, adj_e,
                                     adj_s, adj_w, region_x, region_y,
                                     g_client.config);
      osrs_gl_world_upload_region_models(g_client.gl_world, region, region_x,
                                         region_y, g_client.config);

      /* Re-upload cached neighbors so their edge corners pick up this
       * region's heights and eliminate seams at region boundaries. */
      osrs_region_t *n = client_find_region(region_x, region_y - 1);
      if (n)
        osrs_gl_world_upload_region_ex(
            g_client.gl_world, n, client_find_region(region_x, region_y - 2),
            client_find_region(region_x + 1, region_y - 1),
            client_find_region(region_x, region_y),
            client_find_region(region_x - 1, region_y - 1), region_x,
            region_y - 1, g_client.config);
      osrs_region_t *e = client_find_region(region_x + 1, region_y);
      if (e)
        osrs_gl_world_upload_region_ex(
            g_client.gl_world, e,
            client_find_region(region_x + 1, region_y - 1),
            client_find_region(region_x + 2, region_y),
            client_find_region(region_x + 1, region_y + 1), region,
            region_x + 1, region_y, g_client.config);
      osrs_region_t *s = client_find_region(region_x, region_y + 1);
      if (s)
        osrs_gl_world_upload_region_ex(
            g_client.gl_world, s, region,
            client_find_region(region_x + 1, region_y + 1),
            client_find_region(region_x, region_y + 2),
            client_find_region(region_x - 1, region_y + 1), region_x,
            region_y + 1, g_client.config);
      osrs_region_t *w = client_find_region(region_x - 1, region_y);
      if (w)
        osrs_gl_world_upload_region_ex(
            g_client.gl_world, w,
            client_find_region(region_x - 1, region_y - 1), region,
            client_find_region(region_x - 1, region_y + 1),
            client_find_region(region_x - 2, region_y), region_x - 1, region_y,
            g_client.config);
    }
    FGE_INFO(FGE_LOG_CAT_ASSET, "Region loaded: %d locations",
             region->location_count);
  } else {
    FGE_WARN(FGE_LOG_CAT_ASSET, "Failed to load region %d,%d", region_x,
             region_y);
  }
}

/* Compute the camera focus tile: tracks the player online (pan resets when
 * the player moves), anchors to the target region offline. */
static void client_update_camera_focus(client_state_t *client) {
  if (client->online && client->game.state == OSRS_GAME_IN_GAME) {
    if (client->game.player_x != client->camera.last_player_x ||
        client->game.player_z != client->camera.last_player_z) {
      client->camera.pan_tx = 0;
      client->camera.pan_tz = 0;
      client->camera.last_player_x = client->game.player_x;
      client->camera.last_player_z = client->game.player_z;
    }
    client->camera.focus_x = client->game.player_x + client->camera.pan_tx;
    client->camera.focus_z = client->game.player_z + client->camera.pan_tz;
  } else {
    client->camera.focus_x = client->camera.target_region_x * OSRS_REGION_SIZE +
                             OSRS_REGION_SIZE / 2 + client->camera.pan_tx;
    client->camera.focus_z = client->camera.target_region_y * OSRS_REGION_SIZE +
                             OSRS_REGION_SIZE / 2 + client->camera.pan_tz;
  }
}

static void client_update_regions(void) {
  /* Center the region grid on the camera focus tile. */
  int center_rx = g_client.camera.focus_x >> 6;
  int center_ry = g_client.camera.focus_z >> 6;

  /* Zoom-aware load radius: load more of the map when zoomed out. */
  int radius = (int)(13.0f / (64.0f * g_client.camera.zoom)) + 1;
  if (radius < 1)
    radius = 1;
  if (radius > CLIENT_MAX_LOAD_RADIUS)
    radius = CLIENT_MAX_LOAD_RADIUS;

  /* Load regions in radius */
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      client_load_region(center_rx + dx, center_ry + dy);
    }
  }

  /* Unload distant regions */
  for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
    if (!g_client.region_cache[i].active)
      continue;
    int rx = g_client.region_cache[i].region_x;
    int ry = g_client.region_cache[i].region_y;
    if (abs(rx - center_rx) > radius + 1 || abs(ry - center_ry) > radius + 1) {
      client_unload_region(rx, ry);
    }
  }

  g_client.loaded_region_x = center_rx;
  g_client.loaded_region_y = center_ry;
}

/* -------------------------------------------------------------------------- */
/* Online game integration                                                    */
/* -------------------------------------------------------------------------- */

/* Called when the server sends REBUILD_NORMAL: base tile coords of scene */
static void client_on_map_load(int base_x, int base_z, void *userdata) {
  (void)userdata;
  int rx = base_x >> 6;
  int ry = base_z >> 6;
  FGE_INFO(FGE_LOG_CAT_ASSET, "Server map base: %d,%d (region %d,%d)", base_x,
           base_z, rx, ry);

  /* Center camera on the scene center */
  g_client.camera.target_region_x = rx;
  g_client.camera.target_region_y = ry;
  g_client.camera.x = 0;
  g_client.camera.y =
      -150; /* Position player ~2/3 down screen like real OSRS */

  /* Load the 3x3 region grid around the scene */
  for (int dy = 0; dy < 3; dy++) {
    for (int dx = 0; dx < 3; dx++) {
      client_load_region(rx + dx - 1, ry + dy - 1);
    }
  }
}

static void client_on_chat(const osrs_chat_msg_t *msg, void *userdata) {
  (void)userdata;
  if (msg->name[0])
    FGE_INFO(FGE_LOG_CAT_GENERAL, "[chat] %s: %s", msg->name, msg->text);
  else
    FGE_INFO(FGE_LOG_CAT_GENERAL, "[chat] %s", msg->text);
}

static void client_on_state_change(osrs_game_state_t state, void *userdata) {
  (void)userdata;
  const char *names[] = {"DISCONNECTED", "CONNECTING", "LOGGING_IN", "IN_GAME",
                         "FAILED"};
  FGE_INFO(FGE_LOG_CAT_GENERAL, "Game state: %s", names[state]);
}

/* Called when server sends XTEA keys in REBUILD_NORMAL/REBUILD_LOGIN */
static void client_on_map_keys(const uint32_t keys[][4], int key_count,
                               void *userdata) {
  (void)userdata;
  if (!g_client.xtea_store)
    g_client.xtea_store = osrs_xtea_store_create();
  if (!g_client.xtea_store)
    return;

  int zone_x = g_client.game.zone_x;
  int zone_z = g_client.game.zone_z;

  int min_mx = (zone_x - 6) >> 3;
  if (min_mx < 0)
    min_mx = 0;
  int max_mx = (zone_x + 6) >> 3;
  if (max_mx > 255)
    max_mx = 255;
  int min_mz = (zone_z - 6) >> 3;
  if (min_mz < 0)
    min_mz = 0;
  int max_mz = (zone_z + 6) >> 3;
  if (max_mz > 255)
    max_mz = 255;

  int idx = 0;
  for (int mx = min_mx; mx <= max_mx && idx < key_count; mx++) {
    for (int mz = min_mz; mz <= max_mz && idx < key_count; mz++) {
      uint32_t mapsquare = (uint32_t)((mx << 8) | (mz & 0xFF));
      osrs_xtea_store_add_key(g_client.xtea_store, mapsquare, keys[idx]);
      idx++;
    }
  }
  FGE_INFO(FGE_LOG_CAT_ASSET, "Received %d XTEA map keys", key_count);
}

/* Initiate an online connection using env credentials.
 * OSRS_WORLD (hostname), OSRS_USER, OSRS_PASS must be set. */
static bool client_connect_online(void) {
  const char *host = getenv("OSRS_WORLD");
  const char *user = getenv("OSRS_USER");
  const char *pass = getenv("OSRS_PASS");
  const char *auth_env = getenv("OSRS_AUTH");
  const char *jagex_token = getenv("OSRS_JAGEX_TOKEN");
  OSRS_INFO(OSRS_LOG_CAT_CLIENT, "Connecting to world: %s as user: %s",
            host ? host : "(not set)", user ? user : "(not set)");
  if (!host || !user) {
    OSRS_WARN(OSRS_LOG_CAT_CLIENT,
              "Online mode needs OSRS_WORLD, OSRS_USER (and OSRS_PASS or "
              "OSRS_JAGEX_TOKEN)");
    FGE_WARN(FGE_LOG_CAT_GENERAL,
             "Online mode needs OSRS_WORLD, OSRS_USER (and OSRS_PASS or "
             "OSRS_JAGEX_TOKEN)");
    return false;
  }

  /* Build credentials from the selected auth method. */
  osrs_auth_t auth;
  memset(&auth, 0, sizeof(auth));
  auth.method = osrs_auth_method_from_string(auth_env);
  snprintf(auth.username, sizeof(auth.username), "%s", user);
  if (auth.method == OSRS_AUTH_JAGEX) {
    OSRS_INFO(OSRS_LOG_CAT_AUTH, "Using Jagex account authentication");
    /* Jagex account: secret is the game session token. Use the supplied
     * token, or fetch one interactively via the OAuth flow. */
    const char *tok = jagex_token ? jagex_token : pass;
    static char fetched_token[128];
    if (!tok) {
      OSRS_INFO(OSRS_LOG_CAT_AUTH,
                "No OSRS_JAGEX_TOKEN; starting interactive Jagex sign-in");
      FGE_INFO(FGE_LOG_CAT_GENERAL,
               "No OSRS_JAGEX_TOKEN; starting interactive Jagex sign-in");
      if (!osrs_jagex_login_interactive(fetched_token, sizeof(fetched_token))) {
        OSRS_WARN(OSRS_LOG_CAT_AUTH, "Interactive Jagex sign-in failed");
        FGE_WARN(FGE_LOG_CAT_GENERAL, "Interactive Jagex sign-in failed");
        return false;
      }
      tok = fetched_token;
      OSRS_INFO(OSRS_LOG_CAT_AUTH, "Got Jagex session token");
      FGE_INFO(FGE_LOG_CAT_GENERAL, "Got Jagex session token");
    }
    snprintf(auth.secret, sizeof(auth.secret), "%s", tok);
  } else {
    OSRS_INFO(OSRS_LOG_CAT_AUTH, "Using legacy password authentication");
    if (!pass) {
      OSRS_WARN(OSRS_LOG_CAT_AUTH, "Legacy auth needs OSRS_PASS");
      FGE_WARN(FGE_LOG_CAT_GENERAL, "Legacy auth needs OSRS_PASS");
      return false;
    }
    snprintf(auth.secret, sizeof(auth.secret), "%s", pass);
  }

  /* Parse optional host:port (default OSRS_GAME_PORT). */
  static char host_buf[256];
  uint16_t port = OSRS_GAME_PORT;
  snprintf(host_buf, sizeof(host_buf), "%s", host);
  char *colon = strrchr(host_buf, ':');
  if (colon) {
    *colon = '\0';
    int p = atoi(colon + 1);
    if (p > 0)
      port = (uint16_t)p;
  }

  /* Collect 23 index CRCs for the login block */
  uint32_t crcs[23] = {0};
  for (int i = 0; i < 23; i++) {
    osrs_index_t *idx = osrs_cache_get_index(g_client.cache, i);
    if (idx)
      crcs[i] = (uint32_t)idx->crc;
  }

  g_client.game.on_state_change = client_on_state_change;
  g_client.game.on_map_load = client_on_map_load;
  g_client.game.on_map_keys = client_on_map_keys;
  g_client.game.on_chat = client_on_chat;

  OSRS_INFO(OSRS_LOG_CAT_NET, "Connecting to %s:%d as %s (auth=%s)...",
            host_buf, (int)port, auth.username,
            osrs_auth_method_str(auth.method));
  FGE_INFO(FGE_LOG_CAT_GENERAL, "Connecting to %s:%d as %s (auth=%s)...",
           host_buf, (int)port, auth.username,
           osrs_auth_method_str(auth.method));
  return osrs_game_connect(&g_client.game, host_buf, port, &auth, crcs, 10000);
}

/* -------------------------------------------------------------------------- */
/* Rendering                                                                  */
/* -------------------------------------------------------------------------- */

/* Forward declarations */
static void world_to_screen(const camera_t *cam, int screen_w, int screen_h,
                            int wx, int wz, float *sx, float *sy);
static const char *client_get_object_name(client_state_t *client, int id);
static void render_bars(fge_renderer_t *r, client_state_t *client);

static void render_region_topdown(osrs_region_t *region, int region_x,
                                  int region_y, fge_renderer_t *r,
                                  camera_t *cam) {
  if (!region)
    return;

  int screen_w = r->fb.width;
  int screen_h = r->fb.height;

  /* Calculate tile size on screen */
  float tile_px = cam->zoom * 8.0f;
  int region_px = (int)(OSRS_REGION_SIZE * tile_px);

  /* Calculate offset based on camera and region position */
  float base_offset_x = (screen_w - region_px) / 2.0f + cam->x;
  float base_offset_y = (screen_h - region_px) / 2.0f + cam->y;

  int rx = region_x - cam->target_region_x;
  int ry = region_y - cam->target_region_y;

  float offset_x = base_offset_x + rx * region_px;
  float offset_y = base_offset_y + ry * region_px;

  /* Cull regions entirely off-screen */
  if (offset_x + region_px < 0 || offset_x >= screen_w ||
      offset_y + region_px < 0 || offset_y >= screen_h)
    return;

  /* Render plane 0 tiles */
  for (int x = 0; x < OSRS_REGION_SIZE; x++) {
    for (int y = 0; y < OSRS_REGION_SIZE; y++) {
      osrs_tile_t *tile = &region->tiles[0][x][y];

      int px = (int)(offset_x + x * tile_px);
      int py = (int)(offset_y + y * tile_px);
      int pw = (int)ceilf(tile_px);
      int ph = (int)ceilf(tile_px);

      if (px + pw < 0 || px >= screen_w || py + ph < 0 || py >= screen_h)
        continue;

      uint32_t color =
          osrs_terrain_color_default(tile->underlay_id, tile->overlay_id);

      /* Darken based on height */
      if (tile->height != 0) {
        int darken = tile->height / 4;
        if (darken > 0)
          color = 0xFF000000 | ((((color >> 16) & 0xFF) - darken) << 16) |
                  ((((color >> 8) & 0xFF) - darken) << 8) |
                  ((color & 0xFF) - darken);
      }

      fge_draw_rect(&r->fb, px, py, pw, ph, color);
    }
  }

  /* Render locations (objects) */
  for (int i = 0; i < region->location_count; i++) {
    osrs_location_t *loc = &region->locations[i];
    if (loc->plane != 0)
      continue;

    int px = (int)(offset_x + loc->local_x * tile_px);
    int py = (int)(offset_y + loc->local_y * tile_px);
    int pw = (int)ceilf(tile_px);

    if (px + pw < 0 || px >= screen_w || py + pw < 0 || py >= screen_h)
      continue;

    /* Draw object as colored dot with name */
    uint32_t obj_color = 0xFFFF4444;
    if (loc->type >= 0 && loc->type <= 3)
      obj_color = 0xFFAAAAAA; /* Walls */
    else if (loc->type >= 4 && loc->type <= 8)
      obj_color = 0xFF4444FF; /* Decorative */
    else if (loc->type >= 9 && loc->type <= 11)
      obj_color = 0xFFFFFF44; /* Diagonal walls */
    else if (loc->type == 22)
      obj_color = 0xFF44FF44; /* Floor decoration */
    else
      obj_color = 0xFFFF4444; /* Other */

    fge_draw_rect(&r->fb, px, py, pw, pw, obj_color);

    /* Show object name if zoomed in enough */
    if (tile_px > 12.0f) {
      const char *name = client_get_object_name(&g_client, loc->object_id);
      if (name) {
        char label[80];
        snprintf(label, sizeof(label), "%s", name);
        fge_draw_text(&r->fb, label, px, py - 10, 0xFFFFFFFF, 0.7f);
      }
    }
  }
}

/* Look up an object name, using a small LRU cache. */
static const char *client_get_object_name(client_state_t *client, int id) {
  if (!client->config)
    return NULL;

  /* Check cache */
  for (int i = 0; i < client->obj_name_cache_count; i++) {
    if (client->obj_name_cache[i].id == id &&
        client->obj_name_cache[i].loaded) {
      OSRS_TRACE(OSRS_LOG_CAT_CACHE, "Object name cache hit: id=%d name=%s", id,
                 client->obj_name_cache[i].name);
      return client->obj_name_cache[i].name;
    }
  }

  /* Load from cache */
  OSRS_TRACE(OSRS_LOG_CAT_CACHE, "Object name cache miss: id=%d", id);
  osrs_object_def_t *def = osrs_config_load_object(client->config, id);
  if (!def)
    return NULL;

  int idx = client->obj_name_cache_count % 256;
  client->obj_name_cache[idx].id = id;
  snprintf(client->obj_name_cache[idx].name,
           sizeof(client->obj_name_cache[idx].name), "%s",
           def->name[0] ? def->name : "???");
  client->obj_name_cache[idx].loaded = true;
  if (client->obj_name_cache_count < 256)
    client->obj_name_cache_count++;

  osrs_object_def_free(def);
  return client->obj_name_cache[idx].name;
}

/* ---- Skeletal animation caches (frames, merged NPC models, sequences) ---- */
#define CLIENT_FRAME_CACHE 128
static struct {
  int packed_id;
  osrs_frame_t *frame;
  osrs_framemap_t *framemap;
} g_frame_cache[CLIENT_FRAME_CACHE];
static int g_frame_cache_count;

#define CLIENT_MERGED_CACHE 64
static struct {
  int npc_def_id;
  osrs_model_t *model;
} g_merged_cache[CLIENT_MERGED_CACHE];
static int g_merged_cache_count;

#define CLIENT_SEQ_CACHE 128
static struct {
  int seq_id;
  osrs_sequence_def_t *def;
} g_seq_cache[CLIENT_SEQ_CACHE];
static int g_seq_cache_count;

static osrs_sequence_def_t *client_get_sequence(client_state_t *client,
                                                int seq_id) {
  for (int i = 0; i < g_seq_cache_count; i++)
    if (g_seq_cache[i].seq_id == seq_id)
      return g_seq_cache[i].def;
  osrs_sequence_def_t *def = osrs_config_load_sequence(client->config, seq_id);
  if (def && g_seq_cache_count < CLIENT_SEQ_CACHE) {
    g_seq_cache[g_seq_cache_count].seq_id = seq_id;
    g_seq_cache[g_seq_cache_count].def = def;
    g_seq_cache_count++;
  }
  return def;
}

static bool client_get_frame(client_state_t *client, int packed_id,
                             osrs_frame_t **out_frame,
                             osrs_framemap_t **out_fm) {
  for (int i = 0; i < g_frame_cache_count; i++) {
    if (g_frame_cache[i].packed_id == packed_id) {
      *out_frame = g_frame_cache[i].frame;
      *out_fm = g_frame_cache[i].framemap;
      return true;
    }
  }
  osrs_framemap_t *fm = NULL;
  osrs_frame_t *fr = osrs_frame_load(client->cache, (packed_id >> 16) & 0xFFFF,
                                     packed_id & 0xFFFF, &fm);
  if (!fr)
    return false;
  if (g_frame_cache_count < CLIENT_FRAME_CACHE) {
    g_frame_cache[g_frame_cache_count].packed_id = packed_id;
    g_frame_cache[g_frame_cache_count].frame = fr;
    g_frame_cache[g_frame_cache_count].framemap = fm;
    g_frame_cache_count++;
  }
  *out_frame = fr;
  *out_fm = fm;
  return true;
}

/* Merge an NPC def's body-part models into one (cached), with recolors. */
static osrs_model_t *client_get_merged_npc_model(client_state_t *client,
                                                 osrs_npc_def_t *def,
                                                 int npc_def_id) {
  for (int i = 0; i < g_merged_cache_count; i++)
    if (g_merged_cache[i].npc_def_id == npc_def_id)
      return g_merged_cache[i].model;
  osrs_model_t *parts[16];
  int np = 0;
  for (int j = 0; j < def->model_count && np < 16; j++) {
    int mid = def->model_ids[j];
    if (mid < 0)
      continue;
    osrs_archive_files_t *mf =
        osrs_cache_read_archive_files(client->cache, 7, mid, NULL);
    if (!mf || mf->count < 1) {
      if (mf)
        osrs_archive_files_free(mf);
      continue;
    }
    parts[np] = osrs_model_load(mid, mf->files[0].data, mf->files[0].len);
    osrs_archive_files_free(mf);
    if (parts[np])
      np++;
  }
  osrs_model_t *merged = np > 0 ? osrs_model_merge(parts, np) : NULL;
  for (int i = 0; i < np; i++)
    osrs_model_free(parts[i]);
  if (merged) {
    for (int i = 0; i < def->recolor_count; i++)
      osrs_model_recolor(merged, def->recolor_src[i], def->recolor_dst[i]);
    osrs_model_compute_normals(merged);
  }
  if (merged && g_merged_cache_count < CLIENT_MERGED_CACHE) {
    g_merged_cache[g_merged_cache_count].npc_def_id = npc_def_id;
    g_merged_cache[g_merged_cache_count].model = merged;
    g_merged_cache_count++;
  }
  return merged;
}

/* Render one NPC with skeletal animation (falls back to static if unavailable).
 */
static void client_render_one_npc_animated(client_state_t *client,
                                           osrs_npc_t *npc, osrs_npc_def_t *def,
                                           float wx, float ground_h, float wz,
                                           float rs, float rc, double dt) {
  int seq_id = def->stand_anim > 0 ? def->stand_anim : def->walk_anim;
  osrs_sequence_def_t *seq =
      seq_id > 0 ? client_get_sequence(client, seq_id) : NULL;
  osrs_model_t *merged = client_get_merged_npc_model(client, def, def->id);
  osrs_frame_t *frame = NULL;
  osrs_framemap_t *fm = NULL;

  if (seq && seq->frame_count > 0 && merged) {
    if (npc->anim_seq_id != seq_id) {
      npc->anim_seq_id = seq_id;
      npc->anim_frame_idx = 0;
      npc->anim_timer = 0;
    }
    npc->anim_timer += dt;
    int delay = seq->frame_delays ? seq->frame_delays[npc->anim_frame_idx] : 1;
    double frame_time = delay > 0 ? delay * 0.02 : 0.02;
    if (npc->anim_timer >= frame_time) {
      npc->anim_timer = 0;
      npc->anim_frame_idx = (npc->anim_frame_idx + 1) % seq->frame_count;
    }
    if (npc->anim_frame_idx >= seq->frame_count)
      npc->anim_frame_idx = 0;
    client_get_frame(client, seq->frame_ids[npc->anim_frame_idx], &frame, &fm);
  }

  if (frame && merged) {
    osrs_model_t *posed = osrs_model_copy(merged);
    if (posed) {
      osrs_model_apply_frame(posed, fm, frame);
      osrs_model_compute_normals(posed);
      osrs_gl_world_draw_model_direct(
          client->gl_world, client->config, posed,
          OSRS_LIGHT_ENTITY_AMBIENT(def->ambient),
          OSRS_LIGHT_ENTITY_CONTRAST(def->contrast), wx, ground_h, wz, rs, rc,
          (float)def->width_scale, (float)def->height_scale,
          (float)def->width_scale);
      osrs_model_free(posed);
      return;
    }
  }

  /* Fallback: static per-part draw. */
  for (int j = 0; j < def->model_count; j++) {
    osrs_gl_world_draw_entity(
        client->gl_world, client->config, def->model_ids[j],
        OSRS_LIGHT_ENTITY_AMBIENT(def->ambient),
        OSRS_LIGHT_ENTITY_CONTRAST(def->contrast), def->recolor_src,
        def->recolor_dst, def->recolor_count, wx, ground_h, wz, rs, rc,
        (float)def->width_scale, (float)def->height_scale,
        (float)def->width_scale);
  }
}

/* Render NPCs as 3D models via the GPU world renderer. */
static void client_render_npc_models(client_state_t *client, double dt) {
  if (!client->gl_world || !client->online ||
      client->game.state != OSRS_GAME_IN_GAME)
    return;

  for (int i = 0; i < client->game.npc_count && i < OSRS_GAME_NPC_MAX; i++) {
    osrs_npc_t *npc = &client->game.npcs[i];
    if (!npc->active || !npc->visible ||
        npc->plane != client->game.player_plane)
      continue;

    osrs_npc_def_t *def = osrs_config_load_npc(client->config, npc->id);
    if (!def || def->model_count <= 0) {
      if (def)
        osrs_npc_def_free(def);
      continue;
    }

    /* NPC yaw from its orientation angle (2048 units/rev) */
    float ang = (float)npc->orientation * (float)(M_PI * 2.0 / 2048.0);
    float rs = sinf(ang);
    float rc = cosf(ang);

    /* Model center at NPC tile center */
    float wx = (float)npc->x + 0.5f;
    float wz = (float)npc->z + 0.5f;

    /* Ground height from the region containing the NPC */
    float ground_h = 0.0f;
    int rx = npc->x >> 6;
    int rz = npc->z >> 6;
    osrs_region_t *region = client_find_region(rx, rz);
    if (region) {
      int lx = npc->x & 63;
      int lz = npc->z & 63;
      ground_h = (float)region->tiles[0][lx][lz].height;
    }

    /* Small height bias prevents z-fighting with terrain. */
    ground_h += 2.0f;

    client_render_one_npc_animated(client, npc, def, wx, ground_h, wz, rs, rc,
                                   dt);

    osrs_npc_def_free(def);
  }
}

/* Count active NPCs (for the debug overlay). */
static int client_count_active_npcs(client_state_t *client) {
  int n = 0;
  for (int i = 0; i < client->game.npc_count && i < OSRS_GAME_NPC_MAX; i++) {
    if (client->game.npcs[i].active)
      n++;
  }
  return n;
}

/* Load a single model from cache index 7 by model ID. */
static osrs_model_t *client_load_model(client_state_t *client, int model_id) {
  osrs_archive_files_t *mf =
      osrs_cache_read_archive_files(client->cache, 7, model_id, NULL);
  if (!mf || mf->count < 1) {
    if (mf)
      osrs_archive_files_free(mf);
    return NULL;
  }
  osrs_model_t *m =
      osrs_model_load(model_id, mf->files[0].data, mf->files[0].len);
  osrs_archive_files_free(mf);
  return m;
}

/* Equipment model offsets (OSRS model coords: Y is negative-up).
 * Values are approximate centroids for each wearpos, based on typical
 * player skeleton.  These hardcode the offsets the OSRS client computes
 * dynamically from vertex groups. */
static const int EQUIP_OFFSET_Y[12] = {
    -55, /* 0 HEAD    */
    -40, /* 1 CAPE    */
    -35, /* 2 AMULET  */
    -25, /* 3 WEAPON  */
    -30, /* 4 BODY    */
    -25, /* 5 SHIELD  */
    -28, /* 6 ARMS    */
    -12, /* 7 LEGS    */
    -55, /* 8 HAIR    */
    -25, /* 9 HANDS   */
    -2,  /* 10 BOOTS  */
    -50  /* 11 JAW    */
};

/* Render one player's composed model (ident kits + worn equipment).
 * Merges all parts into a single model (like the OSRS client does) so
 * equipment centred at (0,0,0) is offset to the correct body part. */
static void client_render_one_player(client_state_t *client,
                                     osrs_player_t *pl) {
  if (!pl->appearance_valid)
    return;

  float wx = (float)pl->x + 0.5f;
  float wz = (float)pl->z + 0.5f;

  /* Ground height */
  float ground_h = 0.0f;
  osrs_region_t *region = client_find_region(pl->x >> 6, pl->z >> 6);
  if (region)
    ground_h = (float)region->tiles[0][pl->x & 63][pl->z & 63].height;
  ground_h += 2.0f;

  float rs = 0.0f, rc = 1.0f; /* facing: TODO from movement */

  /* Transformed NPC: render as that NPC model instead of player model */
  if (pl->transformed_npc >= 0) {
    osrs_npc_def_t *def =
        osrs_config_load_npc(client->config, pl->transformed_npc);
    if (def) {
      osrs_model_t *merged = client_get_merged_npc_model(client, def, def->id);
      if (merged) {
        osrs_gl_world_draw_model_direct(
            client->gl_world, client->config, merged,
            OSRS_LIGHT_ENTITY_AMBIENT(def->ambient),
            OSRS_LIGHT_ENTITY_CONTRAST(def->contrast), wx, ground_h, wz, rs, rc,
            128.0f, 128.0f, 128.0f);
      }
      osrs_npc_def_free(def);
    }
    return;
  }

  bool female = (pl->body_type == 1);

  static const int KIT_REPLACE[12] = {0, -1, -1, -1, 2, -1, 3, 5, -1, 4, 6, 1};
  static const int WEARPOS_KIT_SLOT[12] = {-1, -1, -1, -1, 2, -1,
                                           3,  5,  0,  4,  6, 1};

  bool slot_covered[7] = {false};
  for (int i = 0; i < 12; i++) {
    if (pl->equipment[i] >= 0 && !(pl->equipment[i] & 0x8000) &&
        KIT_REPLACE[i] >= 0)
      slot_covered[KIT_REPLACE[i]] = true;
  }

  /* Collect all models: ident kits + equipment.
   * We'll translate equipment models so their origin aligns with the
   * body part they attach to, then merge everything into one model. */
  osrs_model_t *parts[64];
  int np = 0;

  /* 1. Ident kit models (translate to correct body part).
   * Kit overrides in equipment take precedence over base ident kits. */
  for (int i = 0; i < 12 && np < 64; i++) {
    int kit_id = -1;
    if (pl->equipment[i] >= 0 && (pl->equipment[i] & 0x8000)) {
      kit_id = pl->equipment[i] & 0x7FFF; /* kit override */
    } else if (pl->ident_kits[i] >= 0) {
      kit_id = pl->ident_kits[i]; /* base ident kit */
    }
    if (kit_id < 0)
      continue;
    int kit_slot = WEARPOS_KIT_SLOT[i];
    if (kit_slot >= 0 && slot_covered[kit_slot])
      continue;
    osrs_kit_def_t *kit = osrs_config_load_kit(client->config, kit_id);
    if (!kit)
      continue;
    for (int j = 0; j < kit->model_count && np < 64; j++) {
      int mid = kit->model_ids[j];
      if (mid < 0)
        continue;
      osrs_model_t *m = client_load_model(client, mid);
      if (!m)
        continue;
      /* Apply kit recolors */
      for (int k = 0; k < kit->recolor_count; k++)
        osrs_model_recolor(m, kit->recolor_src[k], kit->recolor_dst[k]);
      /* Ident kit models are centred at origin; translate to body part */
      osrs_model_translate(m, 0, EQUIP_OFFSET_Y[i], 0);
      parts[np++] = m;
    }
    osrs_kit_def_free(kit);
  }

  /* 2. Equipment models (centred at origin; translate to body part) */
  for (int i = 0; i < 12 && np < 64; i++) {
    if (pl->equipment[i] < 0)
      continue;
    if (pl->equipment[i] & 0x8000)
      continue; /* ident-kit override, already rendered above */
    int item_id = pl->equipment[i];
    osrs_item_def_t *idef = osrs_config_load_item(client->config, item_id);
    if (!idef)
      continue;
    int model_ids[3];
    int nmodels = 0;
    int m0 = female ? idef->female_model_id : idef->male_model_id;
    int m1 = female ? idef->female_model1_id : idef->male_model1_id;
    int m2 = female ? idef->female_model2_id : idef->male_model2_id;
    if (m0 >= 0)
      model_ids[nmodels++] = m0;
    if (m1 >= 0)
      model_ids[nmodels++] = m1;
    if (m2 >= 0)
      model_ids[nmodels++] = m2;
    for (int mi = 0; mi < nmodels && np < 64; mi++) {
      osrs_model_t *m = client_load_model(client, model_ids[mi]);
      if (m) {
        osrs_model_translate(m, 0, EQUIP_OFFSET_Y[i], 0);
        parts[np++] = m;
      }
    }
    osrs_item_def_free(idef);
  }

  if (np == 0) {
    FGE_INFO(FGE_LOG_CAT_RENDERER, "player %s: no parts collected", pl->name);
    return;
  }

  /* 3. Merge into one model */
  osrs_model_t *merged = osrs_model_merge(parts, np);
  FGE_INFO(FGE_LOG_CAT_RENDERER,
           "player %s: merged %d parts -> merged=%p vcount=%d fcount=%d",
           pl->name, np, (void *)merged, merged ? merged->vertex_count : 0,
           merged ? merged->face_count : 0);

  /* 3.5 Compute normals for merged model */
  if (merged)
    osrs_model_compute_normals(merged);

  /* 4. Apply body colours as recolors.
   * OSRS body colour indices: 0=hair, 1=torso, 2=legs, 3=skin, 4=feet.
   * Each colour is a packed HSL value that replaces the default skin
   * tone in the corresponding body-part models. */
  if (merged) {
    /* Standard OSRS body-colour palette lookup:
     * the cache stores colour definitions (index 2 archive 33) that
     * map a packed index to an HSL value.  We use a simple approximation
     * here: the packed byte IS the HSL value (lower 8 bits).
     * TODO: use real colour lookup when colour defs are loaded. */
    static const int BODY_PART_SRC[5] = {6798, 8741, 25238, 4626, 6798};
    for (int c = 0; c < 5; c++) {
      if (pl->colours[c] > 0) {
        int dst = pl->colours[c];
        osrs_model_recolor(merged, BODY_PART_SRC[c], dst);
      }
    }

    /* 5. Render merged model as single entity */
    osrs_gl_world_draw_model_direct(
        client->gl_world, client->config, merged, OSRS_LIGHT_ENTITY_AMBIENT(64),
        OSRS_LIGHT_ENTITY_CONTRAST(768), wx, ground_h, wz, rs, rc, 128.0f,
        128.0f, 128.0f);
    osrs_model_free(merged);
  }

  /* Free temporary part models */
  for (int i = 0; i < np; i++)
    osrs_model_free(parts[i]);
}

/* Render all players as 3D models. */
static void client_render_player_models(client_state_t *client) {
  if (!client->gl_world || !client->online ||
      client->game.state != OSRS_GAME_IN_GAME)
    return;

  osrs_playerinfo_t *pi = &client->game.pi;
  /* Local player */
  if (pi->local_index >= 0 && pi->local_index < 2048) {
    osrs_player_t *lp = &pi->players[pi->local_index];
    FGE_INFO(FGE_LOG_CAT_RENDERER,
             "local player: appearance_valid=%d name=%s kits=%d,%d,%d "
             "equip=%d,%d,%d",
             lp->appearance_valid, lp->name[0] ? lp->name : "(none)",
             lp->ident_kits[0], lp->ident_kits[1], lp->ident_kits[2],
             lp->equipment[0], lp->equipment[1], lp->equipment[2]);
    if (lp->appearance_valid) {
      client_render_one_player(client, lp);
    } else {
      /* Fallback: render a default male character so the player isn't
       * invisible when appearance decoding fails (protocol mismatch). */
      osrs_player_t fallback = *lp;
      fallback.appearance_valid = true;
      fallback.body_type = 0;       /* male */
      fallback.ident_kits[0] = -1;  /* helmet */
      fallback.ident_kits[1] = -1;  /* cape */
      fallback.ident_kits[2] = -1;  /* amulet */
      fallback.ident_kits[3] = -1;  /* weapon */
      fallback.ident_kits[4] = 18;  /* torso   -> body-part kit 18 */
      fallback.ident_kits[5] = -1;  /* shield */
      fallback.ident_kits[6] = -1;  /* unused */
      fallback.ident_kits[7] = 42;  /* legs    -> body-part kit 42 */
      fallback.ident_kits[8] = 0;   /* hair    -> body-part kit 0  */
      fallback.ident_kits[9] = 34;  /* hands   -> body-part kit 34 */
      fallback.ident_kits[10] = 50; /* feet    -> body-part kit 50 */
      fallback.ident_kits[11] = 10; /* jaw     -> body-part kit 10 */
      for (int i = 0; i < 12; i++) {
        if (i != 4 && i != 7 && i != 8 && i != 9 && i != 10 && i != 11)
          fallback.ident_kits[i] = -1;
      }
      for (int i = 0; i < 12; i++)
        fallback.equipment[i] = -1;
      /* Default body colours */
      fallback.colours[0] = 0; /* hair */
      fallback.colours[1] = 0; /* torso */
      fallback.colours[2] = 0; /* legs */
      fallback.colours[3] = 0; /* skin */
      fallback.colours[4] = 0; /* feet */
      fallback.transformed_npc = -1;
      client_render_one_player(client, &fallback);
    }
  }
  /* Nearby high-res players */
  for (int i = 0; i < pi->snap_count[1]; i++) {
    int idx = pi->snap_index[1][i];
    if (idx < 0 || idx >= 2048 || idx == pi->local_index)
      continue;
    osrs_player_t *pl = &pi->players[idx];
    if (pl->appearance_valid) {
      client_render_one_player(client, pl);
    } else if (pl->active) {
      /* Fallback so other players don't vanish when appearance fails */
      osrs_player_t fallback = *pl;
      fallback.appearance_valid = true;
      fallback.body_type = 0;
      fallback.ident_kits[0] = -1;
      fallback.ident_kits[4] = 10;
      fallback.ident_kits[7] = 18;
      fallback.ident_kits[8] = 0;
      fallback.ident_kits[9] = 36;
      fallback.ident_kits[10] = 26;
      fallback.ident_kits[11] = 33;
      for (int j = 0; j < 12; j++) {
        if (j != 4 && j != 7 && j != 8 && j != 9 && j != 10 && j != 11)
          fallback.ident_kits[j] = -1;
      }
      for (int j = 0; j < 12; j++)
        fallback.equipment[j] = -1;
      for (int j = 0; j < 5; j++)
        fallback.colours[j] = 0;
      fallback.transformed_npc = -1;
      client_render_one_player(client, &fallback);
    }
  }
}

/* Render ground items as 3D models (item inventory models at positions). */
static void client_render_ground_item_models(client_state_t *client) {
  if (!client->gl_world || !client->online ||
      client->game.state != OSRS_GAME_IN_GAME)
    return;

  for (int i = 0;
       i < client->game.ground_item_count && i < OSRS_GAME_GROUND_ITEM_MAX;
       i++) {
    osrs_ground_item_t *item = &client->game.ground_items[i];
    if (!item->active || item->plane != client->game.player_plane)
      continue;

    osrs_item_def_t *idef = osrs_config_load_item(client->config, item->id);
    if (!idef)
      continue;
    int model_id = idef->inventory_model_id;
    int ambient = OSRS_LIGHT_ENTITY_AMBIENT(idef->ambient);
    int contrast = OSRS_LIGHT_ENTITY_CONTRAST(idef->contrast);
    osrs_item_def_free(idef);
    if (model_id < 0)
      continue;

    float wx = (float)item->x + 0.5f;
    float wz = (float)item->z + 0.5f;

    float ground_h = 0.0f;
    osrs_region_t *region = client_find_region(item->x >> 6, item->z >> 6);
    if (region)
      ground_h = (float)region->tiles[0][item->x & 63][item->z & 63].height;

    /* Small height bias prevents z-fighting with terrain. */
    ground_h += 2.0f;

    osrs_gl_world_draw_entity(client->gl_world, client->config, model_id,
                              ambient, contrast, NULL, NULL, 0, wx, ground_h,
                              wz, 0.0f, 1.0f, 128.0f, 128.0f, 128.0f);
  }
}

/* Render ground items. */
static void render_ground_items(fge_renderer_t *r, client_state_t *client) {
  if (!client->online || client->game.state != OSRS_GAME_IN_GAME)
    return;

  int sw = r->fb.width;
  int sh = r->fb.height;

  if (client->iso_mode) {
    osrs_iso_camera_t iso_cam = {
        .x = client->camera.x,
        .y = client->camera.y,
        .zoom = client->camera.zoom,
        .yaw = client->camera.yaw,
        .pitch = client->camera.pitch,
        .center_tile_x = client->camera.focus_x,
        .center_tile_z = client->camera.focus_z,
    };
    for (int i = 0;
         i < client->game.ground_item_count && i < OSRS_GAME_GROUND_ITEM_MAX;
         i++) {
      osrs_ground_item_t *item = &client->game.ground_items[i];
      if (!item->active || item->plane != client->game.player_plane)
        continue;
      float sx, sy;
      if (!osrs_iso_project(&iso_cam, sw, sh, item->x, item->z, 0, &sx, &sy))
        continue;
      fge_draw_rect(&r->fb, (int)sx - 2, (int)sy - 6, 5, 5, 0xFFFF00FF);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", item->quantity);
      fge_draw_text(&r->fb, buf, (int)sx + 4, (int)sy - 10, 0xFFFF00FF, 0.7f);
    }
    return;
  }

  /* Top-down fallback */
  for (int i = 0;
       i < client->game.ground_item_count && i < OSRS_GAME_GROUND_ITEM_MAX;
       i++) {
    osrs_ground_item_t *item = &client->game.ground_items[i];
    if (!item->active || item->plane != client->game.player_plane)
      continue;

    float sx, sy;
    world_to_screen(&client->camera, sw, sh, item->x, item->z, &sx, &sy);
    if (sx < -16 || sx >= sw + 16 || sy < -16 || sy >= sh + 16)
      continue;

    fge_draw_rect(&r->fb, (int)sx - 2, (int)sy - 2, 5, 5, 0xFFFF00FF);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", item->quantity);
    fge_draw_text(&r->fb, buf, (int)sx + 4, (int)sy - 4, 0xFFFF00FF, 0.7f);
  }
}

/* Convert screen pixel to absolute world tile (top-down). */
static void screen_to_world(const camera_t *cam, int screen_w, int screen_h,
                            float sx, float sy, int *wx, int *wz) {
  float tile_px = cam->zoom * 8.0f;
  float region_px = OSRS_REGION_SIZE * tile_px;
  float base_x = (screen_w - region_px) / 2.0f + cam->x;
  float base_y = (screen_h - region_px) / 2.0f + cam->y;
  *wx =
      cam->target_region_x * OSRS_REGION_SIZE + (int)((sx - base_x) / tile_px);
  *wz =
      cam->target_region_y * OSRS_REGION_SIZE + (int)((sy - base_y) / tile_px);
}

/* Find the nearest object to a world tile. Returns distance squared. */
static int find_nearest_object(client_state_t *client, int wx, int wz,
                               osrs_location_t **out_loc) {
  int best_dist = 999999;
  osrs_location_t *best = NULL;

  for (int ri = 0; ri < CLIENT_REGION_CACHE_SIZE; ri++) {
    if (!client->region_cache[ri].active || !client->region_cache[ri].region)
      continue;
    osrs_region_t *region = client->region_cache[ri].region;
    int rbase_x = region->region_x * OSRS_REGION_SIZE;
    int rbase_y = region->region_y * OSRS_REGION_SIZE;

    for (int i = 0; i < region->location_count; i++) {
      osrs_location_t *loc = &region->locations[i];
      if (loc->plane != 0)
        continue;
      int lx = rbase_x + loc->local_x;
      int ly = rbase_y + loc->local_y;
      int dx = lx - wx;
      int dy = ly - wz;
      int dist = dx * dx + dy * dy;
      if (dist < best_dist) {
        best_dist = dist;
        best = loc;
      }
    }
  }

  *out_loc = best;
  return best_dist;
}

/* Find the nearest NPC to a world tile. Returns distance squared. */
static int find_nearest_npc(client_state_t *client, int wx, int wz,
                            osrs_npc_t **out_npc) {
  int best_dist = 999999;
  osrs_npc_t *best = NULL;

  for (int i = 0; i < client->game.npc_count && i < OSRS_GAME_NPC_MAX; i++) {
    osrs_npc_t *npc = &client->game.npcs[i];
    if (!npc->active || !npc->visible ||
        npc->plane != client->game.player_plane)
      continue;
    int dx = npc->x - wx;
    int dy = npc->z - wz;
    int dist = dx * dx + dy * dy;
    if (dist < best_dist) {
      best_dist = dist;
      best = npc;
    }
  }

  *out_npc = best;
  return best_dist;
}

/* Send an object interaction packet (OPLOC1). */
static void send_oploc1(client_state_t *client, int object_id, int x, int z) {
  osrs_game_interact_object(&client->game, object_id, x, z);
}

/* Send an NPC interaction packet (OPNPC1). */
static void send_opnpc1(client_state_t *client, int npc_index) {
  osrs_game_interact_npc(&client->game, npc_index);
}

/* Find the nearest player to a world tile. Returns distance squared. */
static int find_nearest_player(client_state_t *client, int wx, int wz,
                               osrs_player_t **out_player) {
  int best_dist = 999999;
  osrs_player_t *best = NULL;

  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    osrs_player_t *p = &client->game.pi.players[i];
    if (!p->active || !p->high_res || i == client->game.pi.local_index)
      continue;
    int dx = p->x - wx;
    int dz = p->z - wz;
    int dist = dx * dx + dz * dz;
    if (dist < best_dist) {
      best_dist = dist;
      best = p;
    }
  }

  *out_player = best;
  return best_dist;
}

/* Send a player interaction packet (OPPLAYER1). */
static void send_opplayer1(client_state_t *client, int player_index) {
  osrs_game_interact_player(&client->game, player_index);
}

/* Convert absolute world tile to screen pixel (top-down). */
static void world_to_screen(const camera_t *cam, int screen_w, int screen_h,
                            int wx, int wz, float *sx, float *sy) {
  float tile_px = cam->zoom * 8.0f;
  float region_px = OSRS_REGION_SIZE * tile_px;
  float base_x = (screen_w - region_px) / 2.0f + cam->x;
  float base_y = (screen_h - region_px) / 2.0f + cam->y;
  *sx = base_x + (wx - cam->target_region_x * OSRS_REGION_SIZE) * tile_px;
  *sy = base_y + (wz - cam->target_region_y * OSRS_REGION_SIZE) * tile_px;
}

/* Render NPCs on the map. */
static void render_npcs(fge_renderer_t *r, client_state_t *client) {
  if (!client->online || client->game.state != OSRS_GAME_IN_GAME)
    return;

  int sw = r->fb.width;
  int sh = r->fb.height;

  if (client->iso_mode) {
    osrs_iso_camera_t iso_cam = {
        .x = client->camera.x,
        .y = client->camera.y,
        .zoom = client->camera.zoom,
        .yaw = client->camera.yaw,
        .pitch = client->camera.pitch,
        .center_tile_x = client->camera.focus_x,
        .center_tile_z = client->camera.focus_z,
    };
    for (int i = 0; i < client->game.npc_count && i < OSRS_GAME_NPC_MAX; i++) {
      osrs_npc_t *npc = &client->game.npcs[i];
      if (!npc->active || !npc->visible ||
          npc->plane != client->game.player_plane)
        continue;
      osrs_iso_render_marker(&iso_cam, r, sw, sh, npc->x, npc->z, 0, 6,
                             0xFFFFFF00);
      if (npc->name[0]) {
        float sx, sy;
        osrs_iso_project(&iso_cam, sw, sh, npc->x, npc->z, 0, &sx, &sy);
        fge_draw_text(&r->fb, npc->name, (int)sx + 8, (int)sy - 14, 0xFFFFFF00,
                      0.8f);
      }
    }
    return;
  }

  /* Top-down fallback */
  float tile_px = client->camera.zoom * 8.0f;
  int marker = (int)(tile_px * 0.7f);
  if (marker < 4)
    marker = 4;

  for (int i = 0; i < client->game.npc_count && i < OSRS_GAME_NPC_MAX; i++) {
    osrs_npc_t *npc = &client->game.npcs[i];
    if (!npc->active || !npc->visible)
      continue;
    if (npc->plane != client->game.player_plane)
      continue;

    float sx, sy;
    world_to_screen(&client->camera, sw, sh, npc->x, npc->z, &sx, &sy);
    if (sx < -16 || sx >= sw + 16 || sy < -16 || sy >= sh + 16)
      continue;

    fge_draw_rect(&r->fb, (int)sx - marker / 2 - 1, (int)sy - marker / 2 - 1,
                  marker + 2, marker + 2, 0xFFFF0000);
    fge_draw_rect(&r->fb, (int)sx - marker / 2, (int)sy - marker / 2, marker,
                  marker, 0xFFFFFF00);

    if (npc->name[0]) {
      char label[80];
      snprintf(label, sizeof(label), "%s", npc->name);
      fge_draw_text(&r->fb, label, (int)sx + marker, (int)sy - 6, 0xFFFFFF00,
                    0.9f);
    }
  }
}

/* Render a minimap in the top-right corner. */
static void render_minimap(fge_renderer_t *r, client_state_t *client) {
  if (!client->online || client->game.state != OSRS_GAME_IN_GAME)
    return;

  int mm_size = 160;
  int mm_x = r->fb.width - mm_size - 8;
  int mm_y = 58;
  uint32_t bg = 0xCC1A2E1A;
  uint32_t border = 0xFFAAAAAA;

  /* Background */
  fge_draw_rect(&r->fb, mm_x, mm_y, mm_size, mm_size, bg);
  fge_draw_rect(&r->fb, mm_x, mm_y, mm_size, 1, border);
  fge_draw_rect(&r->fb, mm_x, mm_y + mm_size - 1, mm_size, 1, border);
  fge_draw_rect(&r->fb, mm_x, mm_y, 1, mm_size, border);
  fge_draw_rect(&r->fb, mm_x + mm_size - 1, mm_y, 1, mm_size, border);

  /* Draw visible regions as small tiles */
  float tile_px = mm_size / (float)(OSRS_REGION_SIZE * 3);
  for (int ri = 0; ri < CLIENT_REGION_CACHE_SIZE; ri++) {
    if (!client->region_cache[ri].active || !client->region_cache[ri].region)
      continue;
    osrs_region_t *region = client->region_cache[ri].region;
    int rbx =
        (region->region_x - client->camera.target_region_x) * OSRS_REGION_SIZE;
    int rby =
        (region->region_y - client->camera.target_region_y) * OSRS_REGION_SIZE;

    for (int tx = 0; tx < OSRS_REGION_SIZE; tx++) {
      for (int ty = 0; ty < OSRS_REGION_SIZE; ty++) {
        osrs_tile_t *tile = &region->tiles[0][tx][ty];
        int px = mm_x + (int)((rbx + tx + OSRS_REGION_SIZE * 1.5f) * tile_px);
        int py = mm_y + mm_size -
                 (int)((rby + ty + OSRS_REGION_SIZE * 1.5f) * tile_px);
        if (px < mm_x + 1 || px >= mm_x + mm_size - 1 || py < mm_y + 1 ||
            py >= mm_y + mm_size - 1)
          continue;

        uint32_t c =
            osrs_terrain_color_default(tile->underlay_id, tile->overlay_id);
        /* Dim non-walkable tiles slightly */
        if (tile->settings & 0x1)
          c = ((c & 0xFEFEFEFE) >> 1) | 0xFF000000;
        fge_fb_pixel(&r->fb, px, py, c);
      }
    }
  }

  /* Player dot (green) at center */
  int cx = mm_x + mm_size / 2;
  int cy = mm_y + mm_size / 2;
  fge_draw_rect(&r->fb, cx - 2, cy - 2, 5, 5, 0xFF00FF00);

  /* Other players (white dots) */
  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    osrs_player_t *p = &client->game.pi.players[i];
    if (!p->active || !p->high_res || i == client->game.pi.local_index)
      continue;
    int dx = p->x - client->game.player_x;
    int dz = p->z - client->game.player_z;
    int dpx = (int)(dx * tile_px);
    int dpy = -(int)(dz * tile_px);
    int px = cx + dpx;
    int py = cy + dpy;
    if (px >= mm_x + 2 && px < mm_x + mm_size - 2 && py >= mm_y + 2 &&
        py < mm_y + mm_size - 2) {
      fge_fb_pixel(&r->fb, px, py, 0xFFFFFFFF);
    }
  }

  /* NPC dots (yellow) */
  for (int i = 0; i < client->game.npc_count; i++) {
    osrs_npc_t *npc = &client->game.npcs[i];
    if (!npc->active || npc->plane != client->game.player_plane)
      continue;
    int dx = npc->x - client->game.player_x;
    int dz = npc->z - client->game.player_z;
    int dpx = (int)(dx * tile_px);
    int dpy = -(int)(dz * tile_px);
    int px = cx + dpx;
    int py = cy + dpy;
    if (px >= mm_x + 2 && px < mm_x + mm_size - 2 && py >= mm_y + 2 &&
        py < mm_y + mm_size - 2) {
      fge_fb_pixel(&r->fb, px, py, 0xFFFFFF00);
    }
  }

  /* Compass label */
  fge_draw_text(&r->fb, "N", mm_x + mm_size / 2 - 4, mm_y + 2, 0xFFFF0000,
                0.8f);
}

/* Get (or lazily render) an item's inventory sprite. Returns ARGB pixels or
 * NULL if the item can't be rendered. */
static const uint32_t *client_get_item_sprite(client_state_t *client, int id) {
  /* Linear search cache */
  for (int i = 0; i < client->item_sprite_cache_count; i++) {
    if (client->item_sprite_cache[i].id == id)
      return client->item_sprite_cache[i].valid
                 ? client->item_sprite_cache[i].pixels
                 : NULL;
  }
  /* Render and cache */
  if (client->item_sprite_cache_count >= 128) {
    /* Evict oldest (slot 0) */
    for (int i = 1; i < 128; i++)
      client->item_sprite_cache[i - 1] = client->item_sprite_cache[i];
    client->item_sprite_cache_count = 127;
  }
  int slot = client->item_sprite_cache_count++;
  client->item_sprite_cache[slot].id = id;
  client->item_sprite_cache[slot].valid = osrs_item_sprite_render(
      client->config, id, client->item_sprite_cache[slot].pixels, 36, 32);
  return client->item_sprite_cache[slot].valid
             ? client->item_sprite_cache[slot].pixels
             : NULL;
}

/* Render inventory panel when in game. */
static void render_inventory(fge_renderer_t *r, client_state_t *client) {
  if (!client->online || client->game.state != OSRS_GAME_IN_GAME)
    return;
  if (client->game.inv_count == 0)
    return;

  int inv_w = 200;
  int inv_h = 140;
  int ix = r->fb.width - inv_w - 8;
  int iy = r->fb.height - inv_h - 8;
  uint32_t bg = 0xCC222222;
  uint32_t border = 0xFF888888;

  fge_draw_rect(&r->fb, ix, iy, inv_w, inv_h, bg);
  fge_draw_rect(&r->fb, ix, iy, inv_w, 1, border);
  fge_draw_rect(&r->fb, ix, iy + inv_h - 1, inv_w, 1, border);
  fge_draw_rect(&r->fb, ix, iy, 1, inv_h, border);
  fge_draw_rect(&r->fb, ix + inv_w - 1, iy, 1, inv_h, border);

  fge_draw_text(&r->fb, "Inventory", ix + 8, iy + 4, 0xFFFFFFFF, 1.0f);

  int slot = 0;
  int cols = 4;
  int slot_size = 40;
  int start_y = iy + 24;
  for (int i = 0; i < client->game.inv_count && slot < 28; i++) {
    osrs_inv_item_t *item = &client->game.inventory[i];
    if (item->id <= 0)
      continue;
    int col = slot % cols;
    int row = slot / cols;
    int sx = ix + 8 + col * slot_size;
    int sy = start_y + row * slot_size;
    if (sy + slot_size > iy + inv_h)
      break;

    /* Slot background */
    fge_draw_rect(&r->fb, sx, sy, slot_size - 2, slot_size - 2, 0xFF444444);

    /* Item sprite (36x32 centered in the 40x40 slot) */
    const uint32_t *sprite = client_get_item_sprite(client, item->id);
    if (sprite) {
      for (int py = 0; py < 32; py++) {
        for (int px = 0; px < 36; px++) {
          uint32_t c = sprite[py * 36 + px];
          if ((c >> 24) != 0) /* alpha */ {
            int dx = sx + 2 + px;
            int dy = sy + 2 + py;
            if (dx < r->fb.width && dy < r->fb.height)
              r->fb.pixels[dy * r->fb.stride_pixels + dx] = c;
          }
        }
      }
    } else {
      /* Fallback: item name text */
      osrs_item_def_t *idef = osrs_config_load_item(client->config, item->id);
      if (idef) {
        fge_draw_text(&r->fb, idef->name, sx + 2, sy + 2, 0xFFFFFF00, 0.55f);
        osrs_item_def_free(idef);
      }
    }

    if (item->quantity > 1) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d", item->quantity);
      fge_draw_text(&r->fb, buf, sx + 3, sy + 2, 0xFFFFFD00, 0.7f);
    }
    slot++;
  }
}

/* Render HP and Prayer bars (top-left, below debug overlay). */
static void render_bars(fge_renderer_t *r, client_state_t *client) {
  if (!client->online || client->game.state != OSRS_GAME_IN_GAME)
    return;

  int x = 10;
  int y = 145; /* Below debug overlay */
  int bar_w = 120;
  int bar_h = 14;
  uint32_t bg = 0xAA000000;

  /* HP bar (skill 3) */
  int hp = client->game.levels[3];
  int hp_max = client->game.real_levels[3];
  if (hp_max < 1)
    hp_max = 10;
  if (hp < 0)
    hp = 0;
  int hp_w = (int)((float)hp / hp_max * bar_w);
  uint32_t hp_color = hp > hp_max / 4 ? 0xFF44FF44 : 0xFFFF4444;

  fge_draw_rect(&r->fb, x, y, bar_w, bar_h, bg);
  fge_draw_rect(&r->fb, x, y, hp_w, bar_h, hp_color);
  char buf[64];
  snprintf(buf, sizeof(buf), "HP: %d/%d", hp, hp_max);
  fge_draw_text(&r->fb, buf, x + 4, y + 2, 0xFFFFFFFF, 0.8f);
  y += bar_h + 4;

  /* Prayer bar (skill 5) */
  int pray = client->game.levels[5];
  int pray_max = client->game.real_levels[5];
  if (pray_max < 1)
    pray_max = 1;
  if (pray < 0)
    pray = 0;
  int pray_w = (int)((float)pray / pray_max * bar_w);

  fge_draw_rect(&r->fb, x, y, bar_w, bar_h, bg);
  fge_draw_rect(&r->fb, x, y, pray_w, bar_h, 0xFF4488FF);
  snprintf(buf, sizeof(buf), "Pray: %d/%d", pray, pray_max);
  fge_draw_text(&r->fb, buf, x + 4, y + 2, 0xFFFFFFFF, 0.8f);
  y += bar_h + 6;

  /* Combat level */
  osrs_player_t *local = &client->game.pi.players[client->game.pi.local_index];
  if (local->appearance_valid && local->combat_level > 0) {
    snprintf(buf, sizeof(buf), "CB: %d", local->combat_level);
    fge_draw_text(&r->fb, buf, x, y, 0xFFFFFF00, 0.9f);
  }
}

/* Render the local player and nearby high-res players on the map. */
static void render_players(fge_renderer_t *r, client_state_t *client) {
  if (!client->online || client->game.state != OSRS_GAME_IN_GAME)
    return;

  int sw = r->fb.width;
  int sh = r->fb.height;

  if (client->iso_mode) {
    osrs_iso_camera_t iso_cam = {
        .x = client->camera.x,
        .y = client->camera.y,
        .zoom = client->camera.zoom,
        .yaw = client->camera.yaw,
        .pitch = client->camera.pitch,
        .center_tile_x = client->camera.focus_x,
        .center_tile_z = client->camera.focus_z,
    };
    /* Other players (white) */
    for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
      osrs_player_t *p = &client->game.pi.players[i];
      if (!p->active || !p->high_res || i == client->game.pi.local_index)
        continue;
      osrs_iso_render_marker(&iso_cam, r, sw, sh, p->x, p->z, 0, 6, 0xFFFFFFFF);
      if (p->appearance_valid && p->name[0]) {
        float sx, sy;
        osrs_iso_project(&iso_cam, sw, sh, p->x, p->z, 0, &sx, &sy);
        fge_draw_text(&r->fb, p->name, (int)sx + 8, (int)sy - 14, 0xFFFFFF00,
                      0.8f);
      }
    }
    /* Local player (green) */
    if (client->game.pi.local_pos_valid) {
      osrs_iso_render_marker(&iso_cam, r, sw, sh, client->game.player_x,
                             client->game.player_z, 0, 7, 0xFF00FF00);
      osrs_player_t *local =
          &client->game.pi.players[client->game.pi.local_index];
      if (local->appearance_valid && local->name[0]) {
        float sx, sy;
        osrs_iso_project(&iso_cam, sw, sh, client->game.player_x,
                         client->game.player_z, 0, &sx, &sy);
        fge_draw_text(&r->fb, local->name, (int)sx + 8, (int)sy - 18,
                      0xFF00FF00, 0.85f);
      }
    }
    return;
  }

  /* Top-down fallback */
  float tile_px = client->camera.zoom * 8.0f;
  int marker = (int)(tile_px * 0.8f);
  if (marker < 3)
    marker = 3;

  /* Other high-res players (white) with names. */
  for (int i = 1; i < OSRS_PI_CAPACITY; i++) {
    osrs_player_t *p = &client->game.pi.players[i];
    if (!p->active || !p->high_res || i == client->game.pi.local_index)
      continue;
    float sx, sy;
    world_to_screen(&client->camera, sw, sh, p->x, p->z, &sx, &sy);
    if (sx < -16 || sx >= sw + 16 || sy < -16 || sy >= sh + 16)
      continue;
    fge_draw_rect(&r->fb, (int)sx - marker / 2, (int)sy - marker / 2, marker,
                  marker, 0xFFFFFFFF);
    if (p->appearance_valid && p->name[0]) {
      fge_draw_text(&r->fb, p->name, (int)sx + marker, (int)sy - 6, 0xFFFFFF00,
                    1.0f);
    }
  }

  /* Local player (green), drawn on top. */
  if (client->game.pi.local_pos_valid) {
    float sx, sy;
    world_to_screen(&client->camera, sw, sh, client->game.player_x,
                    client->game.player_z, &sx, &sy);
    fge_draw_rect(&r->fb, (int)sx - marker / 2, (int)sy - marker / 2, marker,
                  marker, 0xFF00FF00);
    fge_draw_rect(&r->fb, (int)sx - marker / 2 - 1, (int)sy - marker / 2 - 1,
                  marker + 2, 1, 0xFF00FF00);
  }
}

static void render_debug_overlay(fge_renderer_t *r, client_state_t *client) {
  char buf[256];
  int y = 10;
  uint32_t text_color = 0xFFFFFFFF;
  uint32_t bg_color = 0xAA000000;

  /* Count total locations */
  int total_locs = 0;
  for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
    if (client->region_cache[i].active && client->region_cache[i].region)
      total_locs += client->region_cache[i].region->location_count;
  }

  /* Background panel */
  fge_draw_rect(&r->fb, 5, 5, 340, 150, bg_color);

  snprintf(buf, sizeof(buf), "ORDL OSRS Client | FPS: %.1f | Frame: %llu",
           client->frame_time.fps, (unsigned long long)client->frame_count);
  fge_draw_text(&r->fb, buf, 10, y, text_color, 1.0f);
  y += 18;

  snprintf(buf, sizeof(buf), "Regions: %d loaded | Locations: %d",
           client->region_cache_count, total_locs);
  fge_draw_text(&r->fb, buf, 10, y, text_color, 1.0f);
  y += 18;

  snprintf(buf, sizeof(buf), "Cache: %d indexes | Obj:%d NPC:%d Item:%d",
           client->cache->index_count, client->cache_objects_loaded,
           client->cache_npcs_loaded, client->cache_items_loaded);
  fge_draw_text(&r->fb, buf, 10, y, text_color, 1.0f);
  y += 18;

  snprintf(buf, sizeof(buf), "Focus: %d,%d | Zoom: %.1fx | %s",
           client->camera.focus_x, client->camera.focus_z, client->camera.zoom,
           client->iso_mode ? "ISO" : "TOP");
  fge_draw_text(&r->fb, buf, 10, y, text_color, 1.0f);
  y += 18;

  if (client->online && client->game.state == OSRS_GAME_IN_GAME) {
    snprintf(buf, sizeof(buf), "Player: %d,%d,%d  NPCs: %d",
             client->game.player_x, client->game.player_z,
             client->game.player_plane, client_count_active_npcs(client));
    fge_draw_text(&r->fb, buf, 10, y, text_color, 1.0f);
    y += 18;
  }

  snprintf(buf, sizeof(buf),
           "Controls: Arrows=orbit | 1/2=zoom | F1=mode | ESC=quit");
  fge_draw_text(&r->fb, buf, 10, y, text_color, 1.0f);
}

/* Render connection state + chat box */
static void render_game_overlay(fge_renderer_t *r, client_state_t *client) {
  if (!client->online && client->game.state == OSRS_GAME_DISCONNECTED)
    return;

  char buf[300];
  uint32_t text_color = 0xFFFFFFFF;
  uint32_t ok_color = 0xFF44FF44;
  uint32_t err_color = 0xFFFF4444;
  uint32_t bg_color = 0xAA000000;

  /* Connection status, top-right */
  const char *state_str = "Offline";
  uint32_t state_color = text_color;
  switch (client->game.state) {
  case OSRS_GAME_CONNECTING:
    state_str = "Connecting...";
    break;
  case OSRS_GAME_LOGGING_IN:
    state_str = "Logging in...";
    break;
  case OSRS_GAME_IN_GAME:
    state_str = "In Game";
    state_color = ok_color;
    break;
  case OSRS_GAME_FAILED:
    state_str = "Failed";
    state_color = err_color;
    break;
  default:
    break;
  }

  int panel_w = 300;
  int px = r->fb.width - panel_w - 5;
  int panel_h = client->game.interface_open ? 62 : 44;
  fge_draw_rect(&r->fb, px, 5, panel_w, panel_h, bg_color);
  snprintf(buf, sizeof(buf), "State: %s", state_str);
  fge_draw_text(&r->fb, buf, px + 8, 12, state_color, 1.0f);
  if (client->game.state == OSRS_GAME_FAILED) {
    snprintf(buf, sizeof(buf), "%s",
             osrs_login_response_str(client->game.fail_reason));
    fge_draw_text(&r->fb, buf, px + 8, 30, err_color, 1.0f);
  } else if (client->game.state == OSRS_GAME_IN_GAME) {
    snprintf(buf, sizeof(buf), "pos %d,%d | energy %d", client->game.player_x,
             client->game.player_z, client->game.run_energy);
    fge_draw_text(&r->fb, buf, px + 8, 30, text_color, 1.0f);
    if (client->game.interface_open) {
      snprintf(buf, sizeof(buf), "Interface: %d", client->game.open_interface);
      fge_draw_text(&r->fb, buf, px + 8, 48, 0xFFFFFF00, 0.9f);
    }
  }

  /* Chat box, bottom-left */
  int chat_h = 150;
  int cy = r->fb.height - chat_h - 5;
  fge_draw_rect(&r->fb, 5, cy, 560, chat_h, bg_color);
  int yy = cy + chat_h - 16;
  int shown = 0;
  for (int i = client->game.chat_count - 1; i >= 0 && shown < 8; i--) {
    int idx = (client->game.chat_head - 1 - i + OSRS_GAME_CHAT_MAX * 2) %
              OSRS_GAME_CHAT_MAX;
    osrs_chat_msg_t *m = &client->game.chat[idx];
    if (m->name[0])
      snprintf(buf, sizeof(buf), "%s: %s", m->name, m->text);
    else
      snprintf(buf, sizeof(buf), "%s", m->text);
    fge_draw_text(&r->fb, buf, 12, yy, 0xFFFFFF00, 1.0f);
    yy -= 17;
    shown++;
  }
}

/* -------------------------------------------------------------------------- */
/* Input handling                                                             */
/* -------------------------------------------------------------------------- */

static void client_handle_input(client_state_t *client, float dt) {
  fge_input_state_t *in = &client->platform->input;

  /* Arrow keys orbit the camera around the player (OSRS-style).
   * LEFT/RIGHT rotate yaw; UP/DOWN tilt pitch. Held keys rotate smoothly. */
  float yaw_speed = 2.2f * dt;   /* radians per second */
  float pitch_speed = 1.6f * dt; /* radians per second */
  if (fge_input_key_down(in, FGE_KEY_LEFT))
    client->camera.yaw -= yaw_speed;
  if (fge_input_key_down(in, FGE_KEY_RIGHT))
    client->camera.yaw += yaw_speed;
  if (fge_input_key_down(in, FGE_KEY_UP))
    client->camera.pitch += pitch_speed;
  if (fge_input_key_down(in, FGE_KEY_DOWN))
    client->camera.pitch -= pitch_speed;

  /* Clamp pitch to the classic OSRS arc: enough to tilt up/down around the
   * default elevation, but not so low that terrain occludes the scene or the
   * void dominates (which reads as "black renderings"). */
  if (client->camera.pitch < 0.45f)
    client->camera.pitch = 0.45f;
  if (client->camera.pitch > 1.20f)
    client->camera.pitch = 1.20f;

  /* Wrap yaw to [0, 2PI). */
  const float two_pi = 6.2831853f;
  while (client->camera.yaw < 0.0f)
    client->camera.yaw += two_pi;
  while (client->camera.yaw >= two_pi)
    client->camera.yaw -= two_pi;

  if (fge_input_key_pressed(in, FGE_KEY_1)) {
    client->camera.zoom *= 1.2f;
    if (client->camera.zoom > 4.0f)
      client->camera.zoom = 4.0f;
  }
  if (fge_input_key_pressed(in, FGE_KEY_2)) {
    client->camera.zoom /= 1.2f;
    if (client->camera.zoom < 0.125f)
      client->camera.zoom = 0.125f;
  }

  /* Toggle isometric / top-down */
  if (fge_input_key_pressed(in, FGE_KEY_F1)) {
    client->iso_mode = !client->iso_mode;
    OSRS_INFO(OSRS_LOG_CAT_CLIENT, "Switched to %s mode",
              client->iso_mode ? "isometric" : "top-down");
  }

  /* Click-to-move when in game: convert screen pixel -> absolute world tile */
  if (client->online && client->game.state == OSRS_GAME_IN_GAME &&
      fge_input_mouse_pressed(in, FGE_MOUSE_LEFT)) {
    int wx, wz;
    if (client->iso_mode) {
      osrs_iso_camera_t iso_cam = {
          .x = client->camera.x,
          .y = client->camera.y,
          .zoom = client->camera.zoom,
          .yaw = client->camera.yaw,
          .pitch = client->camera.pitch,
          .center_tile_x = client->camera.focus_x,
          .center_tile_z = client->camera.focus_z,
      };
      osrs_iso_screen_to_world(&iso_cam, CLIENT_WIDTH, CLIENT_HEIGHT,
                               in->mouse_pos.x, in->mouse_pos.y, &wx, &wz);
    } else {
      float tile_px = client->camera.zoom * 8.0f;
      float region_px = OSRS_REGION_SIZE * tile_px;
      float base_x = (CLIENT_WIDTH - region_px) / 2.0f + client->camera.x;
      float base_y = (CLIENT_HEIGHT - region_px) / 2.0f + client->camera.y;
      wx = client->camera.target_region_x * OSRS_REGION_SIZE +
           (int)((in->mouse_pos.x - base_x) / tile_px);
      wz = client->camera.target_region_y * OSRS_REGION_SIZE +
           (int)((in->mouse_pos.y - base_y) / tile_px);
    }
    if (wx >= 0 && wz >= 0) {
      OSRS_DEBUG(OSRS_LOG_CAT_CLIENT, "Click-to-move: %d,%d", wx, wz);
      osrs_game_move_click(&client->game, wx, wz, true);
    }
  }

  /* Right-click interaction: find nearest object or NPC and interact */
  if (client->online && client->game.state == OSRS_GAME_IN_GAME &&
      fge_input_mouse_pressed(in, FGE_MOUSE_RIGHT)) {
    int wx, wz;
    if (client->iso_mode) {
      osrs_iso_camera_t iso_cam = {
          .x = client->camera.x,
          .y = client->camera.y,
          .zoom = client->camera.zoom,
          .yaw = client->camera.yaw,
          .pitch = client->camera.pitch,
          .center_tile_x = client->camera.focus_x,
          .center_tile_z = client->camera.focus_z,
      };
      osrs_iso_screen_to_world(&iso_cam, CLIENT_WIDTH, CLIENT_HEIGHT,
                               in->mouse_pos.x, in->mouse_pos.y, &wx, &wz);
    } else {
      screen_to_world(&client->camera, CLIENT_WIDTH, CLIENT_HEIGHT,
                      in->mouse_pos.x, in->mouse_pos.y, &wx, &wz);
    }
    OSRS_DEBUG(OSRS_LOG_CAT_CLIENT, "Right-click interaction at %d,%d", wx, wz);

    osrs_location_t *loc = NULL;
    osrs_npc_t *npc = NULL;
    osrs_player_t *player = NULL;
    int obj_dist = find_nearest_object(client, wx, wz, &loc);
    int npc_dist = find_nearest_npc(client, wx, wz, &npc);
    int player_dist = find_nearest_player(client, wx, wz, &player);

    /* Interact with whichever is closest (within 3 tiles).
     * Priority: player > NPC > object (combat focus). */
    if (player && player_dist <= 9 && (!npc || player_dist <= npc_dist) &&
        (!loc || player_dist <= obj_dist)) {
      OSRS_INFO(OSRS_LOG_CAT_CLIENT, "Attack player %s at %d,%d",
                player->name[0] ? player->name : "???", wx, wz);
      send_opplayer1(client, (int)(player - client->game.pi.players));
    } else if (loc && obj_dist <= 9 && (!npc || obj_dist <= npc_dist)) {
      const char *name = client_get_object_name(client, loc->object_id);
      OSRS_INFO(OSRS_LOG_CAT_CLIENT, "Interact with %s (id=%d) at %d,%d",
                name ? name : "???", loc->object_id, wx, wz);
      send_oploc1(client, loc->object_id, wx, wz);
    } else if (npc && npc_dist <= 9) {
      OSRS_INFO(OSRS_LOG_CAT_CLIENT, "Interact with NPC %s (index=%d) at %d,%d",
                npc->name[0] ? npc->name : "???", npc->id, wx, wz);
      send_opnpc1(client, npc->id);
    }
  }
}

/* -------------------------------------------------------------------------- */
/* Frame callback                                                             */
/* -------------------------------------------------------------------------- */

static void client_on_frame(fge_platform_t *p, double dt) {
  (void)p;
  client_state_t *client = &g_client;

  FGE_SCOPE(FGE_LOG_CAT_RENDERER, "frame");
  fge_telem_inc(FGE_TELEM_FRAME);

  /* Frame timing (fge_platform_run does not update frame_time for us) */
  fge_frame_time_update(&client->frame_time, &client->clock);

  client->frame_count++;

  /* Periodic TRACE: FPS and entity counts every 60 frames */
  if (client->frame_count % 60 == 0) {
    int total_locs = 0;
    for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
      if (client->region_cache[i].active && client->region_cache[i].region)
        total_locs += client->region_cache[i].region->location_count;
    }
    OSRS_TRACE(OSRS_LOG_CAT_CLIENT,
               "frame=%llu fps=%.1f regions=%d locations=%d npcs=%d",
               (unsigned long long)client->frame_count, client->frame_time.fps,
               client->region_cache_count, total_locs, client->game.npc_count);
  }

  /* Input */
  client_handle_input(client, (float)dt);

  /* Kick off online connection on first frame if requested */
  if (client->connect_requested &&
      client->game.state == OSRS_GAME_DISCONNECTED) {
    client->connect_requested = false;
    client->online = client_connect_online();
  }

  /* Auto-reconnect after a server drop. Wait a few seconds (and keep retrying
   * if the old session is still lingering as "Already logged in") so the game
   * recovers on its own instead of sitting on a blank FAILED screen. */
  if (client->online && client->game.state == OSRS_GAME_FAILED) {
    client->reconnect_timer += dt;
    if (client->reconnect_timer > 5.0) {
      client->reconnect_timer = 0.0;
      osrs_game_disconnect(&client->game); /* back to DISCONNECTED */
      client->connect_requested = true;
    }
  } else {
    client->reconnect_timer = 0.0;
  }

  /* Pump the game session */
  if (client->online)
    osrs_game_update(&client->game, dt);

  /* Update camera focus (tracks player / pans) then stream regions */
  client_update_camera_focus(client);
  client_update_regions();

  /* Render */
  fge_renderer_begin(&client->renderer, 0xFF1A1A2E);

  if (client->iso_mode) {
    osrs_iso_camera_t iso_cam = {
        .x = client->camera.x,
        .y = client->camera.y,
        .zoom = client->camera.zoom,
        .yaw = client->camera.yaw,
        .pitch = client->camera.pitch,
        .center_tile_x = client->camera.focus_x,
        .center_tile_z = client->camera.focus_z,
    };
    int sw = client->renderer.fb.width;
    int sh = client->renderer.fb.height;

    if (client->gl_world) {
      /* GPU-accelerated world rendering */
      osrs_gl_world_begin(client->gl_world, &iso_cam, sw, sh);
      for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
        if (client->region_cache[i].active && client->region_cache[i].region) {
          osrs_gl_world_render_region(client->gl_world,
                                      client->region_cache[i].region,
                                      client->region_cache[i].region_x,
                                      client->region_cache[i].region_y);
        }
      }
      /* Entity models (NPCs + players + ground items) */
      client_render_npc_models(client, dt);
      client_render_player_models(client);
      client_render_ground_item_models(client);
      osrs_gl_world_end(client->gl_world, client->renderer.fb.pixels, sw, sh);
    } else {
      for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
        if (client->region_cache[i].active && client->region_cache[i].region) {
          osrs_iso_render_region(&iso_cam, &client->renderer,
                                 client->region_cache[i].region, sw, sh,
                                 client->config);
        }
      }
    }
  } else {
    /* Legacy top-down */
    for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
      if (client->region_cache[i].active && client->region_cache[i].region) {
        render_region_topdown(client->region_cache[i].region,
                              client->region_cache[i].region_x,
                              client->region_cache[i].region_y,
                              &client->renderer, &client->camera);
      }
    }
  }

  if (client->region_cache_count == 0) {
    /* Draw placeholder */
    fge_draw_text(&client->renderer.fb, "Loading regions...",
                  CLIENT_WIDTH / 2 - 80, CLIENT_HEIGHT / 2, 0xFFFFFFFF, 1.5f);
  }

  render_debug_overlay(&client->renderer, client);
  render_ground_items(&client->renderer, client);
  render_npcs(&client->renderer, client);
  render_players(&client->renderer, client);
  render_minimap(&client->renderer, client);
  render_inventory(&client->renderer, client);
  render_bars(&client->renderer, client);
  render_game_overlay(&client->renderer, client);

  fge_renderer_end(&client->renderer);

  client_dump_frame(client);
}

/* -------------------------------------------------------------------------- */
/* Event callback                                                             */
/* -------------------------------------------------------------------------- */

static void client_on_event(fge_platform_t *p, const fge_event_t *event) {
  (void)p;

  switch (event->type) {
  case FGE_EVENT_KEY_DOWN:
    fge_telem_inc(FGE_TELEM_KEY_PRESS);
    if (event->key.key == FGE_KEY_ESCAPE)
      p->running = false;
    break;
  case FGE_EVENT_KEY_UP:
    fge_telem_inc(FGE_TELEM_KEY_RELEASE);
    break;
  case FGE_EVENT_MOUSE_DOWN:
    fge_telem_inc(FGE_TELEM_MOUSE_DOWN);
    break;
  case FGE_EVENT_MOUSE_UP:
    fge_telem_inc(FGE_TELEM_MOUSE_UP);
    break;
  case FGE_EVENT_RESIZE:
    /* Do NOT resize the framebuffer or recreate the GL world here.
     * Destroying/recreating the GL context drops every cached region VBO
     * and, on some driver/compositor paths, leaves the new context unable
     * to re-upload — the whole environment stops rendering (black
     * viewport).  Keeping the framebuffer + GL surface at creation size and
     * letting the Wayland compositor scale it to the tiled window renders
     * correctly and is robust across resizes. */
    break;
  case FGE_EVENT_CLOSE:
    p->running = false;
    break;
  default:
    break;
  }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char **argv) {
  osrs_log_init();
  OSRS_INFO(OSRS_LOG_CAT_CLIENT, "ORDL OSRS Client - Starting...");

  /* Determine cache path */
  const char *cache_path = NULL;
  if (argc > 1) {
    cache_path = argv[1];
  } else {
    cache_path = getenv("HOME");
    if (!cache_path) {
      OSRS_ERROR(OSRS_LOG_CAT_CLIENT, "Cannot determine home directory");
      return 1;
    }
    static char default_path[4096];
    snprintf(default_path, sizeof(default_path),
             "%s/.runelite/jagexcache/oldschool/LIVE/", cache_path);
    cache_path = default_path;
  }

  /* Logging */
  fge_log_init(FGE_LOG_LEVEL_DEBUG);
  fge_log_add_sink_stdout(FGE_LOG_LEVEL_DEBUG);
  FGE_INFO(FGE_LOG_CAT_GENERAL, "ORDL OSRS Client v0.1.0");
  FGE_INFO(FGE_LOG_CAT_GENERAL, "Cache path: %s", cache_path);

  /* Load cache */
  if (!client_load_cache(cache_path)) {
    FGE_FATAL(FGE_LOG_CAT_GENERAL, "Failed to load cache - exiting");
    return 1;
  }

  /* Create platform window */
  fge_platform_t *platform =
      fge_platform_create(CLIENT_TITLE, CLIENT_WIDTH, CLIENT_HEIGHT, false);
  if (!platform) {
    FGE_FATAL(FGE_LOG_CAT_PLATFORM, "Failed to create window");
    return 1;
  }

  g_client.platform = platform;
  platform->on_frame = client_on_frame;
  platform->on_event = client_on_event;
  platform->user_data = &g_client;

  /* Init renderer */
  if (!fge_renderer_init(&g_client.renderer, CLIENT_WIDTH, CLIENT_HEIGHT)) {
    FGE_FATAL(FGE_LOG_CAT_RENDERER, "Failed to init renderer");
    return 1;
  }

  /* Init GL world renderer (GPU acceleration) */
  g_client.gl_world = osrs_gl_world_create(CLIENT_WIDTH, CLIENT_HEIGHT);
  if (g_client.gl_world) {
    FGE_INFO(FGE_LOG_CAT_RENDERER, "GPU world renderer enabled");
  } else {
    FGE_WARN(FGE_LOG_CAT_RENDERER,
             "GPU world renderer unavailable, using software");
  }

  /* Attach framebuffer to platform for presentation */
  platform->framebuffer = fge_renderer_fb(&g_client.renderer);

  /* Load XTEA keys if available */
  const char *xtea_path = getenv("OSRS_XTEA_KEYS");
  if (xtea_path) {
    g_client.xtea_store = osrs_xtea_store_create();
    if (osrs_xtea_store_load_binary(g_client.xtea_store, xtea_path)) {
      FGE_INFO(FGE_LOG_CAT_GENERAL, "Loaded XTEA keys from %s", xtea_path);
    } else {
      FGE_WARN(FGE_LOG_CAT_GENERAL, "Failed to load XTEA keys from %s",
               xtea_path);
      osrs_xtea_store_destroy(g_client.xtea_store);
      g_client.xtea_store = NULL;
    }
  }

  /* Camera defaults */
  g_client.camera.zoom = 0.5f;
  g_client.camera.yaw = OSRS_ISO_DEFAULT_YAW;
  g_client.camera.pitch = OSRS_ISO_DEFAULT_PITCH;
  g_client.camera.x = 0;
  g_client.camera.y = -150; /* Player ~2/3 down screen like real OSRS */
  const char *zoom_env = getenv("OSRS_ZOOM");
  if (zoom_env)
    g_client.camera.zoom = (float)atof(zoom_env);
  const char *yaw_env = getenv("OSRS_YAW");
  if (yaw_env)
    g_client.camera.yaw = (float)atof(yaw_env);
  const char *pitch_env = getenv("OSRS_PITCH");
  if (pitch_env)
    g_client.camera.pitch = (float)atof(pitch_env);
  g_client.camera.target_region_x = 50;
  g_client.camera.target_region_y = 50;

  /* Enable isometric rendering by default */
  g_client.iso_mode = true;

  /* Initialize game session */
  osrs_game_init(&g_client.game);
  g_client.game.proto.width = CLIENT_WIDTH;
  g_client.game.proto.height = CLIENT_HEIGHT;
  g_client.game.proto.resizable = false;

  /* Online mode: connect if credentials are provided */
  if (getenv("OSRS_WORLD") && getenv("OSRS_USER") && getenv("OSRS_PASS")) {
    g_client.connect_requested = true;
  }

  /* Pre-load initial region */
  client_load_region(50, 50); /* Lumbridge */

  /* Timing */
  fge_clock_init(&g_client.clock);
  fge_frame_time_init(&g_client.frame_time, CLIENT_TARGET_FPS);

  FGE_INFO(FGE_LOG_CAT_GENERAL, "Client initialized, entering main loop");

  /* Run main loop */
  fge_platform_run(platform);

  /* Shutdown */
  FGE_INFO(FGE_LOG_CAT_GENERAL, "Shutting down...");

  /* Free all cached regions */
  for (int i = 0; i < CLIENT_REGION_CACHE_SIZE; i++) {
    if (g_client.region_cache[i].active && g_client.region_cache[i].region)
      osrs_region_free(g_client.region_cache[i].region);
  }
  if (g_client.map)
    osrs_map_free(g_client.map);
  if (g_client.config)
    osrs_config_free(g_client.config);
  if (g_client.xtea_store)
    osrs_xtea_store_destroy(g_client.xtea_store);
  if (g_client.cache)
    osrs_cache_close(g_client.cache);
  if (g_client.gl_world)
    osrs_gl_world_destroy(g_client.gl_world);

  fge_renderer_shutdown(&g_client.renderer);
  fge_platform_destroy(platform);
  fge_log_shutdown();

  OSRS_INFO(OSRS_LOG_CAT_CLIENT, "ORDL OSRS Client - Exited cleanly.");
  return 0;
}

# OSRS Client Header API Reference

Generated from `include/osrs/*.h` — public contract only (no `.c` implementation details).

---

## `auth.h` — Login Authentication Methods

**Purpose:** Defines the two supported authentication methods for OSRS login.

### Types
- `osrs_auth_method_t` — enum:
  - `OSRS_AUTH_LEGACY = 0` — username + password
  - `OSRS_AUTH_JAGEX = 2` — Jagex Account session token
- `osrs_auth_t` — credentials struct:
  - `method` — auth method
  - `username[65]` — legacy username or Jagex account email
  - `secret[128]` — password (legacy) or session token (Jagex)

### Functions
- `osrs_auth_method_from_string(const char *name)` — parse `"legacy"` or `"jagex"`; defaults to legacy
- `osrs_auth_method_str(osrs_auth_method_t method)` — human-readable method name

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Secret buffer size (128) bounds what the RSA login block can carry.

---

## `bignum.h` — Fixed-Size Big Integer Arithmetic

**Purpose:** Fixed-capacity big integers for RSA encryption. Zero-allocation, little-endian limb arrays.

### Constants
- `OSRS_BN_MAX_LIMBS 64` — 2048-bit capacity (enough for 1024-bit RSA products)

### Types
- `osrs_bn_t` — big integer:
  - `limb[64]` — little-endian `uint32_t` limbs
  - `len` — significant limbs count

### Functions
- `osrs_bn_from_bytes(osrs_bn_t *a, const uint8_t *bytes, size_t len)` — import big-endian bytes
- `osrs_bn_to_bytes(const osrs_bn_t *a, uint8_t *out, size_t out_len)` — export big-endian (zero-padded)
- `osrs_bn_set_zero(osrs_bn_t *a)`
- `osrs_bn_copy(osrs_bn_t *dst, const osrs_bn_t *src)`
- `osrs_bn_is_zero(const osrs_bn_t *a)` — bool
- `osrs_bn_cmp(const osrs_bn_t *a, const osrs_bn_t *b)` — returns -1/0/1
- `osrs_bn_bits(const osrs_bn_t *a)` — bit count
- `osrs_bn_test_bit(const osrs_bn_t *a, int bit)` — bool
- `osrs_bn_add(osrs_bn_t *a, const osrs_bn_t *b)` — `a += b`
- `osrs_bn_sub(osrs_bn_t *a, const osrs_bn_t *b)` — `a -= b` (requires `a >= b`)
- `osrs_bn_mul(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *b)` — `r = a * b`
- `osrs_bn_mod(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *m)` — binary long division
- `osrs_bn_mulmod(osrs_bn_t *r, const osrs_bn_t *a, const osrs_bn_t *b, const osrs_bn_t *m)`
- `osrs_bn_modpow(osrs_bn_t *r, const osrs_bn_t *base, const osrs_bn_t *exp, const osrs_bn_t *m)` — square-and-multiply

### Limitations / Notes
- Fixed capacity — operations that overflow `OSRS_BN_MAX_LIMBS` will silently truncate.
- `osrs_bn_sub` requires `a >= b` — caller must ensure precondition.
- `osrs_bn_mod` comment: "a may be larger than capacity/2".

---

## `bzip2.h` — BZip2 Decompressor

**Purpose:** Pure C BZip2 decompression for OSRS cache files (Burrows-Wheeler, MTF, Huffman, RLE).

### Types
- `osrs_bzip2_t` — opaque decompressor context

### Functions
- `osrs_bzip2_create(void)` — allocate context
- `osrs_bzip2_destroy(osrs_bzip2_t *ctx)`
- `osrs_bzip2_decompress(ctx, compressed, compressed_len, output, output_len)` — `output_len` is in/out
- `osrs_bzip2_decompress_oneshot(compressed, compressed_len, output, output_len)` — allocates context internally
- `osrs_bzip2_decompressed_size(compressed, compressed_len)` — reads block header; returns 0 on error

### Limitations / Notes
- Decompress-only; no compression support.
- No TODOs or FIXMEs in header.

---

## `cache.h` — OSRS Cache File System Reader

**Purpose:** Reads the flatfile cache format (`main_file_cache.dat2` + `main_file_cache.idx*`). Supports index/archive reading and container decompression.

### Constants
- `OSRS_COMPRESSION_NONE 0`, `OSRS_COMPRESSION_BZ2 1`, `OSRS_COMPRESSION_GZ 2`
- `OSRS_INDEX_MASTER 255` — master index
- `OSRS_MAX_INDEXES 256`
- `OSRS_MAX_ARCHIVES 65536`
- `OSRS_SECTOR_SIZE 520`
- `OSRS_SECTOR_HEADER 8`
- `OSRS_SECTOR_DATA (520 - 8)`
- `OSRS_INDEX_ENTRY_SIZE 6`

### Types
- `osrs_archive_t` — archive metadata (id, name_hash, crc, sizes, revision, file_ids, file_count)
- `osrs_index_t` — index metadata (id, protocol, named, sized, revision, crc, compression, archives, archive_count)
- `osrs_cache_t` — cache store context (path, FILE handles, indexes)
- `osrs_container_t` — decompression result (data, len, compression, revision, crc)
- `osrs_archive_file_t` — individual file from split archive (id, data, len)
- `osrs_archive_files_t` — array of split files
- `osrs_stream_t` — read cursor over cache data (data, len, pos)

### Functions
**Cache lifecycle:**
- `osrs_cache_open(const char *path)` — open cache store
- `osrs_cache_close(osrs_cache_t *cache)`
- `osrs_cache_load_indexes(osrs_cache_t *cache)` — load all indexes

**Raw reads:**
- `osrs_cache_read_index(cache, index_id, out_len)` — read raw index data from index 255
- `osrs_cache_read_archive(cache, index_id, archive_id, out_len)` — read raw archive data

**Archive splitting:**
- `osrs_cache_read_archive_files(cache, index_id, archive_id, xtea_key)` — read, decrypt (optional), decompress, split into files
- `osrs_archive_files_free(osrs_archive_files_t *files)`

**Container decompression:**
- `osrs_container_decompress(data, len, xtea_key)` — decrypt optional, decompress
- `osrs_container_free(osrs_container_t *container)`

**Index queries:**
- `osrs_cache_get_index(cache, index_id)`
- `osrs_index_find_archive_by_hash(index, name_hash)`
- `osrs_index_free(osrs_index_t *index)`

**Stream helpers:**
- `osrs_stream_init(s, data, len)`
- `osrs_stream_u8 / i8 / u16 / i16 / u32 / i32 / u64`
- `osrs_stream_bytes(s, out, len)` — copy bytes
- `osrs_stream_skip(s, len)`
- `osrs_stream_remaining(s)`
- `osrs_stream_string(s, out, max_len)` — null-terminated string
- `osrs_stream_smart(s)` — signed variable-length int
- `osrs_stream_usmart(s)` — unsigned variable-length int

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Stream helpers are big-endian (Jagex convention).
- `osrs_cache_t` uses `FILE*` — not thread-safe by design.

---

## `config.h` — Game Configuration Loaders

**Purpose:** Loads game config from cache index 2: objects, NPCs, items, sprites, textures, animations, etc.

### Constants — Config Archive IDs (index 2)
| ID | Name |
|----|------|
| 1  | UNDERLAY |
| 3  | IDENTKIT |
| 4  | OVERLAY |
| 5  | INV |
| 6  | OBJECT |
| 8  | ENUM |
| 9  | NPC |
| 10 | ITEM |
| 11 | PARAM |
| 12 | SEQUENCE |
| 13 | SPOTANIM |
| 14 | VARBIT |
| 16 | VARPLAYER |
| 17 | AREA |
| 18 | STRUCT |
| 19 | DBROW |
| 20 | DBTABLE |
| 21 | DBINDEX |
| 22 | WORLDMAP |
| 23 | WORLD |
| 24 | REGION |
| 25 | MAPLABEL |
| 26 | INTERFACE |
| 27 | MIDI |
| 28 | MODEL |
| 29 | SPRITE |
| 30 | TEXTURE |
| 31 | BINARY |
| 32 | JINGLED |
| 33 | CLIENTSCRIPT |
| 34 | FONT |
| 35 | VORBIS |
| 36 | WORLDMAPV2 |

### Types
- `osrs_object_def_t` — object definition (64 fields: id, name, size, interact_type, models, recolors, actions, transforms, etc.)
- `osrs_npc_def_t` — NPC definition (~60 fields: combat stats, animations, models, sounds, slayer info, transforms, etc.)
- `osrs_item_def_t` — item definition (~50 fields: bonuses, equip slot, models, recolors, noted/placeholder IDs, etc.)
- `osrs_sprite_t` — sprite (id, width, height, offsets, 256-entry palette, pixel buffer)
- `osrs_texture_t` — texture (id, file_id, width, height, opaque, mipmap, pixels, average_rgb)
- `osrs_sequence_def_t` — animation/sequence (frames, delays, loop, priority, hand items, precedence)
- `osrs_spotanim_def_t` — spot animation (model, animation, scale, ambient, contrast, recolors)
- `osrs_config_t` — config manager (holds pointers to all relevant cache indexes)

### Functions
**Manager lifecycle:**
- `osrs_config_init(osrs_cache_t *cache)`
- `osrs_config_free(osrs_config_t *config)`

**Definition loaders (return heap-allocated structs):**
- `osrs_config_load_object(config, id)` → `osrs_object_def_t*`
- `osrs_config_load_npc(config, id)` → `osrs_npc_def_t*`
- `osrs_config_load_item(config, id)` → `osrs_item_def_t*`
- `osrs_config_load_sequence(config, id)` → `osrs_sequence_def_t*`
- `osrs_config_load_spotanim(config, id)` → `osrs_spotanim_def_t*`

**Definition free functions:**
- `osrs_object_def_free`, `osrs_npc_def_free`, `osrs_item_def_free`, `osrs_sequence_def_free`, `osrs_spotanim_def_free`

**Asset loaders:**
- `osrs_config_load_sprite(config, archive_id, file_id)` → `osrs_sprite_t*`
- `osrs_config_load_texture(config, id)` → `osrs_texture_t*`
- `osrs_config_load_model(config, model_id, out_len)` → `uint8_t*` (raw model bytes)
- `osrs_config_load_map(config, region_id, xtea_key, out_len)` → `uint8_t*` (terrain data)
- `osrs_config_load_landscape(config, region_id, xtea_key, out_len)` → `uint8_t*` (location data)

**Free asset helpers:**
- `osrs_sprite_free`, `osrs_texture_free`

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Several definition structs contain `unknown_1`, `unknown_2` fields — placeholders for unmapped cache fields.
- Model loader returns raw bytes; no high-level model parsing here.

---

## `crc32.h` — CRC32 Checksum

**Purpose:** IEEE 802.3 CRC32 for cache integrity verification.

### Types
- `osrs_crc32_t` — context with `uint32_t state`

### Functions
- `osrs_crc32_init(osrs_crc32_t *ctx)`
- `osrs_crc32_update(ctx, data, len)`
- `osrs_crc32_final(ctx)` → `uint32_t`
- `osrs_crc32(data, len)` — one-shot

### Limitations / Notes
- No TODOs or FIXMEs in header.

---

## `game.h` — Game Session Orchestrator

**Purpose:** Ties together TCP transport, login handshake, packet dispatch, and player/world state. Main client-facing API.

### Constants
- `OSRS_GAME_CHAT_MAX 32` — chat ring buffer size
- `OSRS_GAME_CHAT_LEN 256` — max chat text length
- `OSRS_GAME_SKILLS 23` — number of skills

### Types
- `osrs_game_state_t` — enum:
  - `OSRS_GAME_DISCONNECTED`, `OSRS_GAME_CONNECTING`, `OSRS_GAME_LOGGING_IN`, `OSRS_GAME_IN_GAME`, `OSRS_GAME_FAILED`
- `osrs_chat_msg_t` — chat message (type, name[64], text[256])
- `osrs_game_t` — main session struct:
  - `osrs_net_t net` + `osrs_protocol_t proto`
  - `state`, `fail_reason` (login response code)
  - Player state: `player_x`, `player_z`, `player_plane`, `zone_x`, `zone_z`, `world_area`, `map_ready`
  - `osrs_playerinfo_t pi` — GPI (local + nearby players)
  - Stats: `levels[23]`, `real_levels[23]`, `xp[23]`, `run_energy`, `run_weight`
  - `varps[4096]`
  - Chat ring buffer: `chat[32]`, `chat_head`, `chat_count`
  - Ping tracking: `ping_v1`, `ping_v2`, `ping_pending`
  - Timing: `keepalive_timer`, `connected_time`
  - Callbacks: `on_state_change`, `on_map_load`, `on_chat`
  - `userdata` — client context pointer

### Functions
**Lifecycle:**
- `osrs_game_init(osrs_game_t *game)` — zero-initializes
- `osrs_game_connect(game, host, port, auth, cache_crcs[23], timeout_ms)` — starts login (async via update)
- `osrs_game_update(game, dt)` — process network I/O and packets; call every frame
- `osrs_game_disconnect(game)`

**Actions:**
- `osrs_game_move_click(game, x, z, run)` — walk/click-to-move
- `osrs_game_send_window_status(game, mode, w, h)` — report window mode
- `osrs_game_send_public_chat(game, msg)` — send public chat
- `osrs_game_send_cheat(game, command)` — send client cheat
- `osrs_game_map_build_complete(game)` — notify server map build done

**Helpers:**
- `osrs_login_response_str(int code)` — human-readable login response
- `osrs_tile_to_region_x(y)` / `osrs_tile_to_region_y(y)` — inline tile→region

### Limitations / Notes
- No TODOs or FIXMEs in header.
- `osrs_game_connect` is non-blocking; the state machine runs inside `osrs_game_update`.
- Chat buffer is a ring buffer of 32 messages; old messages are overwritten.

---

## `gzip.h` — GZip/DEFLATE Decompressor

**Purpose:** GZip decompression (LZ77 + Huffman) for OSRS cache files.

### Types
- `osrs_gzip_t` — opaque decompressor context

### Functions
- `osrs_gzip_create(void)`
- `osrs_gzip_destroy(osrs_gzip_t *ctx)`
- `osrs_gzip_decompress(ctx, compressed, compressed_len, output, output_len)` — `output_len` in/out
- `osrs_gzip_decompress_oneshot(compressed, compressed_len, output, output_len)`
- `osrs_gzip_decompressed_size(compressed, compressed_len)` — reads ISIZE footer; 0 on error

### Limitations / Notes
- Decompress-only.
- No TODOs or FIXMEs in header.

---

## `https.h` — Minimal Blocking HTTPS Client

**Purpose:** HTTP/1.1 over TLS 1.3 client for Jagex OAuth + game-session flows. Supports GET, POST, Content-Length, and chunked transfer encoding.

### Types
- `osrs_https_response_t` — response struct:
  - `status` — HTTP code or -1 on transport error
  - `body` — caller-provided buffer
  - `body_len` — bytes written

### Functions
- `osrs_https_get(host, port, path, body, body_cap, resp)` — returns true if response received
- `osrs_https_post(host, port, path, content_type, req_body, body, body_cap, resp)` — POST with body
- `osrs_https_header(head, name, len)` — case-insensitive header lookup; returns pointer into raw head

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Blocking API — intended for the one-shot OAuth flow, not for the game loop.
- Uses vendored TLS stack (no external OpenSSL/LibreSSL dependency).

---

## `isaac.h` — ISAAC Stream Cipher / PRNG

**Purpose:** ISAAC (Indirection, Shift, Accumulate, Add, and Count) by Bob Jenkins. Used for OSRS game packet opcode obfuscation.

### Constants
- `OSRS_ISAAC_WORDS 256`

### Types
- `osrs_isaac_t` — context:
  - `mem[256]` — memory pool
  - `rsl[256]` — results pool
  - `cnt`, `a`, `b`, `c`, `idx`

### Functions
- `osrs_isaac_init(ctx, seed[4])` — seed with 4×uint32 (from login handshake)
- `osrs_isaac_init_seed(ctx, seed, len)` — seed with arbitrary bytes
- `osrs_isaac_next(ctx)` → `uint32_t`
- `osrs_isaac_bytes(ctx, out, len)` — fill buffer
- `osrs_isaac_refill(ctx)` — refills results pool (called automatically)

### Limitations / Notes
- No TODOs or FIXMEs in header.

---

## `jagex.h` — Jagex Account OAuth + Game Session Flow

**Purpose:** Obtains a Jagex game session token for `OSRS_AUTH_JAGEX` login. Verified against the open-source Bolt launcher.

### Constants — Endpoints & Client Registration
- `OSRS_JAGEX_ORIGIN "account.jagex.com"`
- `OSRS_JAGEX_AUTH_API "auth.jagex.com"`
- `OSRS_JAGEX_AUTH_API_PATH "/game-session/v1/sessions"`
- `OSRS_JAGEX_CLIENT_ID "com_jagex_auth_desktop_launcher"`
- `OSRS_JAGEX_REDIRECT "https://secure.runescape.com/m=weblogin/launcher-redirect"`
- `OSRS_JAGEX_SCOPE "openid offline gamesso.token.create user.profile.read"`

### Functions
- `osrs_jagex_pkce(verifier, verifier_cap, challenge, challenge_cap)` — generate PKCE verifier + S256 challenge (buffers >= 64 / 44 bytes)
- `osrs_jagex_random_state(out, cap)` — random URL-safe state string (32 chars)
- `osrs_jagex_authorize_url(out, cap, challenge, state)` — build OAuth authorize URL (`out` >= 1024 bytes)
- `osrs_jagex_parse_redirect(redirect_url, expect_state, code_out, code_cap)` — extract code and verify state
- `osrs_jagex_exchange_code(code, verifier, id_token_out, id_token_cap, err, err_cap)` — code → id_token
- `osrs_jagex_create_session(id_token, session_id_out, session_id_cap, err, err_cap)` — id_token → sessionId
- `osrs_jagex_login_interactive(session_id_out, session_id_cap)` — full interactive flow (prints URL, prompts for redirect)
- `osrs_jagex_b64url(in, len, out, cap)` — base64url encode (no padding)

### Limitations / Notes
- No TODOs or FIXMEs in header.
- `osrs_jagex_login_interactive` is blocking and reads from stdin.
- Client ID is hardcoded to match the official Jagex desktop launcher.

---

## `map.h` — Map/Region Loading

**Purpose:** Loads terrain and location data from cache index 5 (MAPS). Terrain in `m<rx>_<ry>` archives; locations in `l<rx>_<ry>` (XTEA encrypted).

### Constants
- `OSRS_REGION_SIZE 64`
- `OSRS_REGION_PLANES 4`
- `OSRS_TILE_SIZE 1`

### Types
- `osrs_tile_t` — single tile:
  - `height`, `settings`, `overlay_id`, `overlay_path`, `overlay_rotation`, `underlay_id`
- `osrs_location_t` — world object:
  - `object_id`, `type`, `orientation`, `local_x`, `local_y`, `plane`
- `osrs_region_t` — 64×64 region:
  - `region_x`, `region_y`, `region_id`
  - `tiles[4][64][64]`
  - `locations*` (dynamic array), `location_count`, `location_capacity`
- `osrs_map_t` — map manager (holds `osrs_cache_t*`)

### Functions
- `osrs_map_init(cache)` → `osrs_map_t*`
- `osrs_map_free(map)`
- `osrs_map_load_region(map, region_x, region_y, xtea_key)` → `osrs_region_t*`
- `osrs_map_load_region_by_id(map, region_id, xtea_key)` → `osrs_region_t*`
- `osrs_region_free(region)`

**Inline helpers:**
- `osrs_region_base_x/y(region_x/y)` → `<< 6`
- `osrs_region_id(rx, ry)` → `(rx << 8) | ry`
- `osrs_region_x_from_id(id)` → `id >> 8`
- `osrs_region_y_from_id(id)` → `id & 0xFF`

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Location array is dynamically grown; no hard limit specified.
- Terrain and locations require separate cache archives and XTEA keys.

---

## `net.h` — TCP Client

**Purpose:** POSIX sockets TCP client with linear send/receive buffers.

### Constants
- `OSRS_NET_RECV_CAP 65536`
- `OSRS_NET_SEND_CAP 65536`

### Types
- `osrs_net_t` — TCP state:
  - `fd` — socket descriptor
  - `connected`, `peer_closed`
  - `recv_buf[65536]`, `recv_len`
  - `send_buf[65536]`, `send_len`

### Functions
- `osrs_net_connect(net, host, port, timeout_ms)` — blocking connect
- `osrs_net_close(net)`
- `osrs_net_queue(net, data, len)` — enqueue for send (returns false if full)
- `osrs_net_poll(net)` — flush send + drain recv; returns false on fatal error
- `osrs_net_read(net, out, len)` — consume from recv buffer
- `osrs_net_peek(net, out, len)` — peek without consuming
- `osrs_net_available(net)` → bytes ready
- `osrs_net_consume(net, n)` — discard bytes
- `osrs_tcp_connect(host, port, timeout_ms)` → blocking fd for TLS; -1 on failure

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Buffers are fixed at 64 KiB each; `osrs_net_queue` fails if send buffer is full.
- `peer_closed` is surfaced once the recv buffer drains — caller must handle graceful close.
- `osrs_tcp_connect` is for raw TLS connections (used by HTTPS).

---

## `playerinfo.h` — Player Info (GPI) Decoder

**Purpose:** Decodes the per-cycle `PLAYER_INFO` packet (rev-239). Verified against RSProt osrs-239 and the official client.

### Constants
- `OSRS_PI_CAPACITY 2048` — max player slots
- `OSRS_PI_VIEW_DISTANCE 15` — Chebyshev distance in tiles

**Extended-info mask bits (wire values):**
| Bit | Name |
|-----|------|
| 0x1 | FACE |
| 0x2 | PLAYER_RESET |
| 0x4 | SAY |
| 0x8 | EXT_SHORT |
| 0x20 | APPEARANCE |
| 0x40 | SEQUENCE |
| 0x100 | CHAT |
| 0x200 | TINTING |
| 0x400 | MOVE_SPEED |
| 0x800 | EXT_MEDIUM |
| 0x1000 | TEMP_MOVE_SPEED |
| 0x4000 | EXACT_MOVE |
| 0x10000 | HEADBARS |
| 0x20000 | SPOTANIM |
| 0x40000 | HITMARKS |
| 0x80000 | FREEZE |
| 0x100000 | TRANSPARENCY |

### Types
- `osrs_player_t` — player slot state:
  - `active`, `high_res`, `was_stationary`, `is_stationary`, `has_extinfo`
  - `x`, `z`, `level` — absolute tile coords
  - Appearance: `appearance_valid`, `name[13]`, `combat_level`, `body_type`, `skull_icon`, `overhead_icon`, `transformed_npc`, `equipment[12]`, `colours[5]`
  - `move_speed`, `teleported`
- `osrs_playerinfo_t` — GPI context:
  - `players[2048]`, `local_index`, `view_distance`
  - Decode scratch: `filtered`, `extinfo_order`, `extinfo_count`
  - Snapshots: `snap_index[2][2048]`, `snap_was[2][2048]`, `snap_count[2]`
  - Local player: `local_x`, `local_z`, `local_level`, `local_pos_valid`

### Functions
- `osrs_pi_init(pi)`
- `osrs_pi_seed_login(pi, local_index, data, len)` — parse login rebuild GPI init block (30-bit local coord + 18-bit low-res positions)
- `osrs_pi_decode(pi, data, len)` — decode one `PLAYER_INFO` packet

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Decode scratch arrays (`filtered`, `extinfo_order`, `snap_*`) are preallocated to avoid allocation during decode.
- Comment notes that snapshots freeze "start-of-cycle resolution lists" so later passes aren't affected by mutations.

---

## `proofofwork.h` — SHA256 Hashcash Solver

**Purpose:** Solves OSRS login proof-of-work challenges (server response code 69).

### Functions
- `osrs_pow_leading_zeros(hash[32])` — count leading zero bits in a 32-byte hash
- `osrs_pow_solve(version, difficulty, salt, start)` — brute-force search for a `uint64_t result` such that SHA256(hex(version) + hex(difficulty) + salt + hex(result)) has `difficulty` leading zero bits

### Limitations / Notes
- No TODOs or FIXMEs in header.
- `start` parameter allows resuming search from a previous point.
- No parallelism API exposed in header — single-threaded search.

---

## `protocol.h` — OSRS Network Protocol (Rev 239)

**Purpose:** Byte-buffer primitives, login handshake, ISAAC obfuscation, game packet framing, packet builders/parsers.

### Constants
- `OSRS_PROTOCOL_REVISION 239`
- `OSRS_GAME_PORT 43594`

**Login opcodes (client→server):**
- `OSRS_LOGIN_INIT_GAME_CONNECTION 14`
- `OSRS_LOGIN_INIT_JS5_CONNECTION 15`
- `OSRS_LOGIN_GAMELOGIN 16`
- `OSRS_LOGIN_GAMERECONNECT 18`

**Login response codes (server→client):**
0, 2, 3 (invalid cred), 4 (banned), 5 (duplicate), 6 (out of date), 7 (server full), 8 (loginserver offline), 9 (IP limit), 10 (bad session), 11 (force password change), 12 (need members), 16 (too many attempts), 17 (members area), 18 (locked), 21 (hop blocked), 22 (invalid packet), 56 (authenticator), 69 (proof of work)

**Packet size types:**
- `OSRS_PKT_VAR_BYTE -1`
- `OSRS_PKT_VAR_SHORT -2`

**Outgoing game packet opcodes (client→server):** 1, 4, 5, 8, 10, 24, 29, 33, 34, 35, 51, 54, 95, 67, 69, 89, 114, 115, 50, 73, 103, 104, 15, 116, 102, 13, 19, 45, 85, 46, 74, 7, 31, 43, 93, 3, 92, 37, 57, 59, 99

**Incoming game packet opcodes (server→client):** 49, 125, 28, 85, 2, 96, 7, 23, 80, 63, 22, 18, 46, 64, 31, 97, 12, 44, 74, 19, 124, 57, 58, 115, 83, 143, 6, 54, 9, 105, 111, 42, 36, 92, 37, 43, 3, 103, 114, 50, 35, 118, 77, 47, 68, 67, 113, 93, 117, 39, 126, 116, 66, 79

### Types
- `osrs_packet_t` — Jagex packet buffer:
  - `data[8192]`, `len` (write bytes / read total), `pos` (read cursor)
- `osrs_conn_state_t` — connection state machine:
  - `DISCONNECTED`, `INIT_SENT`, `LOGIN_SENT`, `AWAIT_AUTH_CODE`, `POW`, `GAME`, `FAILED`
- `osrs_protocol_t` — protocol context (~40 fields):
  - ISAAC ciphers: `isaac_in`, `isaac_out`
  - Session: `state`, `session_id`, `xtea_seed[4]`, `login_response`, `revision`, `client_type`, `platform_type`, `second_client_type`
  - Player: `player_index`, `staff_mod_level`, `player_mod`, `members`, `account_hash`, `user_id`, `user_hash`
  - Credentials: `username[65]`, `secret[128]`, `auth_type`
  - `index_crcs[23]`
  - PoW: `pow_version`, `pow_difficulty`, `pow_salt[1200]`
  - Window: `width`, `height`, `resizable`, `low_detail`

### Functions
**Packet buffer:**
- `osrs_packet_init(p)` — zero
- `osrs_packet_wrap(p, data, len)` — wrap external buffer

**Readers:**
- `osrs_g1 / g1s` — unsigned/signed byte
- `osrs_g1alt1` — `v - 128`
- `osrs_g1alt2` — `0 - v`
- `osrs_g1alt3` — `128 - v`
- `osrs_g2 / g2s` — unsigned/signed short
- `osrs_g2alt1` — LE short
- `osrs_g2alt2` — lo byte `- 128`
- `osrs_g2alt3` — LE, lo byte `- 128`
- `osrs_g3` — 24-bit BE
- `osrs_g4` — 32-bit BE
- `osrs_g4alt1` — LE
- `osrs_g4alt2` — bytes `1,0,3,2`
- `osrs_g4alt3` — bytes `2,3,0,1`
- `osrs_g8` — 64-bit BE
- `osrs_gbytes(p, out, len)`
- `osrs_gjstr(p, out, max_len)` — null-terminated string
- `osrs_gjstr2(p, out, max_len)` — `0 + str`
- `osrs_packet_remaining(p)`

**Writers:**
- `osrs_p1` — byte
- `osrs_p1alt1` — `v + 128`
- `osrs_p1alt2` — `-v`
- `osrs_p1alt3` — `128 - v`
- `osrs_p2` — short
- `osrs_p2alt1` — LE
- `osrs_p2alt2` — lo byte `+ 128`
- `osrs_p2alt3` — LE, lo byte `+ 128`
- `osrs_p3` — 24-bit
- `osrs_p4` — 32-bit
- `osrs_p4alt1/alt2/alt3` — LE / `1,0,3,2` / `2,3,0,1`
- `osrs_p8` — 64-bit
- `osrs_pbytes(p, data, len)`
- `osrs_pjstr(p, str)` — null-terminated
- `osrs_pjstr2(p, str)` — `0 + str`

**Protocol context:**
- `osrs_protocol_init(proto)`

**Login handshake:**
- `osrs_login_build_init(out)` — returns byte count for opcode 14 packet
- `osrs_login_process_init_response(proto, data, len, consumed)` — returns 1 = ready, 0 = need more, <0 = error
- `osrs_login_build_gamelogin(proto, out)` — build GAMELOGIN packet (opcode 16 + var short + login block)
- `osrs_login_parse_response(proto, data, len, consumed)` — returns login response code (2 = OK)
- `osrs_login_build_pow_reply(result, out)` — 8-byte big-endian PoW result

**Game packet framing:**
- `osrs_server_packet_sizes[256]` — server→client size table (extern)
- `osrs_client_packet_sizes[256]` — client→server size table (extern)
- `osrs_game_read_packet(proto, data, len, packet, consumed)` — decode one server packet; returns opcode, -1 (need more), -2 (error)
- `osrs_game_write_packet(proto, opcode, payload, payload_len, out)` — encode client packet

**Outgoing packet builders (payload only):**
- `osrs_build_move_gameclick(p, x, z, run)`
- `osrs_build_window_status(p, mode, w, h)`
- `osrs_build_ping_reply(p, v1, v2, fps)`
- `osrs_build_message_public(p, msg, color_effect, move_effect)`
- `osrs_build_event_applet_focus(p, focused)`
- `osrs_build_event_camera_position(p, angle, zoom)`
- `osrs_build_if_button(p, interface_id, child_id)`
- `osrs_build_client_cheat(p, command)`
- `osrs_build_oploc(p, opcode, object_id, x, z)`
- `osrs_build_opnpc(p, opcode, npc_index)`

**Incoming packet parsers:**
- `osrs_parse_rebuild_normal(p, out)` → `osrs_rebuild_normal_t` (zone_x, zone_z, world_area)
- `osrs_parse_ping(p, out)` → `osrs_ping_t` (value1, value2)
- `osrs_parse_message_game(p, out)` → `osrs_message_game_t` (type, has_name, name, text)
- `osrs_parse_update_stat(p, out)` → `osrs_update_stat_t` (level, real_level, experience, skill_id)
- `osrs_parse_varp_small / varp_large`
- `osrs_parse_if_opentop / if_opensub / if_closesub`
- `osrs_parse_u16_packet(p, value)` — for UPDATE_RUNENERGY / UPDATE_RUNWEIGHT

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Packet buffer is fixed at 8192 bytes — no dynamic growth.
- Size tables are initialized lazily on first `osrs_protocol_init()` call.
- Unknown opcodes in size tables default to `-9`.
- `osrs_login_build_gamelogin` requires `xtea_seed` to be randomized by caller.
- `osrs_protocol_t.revision` defaults to `OSRS_PROTOCOL_REVISION` (239).

---

## `rsa.h` — RSA Public-Key Encryption

**Purpose:** RSA encryption for the OSRS login protocol.

### Functions
- `osrs_rsa_encrypt_login(plaintext, plaintext_len, out, out_len)` — encrypt with OSRS public key
  - Returns bytes written, or 0 on failure
  - `out_len` must be >= modulus byte size
  - Plaintext must be `<= modulus size - 1`

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Single hardcoded public key (the OSRS live server key).
- Encrypt-only — no decryption support exposed.

---

## `xtea.h` — XTEA Block Cipher

**Purpose:** XTEA (eXtended TEA) for map/region file decryption. 64-bit block, 128-bit key, 32 rounds.

### Constants
- `OSRS_XTEA_KEY_SIZE 16` (128 bits)
- `OSRS_XTEA_BLOCK_SIZE 8` (64 bits)
- `OSRS_XTEA_ROUNDS 32`

### Types
- `osrs_xtea_t` — context with `uint32_t key[4]`

### Functions
- `osrs_xtea_init(ctx, key[16])` — init from 16-byte array
- `osrs_xtea_init_u32(ctx, key[4])` — init from 4×uint32
- `osrs_xtea_encrypt_block(ctx, block[8])`
- `osrs_xtea_decrypt_block(ctx, block[8])`
- `osrs_xtea_encrypt(ctx, data, len)` — length must be multiple of 8
- `osrs_xtea_decrypt(ctx, data, len)` — length must be multiple of 8

### Limitations / Notes
- No TODOs or FIXMEs in header.
- In-place encryption/decryption (mutates input buffer).
- Length must be a multiple of block size — caller must pad.

---

## `xtea_keys.h` — XTEA Key Store

**Purpose:** Stores and looks up XTEA keys for map decryption by region/mapsquare.

### Types
- `osrs_xtea_key_t` — `mapsquare` (region ID) + `key[4]`
- `osrs_xtea_store_t` — dynamic array of keys (`keys*`, `count`, `capacity`)

### Functions
- `osrs_xtea_store_create(void)`
- `osrs_xtea_store_destroy(store)`
- `osrs_xtea_store_load_binary(store, path)` — binary format: `count uint32le`, then `mapsquare uint32le` + `4×int32le`
- `osrs_xtea_store_get_key(store, mapsquare)` → `const uint32_t*` or NULL
- `osrs_xtea_store_get_key_for_region(store, region_x, region_y)` → `const uint32_t*` or NULL

### Limitations / Notes
- No TODOs or FIXMEs in header.
- Binary format is little-endian.
- Returns pointer into internal array — invalidates if store is resized.

---

## Cross-Cutting Observations

### No Header TODOs/FIXMEs/XXXs
A grep across all `include/osrs/*.h` found **zero** TODO, FIXME, XXX, HACK, or BUG comments in headers. All such markers live in `.c` implementation files.

### Design Patterns
1. **Zero external dependencies** — every module is self-contained; no libcurl, OpenSSL, zlib, etc.
2. **Fixed buffers everywhere** — `osrs_packet_t` (8192), `osrs_net_t` buffers (64K), `osrs_bn_t` (2048-bit), chat ring (32). No hidden allocations in hot paths.
3. **Opaque types for decompressors** — `osrs_gzip_t`, `osrs_bzip2_t` are forward-declared structs; implementation hidden.
4. **Explicit free functions** — every heap-allocated type has a matching `*_free` or `*_destroy`.
5. **Inline helpers for coordinate math** — tile→region, region→base, region ID packing/unpacking.
6. **Revision pinned** — `OSRS_PROTOCOL_REVISION 239` is hardcoded in `protocol.h`; bumping requires updating the protocol constants, packet sizes, and GPI decoder.

### Files Present in `include/osrs/` but Not in User's List
- `https.h` — included above (minimal HTTPS client)
- `proofofwork.h` — included above (SHA256 hashcash solver)


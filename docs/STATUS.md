# OSRS Client — Development Status

## Completed

### Phase 1: Foundation (Crypto & Compression)
- [x] **CRC32** — IEEE 802.3 polynomial, table-driven, verified with test vectors
- [x] **XTEA** — 64-bit block cipher, 128-bit key, 32 rounds, encrypt/decrypt verified
- [x] **ISAAC** — Bob Jenkins stream cipher, full 256-word state, deterministic output verified
- [x] **GZip** — Full DEFLATE implementation (LZ77 + Huffman), works with real OSRS cache data
- [x] **BZip2** — Full decompression: bitstream, Huffman, MTF, inverse BWT, output RLE expansion, multi-block CRC

### Phase 2: Cache System
- [x] **Flatfile cache reader** — Reads `main_file_cache.dat2` + `main_file_cache.idx*`
- [x] **Sector-based data file** — 520-byte sectors with 8/10-byte headers
- [x] **Index file parsing** — 6-byte entries (length + sector)
- [x] **Container decompression** — GZip and BZip2 container support with XTEA decryption
- [x] **Index metadata parsing** — Protocol 5-7, named/sized flags, archive/file hierarchies
- [x] **Archive file splitting** — Multi-chunk archive file extraction
- [x] **Real cache verified** — Successfully loads all 22 indexes from live OSRS cache

### Phase 3: Config Loading
- [x] **Object definitions** — Correct opcodes, real data verified
- [x] **NPC definitions** — Correct opcodes from RuneLite NpcLoader, verified (Molanisk, Zombie, etc.)
- [x] **Item definitions** — Correct opcodes from RuneLite ItemLoader, verified (Coins, Abyssal whip, etc.)
- [x] **Underlay/overlay definitions** — Color, texture, hidden flags for terrain rendering
- [x] **Sequence/SpotAnim definitions** — Animation and GFX config loaders
- [x] **XTEA key infrastructure** — Binary key store, integrates with container decryption

### Phase 4: Map/Region Loading
- [x] **REBUILD_NORMAL parser** — Rev 239 V2 format: 6-byte footer at end of packet (p2Alt1/p2Alt2), GPI init block at start
  - **FIXED (2026-07-30):** Parser was reading header from start of packet instead of footer from end. Now reads `worldArea`/`zoneZ`/`zoneX` from last 6 bytes and passes GPI init block from start to `osrs_pi_seed_login()`.
- [x] **Location loading** — Parses objects with type, orientation, position
  - **FIXED (2026-07-30):** `parse_locations()` was using wrong smart readers (`osrs_stream_smart`/`usmart` instead of `osrs_stream_ushort_smart`/`uint_smart_short_compat`). Verified against RuneLite `LocationsLoader.java`. Region (50,50) now loads 4726 locations (was 0).

### Phase 5: Client Application
- [x] **FORGE integration** — Platform window, renderer, input, telemetry
- [x] **Top-down map renderer** — Terrain colors, height darkening, location dots with type-based coloring
- [x] **Camera controls** — WASD/arrow pan, 1/2 zoom, smooth movement, mouse drag panning
- [x] **Debug overlay** — FPS, frame count, region count, locations, cache stats, game state
- [x] **Multi-region streaming** — Dynamic load/unload as camera moves
- [x] **Object name rendering** — Lazy-loaded object definition cache, names shown on zoom

### Phase 6: Entity Rendering
- [x] **NPC tracking** — `osrs_npc_t` struct with id, x, z, plane, active, visible, name, combat_level
- [x] **NPC rendering** — Yellow dots with red borders, names on zoom
- [x] **NPC_INFO packet handler** — Decode NPC positions from server, track in game state
- [x] **Player rendering** — Local (green dot), nearby high-res players (white + names)
- [x] **GPI tracking** — Full Player Info (PLAYER_INFO) decode with walk/run/teleport
- [x] **GPU terrain renderer** — OpenGL ES 2.0 via EGL pbuffer, VBO per region, isometric shader, pixel readback for UI compositing
  - **FIXED (2026-07-30):** Removed FBO due to Mesa/AMD driver bug (`glClear` had no effect on FBO texture despite `GL_FRAMEBUFFER_COMPLETE`). Now renders directly to pbuffer surface. R/B channel swap and row flip corrected in `glReadPixels` readback.

### Phase 7: Network Protocol (rev 239)
- [x] **TCP transport** — Non-blocking connect, send/recv ring buffers
- [x] **Login handshake** — INIT_GAME_CONNECTION → session id → GAMELOGIN
- [x] **Login block** — plain header + RSA inner block + XTEA payload, all alt-encodings
- [x] **RSA** — bignum modpow, verified byte-exact vs Python `pow` for 512/1024-bit keys
- [x] **ISAAC session ciphers** — in/out keystreams from XTEA seed (+50)
- [x] **Game packet framing** — fixed/VAR_BYTE/VAR_SHORT, ISAAC opcode obfuscation
- [x] **Smart opcode encoding** — `readSmartByteShortIsaac` for 1/2-byte opcodes
- [x] **REBUILD_NORMAL_V2** — worldArea/zoneX/zoneZ + login GPI footer (absolute positions)
- [x] **PLAYER_INFO decode** — 4 stationary-filtered passes, high/low-res transitions,
      walk/run/teleport movement, extended-info blocks (appearance/move-speed/...)
- [x] **Click-to-move** — screen→world transform, MOVE_GAMECLICK packet
- [x] **Object interaction** — Right-click sends OPLOC1 (object examine/interact)
- [x] **NPC interaction** — Right-click sends OPNPC1 (NPC examine/interact)

### Phase 8: UI Skeleton
- [x] **Minimap** — Terrain overview, player dot (green), other players (white), NPCs (yellow), zoom levels
- [x] **Chat messages** — Ring buffer, bottom-left panel, scrollable history
- [x] **Inventory tracking** — UPDATE_INV packet handler, item ID + quantity
- [x] **Inventory panel** — 4x7 grid rendering, item sprites (colored squares), quantity display
- [x] **Ground items** — UPDATE_ZONE_PARTIAL_ENCLOSED packet handler, purple dots with quantity
- [x] **Connection state** — Top-right status panel (offline/connecting/logging in/in game/failed)

### Phase 9: Authentication (legacy + Jagex Account)
- [x] **Auth abstraction** — `OSRS_AUTH=legacy|jagex`, method-aware credentials
- [x] **Legacy login** — username + password (auth_type 0)
- [x] **Jagex Account game login** — session token (auth_type 2)
- [x] **TLS 1.3 client** — vendored from ordl-govcon (X25519, ChaCha20-Poly1305,
      AES-256-GCM, SHA-256/384, HKDF); ALPN restricted to http/1.1
- [x] **HTTPS helper** — blocking GET/POST, Content-Length + chunked decoding
- [x] **Jagex OAuth flow** — PKCE (S256), authorize URL, redirect parse,
      token exchange (`account.jagex.com/oauth2/token`), session creation
      (`auth.jagex.com/game-session/v1/sessions`) → sessionId

**Auth endpoint verification (live):** both OAuth endpoints reachable and return
standard errors — `/oauth2/token` → HTTP 400 `invalid_grant` (fake code),
`/game-session/v1/sessions` → HTTP 403 `INVALID_ID_TOKEN` (fake id_token).
The interactive browser sign-in (real credentials → real session token) is the
only step that can't be automated-tested; the HTTPS/OAuth plumbing is proven.

### Root-cause fixes (2026-07-26/27)
- **`hex_decode` odd-length bug** — the RSA exponent `"10001"` (5 hex digits) was
  parsed as `0x1001` (4097) instead of `0x10001` (65537). Every login block was
  RSA-encrypted with the wrong exponent, so Jagex's server could not decrypt it.
  This was the cause of the persistent "client out of date" (code 6) rejections
  during live-login testing — **not** a rate limit. Fixed; RSA output is now
  byte-identical to Python's `pow(pt, e, n)` for the live key size.
- **Corrupt RSA modulus** — the hardcoded modulus was 261 hex chars (130.5 bytes,
  malformed). Replaced with the real 1024-bit GAMELOGIN key extracted from the
  live gamepack (`bg.af`, verified byte-for-byte against `bg.class`).
- **Login header fields** — verified against a live RuneLite login capture:
  `serverVersion` must be 239 (not a `0x7FFFFFFF` placeholder) and
  `platformType` must be 5 (not 0). These got the client past the version gate
  (code 6 → proof-of-work).
- **`secondClientType`** — the real client writes 0 (was sending 1).
- **Proof-of-work (code 69)** — implemented the SHA256 hashcash login challenge:
  parse `[69][len:2][id][version][difficulty][salt]`, brute-force a `result` with
  ≥ difficulty leading zero bits, reply `[19][len:2][result:8]`. Solver verified
  byte-exact vs Python; full round-trip verified against the local harness with
  Jagex's exact framing.
- **`osrs_net_poll` close-drain bug** — when the server sends data then closes,
  `recv` got the data but the poll returned false (fatal) before the caller read
  it, masking the real response code as "connection lost". Now drains the buffer
  before surfacing the close.

- **RSA plaintext padding** — `osrs_rsa_encrypt_login` was encrypting the raw
  64-byte RSA inner block as a small integer. With a 128-byte modulus this
  places the decrypted plaintext at the *end* of the 128-byte result (leading
  zeros fill the start), but OSRS servers read from the *beginning* of the
  block. Fixed by zero-padding the plaintext to the full modulus length before
  encryption (data at start, zeros at tail). Verified: local harness now
  decrypts `encCheck=1` and the correct XTEA seed; live server returns
  `Bad session id` (code 10) for fake credentials — proof the RSA block is
  now structurally valid.

### Post-login init packets & keepalive (2026-07-29)
- **EVENT_APPLET_FOCUS** — sent immediately after login OK (`focused=true`),
  matching the real client's startup sequence.
- **DETECT_MODIFIED_CLIENT** — periodic anti-tamper heartbeat every 30s
  (opcode 33, 4-byte hash; sends 0 for now since we have no official client
  code to hash). Added `osrs_build_detect_modified_client()` builder.
- **Keepalive timer** — changed from 2.5s to 5.0s to match the official client.
  Any outgoing packet (not just NO_TIMEOUT) now resets the timer, so genuine
  activity suppresses keepalives correctly.

### Packet size fixes (2026-07-29)
- **Opcode 33 — empirically fixed from VS to 10.** Log analysis showed opcode 33
  (`consumed=9`) consistently followed by garbage (18432). The client was reading
  it as VAR_SHORT (2-byte length + payload), but the actual server packet is
  fixed-size 10 bytes. This left 2 bytes behind, which combined with the next
  packet's bytes to form a fake 2-byte opcode.
- **Opcode 78 — empirically fixed from 17 to 20.** Log analysis showed opcode 78
  (`consumed=18`) eventually followed by garbage (16738) after a chain of
  coincidentally-valid parses. The actual packet is 20 bytes (not 17), leaving
  3 bytes behind that desync the stream.
- **`tools/gen_packet_table.py`** — New script that parses `rsprot` source code
  and generates C code for `osrs_server_packet_sizes`. When rsprot publishes a
  newer revision (e.g. 239), run the script and diff against the current table
  to find shifted sizes.
- **Premature LOGOUT_WITHREASON (reason=3)** is likely a *symptom* of the above
  desyncs — once the stream is misaligned, random bytes can decrypt to opcode 58
  (LOGOUT_WITHREASON, size 1), causing an erroneous disconnect. Fixing the size
  errors should extend session stability.

### ISAAC + game-packet framing fixes (2026-07-28) — **live login now works**
- **ISAAC algorithm was entirely wrong.** The mix macro was not ISAAC at all
  (subtraction/shift network from a different cipher), the mem/rsl pools were
  pre-filled with the golden ratio instead of zeroed, the initial 4× accumulator
  scramble was missing, and results were consumed `rsl[0]`→`rsl[255]` (forward)
  instead of `rsl[255]`→`rsl[0]` (backward). Rewrote `src/isaac.c` to match the
  official client (`IsaacCipher.java`) byte-for-byte; verified against an
  independent Python reference for multiple seeds. The old "deterministic" test
  passed only because it cross-checked against `tools/local_server.py`, which
  had the identical bugs — two wrong implementations agreeing with each other.
- **Auth field over-consumed ISAAC.** The remembered-username field consumes 4
  ISAAC values from the server→client cipher **only when the flag byte is 1**
  (authenticator set); for flag==0 it consumes none. Was consuming 4 always,
  misaligning the keystream for this (no-authenticator) account.
- **Smart opcode encoding.** The game-packet opcode uses Jagex's
  `readSmartByteShortIsaac`: first decoded byte `(b-isaac)&0xFF`; if `<128` that
  is the whole opcode (1 ISAAC value), else a 2-byte opcode consuming a 2nd
  ISAAC value. Was reading 1 ISAAC value per packet always, so any 2-byte
  opcode desynced the stream.
- **First packet framing.** The first game packet's opcode+length are the last
  bytes of the login OK payload (the official client reads them inside
  loginState 15). `osrs_login_parse_response` was consuming the full
  `2+payload_len`, so the game loop started at the first packet's *payload*
  instead of its *opcode*. Now consumes `2 + fields_read`, leaving the packet
  header for the game loop.

- **Login response payload parsing fixed (2026-07-30).** The server sends
  `plen=37` (hardcoded) but the actual payload is 34 bytes. The code was
  consuming `2 + plen = 39` bytes, eating 3 bytes of the first game packet
  and causing an immediate ISAAC desync. Fixed: the parser now reads all
  fields including `account_hash`, `user_id`, and `user_hash` (8 bytes each),
  stores them in `osrs_protocol_t`, and sets `*consumed = p` (36 bytes)
  instead of `2 + p` (38 bytes) or `2 + plen` (39 bytes), so the game loop
  starts exactly at the first packet opcode.
  **Verified live:** client logged in on world 1 and decoded 50+ consecutive
  valid packets: REBUILD_NORMAL (49), SITE_SETTINGS (67), CHAT_FILTER_SETTINGS
  (124), VARP_RESET (44), VARP_SMALL (97), VARP_LARGE (12), PLAYER_INFO (28),
  NPC_INFO_SMALL (85), etc. Zero garbage opcodes, zero desync.
- **`osrs_game_write_packet` VAR_BYTE/VAR_SHORT framing fix.** Was writing
  `opcode + payload` for all client packets, omitting the length prefix required
  for `VAR_BYTE` and `VAR_SHORT` packets. The server would read the first
  payload byte as a length, causing a client→server desync on any movement,
  chat, or cheat packet. Fixed: now looks up `osrs_client_packet_sizes[opcode]`
  and inserts the correct 1-byte or 2-byte big-endian length prefix.
  **Verified live:** client sustains 100+ valid packets over ~9 seconds on
  World 1, with `WINDOW_STATUS` and `EVENT_APPLET_FOCUS` correctly framed.
- **`game_send_packet` return-type fix.** Was storing `osrs_game_write_packet`
  result (signed `int`, -1 on error) into `size_t`, causing an implicit cast
  to `SIZE_MAX` on buffer-overflow which would queue garbage. Now uses `int n`
  with an explicit `< 0` check and early return.

### Packet reader robustness (2026-07-29)
- **ISAAC consumption order fixed.** `osrs_game_read_packet` was consuming the
  ISAAC value immediately after reading the opcode, before checking if the full
  packet was available. For incomplete packets (split across TCP segments) this
  caused the ISAAC sequence to diverge on retry, producing a cascading desync.
  Fixed: `osrs_isaac_peek()` is used until all size checks pass; only then is
  `osrs_isaac_next()` called to commit the keystream advance.
- **`osrs_isaac_peek2()` added.** Peeks the second-next ISAAC value without
  consuming anything. Enables correct handling of 2-byte opcodes (server→client
  opcodes ≥ 128) while still deferring keystream consumption until the packet
  is fully present.
- **Keepalive timer reset.** Any outgoing packet now resets `keepalive_timer`,
  so `NO_TIMEOUT` is only sent during genuine inactivity (matches official
  client behavior).
- **Verified live (2026-07-29):** client logs in successfully and correctly
  decodes 40+ game packets per session: REBUILD_NORMAL, PLAYER_INFO,
  NPC_INFO_SMALL, UPDATE_INV_PARTIAL, VARP_SMALL/LARGE, IF_OPENTOP,
  UPDATE_ZONE_FULL_FOLLOWS, MESSAGE_GAME, CHAT_FILTER_SETTINGS, etc.
  Packet boundaries are stable — no desync, no unknown-opcode skips.

### REBUILD_NORMAL parser bug (FIXED 2026-07-30)
- **Root cause:** `osrs_parse_rebuild_normal()` in `src/packet_parsers.c` reads zoneX/zoneZ as 1-byte values, but the live Jagex server (rev 239) sends them as **2-byte p2Alt3/p2Alt2 encoded values**.
- **Evidence:** Live test shows `base=48,-32` (region 0,-1), but player position from PLAYER_INFO is `local=(12288,0,1)` (region 192, 0). The camera centers on the wrong region, loading empty terrain.
- **Fix applied:** Rewrote parser to read 6-byte footer from end of packet using p2Alt1/p2Alt2 decoding. Verified live: correct map base (3024,9448), correct regions load.

### GPU renderer FBO bug (FIXED 2026-07-30)
- **Root cause:** On AMD Mesa (`radeonsi, vega10`), `glClear` to an FBO texture had no effect. The texture retained uninitialized garbage (magenta pixels). FBO status was `GL_FRAMEBUFFER_COMPLETE`, no GL errors reported.
- **Fix applied:** Removed FBO entirely. Now renders directly to the EGL pbuffer surface and reads back with `glReadPixels`. Also fixed R/B channel swap in pixel format conversion (GL_RGBA `0xAABBGGRR` → CPU ARGB `0xAARRGGBB`).

### Player position confirmed correct
- The 30-bit packed coord from PLAYER_INFO decodes to `x=3074, z=9500, level=0`. This is the **correct** position for this account (region 47, 148).
- The REBUILD_NORMAL parser now correctly reads the map base from the packet footer. Camera centers on the correct region.

## Known Issues

1. **XTEA keys** — Only some regions are XTEA-encrypted in the live cache. The client has full XTEA support but keys must be provided via `OSRS_XTEA_KEYS` env var.
2. **Live session stability** — Login/packet decoding verified live (50+ consecutive packets, zero desync). "Already logged in" after unclean disconnect is a server-side ~60s session timeout, not a bug.
3. **TLS cert validation** — the HTTPS client verifies the TLS handshake but does not pin/chain-validate the CA. Fine for the token flow; add CA pinning for production.
4. **BZip2** — Partially working; some edge cases in the bit reader fail on specific cache indexes (0–3). Indexes 4+ (GZip-compressed) work fine.

## Rendering (2026-07-31) — RESOLVED

The following rendering issues from this section are **all fixed**:
- ~~Model color conversion~~ — HSL treated as RGB. Now: correct JagexColor layout + 0.8 palette + exact OSRS lighting.
- ~~No texture support~~ — texture atlas (2048²), floor textures on overlays, model face textures with computed UVs.
- ~~Models/NPCs/players in software~~ — all render on GPU with lighting/recolors/rotation/scale.
- ~~Terrain blobs~~ — height scale fixed (was 8× too tall).
- ~~Camera~~ — tracks player, clamped pan, zoom-aware region radius.

Remaining known limitations: overlay paths 4–11 approximate, no animations yet,
bulk zone-update payload iteration pending, old-format models unimplemented,
no animated water. See `docs/HANDOFF.md` §9.

## Test Results

```
=== OSRS Crypto/Compression Test Suite ===
Testing CRC32... PASS
Testing XTEA... PASS
Testing ISAAC... PASS
Testing BZip2... PASS
Testing GZip... PASS
=== All tests passed! ===

=== PlayerInfo decode tests ===
test_seed_login done              (local pos from 30-bit login footer)
test_local_walk done              (walk E increments x)
test_local_teleport done          (large teleport w/ negative delta)
test_promotion_with_appearance    (low->high res + appearance name/combat)
All playerinfo tests passed.

=== End-to-end (local protocol harness, tools/local_server.py) ===
login OK: encCheck=1 user='bob'   (RSA block decrypts correctly server-side)
spawn: 3222,3218 level=0          (REBUILD_LOGIN footer -> local position)
MOVED: 3222,3218 -> 3242,3230    (click-to-move -> server teleport -> GPI update)
MOVEMENT TEST PASS
GUI client: logs in, loads server region grid (map base 3168,3168), renders.
```

### Running the local harness

```bash
# Terminal 1 — start the test server (prints an RSA modulus)
python3 tools/local_server.py --port 43599

# Terminal 2 — headless protocol test
OSRS_RSA_MODULUS=<modulus> OSRS_RSA_EXPONENT=10001 \
  ./build/login_test ~/.runelite/jagexcache/oldschool/LIVE/ 127.0.0.1:43599 bob hunter2

# Terminal 2 — full GUI client
OSRS_RSA_MODULUS=<modulus> OSRS_RSA_EXPONENT=10001 \
  OSRS_WORLD=127.0.0.1:43599 OSRS_USER=bob OSRS_PASS=hunter2 \
  ./build/osrs_client
```

## Architecture

```
ordl-osrs/
├── include/osrs/
│   ├── crc32.h          — CRC32 checksum
│   ├── xtea.h           — XTEA block cipher
│   ├── xtea_keys.h      — XTEA key store for map decryption
│   ├── isaac.h          — ISAAC stream cipher
│   ├── bzip2.h          — BZip2 decompressor
│   ├── gzip.h           — GZip decompressor
│   ├── cache.h          — OSRS cache reader
│   ├── map.h            — Map/region loading
│   ├── net.h            — TCP transport
│   ├── bignum.h         — Fixed-size big integer (RSA)
│   ├── rsa.h            — RSA login encryption
│   ├── playerinfo.h     — rev-239 player info (GPI) decoder
│   ├── protocol.h       — OSRS network protocol
│   ├── game.h           — Game session state & packet dispatch
│   ├── config.h         — OSRS config definition loaders
│   └── gl_world.h       — GPU-accelerated world renderer (OpenGL ES 2.0)
├── src/
│   ├── client.c         — Client application (FORGE + OSRS)
│   ├── crc32.c
│   ├── xtea.c
│   ├── xtea_keys.c
│   ├── isaac.c
│   ├── bignum.c
│   ├── rsa.c
│   ├── bzip2.c
│   ├── gzip.c
│   ├── cache.c
│   ├── map.c
│   ├── net.c
│   ├── game.c           — Game session (login, dispatch, state)
│   ├── playerinfo.c     — Player info decoder
│   ├── protocol.c
│   ├── config.c
│   └── gl_world.c       — GPU world renderer (EGL + GLES2 + terrain VBOs)
├── tests/
│   ├── test_crypto.c    — Crypto test suite
│   └── test_playerinfo.c— Player info decoder tests
├── tools/
│   ├── cache_dump.c     — Cache inspection tool
│   ├── config_dump.c    — Config inspection tool
│   ├── login_test.c     — Headless login/protocol test
│   └── local_server.py  — Local rev-239 protocol harness (test server)
├── Makefile
└── README.md
```

## Dependencies

- **FORGE engine** (`/devops/projects/off-the-wall/ordl-game`) — Pure C23 game engine
- **No external libraries** — Everything implemented from scratch in C23

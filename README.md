# ORDL OSRS Client — Pure C23, From Scratch

An OldSchool RuneScape client written in pure C23 with zero external dependencies,
built on top of the FORGE game engine.

## Architecture

```
+-------------------------------------------------------------+
|                    OSRS Client Application                   |
+-------------------------------------------------------------+
|  Game Logic  |  Scene Graph  |  Entity Manager  |  UI/HUD   |
+-------------------------------------------------------------+
|  OSRS Protocol Handler  |  Login/Game Packets  |  ISAAC     |
+-------------------------------------------------------------+
|  OSRS Cache Reader  |  BZip2/GZip  |  XTEA  |  CRC32       |
+-------------------------------------------------------------+
|  FORGE Engine (Network, Renderer, Platform, UI, Memory)     |
+-------------------------------------------------------------+
```

## Features

### Foundation (Complete)
- [x] **CRC32** — table-driven, verified with test vectors
- [x] **XTEA** — 128-bit key block cipher, encrypt/decrypt verified
- [x] **ISAAC** — stream cipher, verified byte-exact against official client
- [x] **BZip2** — full decompression with multi-block support
- [x] **GZip** — full DEFLATE (LZ77 + Huffman)

### Cache System (Complete)
- [x] Flatfile cache reader (`.dat2` + `.idx*`)
- [x] Sector-based data file (520-byte sectors)
- [x] Container decompression (BZip2 + GZip + XTEA)
- [x] Archive file splitting (multi-chunk extraction)
- [x] All 22 indexes load from live OSRS cache

### Config Loading (Complete)
- [x] Object definitions — 62,194 objects parsed
- [x] NPC definitions — correct opcodes, verified
- [x] Item definitions — correct opcodes, verified
- [x] Underlay/overlay definitions — terrain colors
- [x] Sequence/SpotAnim definitions — animation config

### Map/World Loading (Complete)
- [x] Terrain loading — height, underlay, overlay with real colors
- [x] Location loading — objects with type/orientation/position
- [x] Multi-region streaming — 3x3 grid around camera
- [x] XTEA key support for encrypted regions

### Client Application (Complete)
- [x] FORGE integration (window, renderer, input)
- [x] Top-down map renderer with terrain colors + height darkening
- [x] Camera pan (WASD/arrows) and zoom (1/2 keys)
- [x] Mouse drag panning
- [x] Object name rendering (lazy-loaded definition cache)
- [x] Debug overlay (FPS, regions, locations, game state)

### Entity Rendering (Complete)
- [x] NPC tracking + rendering (yellow dots with red borders, names)
- [x] Player rendering (local green, others white with names)
- [x] GPI (Player Info) decode with walk/run/teleport movement
- [x] Ground item tracking + rendering (purple dots)

### UI Skeleton (Complete)
- [x] Minimap — terrain overview, player/NPC/player dots, zoom
- [x] Chat messages — ring buffer, bottom-left panel
- [x] Inventory — UPDATE_INV tracking, 4x7 grid rendering
- [x] Connection state — top-right status panel

### Network Protocol (rev 239 — validated end-to-end)
- [x] TCP transport (non-blocking connect, ring buffers)
- [x] Login handshake (INIT → session id → GAMELOGIN → PoW → OK)
- [x] RSA + XTEA login block (byte-exact vs Python for live key size)
- [x] ISAAC session ciphers (in/out keystreams)
- [x] Smart opcode encoding (1/2-byte opcodes)
- [x] Game packet framing + opcode dispatch
- [x] REBUILD_NORMAL_V2 + player-info (GPI) decode
- [x] Click-to-move movement
- [x] Object interaction (OPLOC1)
- [x] NPC interaction (OPNPC1)
- [x] Local protocol harness for end-to-end testing

### Authentication (Complete)
- [x] Legacy login (username + password)
- [x] Jagex Account login (session token + OAuth flow)
- [x] TLS 1.3 client (X25519, ChaCha20-Poly1305, AES-256-GCM)
- [x] HTTPS helper with chunked encoding support

## Build

```bash
make
```

## Run

```bash
# Auto-detects cache at ~/.runelite/jagexcache/oldschool/LIVE/
./build/osrs_client

# Or specify cache path
./build/osrs_client /path/to/cache/

# With XTEA keys for encrypted regions
OSRS_XTEA_KEYS=/path/to/keys.bin ./build/osrs_client

# Online mode (connect + login + play)
OSRS_WORLD=<host> OSRS_USER=<name> OSRS_PASS=<pass> ./build/osrs_client
```

## Authentication

Two login methods are supported via `OSRS_AUTH`:

**Legacy (username + password)** — the default:
```bash
OSRS_AUTH=legacy OSRS_WORLD=<host> OSRS_USER=<name> OSRS_PASS=<pass> \
  ./build/osrs_client
```

**Jagex Account (session token)** — for accounts on the newer Jagex Account
system. Supply a session token directly:
```bash
OSRS_AUTH=jagex OSRS_WORLD=<host> OSRS_USER=<email> \
  OSRS_JAGEX_TOKEN=<session-token> ./build/osrs_client
```

Or let the client fetch a token via the Jagex OAuth flow (opens a sign-in URL,
you paste back the redirect):
```bash
OSRS_AUTH=jagex OSRS_WORLD=<host> OSRS_USER=<email> ./build/osrs_client
```

The OAuth flow uses PKCE against `account.jagex.com` and creates a game session
via `auth.jagex.com/game-session/v1/sessions`. Headless testing:
```bash
# jagex test (offline URL/PKCE checks + live endpoint error responses)
./build/test_jagex
```

## Local protocol harness

Test the full stack (login → map rebuild → player info → movement) without
Jagex, using the bundled rev-239 test server:

```bash
# Terminal 1
python3 tools/local_server.py --port 43599     # prints an RSA modulus

# Terminal 2 — GUI client
OSRS_RSA_MODULUS=<modulus> OSRS_RSA_EXPONENT=10001 \
  OSRS_WORLD=127.0.0.1:43599 OSRS_USER=bob OSRS_PASS=hunter2 ./build/osrs_client

# or headless protocol check
OSRS_RSA_MODULUS=<modulus> OSRS_RSA_EXPONENT=10001 \
  ./build/login_test ~/.runelite/jagexcache/oldschool/LIVE/ 127.0.0.1:43599 bob hunter2
```

## Controls

| Key | Action |
|-----|--------|
| W/A/S/D | Pan camera |
| Arrows | Pan camera |
| 1 | Zoom in |
| 2 | Zoom out |
| Mouse drag | Pan camera |
| Left-click | Walk to tile |
| Right-click | Interact with object/NPC |
| ESC | Quit |

## Tools

```bash
# Inspect cache indexes and archives
./build/cache_dump ~/.runelite/jagexcache/oldschool/LIVE/

# Dump config definitions
./build/config_dump ~/.runelite/jagexcache/oldschool/LIVE/ object 1
./build/config_dump ~/.runelite/jagexcache/oldschool/LIVE/ npc 1
./build/config_dump ~/.runelite/jagexcache/oldschool/LIVE/ item 995

# Run crypto/compression test suite
make test
```

## Dependencies

- FORGE engine (`/devops/projects/off-the-wall/ordl-game`) — pure C23, zero deps
- No external libraries

## References

- RuneLite cache module (Java): `/devops/ordl/applications/gaming/ordl-swapper/runelite/cache/`
- FORGE engine: `/devops/projects/off-the-wall/ordl-game/`

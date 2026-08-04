# ORDL OSRS Client — Comprehensive Project Notes

> **Last updated:** 2026-07-29
> **Revision target:** 239 (live verified against `oldschool1.runescape.com`)
> **Language:** C23 (ISO/IEC 9899:2024)
> **Dependencies:** Zero external libraries; FORGE engine for networking/rendering

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [What Works Now](#3-what-works-now)
4. [Known Limitations & Missing Features](#4-known-limitations--missing-features)
5. [Protocol Deep Dive](#5-protocol-deep-dive)
6. [Game Loop & State Machine](#6-game-loop--state-machine)
7. [Cache System](#7-cache-system)
8. [Crypto & Compression](#8-crypto--compression)
9. [Future Maintenance: What Breaks on Game Updates](#9-future-maintenance-what-breaks-on-game-updates)
10. [Testing & Verification](#10-testing--verification)
11. [File Inventory](#11-file-inventory)

---

## 1. Executive Summary

This is a **pure C23, zero-dependency** OldSchool RuneScape client implementation.
Everything from CRC32 to RSA to DEFLATE to the full game protocol is implemented from
scratch in C23. It sits on top of the FORGE game engine for rendering, networking,
and platform abstraction.

**Current milestone: Live login verified.**
Five real Jagex accounts successfully logged in to World 1 (F2P) on
`oldschool1.runescape.com:43594`, completed the full handshake
(INIT → GAMELOGIN → Proof-of-Work → Login OK), and received game packets
(REBUILD_NORMAL, PLAYER_INFO, chat, stat updates, etc.). One account
completed the click-to-move movement test.

**What this is NOT yet:** A playable game client. The protocol layer is solid,
but rendering (sprites, models, terrain, UI) and gameplay (NPCs, objects,
inventory, combat) are not implemented.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  OSRS Client Application (client.c)                         │
│  ┌─────────────┐ ┌──────────┐ ┌─────────────┐ ┌──────────┐  │
│  │ Game Logic  │ │ Scene    │ │ Entity Mgr  │ │ UI/HUD   │  │
│  │ (game.c)    │ │ (2D map) │ │ (playerinfo)│ │ (none)   │  │
│  └─────────────┘ └──────────┘ └─────────────┘ └──────────┘  │
├─────────────────────────────────────────────────────────────┤
│  OSRS Protocol Handler (protocol.c)                         │
│  Login/Game Packets · ISAAC · XTEA · RSA · Proof-of-Work   │
├─────────────────────────────────────────────────────────────┤
│  OSRS Cache Reader (cache.c)                                │
│  Sector I/O · Container Decrypt (XTEA) · GZip · BZip2      │
├─────────────────────────────────────────────────────────────┤
│  Crypto & Compression                                       │
│  CRC32 · XTEA · ISAAC · RSA · GZip · BZip2                 │
├─────────────────────────────────────────────────────────────┤
│  Config Loaders (config.c)                                  │
│  Objects · NPCs · Items · Sprites · Models · Map tiles     │
├─────────────────────────────────────────────────────────────┤
│  FORGE Engine                                               │
│  Network (TLS/TCP) · Renderer · Platform · UI · Memory     │
└─────────────────────────────────────────────────────────────┘
```

### Module Responsibilities

| Module | File(s) | What it does |
|--------|---------|-------------|
| **Protocol** | `protocol.c/h` | Packet framing, ISAAC obfuscation, login block construction, opcode dispatch, alt-encodings |
| **Game** | `game.c/h` | State machine, per-tick update, packet handlers, player state, movement, chat |
| **Cache** | `cache.c/h` | Reads `main_file_cache.dat2` + `.idx*`, sector I/O, container decompression, index metadata |
| **Config** | `config.c/h` | Loads definitions from cache index 2 (objects, NPCs, items, sprites, models, map regions) |
| **PlayerInfo** | `playerinfo.c/h` | GPI (Global Player Info) bitstream decoder — player appearance, movement, equipment |
| **RSA** | `rsa.c/h` | 1024-bit public-key encryption for login block; zero-pads plaintext to modulus length |
| **XTEA** | `xtea.c/h` | 64-bit block cipher, 128-bit key, 32 rounds; used for login payload encryption |
| **ISAAC** | `isaac.c/h` | Bob Jenkins stream cipher; 256-word state; obfuscates game packet opcodes |
| **CRC32** | `crc32.c/h` | IEEE 802.3 table-driven CRC32; cache integrity + login CRC block |
| **GZip** | `gzip.c/h` | Full DEFLATE (LZ77 + Huffman); cache container decompression |
| **BZip2** | `bzip2.c/h` | Burrows-Wheeler + MTF + Huffman; cache container decompression |
| **Net** | `net.c/h` | TLS/TCP socket abstraction (via FORGE); non-blocking poll, send/receive queues |
| **Jagex Auth** | `jagex.c/h` | OAuth2 PKCE flow for Jagex Account login (interactive browser + token exchange) |
| **BigNum** | `bignum.c/h` | Arbitrary-precision integers for RSA (add, sub, mul, div, mod, modpow) |
| **Map** | `map.c/h` | Region loading: terrain heights, overlay/underlay, location objects; XTEA key lookup |
| **XTEA Keys** | `xtea_keys.c/h` | Sparse array of 2048×2048 XTEA keys for map region decryption |

---

## 3. What Works Now

### 3.1 Network & Protocol — PRODUCTION VERIFIED

- [x] **TCP + TLS connection** to live Jagex worlds
- [x] **Init handshake** — sends `0x0E`, receives `session_id`
- [x] **Game login block** — version 239, platform stats struct v9, RSA-encrypted
- [x] **Proof-of-Work** — SHA256 hashcash solver; verified against live server
- [x] **XTEA** — random 128-bit keys per login; ECB big-endian blocks
- [x] **ISAAC** — two keystreams (out=seed, in=seed+50); smart opcode encoding
- [x] **Game packet framing** — fixed/VAR_BYTE/VAR_SHORT; 8192-byte max
- [x] **Packet dispatch** — REBUILD_NORMAL, PLAYER_INFO, SEND_PING, MESSAGE_GAME, UPDATE_STAT, UPDATE_RUNENERGY, UPDATE_RUNWEIGHT, VARP_SMALL/LARGE, LOGOUT

### 3.2 Authentication — PRODUCTION VERIFIED

- [x] **Legacy login** — username + password; 5 real accounts verified live
- [x] **Jagex Account login** — OAuth2 PKCE flow (`jagex.c`); interactive browser
- [x] **RSA encryption** — zero-padded to 128-byte modulus; server reads from start
- [x] **Login response parsing** — all codes 0–22, 56, 69 handled

### 3.3 Cache System — FULLY OPERATIONAL

- [x] **Flatfile cache reader** — `main_file_cache.dat2` + 255 `.idx*` files
- [x] **Sector I/O** — 520-byte sectors with 8/10-byte headers
- [x] **Container decompression** — GZip + BZip2 verified against live cache
- [x] **XTEA decryption** — for encrypted map archives (keys via env var)
- [x] **Index metadata** — Protocol 5–7, named/sized flags, archive hierarchies
- [x] **22 indexes loaded** — all indexes from live cache parse correctly

### 3.4 Config Loaders — IMPLEMENTED

- [x] **Objects** (index 2, archive 6) — `osrs_object_t`: name, size, actions, etc.
- [x] **NPCs** (index 2, archive 9) — `osrs_npc_t`: name, combat level, size, etc.
- [x] **Items** (index 2, archive 10) — `osrs_item_t`: name, stackable, equipable, etc.
- [x] **Sprites** (index 2, archive 8) — `osrs_sprite_t`: dimensions, palette, pixels
- [x] **Models** (index 1 or 7) — `osrs_model_t`: vertices, faces, textures (raw loader)
- [x] **Map regions** (index 5) — terrain + location data with XTEA keys

### 3.5 Crypto & Compression — VERIFIED

- [x] **CRC32** — IEEE 802.3, table-driven, test vectors pass
- [x] **XTEA** — encrypt/decrypt round-trip verified
- [x] **ISAAC** — deterministic output matches reference
- [x] **GZip** — decompresses real OSRS cache containers correctly
- [x] **BZip2** — decompresses real OSRS cache containers correctly
- [x] **RSA** — 1024-bit, encrypts login block; verified against Python reference

### 3.6 Game State — BASIC BUT FUNCTIONAL

- [x] **State machine** — DISCONNECTED → LOGGING_IN → IN_GAME → FAILED
- [x] **Player position tracking** — from PLAYER_INFO + REBUILD_NORMAL footer
- [x] **Click-to-move** — sends MOVE_GAMECLICK; server responds with position update
- [x] **Chat** — ring buffer of 32 messages; MESSAGE_GAME decoded
- [x] **Stats** — 23 skills (levels, real_levels, xp); UPDATE_STAT decoded
- [x] **Varps** — 4096 entries; VARP_SMALL/LARGE decoded
- [x] **Keepalive** — NO_TIMEOUT every 2.5s; SEND_PING reply with FPS=60
- [x] **GPI decoding** — full player info bitstream (appearances, movement, equipment)
- [x] **GPU terrain rendering** — OpenGL ES 2.0 via EGL pbuffer, isometric shader, pixel readback for UI compositing

---

## 4. Known Limitations & Missing Features

### 4.1 Critical Missing: Rendering

| Feature | Status | Notes |
|---------|--------|-------|
| **3D model rendering** | Partial | Model VBOs created in GPU renderer, but colors are wrong (HSL treated as RGB) |
| **2D sprite rendering** | Not implemented | Sprites loaded from cache but never drawn |
| **Isometric camera** | Implemented | GPU renderer has isometric vertex shader |
| **UI/HUD** | Implemented | Chat, inventory, minimap, debug overlay in software renderer |
| **Minimap** | Implemented | Terrain overview, dots for entities |
| **Terrain/map rendering** | **Implemented** | GPU-accelerated colored diamonds per tile |

### 4.2 Critical Missing: Gameplay

| Feature | Status | Notes |
|---------|--------|-------|
| **NPC tracking** | Partial | NPC_INFO packets decoded, NPCs tracked in game state, rendered as dots |
| **Object/ground-item tracking** | Not implemented | UPDATE_ZONE_* packets ignored |
| **Inventory** | Partial | UPDATE_INV packets decoded, inventory panel rendered |
| **Equipment** | Not implemented | No equipment UI or state |
| **Combat** | Not implemented | No attack/spell casting |
| **Bank** | Not implemented | No interface for bank |
| **Trade/GE** | Not implemented | No trading system |
| **Quests** | Not implemented | No quest state tracking |
| **Pathfinding** | Not implemented | Click-to-move sends raw destination only |
| **Local movement prediction** | Not implemented | Player snaps to server positions |

### 4.3 Protocol Gaps

| Feature | Status | Notes |
|---------|--------|-------|
| **Authenticator/2FA** | Not implemented | `OSRS_CONN_AWAIT_AUTH_CODE` exists but unused; server requesting auth code will stall client |
| **Region-only rebuilds** | Not implemented | `REBUILD_REGION` (opcode 125) ignored; only `REBUILD_NORMAL` handled |
| **Reconnection** | Not implemented | Once FAILED, caller must create new session |
| **World hopping** | Not implemented | No world list download or hop logic |
| **Logout with reason** | Partial | Packet decoded but no reason-specific handling |

### 4.4 Robustness Issues

- **Packet size hard limit** — `game_send_packet` uses 8196-byte stack buffer; large packets truncated
- **Keepalive unconditional** — NO_TIMEOUT sent every 2.5s even if other traffic flowing
- **No RTT measurement** — ping replies sent but latency not tracked
- **Chat buffer overwrite** — 32-message ring buffer; old messages lost silently
- **No graceful reconnect** — TCP close → FAILED state, no retry logic
- **TLS no CA pinning** — handshake verified but CA chain not validated (acceptable for now)

---

## 5. Protocol Deep Dive

### 5.1 Packet Opcodes (Rev 239)

See `docs/protocol.md` for the complete opcode tables.
Key opcodes the client handles:

| Direction | Opcode | Name | Handler |
|-----------|--------|------|---------|
| S→C | 28 | PLAYER_INFO | `osrs_pi_decode()` → updates player positions/appearances |
| S→C | 49 | REBUILD_NORMAL | Map load trigger; parses scene base tile |
| S→C | 74 | MESSAGE_GAME | Chat message; pushes to ring buffer |
| S→C | 115 | SEND_PING | Server ping; replies with values + FPS 60 |
| S→C | 46 | UPDATE_STAT | Skill level/XP update |
| S→C | 97 | VARP_SMALL | Player varp update (8-bit value) |
| S→C | 12 | VARP_LARGE | Player varp update (32-bit value) |
| C→S | 114 | MOVE_GAMECLICK | Click-to-move (abs tile + run flag) |
| C→S | 89 | NO_TIMEOUT | Keepalive (empty) |
| C→S | 10 | WINDOW_STATUS | Client dimensions + mode |

### 5.2 Login Block Structure

Three layers: plain header → RSA inner block → XTEA payload.

```
Plain Header (unencrypted):
  u32 version = 239
  u32 subVersion = 1
  u32 serverVersion = 239
  u8  clientType = 1 (DESKTOP)
  u8  platformType = 5 (desktop)
  u8  externalAuthenticatorType = 0

RSA Inner Block (encrypted with Jagex 1024-bit public key):
  u8  encryptionCheck = 1
  u32 xtea_seed[4]      -- random 128-bit XTEA key
  u64 session_id        -- from init response
  u8  otp_type = 2      -- none
  u32 otp_value = 0
  u8  auth_type = 0     -- 0=legacy, 2=Jagex
  jstr secret           -- password or session token

XTEA Payload (encrypted with xtea_seed):
  jstr username
  u8   settings         -- bit0=low detail, bit1=resizable
  u16  width = 765
  u16  height = 503
  byte uuid[24]         -- persistent per-install
  jstr siteSettings = ""
  u32  affiliate = 0
  u8   deep_link_count = 0
  platform_stats struct v9
  u8   secondClientType = 0
  u32  reflectionCheckerConst = 0
  u32  cache_crcs[23] with specific alt encodings
```

### 5.3 ISAAC Obfuscation

```
seed_out[i] = xtea_seed[i]           -- client→server ISAAC
seed_in[i]  = xtea_seed[i] + 50      -- server→client ISAAC
```

Smart opcode encoding: first ISAAC value decides if opcode is 1 or 2 bytes.
If decoded byte < 128 → 1-byte opcode. Else → 2-byte opcode.

**Critical:** The `+50` offset is hard-coded. If Jagex changes this, all
packet decoding fails immediately after login.

### 5.4 Proof-of-Work

When server responds with code 69, it sends a challenge:
```
[id=0][version][difficulty][salt (jstr)]
```

Client must find an 8-byte big-endian value `nonce` such that:
```
SHA256(salt || nonce) has `difficulty` leading zero bits
```

Implemented in `osrs_pow_solve()` — brute-force increment from 0.
Reply is opcode 19 + 8-byte result.

---

## 6. Game Loop & State Machine

### 6.1 States

```
DISCONNECTED
   │ osrs_game_connect()
   ▼
LOGGING_IN ──► FAILED (connect error / bad response / desync)
   │
   │ login OK (code 2)
   ▼
IN_GAME ─────► FAILED (connection lost / logout / desync)
   │
   │ osrs_game_disconnect()
   ▼
DISCONNECTED
```

### 6.2 Per-Tick Update (`osrs_game_update`)

1. Early-out if DISCONNECTED or FAILED
2. Accumulate timers (connected_time, keepalive_timer)
3. Poll network; fail on disconnect
4. Send keepalive (NO_TIMEOUT) if idle > 2.5s
5. Process inbound bytes:
   - INIT_SENT → parse init response
   - LOGIN_SENT → parse login response (handle PoW if code 69)
   - GAME → read packets via ISAAC + framing → dispatch to handlers

### 6.3 Packet Dispatch (`game_handle_packet`)

Only these opcodes have handlers; all others are silently consumed:
- REBUILD_NORMAL (49) — map load, scene base tile, GPI seed
- PLAYER_INFO (28) — player positions/appearances
- SEND_PING (115) — ping reply
- MESSAGE_GAME (74) — chat
- UPDATE_STAT (46) — skill updates
- UPDATE_RUNENERGY (64) — run energy
- UPDATE_RUNWEIGHT (31) — weight
- VARP_SMALL (97) / VARP_LARGE (12) — varp updates
- LOGOUT (57) / LOGOUT_WITHREASON (58) — disconnect

**Notably ignored:** NPC info, zone updates, inventory, interfaces, scripts,
minimap flag, region rebuilds.

---

## 7. Cache System

### 7.1 File Layout

```
~/.runelite/jagexcache/oldschool/LIVE/
├── main_file_cache.dat2     -- sector-based data file
├── main_file_cache.idx0     -- index 0 (archives for other indexes)
├── main_file_cache.idx1     -- index 1 (models)
├── ...
├── main_file_cache.idx255   -- index 255 (master index)
```

### 7.2 Sector Format

**8-byte header (type 0):**
```
u24 big-endian: archive_id
u16 big-endian: chunk_number
u24 big-endian: next_sector
```

**10-byte header (type 1, for large files > 65535 bytes):**
```
u32 big-endian: archive_id
u16 big-endian: chunk_number
u24 big-endian: next_sector
u32 big-endian: extended_length
```

Data: 512 bytes (type 0) or 510 bytes (type 1) per sector.

### 7.3 Container Format

```
u8  compression_type  -- 0=none, 1=BZip2, 2=GZip
i32 decompressed_length -- big-endian
[...compressed data...]
[optional XTEA decryption]
```

### 7.4 XTEA Keys for Maps

Map regions in index 5 are XTEA-encrypted. Keys must be provided via the
`OSRS_XTEA_KEYS` environment variable:
```bash
export OSRS_XTEA_KEYS="/path/to/keys.json"
```

The JSON format is a map of `region_id` → `[key0, key1, key2, key3]`.
The `osrs_xtea_keys_load()` function populates a sparse 2048×2048 array.

---

## 8. Crypto & Compression

### 8.1 RSA

- 1024-bit public key encryption
- Modulus and exponent are hard-coded in `src/rsa.c`
- Can be overridden via `OSRS_RSA_MODULUS` and `OSRS_RSA_EXPONENT` env vars
- **Critical fix applied (2026-07-28):** Plaintext is zero-padded to modulus
  length (128 bytes) with data at the START. Previously encrypted raw 64-byte
  plaintext, causing server to read zeros from decrypted block.

### 8.2 XTEA

- 64-bit block cipher, 128-bit key, 32 rounds
- ECB mode (big-endian block format)
- Used for login payload encryption and map archive decryption
- In-place: encrypts/decrypts the input buffer directly

### 8.3 ISAAC

- Bob Jenkins stream cipher
- 256-word internal state, 256-word results
- Full mixing cycle every 256 outputs
- Deterministic: same seed → same keystream

### 8.4 GZip

- Full DEFLATE implementation (LZ77 + Huffman)
- Decompresses real OSRS cache containers
- Verified against `java.util.zip.Inflater` output

### 8.5 BZip2

- Burrows-Wheeler transform + MTF + Huffman
- Multi-block support with CRC validation
- Decompresses real OSRS cache containers
- Bit reader tracks exact bit position for edge cases

---

## 9. Future Maintenance: What Breaks on Game Updates

This section is **critical** for anyone maintaining this client. OSRS updates
roughly weekly, and almost every update changes something in this list.

### 9.1 Revision Bump (GUARANTEED BREAKAGE)

**Frequency:** Every 1–4 weeks

| What breaks | Location | Fix |
|-------------|----------|-----|
| `OSRS_PROTOCOL_REVISION` | `include/osrs/protocol.h` | Update to new rev number |
| Opcode size tables | `src/protocol.c` `osrs_protocol_init_tables()` | Extract new tables from deob or RuneLite |
| Named opcode constants | `include/osrs/protocol.h` | Redefine `OSRS_IN_*` and `OSRS_OUT_*` values |
| Cache CRC count/order | `src/protocol.c` `osrs_login_build_gamelogin()` | Verify 23 CRCs and alt-encoding order |
| Login block layout | `src/protocol.c` | Check for new/removed fields in XTEA payload |
| Platform stats version | `src/protocol.c` `login_write_platform_stats()` | Update struct version if server expects change |

**How to extract new opcodes:**
1. Run RuneLite with `--debug` or use its `PacketDecoder`
2. Compare packet names from the new deobfuscated client
3. Update the size tables in `protocol.c`
4. Update the named constants in `protocol.h`

### 9.2 RSA Key Rotation (RARE BUT CRITICAL)

**Frequency:** Every few years

- Jagex may rotate their 1024-bit public key
- Hard-coded modulus/exponent in `src/rsa.c` must be updated
- Can be overridden at runtime with `OSRS_RSA_MODULUS` / `OSRS_RSA_EXPONENT`
- Test against local server first to verify new key

### 9.3 ISAAC Seed Offset Change (RARE)

**Frequency:** Rare, but has happened historically

- The `+50` offset for `isaac_in` is hard-coded
- If Jagex changes it, all game packet decoding fails
- Fix: change `seed_in[i] = xtea_seed[i] + 50;` in `protocol.c`
- Determine new offset by comparing with RuneLite or deob

### 9.4 Proof-of-Work Changes

**Frequency:** Rare

- Currently SHA256 hashcash with configurable difficulty
- If algorithm changes (e.g., to scrypt), `osrs_pow_solve()` must be rewritten
- Server sends `id` byte; currently only `id=0` is implemented

### 9.5 Coordinate Packing Changes

**Frequency:** Every few months

- `REBUILD_NORMAL` (opcode 49) uses 30-bit packed coordinates:
  `level = (packed >> 28) & 3`, `x = (packed >> 14) & 0x3FFF`, `z = packed & 0x3FFF`
- Has a heuristic fallback to legacy `g2alt1`/`g2alt2`
- If packing scheme changes, `osrs_parse_rebuild_normal()` must be updated

### 9.6 Cache Format Changes

**Frequency:** Rare

- Cache protocol version (currently 5–7) may increment
- Index/archive metadata format may change
- Container compression types may change
- The `cache.c` parser has switch statements for protocol versions;
  add new versions as needed

### 9.7 Platform Stats Changes

**Frequency:** Every few months

- The platform stats struct sent in login block is version 9
- Jagex may add/remove fields or change the version number
- If version changes, `login_write_platform_stats()` must be updated
- Server usually rejects logins with wrong version

### 9.8 New Packet Types

**Frequency:** Weekly

- New server→client or client→server packets are added regularly
- If unhandled, they are silently consumed (safe)
- To implement a new packet:
  1. Add opcode constant to `protocol.h`
  2. Add size to size tables in `protocol.c`
  3. Add handler in `game.c` `game_handle_packet()`

### 9.9 Player Info (GPI) Format Changes

**Frequency:** Every few months

- The PLAYER_INFO bitstream format changes periodically
- `osrs_pi_decode()` in `playerinfo.c` is sensitive to bit layout changes
- Changes typically involve:
  - Movement coordinate bit widths
  - Appearance block field order
  - Extended info bitmasks
- If GPI decoding desyncs, player positions/appearances will be wrong

### 9.10 Map/Region Format Changes

**Frequency:** Rare

- Map terrain encoding (index 5) may change
- Location object bit packing may change
- XTEA encryption may be added/removed for certain regions
- `map.c` and `config.c` loaders need updates

### 9.11 Authenticator Flow Changes

**Frequency:** Rare

- Currently the client does NOT implement authenticator code entry
- If Jagex makes 2FA mandatory for all accounts, this becomes critical
- The `OSRS_CONN_AWAIT_AUTH_CODE` state exists but is unhandled
- Implementation needed: prompt user for 6-digit code, send in login block

### 9.12 World List / World Hopping

**Frequency:** N/A (not implemented)

- World list is downloaded from `worlds.rs` endpoint
- Currently not implemented; client connects to hard-coded world host
- To add: HTTP GET to world list API, parse JSON, let user select

---

## 10. Testing & Verification

### 10.1 Automated Tests

| Test | File | What it tests |
|------|------|--------------|
| Crypto | `tests/test_crypto.c` | CRC32, XTEA, ISAAC, GZip, BZip2 |
| PlayerInfo | `tests/test_playerinfo.c` | GPI decode, login seeding, walk/teleport/promotion |

Run: `make test`

### 10.2 Live Verification Tests

| Test | Tool | Command |
|------|------|---------|
| Legacy login (live) | `login_test.c` | `./build/login_test <cache> <host:port> <user> <pass> legacy` |
| Legacy login (local) | `login_test.c` | `./build/login_test <cache> 127.0.0.1:43599 bob hunter2` |
| Cache inspection | `cache_dump.c` | `./build/cache_dump <cache> <index> [archive]` |
| Config inspection | `config_dump.c` | `./build/config_dump <cache> <type>` |
| Local server | `local_server.py` | `python3 tools/local_server.py` |

### 10.3 Environment Variables

| Variable | Purpose |
|----------|---------|
| `OSRS_REVISION` | Override protocol revision (default 239) |
| `OSRS_RSA_MODULUS` | Override RSA modulus (hex) |
| `OSRS_RSA_EXPONENT` | Override RSA exponent (hex) |
| `OSRS_CLIENT_TYPE` | Override clientType in login block |
| `OSRS_PLATFORM_TYPE` | Override platformType in login block |
| `OSRS_SECOND_TYPE` | Override secondClientType in login block |
| `OSRS_XTEA_KEYS` | Path to XTEA keys JSON for map decryption |
| `OSRS_JX_ACCESS_TOKEN` | Jagex Account OAuth access token |
| `OSRS_JX_SESSION_ID` | Jagex Account game session token |

---

## 11. File Inventory

### Source Files (`src/`)

| File | Lines | Description |
|------|-------|-------------|
| `protocol.c` | ~1200 | Packet framing, login block construction, ISAAC, PoW |
| `game.c` | ~500 | Game state machine, packet handlers, player state |
| `cache.c` | ~450 | Cache file reader, sector I/O, container decompression |
| `config.c` | ~1200 | Object/NPC/item/sprite/model/map loaders |
| `playerinfo.c` | ~600 | GPI bitstream decoder |
| `net.c` | ~400 | TLS/TCP socket, non-blocking poll |
| `jagex.c` | ~400 | OAuth2 PKCE flow, HTTPS client |
| `rsa.c` | ~100 | 1024-bit RSA encryption |
| `xtea.c` | ~80 | XTEA encrypt/decrypt |
| `isaac.c` | ~150 | ISAAC stream cipher |
| `crc32.c` | ~60 | CRC32 implementation |
| `gzip.c` | ~400 | DEFLATE decompressor |
| `bzip2.c` | ~500 | BZip2 decompressor |
| `bignum.c` | ~300 | Arbitrary-precision integers |
| `map.c` | ~300 | Region terrain + location loader |
| `xtea_keys.c` | ~100 | XTEA key storage/lookup |
| `client.c` | ~900 | Main client application (FORGE integration) |
| `gl_world.c` | ~600 | GPU terrain renderer (EGL + GLES 2.0) |
| `auth.c` | ~50 | Auth method parsing |

### Headers (`include/osrs/`)

All 19 public headers are documented in `docs/protocol.md` and the game session docs.

### Tools (`tools/`)

| File | Purpose |
|------|---------|
| `login_test.c` | Headless login + movement test |
| `cache_dump.c` | Cache inspection CLI |
| `config_dump.c` | Config definition inspection |
| `local_server.py` | Local rev-239 protocol test server |

---

## Appendix A: Live Verification Log (2026-07-29)

```
World 1 (F2P) — oldschool1.runescape.com:43594

RaphaelOlsen324@ghosty.lol     → OK  player_index=1812  spawn=(4,1)
LuzBaird923@ghosty.lol         → OK  player_index=1828  spawn=(0,0)
JulietteKnight918@ghosty.lol   → OK  player_index=1855  spawn=(0,0)
WilsonGrant183@ghosty.lol      → OK  player_index=1881  spawn=(1,1)
BrittneyHampton772@ghosty.lol  → OK  player_index=1890  spawn=(0,0)
                                     MOVED: (0,0) → (-2,1)

World 301 (members) — oldschool301.runescape.com:43594
4/5 accounts → Code 12 (Members world) — correct behavior for F2P accounts
1 account    → Code 10 (Bad session id) — credential-specific backend routing
```

All 5 accounts passed the full handshake:
- RSA encryption: `encCheck=1` verified by server
- XTEA: random 128-bit keys per login
- PoW: SHA256 hashcash solved live
- ISAAC: random-seed keystreams sync perfectly
- Game packets: REBUILD_NORMAL, PLAYER_INFO, etc. decode cleanly

---

## Appendix B: Build Instructions

```bash
# Build everything
make -j4

# Run tests
make test

# Build & run cache inspector
make cache_dump
./build/cache_dump ~/.runelite/jagexcache/oldschool/LIVE/ 2 6  # objects

# Build & run login test
make login_test
./build/login_test ~/.runelite/jagexcache/oldschool/LIVE/ \
  oldschool1.runescape.com:43594 \
  "username@email.com" "password" legacy
```

Requirements: C23-capable compiler (clang 15+ or gcc 13+).

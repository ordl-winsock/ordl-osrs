# OSRS Protocol Documentation (Revision 239)

> Auto-generated from `src/protocol.c` and `include/osrs/protocol.h`.
> This document covers the complete network protocol implementation as of rev 239.

---

## 1. Packet Opcodes

Game packet opcodes are **revision-specific**; Jagex shuffles the size tables every release. The tables below are the *canonical* mappings for revision 239, initialized in `osrs_protocol_init_tables()`.

Opcode bytes on the wire are obfuscated with ISAAC (see §3). `XX` = unknown/unused (`-9`).

### 1.1 Server → Client (Incoming)

| Opcode | Size | Name |
|--------|------|------|
| 0 | 4 | `MIDI_SONG_STOP` |
| 1 | 5 | `CAM_TARGET_V3` |
| 2 | `VAR_SHORT` | `NPC_INFO_LARGE_V5` |
| 3 | 0 | `CAM_RESET` |
| 4 | 9 | `NPC_HEADICON_SPECIFIC` |
| 5 | 1 | `CHAT_FILTER_SETTINGS_PRIVATECHAT` |
| 6 | `VAR_BYTE` | `SET_PLAYER_OP` |
| 7 | 7 | `IF_OPENSUB` |
| 8 | `VAR_SHORT` | `REFLECTION_CHECKER` |
| 9 | 3 | `UPDATE_ZONE_PARTIAL_FOLLOWS` |
| 10 | 2 | `LOC_DEL` |
| 11 | `VAR_BYTE` | `VARCLAN` |
| 12 | 6 | `VARP_LARGE` |
| 13 | 14 | `OBJ_COUNT_SPECIFIC` |
| 14 | 8 | `IF_SETROTATESPEED` |
| 15 | `VAR_SHORT` | `MESSAGE_PRIVATE_ECHO` |
| 16 | 6 | `IF_SETPLAYERMODEL_BASECOLOUR` |
| 17 | 10 | `IF_SETANGLE` |
| 18 | `VAR_SHORT` | `UPDATE_INV_PARTIAL` |
| 19 | `VAR_SHORT` | `MESSAGE_PRIVATE` |
| 20 | 5 | `NPC_ANIM_SPECIFIC` |
| 21 | 1 | `HIDELOCOPS` |
| 22 | `VAR_SHORT` | `UPDATE_INV_FULL` |
| 23 | 4 | `IF_CLOSESUB` |
| 24 | `VAR_BYTE` | `UNKNOWN_STRING` |
| 25 | `VAR_SHORT` | `IF_RESYNC_V2` |
| 26 | 1 | `SET_HEATMAP_ENABLED` |
| 27 | `VAR_SHORT` | `UPDATE_FRIENDCHAT_CHANNEL_FULL_V2` |
| 28 | `VAR_SHORT` | `PLAYER_INFO` |
| 29 | 4 | `CAM_SMOOTHRESET` |
| 30 | 6 | `LOC_ANIM_SPECIFIC` |
| 31 | 2 | `UPDATE_RUNWEIGHT` |
| 32 | 7 | `SOUND_AREA` |
| 33 | `-2` | `LOC_ADD_CHANGE_V2` *(zone-internal special)* |
| 34 | 14 | `LOC_MERGE` |
| 35 | 10 | `MIDI_SONG_V2` |
| 36 | 0 | `FRIENDLIST_LOADED` |
| 37 | `VAR_BYTE` | `UPDATE_REBOOT_TIMER_V2` |
| 38 | 1 | `CAM_MODE` |
| 39 | `VAR_SHORT` | `URL_OPEN` |
| 40 | 7 | `CAM_ROTATETO` |
| 41 | 10 | `OBJ_DEL_SPECIFIC` |
| 42 | `VAR_SHORT` | `UPDATE_IGNORELIST` |
| 43 | 1 | `MINIMAP_TOGGLE` |
| 44 | 0 | `VARP_RESET` |
| 45 | 2 | `UPDATE_INV_STOPTRANSMIT` |
| 46 | 7 | `UPDATE_STAT_V2` |
| 47 | 3 | `SET_ACTIVE_WORLD_V2` |
| 48 | 27 | `PROJANIM_SPECIFIC_V4` |
| 49 | `VAR_SHORT` | `REBUILD_NORMAL_V2` |
| 50 | 6 | `HINT_ARROW` |
| 51 | 7 | `CAM_ROTATEBY` |
| 52 | 20 | `OBJ_CUSTOMISE_SPECIFIC` |
| 53 | 20 | `UPDATE_STOCKMARKET_SLOT` |
| 54 | 3 | `UPDATE_ZONE_FULL_FOLLOWS` |
| 55 | 6 | `IF_SETCOLOUR` |
| 56 | `VAR_SHORT` | `UPDATE_TRADINGPOST` |
| 57 | 0 | `LOGOUT` |
| 58 | 1 | `LOGOUT_WITHREASON` |
| 59 | 6 | `IF_SETANIM` |
| 60 | 0 | `VARCLAN_DISABLE` |
| 61 | `VAR_BYTE` | `MESSAGE_FRIENDCHANNEL` |
| 62 | `VAR_BYTE` | `MESSAGE_CLANCHANNEL_SYSTEM` |
| 63 | 5 | `IF_SETHIDE` |
| 64 | 2 | `UPDATE_RUNENERGY` |
| 65 | 4 | `OCULUS_SYNC` |
| 66 | 2 | `RESET_INTERACTION_MODE` |
| 67 | `VAR_BYTE` | `SITE_SETTINGS` |
| 68 | 8 | `ACCOUNT_FLAGS` |
| 69 | `VAR_SHORT` | `CLANCHANNEL_FULL` |
| 70 | 7 | `OBJ_ENABLED_OPS_SPECIFIC` |
| 71 | `VAR_SHORT` | `CLANSETTINGS_DELTA` |
| 72 | 9 | `PLAYER_SPOTANIM_SPECIFIC` |
| 73 | 1 | `HIDEOBJOPS` |
| 74 | `VAR_BYTE` | `MESSAGE_GAME` |
| 75 | 1 | `HIDENPCOPS` |
| 76 | 0 | `VARP_SYNC` |
| 77 | 5 | `SYNTH_SOUND` |
| 78 | 17 | `OBJ_ADD_SPECIFIC` |
| 79 | 4 | `SET_INTERACTION_MODE` |
| 80 | `VAR_SHORT` | `IF_SETTEXT` |
| 81 | `VAR_SHORT` | `CLANSETTINGS_FULL` |
| 82 | 6 | `IF_SETSCROLLPOS` |
| 83 | 0 | `SERVER_TICK_END` |
| 84 | 6 | `IF_SETNPCHEAD` |
| 85 | `VAR_SHORT` | `NPC_INFO_SMALL_V5` |
| 86 | 6 | `MAP_ANIM` |
| 87 | 5 | `CAM_TARGET_V4` |
| 88 | 0 | `VARCLAN_ENABLE` |
| 89 | 6 | `IF_SETNPCHEAD_ACTIVE` |
| 90 | 8 | `MAP_ANIM_SPECIFIC` |
| 91 | `VAR_BYTE` | `LOGOUT_TRANSFER` |
| 92 | 0 | `RESET_ANIMS` |
| 93 | 2 | `PACKET_GROUP_START` |
| 94 | 4 | `IF_CLEARINV` |
| 95 | `VAR_SHORT` | `CLANCHANNEL_DELTA` |
| 96 | 2 | `IF_OPENTOP` |
| 97 | 3 | `VARP_SMALL` |
| 98 | 8 | `IF_SETPLAYERMODEL_OBJ` |
| 99 | 12 | `MIDI_SONG_WITHSECONDARY` |
| 100 | 5 | `IF_SETPLAYERMODEL_BODYTYPE` |
| 101 | 9 | `NPC_SPOTANIM_SPECIFIC` |
| 102 | 8 | `IF_SETPOSITION` |
| 103 | 4 | `CAM_SHAKE` |
| 104 | 10 | `OBJ_UNCUSTOMISE_SPECIFIC` |
| 105 | `VAR_SHORT` | `UPDATE_ZONE_PARTIAL_ENCLOSED` |
| 106 | 10 | `IF_SETOBJECT` |
| 107 | 8 | `MIDI_SWAP` |
| 108 | 16 | `IF_SETEVENTS_V2` |
| 109 | `VAR_SHORT` | `REBUILD_WORLDENTITY_V4` |
| 110 | 4 | `LOC_ANIM` |
| 111 | `VAR_SHORT` | `UPDATE_FRIENDLIST` |
| 112 | `VAR_BYTE` | `UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER` |
| 113 | 28 | `UPDATE_UID192` |
| 114 | `VAR_SHORT` | `RUNCLIENTSCRIPT` |
| 115 | 8 | `SEND_PING` |
| 116 | 2 | `SET_NPC_UPDATE_ORIGIN` |
| 117 | 0 | `TRIGGER_ONDIALOGABORT` |
| 118 | 5 | `MIDI_JINGLE` |
| 119 | 3 | `ANIM_SPECIFIC` |
| 120 | 4 | `IF_SETPLAYERHEAD` |
| 121 | 5 | `IF_SETPLAYERMODEL_SELF` |
| 122 | `VAR_SHORT` | `WORLDENTITY_INFO_V7` |
| 123 | 8 | `IF_MOVESUB` |
| 124 | 2 | `CHAT_FILTER_SETTINGS` |
| 125 | `VAR_SHORT` | `REBUILD_REGION_V2` |
| 126 | `VAR_SHORT` | `HISCORE_REPLY` |
| 127 | `VAR_BYTE` | `MESSAGE_CLANCHANNEL` |
| 128 | 15 | `CAM_MOVETO_ARC_V3` |
| 129 | 14 | `CAM_MOVETO_ARC_V2` |
| 130 | 11 | `CAM_MOVETO_CYCLES_V3` |
| 131 | 1 | `CAM_UNLOCK` |
| 132 | `VAR_SHORT` | `GROUP_FULL` |
| 133 | 13 | `GROUP_VAR_LONG` |
| 134 | 10 | `CAM_MOVETO_CYCLES_V2` |
| 135 | 4 | `CAM_SKYBOX` |
| 136 | `VAR_SHORT` | `GROUP_VAR` |
| 137 | 10 | `CAM_LOOKAT_CYCLES` |
| 138 | 1 | `AMBIENTSOUND_STOP` |
| 139 | 9 | `CAM_LOOKAT_V2` |
| 140 | 9 | `GROUP_VAR_INT` |
| 141 | 9 | `CAM_ROTATETO_COORDINATE_V2` |
| 142 | 8 | `IF_SETMODEL_V2` |
| 143 | 4 | `SET_MAP_FLAG_V2` |
| 144 | 10 | `CAM_LOOKAT_V3` |
| 145 | 3 | `AMBIENTSOUND_START` |
| 146 | 8 | `CAM_MOVETO_V2` |
| 147 | 9 | `CAM_MOVETO_V3` |
| 148 | 11 | `CAM_ROTATETO_COORDINATE_V3` |

### 1.2 Client → Server (Outgoing)

| Opcode | Size | Name |
|--------|------|------|
| 0 | 2 | `OPNPC6` |
| 1 | 10 | `SEND_PING_REPLY` |
| 2 | 1 | `CLANSETTINGS_FULL_REQUEST` |
| 3 | 3 | `OPPLAYER2` |
| 4 | 0 | `IDLE` |
| 5 | 0 | `EXIT_FREECAM` |
| 6 | `VAR_BYTE` | `EVENT_NATIVE_MOUSE_MOVE` |
| 7 | 8 | `OPOBJ3_V2` |
| 8 | 4 | `EVENT_CAMERA_POSITION` |
| 9 | `VAR_BYTE` | `IGNORELIST_DEL` |
| 10 | 5 | `WINDOW_STATUS` |
| 12 | `VAR_BYTE` | `REFLECTION_CHECK_REPLY` |
| 13 | 4 | `OPNPC2_V2` |
| 14 | 3 | `OPWORLDENTITY1` |
| 15 | 8 | `OPLOC4_V2` |
| 16 | 2 | `EVENT_MOUSE_SCROLL` |
| 17 | `VAR_BYTE` | `HISCORE_REQUEST` |
| 19 | 4 | `OPNPC3_V2` |
| 20 | `VAR_BYTE` | `FRIENDCHAT_JOIN_LEAVE` |
| 22 | 3 | `OPPLAYER7` |
| 23 | 11 | `OPNPCU` |
| 24 | 0 | `MAP_BUILD_COMPLETE` |
| 26 | `VAR_BYTE` | `RESUME_P_NAMEDIALOG` |
| 27 | 16 | `IF_BUTTONT` |
| 28 | `VAR_BYTE` | `FRIENDLIST_ADD` |
| 29 | 1 | `EVENT_APPLET_FOCUS` |
| 31 | 8 | `OPOBJ4_V2` |
| 32 | 2 | `RESUME_P_OBJDIALOG` |
| 33 | 4 | `DETECT_MODIFIED_CLIENT` |
| 34 | `VAR_BYTE` | `CLIENT_CHEAT` |
| 35 | 7 | `EVENT_MOUSE_CLICK_V2` |
| 37 | 3 | `OPPLAYER4` |
| 38 | `VAR_BYTE` | `CLANCHANNEL_KICKUSER` |
| 39 | 4 | `SOUND_JINGLEEND` |
| 40 | 10 | `IF_SUBOP` |
| 43 | 8 | `OPOBJ5_V2` |
| 44 | 1 | `SET_HEADING` |
| 45 | 4 | `OPNPC4_V2` |
| 46 | 8 | `OPOBJ1_V2` |
| 47 | 9 | `IF_BUTTONX` |
| 48 | 16 | `IF_BUTTOND` |
| 50 | 9 | `TELEPORT` |
| 51 | 4 | `IF_BUTTON` |
| 52 | 15 | `OPLOCU` |
| 53 | 3 | `OPPLAYER8` |
| 54 | `VAR_SHORT` | `EVENT_KEYBOARD` |
| 55 | `VAR_BYTE` | `FRIENDCHAT_SETRANK` |
| 56 | `VAR_SHORT` | `IF_SCRIPT_TRIGGER` |
| 57 | 3 | `OPPLAYER5` |
| 58 | 2 | `OPOBJ6` |
| 59 | 3 | `SET_CHATFILTERSETTINGS` |
| 60 | `VAR_BYTE` | `SEND_SNAPSHOT` |
| 61 | 2 | `MEMBERSHIP_PROMOTION_ELIGIBILITY` |
| 62 | 15 | `OPLOCT` |
| 63 | 8 | `RESUME_P_COUNTDIALOG_LONG` |
| 64 | `VAR_BYTE` | `RESUME_P_STRINGDIALOG` |
| 65 | `VAR_BYTE` | `FRIENDLIST_DEL` |
| 66 | 6 | `EVENT_MOUSE_CLICK_V1` |
| 67 | `VAR_BYTE` | `MOVE_MINIMAPCLICK` |
| 69 | `VAR_BYTE` | `MESSAGE_PUBLIC` |
| 70 | 3 | `OPPLAYER6` |
| 72 | 11 | `OPWORLDENTITYT` |
| 73 | 8 | `OPLOC1_V2` |
| 74 | 8 | `OPOBJ2_V2` |
| 75 | 4 | `RESUME_P_COUNTDIALOG` |
| 76 | 15 | `OPOBJT` |
| 77 | 26 | `UPDATE_PLAYER_MODEL_V2` |
| 78 | 2 | `OPLOC6` |
| 79 | 11 | `OPPLAYERT` |
| 80 | 3 | `OPWORLDENTITY2` |
| 82 | `VAR_BYTE` | `IGNORELIST_ADD` |
| 83 | 3 | `OPWORLDENTITY4` |
| 84 | `VAR_BYTE` | `AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL` |
| 85 | 4 | `OPNPC5_V2` |
| 86 | 11 | `OPNPCT` |
| 87 | 11 | `OPPLAYERU` |
| 89 | 0 | `NO_TIMEOUT` |
| 90 | 11 | `OPWORLDENTITYU` |
| 91 | 22 | `IF_CRMVIEW_OP` |
| 92 | 3 | `OPPLAYER3` |
| 93 | 3 | `OPPLAYER1` |
| 95 | 0 | `CLOSE_MODAL` |
| 96 | `VAR_BYTE` | `AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL` |
| 97 | `VAR_BYTE` | `CONNECTION_TELEMETRY` |
| 98 | 2 | `OPWORLDENTITY6` |
| 99 | 4 | `CLICKWORLDMAP` |
| 100 | 3 | `OPWORLDENTITY3` |
| 101 | `VAR_BYTE` | `FRIENDCHAT_KICK` |
| 102 | 4 | `OPNPC1_V2` |
| 103 | 8 | `OPLOC2_V2` |
| 104 | 8 | `OPLOC3_V2` |
| 105 | 1 | `RSEVEN_STATUS` |
| 106 | 3 | `OPWORLDENTITY5` |
| 108 | `VAR_SHORT` | `BUG_REPORT` |
| 110 | `VAR_BYTE` | `EVENT_MOUSE_MOVE` |
| 111 | `VAR_SHORT` | `MESSAGE_PRIVATE` |
| 112 | 1 | `CLANCHANNEL_FULL_REQUEST` |
| 113 | 15 | `OPOBJU` |
| 114 | `VAR_BYTE` | `MOVE_GAMECLICK` |
| 115 | 6 | `RESUME_PAUSEBUTTON` |
| 116 | 8 | `OPLOC5_V2` |

### 1.3 Login Opcodes

| Direction | Opcode | Name |
|-----------|--------|------|
| C→S | 14 | `OSRS_LOGIN_INIT_GAME_CONNECTION` |
| C→S | 15 | `OSRS_LOGIN_INIT_JS5_CONNECTION` *(not implemented)* |
| C→S | 16 | `OSRS_LOGIN_GAMELOGIN` |
| C→S | 18 | `OSRS_LOGIN_GAMERECONNECT` *(not implemented)* |
| C→S | 19 | `POW_REPLY` *(hardcoded in `osrs_login_build_pow_reply`)* |

---

## 2. Packet Framing Format

### 2.1 Game Packets

Every game packet has three parts: **opcode**, **[length]**, **payload**.

**Opcode encoding (ISAAC-obfuscated):**
- Outbound (client): `wire_byte = (opcode + isaac_next()) & 0xFF`
- Inbound (server): `var1 = (wire_byte - isaac_next()) & 0xFF`
  - If `var1 < 128`: opcode = `var1` (1 byte consumed).
  - Else: opcode = `((var1 - 128) << 8) + ((wire[1] - isaac_next()) & 0xFF)` (2 bytes consumed).

**Length encoding:**

| Size Table Value | Meaning | On-wire |
|------------------|---------|---------|
| `≥ 0` | Fixed size | No length field; payload is exactly N bytes |
| `OSRS_PKT_VAR_BYTE` (`-1`) | Variable, ≤ 255 | 1-byte unsigned length follows opcode |
| `OSRS_PKT_VAR_SHORT` (`-2`) | Variable, ≤ 65535 | 2-byte big-endian length follows opcode |

The maximum payload size is bounded by `sizeof(osrs_packet_t.data)` = **8192 bytes**.

### 2.2 Login Packets

| Stage | Framing |
|-------|---------|
| Init request | Single byte: `0x0E` |
| Init response | Single byte status; if `0`, followed by 8-byte big-endian `session_id` |
| Game login | Opcode `16` + 2-byte big-endian length (`VAR_SHORT`) + encrypted login block |
| POW reply | Opcode `19` + 2-byte big-endian length (`8`) + 8-byte big-endian result |
| OK response | `VAR_BYTE`: 1-byte length + plaintext payload |

---

## 3. ISAAC Obfuscation

The protocol uses **two independent ISAAC stream ciphers**, derived from the same 128-bit XTEA seed exchanged during login.

### 3.1 Key Derivation

```c
uint32_t seed_out[4], seed_in[4];
for (int i = 0; i < 4; i++) {
    seed_out[i] = proto->xtea_seed[i];          // client→server
    seed_in[i]  = proto->xtea_seed[i] + 50;     // server→client
}
```

- **`isaac_out`** (seed `xtea_seed`) — obfuscates **outgoing** opcodes.
- **`isaac_in`** (seed `xtea_seed + 50`) — de-obfuscates **incoming** opcodes.

### 3.2 Smart Opcode Encoding

OSRS uses a Jagex-specific "smart byte-short" ISAAC layer:

- The first ISAAC value decides whether the opcode is 1 or 2 bytes.
- If the decoded first byte is `< 128`, the opcode fits in one byte.
- Otherwise the opcode is `((byte1 - 128) << 8) | byte2`, consuming a second ISAAC value.

This means **the opcode byte count on the wire is non-deterministic** and depends on the ISAAC keystream.

### 3.3 Auth-Code Keystream Advance

When the login response indicates an authenticator is required (`auth_flag == 1`), the server→client ISAAC cipher is **advanced by 4 values** to keep both peers synchronized:

```c
if (auth_flag == 1) {
    for (int i = 0; i < 4; i++)
        (void)osrs_isaac_next(&proto->isaac_in);
}
```

---

## 4. Login Block Structure

The game-login block (`osrs_login_build_gamelogin`) is built in three layers.

### 4.1 Plain Header (unencrypted)

| Field | Type | Value (rev 239) |
|-------|------|-----------------|
| `version` | u32 BE | `proto->revision` (default 239) |
| `subVersion` | u32 BE | `1` |
| `serverVersion` | u32 BE | `proto->revision` (239) |
| `clientType` | u8 | `1` (`DESKTOP`) |
| `platformType` | u8 | `5` (`desktop`) |
| `externalAuthenticatorType` | u8 | `0` |

### 4.2 RSA Inner Block

This sub-block is RSA-encrypted with Jagex's 1024-bit public key.

| Field | Type | Notes |
|-------|------|-------|
| `encryptionCheck` | u8 | `1` |
| `xtea_seed` | 4 × u32 BE | Random 128-bit key for XTEA payload |
| `session_id` | u64 BE | From init handshake |
| `otp_type` | u8 | `2` = none |
| `otp_value` | u32 BE | `0` |
| `auth_type` | u8 | `0` = legacy password; `2` = Jagex session token |
| `secret` | jagex string | Password or session token |

**RSA padding:** The plaintext is copied to the **start** of a zero-padded buffer of modulus length (128 bytes). The server reads from the beginning of the decrypted block.

The RSA key can be overridden at runtime via environment variables `OSRS_RSA_MODULUS` and `OSRS_RSA_EXPONENT` for private-server testing.

### 4.3 XTEA Payload

Encrypted with the `xtea_seed` from the RSA block. Must be padded to a multiple of 8 bytes.

| Field | Type | Notes |
|-------|------|-------|
| `username` | jagex string | Lowercased by caller |
| `settings` | u8 | bit 0 = low detail; bit 1 = resizable |
| `width` | u16 BE | Defaults to `765` |
| `height` | u16 BE | Defaults to `503` |
| `uuid` | 24 bytes | Persistent per-install hex identifier |
| `siteSettings` | jagex string | Empty |
| `affiliate` | u32 BE | `0` |
| `deep_link_count` | u8 | `0` |
| `platform_stats` | struct | See §4.4 |
| `secondClientType` | u8 | `0` |
| `reflectionCheckerConst` | u32 BE | `0` |
| `cache_crcs` | 23 × u32 | Specific alt encodings; see §7.2 |

After building, the payload is zero-padded to `len % 8 == 0` and encrypted with XTEA in ECB mode (big-endian block format).

### 4.4 Platform Stats Block (struct version 9)

This block is written by `login_write_platform_stats()` and mirrors the real desktop client's `vs.ag` serializer.

| Field | Type | Value (this build) |
|-------|------|--------------------|
| struct version | u8 | `9` |
| osType | u8 | `3` (Linux) |
| os64Bit | u8 | `1` |
| osVersion | u16 BE | `61840` (kernel 6.18.40) |
| javaVendor | u8 | `1` (Oracle/OpenJDK) |
| javaMajor | u8 | `26` |
| javaMinor | u8 | `0` |
| javaPatch | u8 | `2` |
| notApplet | u8 | `1` |
| maxMemoryMB | u16 BE | `4096` |
| processors | u8 | `24` |
| systemMemoryMB | u24 BE | `98855` |
| systemSpeedMHz | u16 BE | `3330` |
| gpuDxName | jstr2 | `""` |
| gpuGlName | jstr2 | `"AMD Radeon RX Vega"` |
| gpuDxVersion | jstr2 | `""` |
| gpuGlVersion | jstr2 | `"4.6 (Compatibility Profile) Mesa 26.1.5"` |
| driverMonth | u8 | `1` |
| driverYear | u16 BE | `2026` |
| cpuManufacturer | jstr2 | `"GenuineIntel"` |
| cpuBrand | jstr2 | `"Intel(R) Xeon(R) CPU X5680 @ 3.33GHz"` |
| cpuCount1 (cores) | u8 | `12` |
| cpuCount2 (threads) | u8 | `24` |
| cpuFeature1 | u32 BE | `0xbfebfbff` |
| cpuFeature2 | u32 BE | `0x00000000` |
| cpuFeature3 | u32 BE | `0x00000000` |
| cpuSignature | u32 BE | `0x000206c2` |
| clientName | jstr2 | `"osrs-launcher"` |
| deviceName | jstr2 | `"forge.ordl.org"` |

---

## 5. Alt-Encodings

Jagex byte-buffer alt encodings swap byte order or offset individual bytes by 128.

### 5.1 1-byte (`p1` / `g1`)

| Variant | Writer | Reader |
|---------|--------|--------|
| default | `v` | `v` |
| alt1 | `v + 128` | `v - 128` |
| alt2 | `-v` | `0 - v` |
| alt3 | `128 - v` | `128 - v` |

### 5.2 2-byte (`p2` / `g2`)

| Variant | Byte order | Low-byte offset |
|---------|------------|-----------------|
| default | BE | none |
| alt1 | LE | none |
| alt2 | BE | `+ 128` on write / `- 128` on read |
| alt3 | LE | `+ 128` on write / `- 128` on read |

### 5.3 4-byte (`p4` / `g4`)

| Variant | Byte layout (write order) |
|---------|---------------------------|
| default | `3,2,1,0` (big-endian) |
| alt1 | `0,1,2,3` (little-endian) |
| alt2 | `1,0,3,2` (swap 16-bit halves) |
| alt3 | `2,3,0,1` (swap 32-bit halves) |

### 5.4 3-byte (`p3` / `g3`)

Always big-endian (`[hi, mid, lo]`). No alt variants.

### 5.5 8-byte (`p8` / `g8`)

Always big-endian. No alt variants.

### 5.6 String encodings

| Variant | Format |
|---------|--------|
| `pjstr` / `gjstr` | C-style NUL-terminated string |
| `pjstr2` / `gjstr2` | Length-prefixed: single `0x00` byte, then NUL-terminated string |

---

## 6. Login Response Codes

| Code | Constant | Meaning |
|------|----------|---------|
| `0` | `OSRS_LOGINRESP_SUCCESSFUL` | Init handshake accepted; 8-byte `session_id` follows |
| `2` | `OSRS_LOGINRESP_OK` | Login accepted; game payload follows (`VAR_BYTE` framed) |
| `3` | `OSRS_LOGINRESP_INVALID_CRED` | Invalid username or password |
| `4` | `OSRS_LOGINRESP_BANNED` | Account banned |
| `5` | `OSRS_LOGINRESP_DUPLICATE` | Account already logged in |
| `6` | `OSRS_LOGINRESP_OUT_OF_DATE` | Client revision mismatch |
| `7` | `OSRS_LOGINRESP_SERVER_FULL` | World full |
| `8` | `OSRS_LOGINRESP_LOGINSERVER_OFFLINE` | Login server offline |
| `9` | `OSRS_LOGINRESP_IP_LIMIT` | Too many connections from this IP |
| `10` | `OSRS_LOGINRESP_BAD_SESSION_ID` | Session ID mismatch / expired |
| `11` | `OSRS_LOGINRESP_FORCE_PASSWORD_CHANGE` | Password change required |
| `12` | `OSRS_LOGINRESP_NEED_MEMBERS` | Members-only world |
| `16` | `OSRS_LOGINRESP_TOO_MANY_ATTEMPTS` | Too many login attempts |
| `17` | `OSRS_LOGINRESP_IN_MEMBERS_AREA` | Members area (F2P account) |
| `18` | `OSRS_LOGINRESP_LOCKED` | Account locked |
| `21` | `OSRS_LOGINRESP_HOP_BLOCKED` | World-hop blocked |
| `22` | `OSRS_LOGINRESP_INVALID_LOGIN_PACKET` | Malformed login block |
| `56` | `OSRS_LOGINRESP_AUTHENTICATOR` | 2FA / authenticator code required |
| `69` | `OSRS_LOGINRESP_PROOF_OF_WORK` | Proof-of-work challenge issued |

### 6.1 Proof-of-Work Challenge

When response code `69` is received, the server sends a `VAR_SHORT`-framed challenge:

```
[69][len:2][id][version][difficulty][salt (jstr)]
```

- `id` (`u8`): challenge type (`0` = SHA256).
- `version` (`u8`): stored in `proto->pow_version`.
- `difficulty` (`u8`): stored in `proto->pow_difficulty`.
- `salt`: NUL-terminated string (max 1200 bytes), stored in `proto->pow_salt`.

The client must solve the puzzle and reply with opcode `19` + 8-byte big-endian result (`osrs_login_build_pow_reply`).

---

## 7. Protocol Constants That Change on Revision Bump

The following values are **hard-coded for rev 239** and will break on almost every game update.

### 7.1 Revision Number

- `OSRS_PROTOCOL_REVISION` → `239`
- Overridable at runtime via `OSRS_REVISION` env var.

### 7.2 Opcode Size Tables

- `osrs_server_packet_sizes[256]`
- `osrs_client_packet_sizes[256]`

Both tables map opcode → fixed size / `VAR_BYTE` / `VAR_SHORT`. Jagex shuffles opcodes and changes sizes every revision. The `XX` (`-9`) sentinel marks unknown opcodes.

### 7.3 Named Opcode Constants

All `#define OSRS_IN_*` and `#define OSRS_OUT_*` values in `protocol.h` are revision-specific. Even if the *name* stays the same, the numeric value changes.

### 7.4 Cache CRC Count & Encoding Order

The login block writes **23 cache CRCs** in a specific order with specific alt encodings:

```c
osrs_p4alt2(p, crcs[18]);
osrs_p4alt2(p, crcs[19]);
osrs_p4   (p, crcs[22]);
osrs_p4alt1(p, crcs[12]);
osrs_p4alt1(p, crcs[9]);
osrs_p4alt2(p, crcs[1]);
osrs_p4alt2(p, crcs[17]);
osrs_p4alt3(p, crcs[16]);
osrs_p4alt3(p, crcs[7]);
osrs_p4alt1(p, crcs[13]);
osrs_p4   (p, crcs[3]);
osrs_p4alt2(p, crcs[5]);
osrs_p4alt3(p, crcs[10]);
osrs_p4   (p, crcs[11]);
osrs_p4alt2(p, crcs[2]);
osrs_p4alt1(p, crcs[8]);
osrs_p4alt3(p, crcs[0]);
osrs_p4   (p, crcs[21]);
osrs_p4alt3(p, crcs[4]);
osrs_p4alt2(p, crcs[20]);
osrs_p4alt1(p, crcs[6]);
osrs_p4alt3(p, crcs[14]);
osrs_p4alt1(p, crcs[15]);
```

**If the count of cache indexes changes, or the order/encodings change, this block must be rewritten.**

### 7.5 Platform Stats Version

- `login_write_platform_stats` writes struct version `9`. If the server starts expecting version `10` or changes field order, the login will be rejected.

### 7.6 ISAAC Seed Offsets

- `isaac_in` seed = `xtea_seed[i] + 50`

If Jagex changes the `+50` offset (or switches to a different PRNG), game-packet de-obfuscation will fail immediately after login.

### 7.7 RSA Public Key

- Default 1024-bit modulus and exponent are hard-coded in `src/rsa.c`.
- Can be overridden with `OSRS_RSA_MODULUS` / `OSRS_RSA_EXPONENT` env vars.
- If Jagex rotates the live key, the hard-coded values must be updated.

### 7.8 Coordinate Packing

`REBUILD_NORMAL_V2` (opcode 49) changed in recent revisions to a **30-bit packed coordinate**:

```c
uint32_t packed = le32(data);
int level = (packed >> 28) & 0x3;
int tile_x = (packed >> 14) & 0x3FFF;
int tile_z = packed & 0x3FFF;
```

The parser has a heuristic fallback to legacy `g2alt1`/`g2alt2` format. If the server changes the packing scheme again, `osrs_parse_rebuild_normal` must be updated.

### 7.9 Login Block Field Layout

Any of the following fields may be added, removed, or reordered by Jagex:

- Plain header fields (version, subVersion, serverVersion, clientType, platformType, externalAuth)
- RSA inner block fields (encryptionCheck, OTP, auth_type)
- XTEA payload fields (settings, dimensions, uuid length, platform stats, reflectionCheckerConst)

The current implementation is verified against a **live RuneLite login capture (World 301)** for rev 239.

---

## 8. Connection State Machine

```
DISCONNECTED
    │ send INIT (opcode 14)
    ▼
INIT_SENT ──► receives SUCCESSFUL (0) + session_id
    │         (or failure code → FAILED)
    ▼ send GAMELOGIN (opcode 16)
LOGIN_SENT ──► receives OK (2) + payload
    │            (or other code → FAILED)
    ▼
GAME
    │ (or AUTHENTICATOR → AWAIT_AUTH_CODE)
    │ (or PROOF_OF_WORK → POW)
```

States defined in `osrs_conn_state_t`:
- `OSRS_CONN_DISCONNECTED`
- `OSRS_CONN_INIT_SENT`
- `OSRS_CONN_LOGIN_SENT`
- `OSRS_CONN_AWAIT_AUTH_CODE`
- `OSRS_CONN_POW`
- `OSRS_CONN_GAME`
- `OSRS_CONN_FAILED`

---

## 9. Notable Implementation Details

- **Packet buffer max size:** 8192 bytes (`osrs_packet_t.data[8192]`).
- **UUID persistence:** A 24-character lowercase-hex UUID is stored in `~/.cache/ordl-osrs/uuid` and reused across sessions.
- **Client type overrides:** `OSRS_CLIENT_TYPE`, `OSRS_PLATFORM_TYPE`, `OSRS_SECOND_TYPE` env vars can modify login fields for testing.
- **POW reply:** Hard-coded to opcode `19` with a fixed `VAR_SHORT` length of `8`.
- **Auth flag handling:** When `auth_flag == 1`, the `isaac_in` keystream is advanced by 4 values, but the actual auth code bytes are **not** stored by the current parser (they are read and discarded).

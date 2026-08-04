# ORDL OSRS Client — Session Handoff Document

**Date:** 2026-07-31
**Project:** ordl-osrs — Pure C23 OldSchool RuneScape Client
**Working Directory:** `/devops/projects/off-the-wall/ordl-osrs`

---

## Executive Summary

The client **logs in to live Jagex servers (rev 239), stays connected, and renders
the full game world with real assets**: blended terrain, overlay shapes, floor
textures, all location-object models with OSRS lighting/shading, NPC models,
player models (ident kits + equipment), ground items, and inventory item
sprites. Camera tracks the player with pan + zoom-aware region streaming.

This session was a **full rendering overhaul**. The GPU world renderer
(`src/gl_world.c`) was rewritten end-to-end and a long chain of
foundational bugs in the asset pipeline were fixed (HSL layout, model
decoder section map, smart-reader precedence, config-parser misalignment,
terrain height scale, camera model).

**Verified live:** login on World 1 (F2P), correct regions load, terrain
renders with correct colors/heights/textures, objects/NPCs render (verified
at Lumbridge farms + Tutorial Island).

---

## 1. Authentication

### Live Credentials (Legacy Auth)
```
User: RaphaelOlsen324@ghosty.lol
Pass: nu3qsWtreYP6
```

### Environment Variables for Live Login
```bash
export OSRS_WORLD=oldschool1.runescape.com
export OSRS_USER=RaphaelOlsen324@ghosty.lol
export OSRS_PASS=nu3qsWtreYP6
export OSRS_LOG_LEVEL=info
./build/osrs_client "$HOME/.runelite/jagexcache/oldschool/LIVE/"
```

Note: after an unclean disconnect the server may reply **"Already logged in"**
for ~60 s (session timeout) — wait and retry.

Login flow (unchanged, working): INIT → session_id → GAMELOGIN (RSA+XTEA)
→ optional PoW → OK. See `docs/protocol.md` and prior handoffs for the
login block/RSA/ISAAC details — all verified and untouched this session.

---

## 2. Rendering Architecture

```
┌──────────────────────────────────────────────────────────────┐
│ client.c  (frame loop, camera, input, UI, entity dispatch)   │
├──────────────────────────────────────────────────────────────┤
│ gl_world.c (GPU: EGL pbuffer + GLES2, draws to CPU fb)       │
│   terrain VBO/region · lit-model VBO cache · texture atlas   │
├──────────────────────────────────────────────────────────────┤
│ map.c (terrain/locations) · model.c (model decode+light)     │
│ config.c (defs: obj/npc/item/kit/underlay/overlay/texture)   │
│ render_utils.c (JagexColor HSL palette + terrain colors)     │
│ item_sprite.c (SW 3D rasterizer for inventory icons)         │
└──────────────────────────────────────────────────────────────┘
```

**GL pipeline:** EGL + pbuffer (no FBO — Mesa/AMD quirk), one GLES2 program
for terrain + models, `glReadPixels` readback into the FORGE CPU framebuffer,
software UI drawn on top. Depth buffer: 24-bit, depth computed in-shader as
`(dx + dz + h·128)/65536` matching the dimetric view direction `(1,128,1)`.

**Projection (2:1 dimetric):** tile = `128×64` px at zoom 1.0; height
`0.5` px per model unit (128 units = 1 tile, `LOCAL_TILE_SIZE = 128`).
Screen-centering + camera-focus uniforms in the vertex shader.

**Camera model:** `camera.focus_x/z` (world tile) tracks the player;
WASD pans in world tiles clamped to ±40; pan resets when the player moves;
region loading is centered on the focus with a zoom-aware radius
(`radius = 13/(64·zoom)+1`, ≤3, cache 32).

---

## 3. Terrain Rendering (map.c / gl_world.c)

- **Underlay:** full-tile diamonds, **per-corner HSL blend** of the up-to-4
  adjacent underlay defs (`Σ(hue·256/mult)/count`, `Σsat/count`, `Σlight/count`
  → `osrs_hsl_full_to_rgb`). Def lookup is **`id-1`** (Jagex +1 convention).
  Smooth gouraud terrain — the classic OSRS look.
- **Heights:** per-corner grid from `tile->height` (model units), bilinear for
  sub-tile overlay points. **Scale fixed this session:** `hs = 0.5·zoom`
  (was `4·zoom` = 8× too tall, producing exaggerated "blob" mountains and a
  buried camera).
- **Overlay shapes:** path 0 full, 1 diagonal-half (4 rots), 2 three-quarter
  (4 rots), 3 quarter (4 rots) — exact geometry on the 4×4 sub-grid; rarer
  paths 4–11 currently fall back to full-tile (documented limitation).
  Overlays get `+0.06` height epsilon to win depth over the underlay.
- **Textures:** overlays with a texture id sample the **texture atlas**
  (2048², 256×128px slots, `glTexSubImage2D` on demand). Grass/dirt are
  vertex-colored (as in the real game); stone/wood/mosaic floors are textured.

---

## 4. Model Pipeline (model.c / gl_world.c)

- **Decoder:** Type3 (`FF FD`), Type2 (`FF FE`), Type1 (`FF FF`) all
  implemented with the exact RuneLite section maps. Type2 packed face-flag
  bytes (renderType + texture index). Old format unimplemented (rare).
  Face render types, priorities, transparencies, face textures, texture
  coords + texIndices decoded.
- **Normals + lighting:** exact `ModelDefinition.computeNormals` (256-length
  face normals, vertex magnitude = face count) and `ItemSpriteFactory.light`
  (`ambient+64`, `contrast+768`, light `(-50,-10,-50)`, `var7 = mag·contrast>>8`,
  per-vertex gouraud; flat faces use face normals with `var7·1.5` divisor).
- **Color:** `osrs_hsl_shade_to_rgb` → 64K JagexColor palette
  (`hue<<10|sat<<7|lum`, half-step offsets, `pow(c,0.8)` brightness).
- **UVs:** `computeTextureUVCoordinates` (texIndices mapping triangle,
  barycentric solve) → atlas UVs; textured faces get white vertex colors
  (lit shade modulates texture).
- **Y convention:** cache models use **negative Y = up**; terrain uses
  positive height = up. Model VBOs store raw vertex Y; the shader projects
  with the same formula as terrain and it comes out correct (verified:
  Mining Instructor NPC renders upright).

### Object/location models
- All location types 0–22, correct model selection (`model_types[i] == loc.type`,
  else first entry; type-less defs merge all models).
- Per-object `ambient+64` / `contrast·25+768` lighting (contrast is ×25 in the def).
- Recolors applied; model size scaling (x/16384, y/128, z/16384 in shader units).
- Instance rotation from the 2-bit orientation; placed at `tile center`,
  `ground_h = tile->height` at the location tile.
- Object-def cache (open addressing) + lit-model VBO cache keyed on
  `(model_id, ambient, contrast, recolor_hash)`.

### NPC models
- `NPC_INFO_SMALL/LARGE_V5` **bitstream decoder** (new): 8-bit count header,
  high-res movement loop (stationary/walk/run/crawl/remove), low-res spawn
  loop (index/spawnCycle/ext/deltaX/dir/deltaZ/jump/id14), origin from
  `SET_NPC_UPDATE_ORIGIN` (player pos − scene base). Movement via 3-bit
  direction opcodes; `TURN_ANGLES` for facing.
- NPCs rendered per frame from their def's `model_ids` with def ambient/contrast,
  recolors, width/height scale, and orientation → sin/cos yaw.

### Player models
- Appearance block: `body_type`, `equipment[12]`, `ident_kits[12]` (255→-1),
  `colours[5]`.
- Composition: ident kits (`osrs_config_load_kit`, index 2 archive 3) for
  uncovered slots + worn item models (`male_model0`/`female_model0`) for
  `equipment >= 512`. Body colour (skin) applied as a recolor.
- Local player + nearby high-res players rendered.

### Ground items
- Handlers: `OBJ_ADD_SPECIFIC` (78), `OBJ_DEL_SPECIFIC` (41),
  `OBJ_COUNT_SPECIFIC` (13) with exact alt-endian field decodes
  (`p4Alt2/p4Alt3` for packed coord + quantity).
- Rendered as item inventory models at their tiles. Bulk zone updates
  (`UPDATE_ZONE_FULL_FOLLOWS` etc.) are parsed but payload iteration is
  future work.

---

## 5. Sprites & Textures (config.c / render_utils.c / item_sprite.c)

- **Sprite loader rewritten** — trailer-based format: `spriteCount` at
  `len-2`, header block (maxW/H, paletteLen, per-sprite offsetX/Y/W/H) at
  `len-7-count·8`, u24 palette before that, pixel data from offset 0 with
  per-sprite flag byte (bit0 vertical-scan, bit1 has-alpha). Multi-frame
  archives supported (`file_id` = frame).
- **Texture defs** (index 9 archive 0, file = texture id): `fileId u16`
  (→ sprite archive), `missingColor u16` (HSL16 average), flags, anim
  dir/speed. `osrs_config_texture_pixels` → 128×128 ARGB with `pow(c,0.8)`.
- **Item sprites** (`item_sprite.c`): software 3D rasterizer — model load,
  recolor, light (`ambient+64`, `contrast+768`), zan/yan/xan rotation,
  offsets, perspective (focal 512), painter-sorted gouraud/textured
  triangles into 36×32 ARGB. Cached per item id; blitted into the
  inventory panel with quantity text.

---

## 6. Critical Bug Fixes This Session

| Bug | Root cause | Fix |
|---|---|---|
| All model colors wrong | HSL bits hue/lum swapped + no brightness | Correct JagexColor layout + 0.8 palette |
| Models decode garbage | section map missing `+vertex_count`; `ms_smart_s` precedence bug (`a | b - c` → `a | (b-c)`) | exact RuneLite section map + parens |
| Type2/Type1 models fail | not implemented | full Type1/Type2 decoders |
| Bogus "u32 model ids" (16.7M) | config parsers misalign on unknown opcodes (96 raise, 78/79 sounds, 100-102 entity ops, 249 params) and bounded arrays (models>16, recolors>8, transforms>16) | complete opcode table + consume-all-stream reads |
| Terrain 8× too tall | `hs = 4·zoom` assumed raw bytes = ⅛ tile | `hs = 0.5·zoom` (128 units = 1 tile) |
| Green blob / nothing renders | camera never centered on screen; region loader used top-down math, unloaded the visible region | screen-centering in shader; region loader centers on iso focus |
| Draws no-op in client | nothing (probe) — real cause was the region mismatch above | (kept `eglMakeCurrent` per frame as belt-and-braces) |
| Zoom-out = brown blob | fixed 3×3 region grid | zoom-aware load radius, cache 32 |
| Camera stuck in void | unbounded screen-px pan | world-tile pan clamped ±40, resets on move |
| FPS 0.0 | `fge_frame_time_update` never called | call it each frame |
| NPCs never tracked | NPC_INFO handler was a stub clearing the list | full bitstream decoder |
| Models white/grey | textured faces rendered with placeholder color (127) | texture atlas + UV-computed textured faces |

---

## 7. File Inventory (rendering-relevant)

```
src/gl_world.c        GPU world renderer (terrain, models, atlas, entities)
src/model.c           Type1/2/3 model decode, normals, lighting, recolor, UVs
src/map.c             terrain/location parse, region colors
src/config.c          def loaders (memoized archives), sprite/texture/kit loaders
src/render_utils.c    JagexColor palette, HSL helpers, terrain color
src/item_sprite.c     software 3D item-sprite rasterizer
src/client.c          frame loop, camera, input, UI, entity dispatch, item cache
src/game.c            NPC_INFO + OBJ_* handlers, game state
src/playerinfo.c      PLAYER_INFO decode (+ ident_kits now stored)
tools/test_models.c   model decoder validator
tools/test_gl_world.c standalone renderer harness (zoom/NPC/OBJ/NO_MODELS env)
tools/test_texture.c  texture pipeline verifier
tools/test_item_sprite.c  item sprite verifier
tools/dump_region.c   region tile stats
tools/dump_defs.c     underlay/overlay def colors
```

---

## 8. Build & Test

```bash
make            # everything (lib + tools + client), zero warnings
make test       # crypto + playerinfo suites — ALL PASS
./build/osrs_client "$HOME/.runelite/jagexcache/oldschool/LIVE/"
```

Debug env: `OSRS_FRAME_DUMP=/tmp/frame` (PPM every 60th frame),
`OSRS_GL_DEBUG_MODEL=1` (model load failures),
`GLW_ZOOM/GLW_NPC/GLW_OBJ/GLW_NO_MODELS` (harness toggles).

---

## 9. Known Limitations / TODO

- **Overlay paths 4–11** approximate to full tile (rare; mostly curved path
  borders). Paths 0–3 are exact.
- **Animations** (sequences/frames) not applied — NPCs/players are static
  stand poses. Frame/framemap formats are extracted and ready
  (`docs` + scout notes).
- **Bulk zone updates** (UPDATE_ZONE_FULL_FOLLOWS / PARTIAL_*) parsed but
  payload packet iteration not implemented (ground items currently via
  OBJ_ADD/DEL/COUNT_SPECIFIC only).
- **Old-format models** unimplemented (pre-Type1; very rare in rev 239).
- **Water** renders as flat colored overlay (no animated water shader).
- **Model face alpha** (transparency) decoded but not blended (skip −2 only).
- Top-down legacy renderer (`render_region_topdown`) still uses the old
  blended `render_color` (iso/GL path is the primary).

### Next priorities
1. Animation system (frames + vertex groups → animated NPCs/players).
2. Bulk zone update payload parsing (all ground items + LOC changes).
3. Overlay shapes 4–11 exact tables.
4. Player appearance: correct per-part body-colour recolors + equipment
   head models (male_head/female_head) + jaw/arms/legs variants.
5. Software iso renderer parity fixes (it shares the old color/scale bugs).

---

## 10. References

- **RuneLite cache module:** `/devops/ordl/applications/gaming/ordl-swapper/runelite/cache/`
  (definitions, loaders, `models/JagexColor`, `item/ItemSpriteFactory` — the
  lighting + item-sprite ground truth)
- **RuneLite client GPU plugin:** `.../runelite-client/plugins/gpu/SceneUploader.java`,
  `runelite-api/Perspective.java` (`LOCAL_TILE_SIZE = 128`)
- **rsprot protocol:** `/tmp/rsprot/protocol/osrs-239/` (NPC_INFO, zone packets,
  JagexByteBuf alt-endian primitives)
- Live cache: `~/.runelite/jagexcache/oldschool/LIVE/`

---

*End of handoff. The client renders the world with real assets end-to-end.
Next session: animations, bulk zone updates, overlay shapes 4–11.*

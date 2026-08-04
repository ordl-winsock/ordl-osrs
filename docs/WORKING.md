# OSRS Client - Working Document
|**Date:** 2026-08-01
|**Session:** Rendering correctness overhaul — upside-down world, walls, players, anvils
|**Character:** RaphaelOlsen324@ghosty.lol (Tutorial Island, 3074,9500,0)

---

## Current State Summary

**The client renders the world right-side up with players, NPCs, objects, and
terrain.** This session fixed a chain of compounding orientation bugs that
made the world render vertically mirrored and models sunk/backwards.

### Root-cause fixes (this session)
1. **Row flip never flipped (THE "everything is upside down" bug)** —
   `osrs_gl_world_end`'s "row flip" swapped color *channels* per pixel but
   assigned `top[x]=swap(t)`, `bot[x]=swap(b)`, i.e. each row got its *own*
   value back; rows were never exchanged. The GL bottom-up readback went
   straight to the top-down FORGE framebuffer, so the world was vertically
   mirrored forever. Fixed to `top[x]=swap(b)`, `bot[x]=swap(t)`. (This bug
   also explains the earlier "models must be sunk" illusion — the mirrored
   pipeline made sunk models *look* upright.)
2. **Model Y-sign** — cache models are negative-Y-up (anvil spans y -93..0,
   base at 0). The shared VBO builder (`glw_upload_model_vbo`) used raw Y,
   sinking every model below the terrain by its full height: small objects
   (anvils, players, NPCs) vanished; big ones rendered displaced/upside-down.
   Now negated at emission.
3. **Projection was from-below** — old `vert=fwd*sp-h*cp`, `depth=fwd*cp+h*sp`
   described a camera *below* the ground looking up: every view ray pierced
   the ground plane below upright models, so terrain occluded everything
   (only sunken models ever won the depth test). New projection is a true
   from-above orthographic view: `vert=(-fwd*sp-h*cp)*71.55`,
   `depth=(fwd*cp-h*sp)/256` (depth increases along the NE-down view rays).
4. **Camera yaw was 45° off** — subtracted π/4 from the yaw delta in the
   shader, `osrs_iso_project`, and `osrs_iso_screen_to_world` so the default
   view matches real OSRS yaw 0: north up-screen, east right-screen (verified:
   Lumbridge castle N/top, town S/below, river E/right; matches the minimap).
5. **Object rotation mirrored ("some walls backwards")** — the shader rotated
   CCW-from-above; OSRS `ModelData.rotate` goes CW (`x'=x*cos+z*sin`,
   `z'=z*cos-x*sin`). Orientations 1/3 were mirrored, 0/2 fine — hence
   "some" walls backwards. Shader now matches OSRS exactly.
6. **Appearance blocks never decoded (the "no player model" bug)** — the rev
   239 appearance payload (a) is `pdataAlt2` (every byte +128 on the wire —
   we read it raw, guaranteed garbage), (b) uses variable-length wear slots
   (single 0 byte = empty; else u16: `>=0x800` = item, `>=0x100` = kit
   override). Rewrote `pi_decode_appearance`: strip +128 per byte, decode
   slots correctly (items = `v-0x800`, kit overrides = `(v-0x100)|0x8000`),
   ident kits same encoding. Renderer handles kit overrides. Test updated to
   the 239 wire format; all playerinfo tests pass.
7. **Roof/ceiling removal** — models floating entirely above ground
   (min up-height > 128, e.g. cave-ceiling quads at +240) are hidden within
   a 24-tile radius of the camera focus, so interiors/pits render (this is
   what hid the Tutorial Island anvils); distant roofs stay put.

### Verified
- Offline (`tools/test_gl_world.c`): Lumbridge layout matches the real map
  (castle N, river E); Tutorial Island pit open with anvils/rocks/dunes;
  stalagmites point up; models land exactly at CPU-projected screen rows.
- Live: login OK, world upright, player model + name at screen center,
  NPCs render, water/island layout correct.
- All tests pass (crypto, playerinfo); zero build warnings.

### Follow-up fixes (same session, part 2)
8. **Lighting washout** — reverted the `ambient+256` hack (a sunk-model-era
   workaround) to the deob values: scene objects `ambient+64,
   contrast*25+768`; NPCs/kits/items `ambient+64, contrast+768`. The
   furnace now renders dark with its red mouth instead of white; the cave
   reads as warm dark brown instead of washed out.
9. **Default pitch 26.57° → 45°** — matches the OSRS default camera
   (256/2048 units); the steeper angle sees over low walls into
   pits/interiors like the real client.
10. **Roof removal tuned** — only models floating entirely above ground
    (min up-height > 200) within 3 tiles of the camera focus are hidden;
    wall-mounted props (lanterns at +130) and the dune rim stay.

### Known remaining issues (polish territory)
- **Scene light/shadow** — the fixed light (-50,-10,-50) with def
  ambient/contrast is exact, but OSRS also darkens via a height-based
  shadow map and per-tile baked light; south-facing walls read dark.
- Terrain texture filtering is nearest-neighbor (visible aliasing on the
  plowed-field texture); water is a flat overlay (no animated water).
- Overlay paths 4–11 approximate; bulk zone payload iteration pending;
  some NPCs with sparse skeleton pivots mis-pose; player body-colour
  recolors not yet applied (kits use def defaults).
- Two-region border: heights/colors match (clamped edge) but corner blend
  uses in-region tiles only — subtle seam possible at region borders.

---

## Prior session (2026-07-31): full rendering overhaul — assets, terrain, models, entities

**Character:** RaphaelOlsen324@ghosty.lol (Tutorial Island, 3074,9500,0)

---

## Current State Summary

**The client renders the full game world with real assets.** GPU renderer
(`gl_world.c`) rewritten end-to-end; the entire asset pipeline (cache →
defs → models → colors → display) was audited and fixed against RuneLite.

### What works now
- **Terrain:** per-corner underlay HSL blend (smooth gouraud), overlay
  shapes (paths 0–3 exact, 4–11 fallback), per-corner heights at correct
  scale, floor textures via a 2048² atlas.
- **Objects:** all location types 0–22, per-object lighting, recolors,
  rotation, terrain-height placement, model-size scaling, textured faces.
- **NPCs:** `NPC_INFO_*_V5` bitstream decoder (movement + spawns) + NPC
  models with def lighting/recolors/scale. Verified: Mining Instructor
  renders upright at Tutorial Island.
- **Players:** ident kits + worn equipment + body colours; local + nearby.
- **Ground items:** OBJ_ADD/DEL/COUNT_SPECIFIC handlers + item models.
- **Inventory:** software 3D rasterizer renders item sprites into the panel.
- **Camera:** tracks player, WASD pan (clamped), zoom-aware region radius.
- **Tests:** all crypto + playerinfo suites pass; build has zero warnings.

### Root-cause fixes (this session)
1. **HSL bit layout** — hue/lum were swapped; no brightness transform.
   Correct JagexColor (`hue<<10|sat<<7|lum`, 0.8 pow) as a 64K palette.
2. **Model decoder section map** — missing `+vertex_count` after the vertex
   flags stream shifted every downstream stream; plus a C precedence bug in
   `ms_smart_s` (`a | b - c` parsed as `a | (b-c)`), corrupting 2-byte smarts.
3. **Type1/Type2 model formats** — implemented (were `return NULL`).
4. **Config parser misalignment** — unknown opcodes (96 raise, 78/79/91/93/95
   sound, 100–102 entity ops, 249 params) and bounded arrays (models>16,
   recolors>8, transforms>16) desynced the stream → garbage "16.7M model ids".
   Now: complete opcode table + consume-all-stream bounded reads.
5. **Terrain height scale** — was `4·zoom` (assumed raw byte = ⅛ tile),
   actually 128 units = 1 tile → `0.5·zoom`. This was the "blobs" bug.
6. **Camera/region mismatch** — shader never centered on screen, and the
   region loader used top-down math that unloaded the visible region.
   ("green blob" + "nothing renders"). Fixed both.
7. **Zoom-out brown blob** — fixed 3×3 grid → zoom-aware radius (cache 32).
8. **Camera stuck in void** — unbounded pan → clamped world-tile pan ±40,
   resets on player move.
9. **NPC stub** — NPC_INFO just cleared the list → full bitstream decoder.
10. **White models** — textured faces used placeholder white → atlas + UVs.
11. **Terrain transpose** — `gl_world.c` terrain VBO read `tiles[0][z][x]`
    but the map stores `tiles[plane][x][y]`; the whole map was mirrored along
    the diagonal, so land rendered as water and coastlines ran the wrong way.
    Fixed the tile lookup to `tiles[0][x][z]`.
12. **Location position accumulation** — `parse_locations` used each
    `pos_delta` as an absolute position (`position = pos_delta - 1`) instead
    of accumulating (`position += pos_delta - 1`). Positions are sorted deltas,
    so 99% of objects collapsed to local_x 0–7 (west edge) — "literally nothing
    rendering". Histogram before `4149 20 10 10 4 2 0 0`, after
    `468 424 493 443 463 500 512 512` (even 63×63 spread).
13. **Model Y-sign (THE "nothing renders" bug)** — OSRS model data uses
    **negative Y = up** (head at −196, feet at 0; `item_sprite.c` already
    computes height as `-vertex_y`). `gl_world.c` VBO build used `vertex_y`
    as-is, and the shader does `py = base − wp.y·hs` + `depth ∝ wp.y`, so every
    model was placed *below* the terrain and lost the `GL_LESS` depth test —
    0/1016 entity pixels survived. Negated Y in the VBO build → models stand
    upright, 1062/1062 survive, buildings/trees/NPCs render.
14. **Rebuild base sanity check** — a framing desync produced a garbage
    REBUILD_NORMAL (zone 32700,12417), teleporting the view to a void and
    clobbering the seeded player (so the player vanished). Reject zone coords
    outside [6,2600].
15. **Auto-reconnect** — on FAILED, wait 5s and re-login (retries while the
    old session lingers as "Already logged in"). No more permanent blank screen.
16. **GL context on region upload (THE live "nothing renders" bug)** — the
    live client builds model VBOs during region load, which runs *between*
    frames right after FORGE's present steals the GL context. VBOs landed on
    FORGE's context → invalid in gl_world → live client rendered terrain only
    (0 lanterns vs harness's 240). Fixed by `eglMakeCurrent` in upload_region
    and upload_region_models. Live objects: 480→29948 rock-pixels.

### Skeletal animation (this session)
17. **Sequence loader** — was fundamentally broken: only parsed a def when
    `current_id == id` (never skipping others → stream desync, only seq 0
    loaded), and read frame data from a delta-blob instead of per-def archive
    files (the NPC/item/object pattern). Rewrote to file-based iteration +
    correct RuneLite opcode table (frame data order: delays, id-low16,
    id-high16; maya opcode sizes). Sequences now load (14468 defs).
18. **Model vertex groups** — `packedVertexGroups` (per-vertex bone label)
    parsed for Type1/Type2/Type3 formats (was only stubbed for Type3).
19. **Framemap + frame loaders** (new `anim.c`) — framemap (SKELETONS idx 1)
    and frame (ANIMATIONS idx 0) loaders; `osrs_model_apply_frame` implements
    the RuneLite origin/translate/rotate/scale transform with CircularAngle.
20. **Frame smart reader** — `as_smart_s` used `-0xC000` instead of `-16384`
    (double-subtracting the high byte) → garbage rotations (−32847).
21. **Framemap layout (THE mangling bug)** — RuneLite reads framemap as
    length → all types → all counts → all labels (separated), but I read
    count+labels *interleaved* per bone → garbage label assignments → every
    model over-rotated and exploded. Fixed to the separated layout → NPCs
    walk upright with a proper gait.
22. **Animation wiring** — NPCs merge body-part models (`osrs_model_merge`),
    apply the current sequence frame with per-frame timing, render via
    `osrs_gl_world_draw_model_direct`. Frame/sequence/merged-model caches.
    Verified: Mining tutor walks upright (proper gait). Note: some NPCs with
    sparse skeleton pivots (e.g. common Man) still mis-pose — per-skeleton
    pivot alignment is a follow-up.

### Verified
- Model decoder: 6/6 + 9/9 (Mining Instructor parts) decode, 0 bad indices,
  normals + lit colors plausible.
- Textures: ids 1/2/24/27 → real game textures (water, thatch, marble,
  shingles).
- Item sprites: whip/pickaxe/sword/armor/vial recognizable, shaded.
- Live: login, 25–34 regions streamed, correct colors, NPC renders.

---

## Progress Log

| Time | Action | Status |
|------|--------|--------|
| 07:00 | Reviewed docs/codebase; built baseline (green blob world) | ✅ |
| 07:30 | Found HSL layout bug (JagexColor) | ✅ |
| 08:00 | Found model section-map + smart-reader bugs; decoder validated | ✅ |
| 08:30 | Found "nothing renders" = camera centering + region mismatch | ✅ |
| 09:00 | Camera/region fix; terrain renders with wrong (red) colors | ✅ |
| 09:30 | Fixed underlay blend formula (`hue·256/mult`) → correct greens | ✅ |
| 10:00 | Type1/Type2 decoders; model coverage 301→5000 instances | ✅ |
| 10:30 | Config parser robustness (bogus u32 ids eliminated) | ✅ |
| 11:00 | Sprite loader rewrite; texture defs + atlas; floor textures | ✅ |
| 11:30 | Model face textures (UV compute); NPC_INFO scout | ✅ |
| 12:00 | NPC_INFO decoder; NPC models render; camera fixes (user req) | ✅ |
| 12:30 | **Height scale bug found** (`4→0.5`) — "blobs" fixed | ✅ |
| 13:00 | NPC Y-sign investigation (models upright confirmed); player + ground-item renderers; item sprites wired | ✅ |
| 13:15 | Full clean build, all tests pass, docs updated | ✅ |
| 14:30 | **Terrain transpose fix** (`tiles[0][z][x]`→`[x][z]`) — map mirrored | ✅ |
| 15:00 | **Location position accumulation fix** — objects no longer pile at west edge; spread 63×63 | ✅ |
| 15:40 | **Model Y-sign fix** (`-vertex_y` in VBO) — models no longer buried under terrain; buildings/trees/NPCs render upright | ✅ |
| 16:30 | **Skeletal animation** — sequence/framemap/frame loaders + vertex groups + transform; NPCs walk upright (Mining tutor verified) | ✅ |

---

## Next Steps

1. **Animations** — sequence/frame/framemap decode + vertex-group apply
   (formats extracted). Makes NPCs/players move.
2. **Bulk zone updates** — iterate UPDATE_ZONE_* payload packets
   (ObjAdd/Del/Count/LocAdd/LocDel/LocMerge/MapAnim).
3. **Overlay shapes 4–11** — exact 4×4-grid tables.
4. **Player appearance polish** — per-part body-colour recolors, head models.
5. **Software iso renderer parity** — shares old color/scale bugs (fallback path).

## Notes for Next Session
- The player is on **Tutorial Island** (Mining Instructor nearby). Sparse
  coastal area — use Lumbridge (offline, region 50,50) for rich-scene tests.
- `GLW_*`/`OSRS_*` env toggles in `tools/test_gl_world.c` + client are the
  fast verification path (no login needed offline).
- RuneLite cache module + `item/ItemSpriteFactory.java` is the ground truth
  for anything color/lighting/model related. rsprot for wire formats.

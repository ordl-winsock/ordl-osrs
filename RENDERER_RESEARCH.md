# OSRS Renderer Research & Comparison

## Executive Summary

After studying the rendering pipelines of every major open-source OSRS client and tool, the conclusion is clear: **RuneLite's GPU plugin is the gold standard** — it is the only production-proven, GPU-accelerated OSRS renderer. The rs-map-viewer project adds modern WebGL2 techniques (instancing, multi-draw, GPU-driven terrain). The original Java clients (2004scape, 2009scape) use pure software rasterization and are irrelevant for a GPU pipeline.

---

## 1. RuneLite GPU Plugin (Java + OpenGL 3.3 / LWJGL)

**Status:** Production-proven, 50k+ daily users, battle-tested for years.
**Repository:** https://github.com/runelite/runelite

### Architecture

| Component | Implementation |
|-----------|---------------|
| **API** | OpenGL 3.3 Core (via LWJGL) |
| **Terrain** | SceneUploader: uploads the entire scene as VBOs, split into 8x8 zones |
| **Models** | ModelUploader: per-frame CPU sorting by depth (bucket sort), uploaded to VBO |
| **Textures** | `sampler2DArray` — all textures in a single array, 128x128 each |
| **Shaders** | Vertex + fragment, `#version 330` |
| **Depth** | `GL_LEQUAL` with per-face depth bias (priority) |
| **Instancing** | No (except UI) |
| **Multi-draw** | Optional (`WEBGL_multi_draw` style extension) |

### Vertex Format (packed into 32-bit integers)

```
Position:   float x, float y, float z        (3 floats = 12 bytes)
Color:      int abhsl                        (1 int = 4 bytes)
              - bits 24-31: alpha
              - bits 16-23: depth bias
              - bits 10-15: hue
              - bits 7-9:   saturation
              - bits 0-6:   lightness
Texture:    int packed_tex                   (1 int = 4 bytes)
              - bits 16-31: u coordinate (8.8 fixed)
              - bits 0-15:  texture_id + 1
UV:         int sv                           (1 short = 2 bytes)
              - v coordinate (8.8 fixed)
```

Total: ~22 bytes per vertex. Faces are uploaded as expanded triangles (no index buffer for models).

### Model Depth Sorting

RuneLite sorts model faces on the CPU every frame using a **bucket sort** (counting sort):
1. Project all vertices to screen space
2. Compute face distance = radius + avg(vertex distances)
3. Bucket faces into `diameter` buckets
4. Draw back-to-front within each priority level

This is the biggest bottleneck in their pipeline — O(n) but still CPU-bound for many models.

### Shader Highlights

**Vertex Shader:**
- Computes HSL → RGB in shader via lookup table
- Applies entity tinting (for hitsplats, etc.)
- Computes fog amount based on distance from scene edge with corner rounding
- Applies depth bias: `gl_Position.z += float(bias) / 128.0`
- Texture animation: `fUv += tick * textureAnim * TEXTURE_ANIM_UNIT`

**Fragment Shader:**
- Textured faces: sample array, apply brightness pow(), multiply by light intensity
- Untextured faces: mix between RGB vertex color and HSL-reconstructed color (smooth banding option)
- Fog: linear mix with fog color
- Alpha test for texture edges

### Key Strengths

1. **Correctness:** Pixel-perfect match to software renderer (with minor exceptions)
2. **Performance:** 60+ FPS on mid-range hardware with 184-tile draw distance
3. **Feature-complete:** All OSRS visual features (fog, textures, animations, overlays, roofs)
4. **Robust:** Handles edge cases (bridge tiles, visbelow, multi-level roofs)

### Key Weaknesses

1. **CPU sorting:** Model faces sorted every frame on CPU
2. **No instancing:** Each model uploaded separately
3. **Java overhead:** LWJGL JNI calls, GC pressure
4. **No GPU culling:** All geometry uploaded regardless of visibility

---

## 2. rs-map-viewer (TypeScript + WebGL2)

**Status:** Active project, web-based map explorer.
**Repository:** https://github.com/dennisdev/rs-map-viewer

### Architecture

| Component | Implementation |
|-----------|---------------|
| **API** | WebGL2 (via PicoGL) |
| **Terrain** | Vertex shader samples height map texture, generates geometry on GPU |
| **Models** | Instanced rendering with model info texture (position, height, etc.) |
| **Textures** | `sampler2DArray` with BGRA swizzle |
| **Shaders** | `#version 300 es` |
| **Depth** | `GL_LEQUAL` with priority bias in shader |
| **Instancing** | Yes — `gl_InstanceID` for object placement |
| **Multi-draw** | Yes — `WEBGL_multi_draw` extension |

### Advanced Techniques

1. **GPU Height Map:** Terrain height is sampled from a texture in the vertex shader, enabling smooth LOD and contouring
2. **Instanced Objects:** Objects placed via instance IDs with data stored in a texture
3. **Multi-draw:** Batches multiple draw calls into one
4. **Material System:** Texture animation and alpha cutoff per material
5. **Color Banding:** Intentionally quantizes colors to match OSRS aesthetic:
   ```glsl
   round(v_color.rgb * u_colorBanding) / u_colorBanding
   ```
6. **Smooth Terrain:** Optional smooth interpolation between tile heights
7. **Load Animation:** Tiles fade in with `smoothstep`

### Key Strengths

1. **Modern GPU usage:** Instancing, multi-draw, uniform buffers
2. **Web-based:** Runs anywhere
3. **Clean architecture:** Well-separated concerns

### Key Weaknesses

1. **Not a game client:** No gameplay, just map viewing
2. **WebGL limitations:** No compute shaders, limited texture sizes
3. **Incomplete:** Missing many gameplay features

---

## 3. 2004Scape / Lost City Client (TypeScript / Java)

**Status:** Source port of original OSRS client software renderer.
**Repository:** https://github.com/2004Scape/Client2

### Architecture

- Pure software rasterization (no GPU)
- Original algorithms: Painter's algorithm, edge-walking, flat shading
- Relevant only as reference for correct behavior

---

## 4. Our Current Renderer (C + OpenGL ES 3.0)

### Architecture

| Component | Implementation |
|-----------|---------------|
| **API** | OpenGL ES 3.0 (via EGL) |
| **Terrain** | Single VBO per region, CPU-generated vertices |
| **Models** | Per-entity VBOs, CPU lighting, no sorting |
| **Textures** | `sampler2DArray` (optional) + atlas fallback |
| **Shaders** | `#version 300 es` |
| **Depth** | `GL_GREATER` (inverted Z) |
| **Instancing** | No |

### Current Issues

1. **Terrain winding bug:** Fixed — was using inconsistent triangle winding
2. **Missing normals on merged models:** Fixed — `osrs_model_compute_normals` now called
3. **CPU HSL→RGB:** Done in `osrs_model_light_vertex`, not shader
4. **No face sorting:** Models draw in arbitrary order, causing visual artifacts
5. **No fog:** Not implemented
6. **No depth bias:** Priority faces can z-fight
7. **Black pixels:** Overlay shapes with `col==0` render black; corner colors default to black for tiles without underlay

---

## Comparison Matrix

| Feature | RuneLite GPU | rs-map-viewer | Our Renderer |
|---------|-------------|---------------|--------------|
| Terrain | Zone-based VBO | GPU height map | Single region VBO |
| Model sorting | CPU bucket sort | None (static) | None |
| Textures | Array | Array | Array + atlas |
| HSL→RGB | Shader | Shader | CPU |
| Fog | Edge-rounded | Edge-rounded | None |
| Instancing | No | Yes | No |
| Multi-draw | Optional | Yes | No |
| Depth bias | Per-face | Per-face+priority | None |
| Brightness | Shader pow() | Shader pow() | CPU pre-multiply |
| Texture anim | Shader | Shader | None |

---

## Key Insights for Our New Renderer

### 1. Move HSL→RGB to Shader
RuneLite packs HSL into a single int and converts in the vertex shader. This:
- Reduces vertex buffer size
- Enables smooth banding / no banding toggle
- Allows entity tinting in shader

### 2. Implement Depth Bucket Sort
For correct model rendering, faces MUST be sorted back-to-front. RuneLite's bucket sort is optimal for OSRS because:
- Face counts are bounded (max ~6500 vertices, ~8192 faces)
- Distances are integers in a known range
- Counting sort is O(n) and cache-friendly

### 3. Use Texture Arrays
Both RuneLite and rs-map-viewer use `sampler2DArray`. We already have this — keep it.

### 4. Add Depth Bias for Priority
OSRS uses face priorities (0-10) to control draw order within a model. RuneLite adds `bias/128.0` to `gl_Position.z`. We need this to prevent z-fighting.

### 5. Implement Fog
RuneLite's fog formula with rounded corners is visually important for OSRS's look:
```glsl
float fogDistance = nearestEdgeDistance - rounding * max(0, (nearest + rounding²) / (secondNearest + rounding²));
```

### 6. Consider GPU-Driven Terrain
rs-map-viewer's approach of sampling height maps in the vertex shader is elegant. For our C renderer, we could:
- Pre-compute height map textures per region
- Upload minimal vertex data (tile corners)
- Let GPU interpolate and displace

### 7. Pack Vertex Data
RuneLite packs everything into ints to minimize VBO size and upload time. For ES 3.0:
- Position: 3 floats (12 bytes) or 3 half-floats (6 bytes)
- Color: 1 uint (4 bytes) packed HSL + alpha + bias
- Texture: 1 uint (4 bytes) packed texId + UVs
- Total: ~20 bytes/vertex vs our current 36 bytes

---

## Recommended Architecture for New Renderer

Based on the research, here is the recommended architecture for our new supreme rendering pipeline:

### Phase 1: Fix Current Renderer (Immediate)
1. ✅ Fix terrain winding (done)
2. ✅ Fix merged model normals (done)
3. Fix overlay black color fallback
4. Add depth bias for model priorities
5. Add simple face sorting for models

### Phase 2: Shader-Centric Pipeline
1. Pack HSL into vertex attributes, convert in shader
2. Move brightness/texture animation to shader
3. Add fog computation in shader
4. Implement proper depth bias

### Phase 3: GPU-Driven Optimizations
1. Batch model rendering (uniform buffer with model transforms)
2. Consider GPU terrain generation from height maps
3. Implement frustum culling
4. Add LOD for distant models

### Phase 4: Advanced Features
1. Multi-draw for terrain zones
2. Occlusion culling
3. GPU particle effects
4. Post-processing (FXAA, bloom)

---

## References

- [RuneLite GPU Plugin](https://github.com/runelite/runelite/tree/master/runelite-client/src/main/java/net/runelite/client/plugins/gpu)
- [rs-map-viewer](https://github.com/dennisdev/rs-map-viewer)
- [2004Scape Client2](https://github.com/2004Scape/Client2)
- [OSRS Wiki - Rendering](https://oldschool.runescape.wiki/w/Graphics)

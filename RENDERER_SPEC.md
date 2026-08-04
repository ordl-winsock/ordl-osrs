# OSRS GPU Renderer Specification

## Goals

1. **Correctness:** Pixel-perfect or near-pixel-perfect match to official OSRS client
2. **Performance:** 60+ FPS on integrated graphics, 144+ FPS on discrete GPUs
3. **Efficiency:** Minimize CPU→GPU uploads, minimize draw calls, maximize GPU utilization
4. **Simplicity:** Clean C code, no external dependencies beyond OpenGL/EGL

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         CPU SIDE                            │
├─────────────────────────────────────────────────────────────┤
│  Cache Loader → Model Decoder → Animation → Normal Compute  │
│         ↓              ↓            ↓            ↓          │
│  ┌─────────┐    ┌─────────┐   ┌─────────┐  ┌─────────┐    │
│  │ Terrain │    │ Models  │   │Entities │  │  UI     │    │
│  │ Builder │    │  Cache  │   │  List   │  │ Layer   │    │
│  └────┬────┘    └────┬────┘   └────┬────┘  └────┬────┘    │
│       │              │             │            │          │
│       ▼              ▼             ▼            ▼          │
│  ┌─────────────────────────────────────────────────────┐  │
│  │              GPU UPLOAD & DRAW DISPATCH              │  │
│  └─────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                        GPU SIDE                             │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  Vertex  │  │ Fragment │  │  Depth   │  │  Blend   │   │
│  │  Shader  │  │  Shader  │  │  Test    │  │          │   │
│  │          │  │          │  │          │  │          │   │
│  │ HSL→RGB  │  │ Textures │  │ LEQUAL   │  │ Alpha    │   │
│  │ Transform│  │ Fog      │  │ + Bias   │  │ Blend    │   │
│  │ Fog Calc │  │ Discard  │  │          │  │          │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## Vertex Formats

### Terrain Vertex (20 bytes)

```c
typedef struct {
    float pos[3];        // x, y, z in tile-local space
    uint32_t color;      // Packed: AABBGGRR (matches OSRS palette)
    float uv[2];         // Texture coordinates (0 for non-textured)
    float tex_id;        // Texture array layer + 1 (0 = no texture)
} terrain_vertex_t;
```

Alternative packed format (12 bytes):
```c
typedef struct {
    uint16_t pos[3];     // Tile-local: x,z in [0,64], y in [-1024,1024]
    uint16_t padding;
    uint32_t color;      // Packed HSL + alpha + bias
    uint16_t uv[2];      // 8.8 fixed point
    uint16_t tex_id;     // Texture layer + 1
} packed_terrain_vertex_t;
```

### Model Vertex (20 bytes, matching RuneLite)

```c
typedef struct {
    float pos[3];        // x, y, z in model-local space
    uint32_t abhsl;      // Packed: AA(8) | Bias(8) | Hue(6) | Sat(3) | Lum(7)
    uint32_t tex_info;   // Packed: U(16) | TexId+1(16)
    uint16_t v;          // V coordinate (8.8 fixed)
    uint16_t padding;
} model_vertex_t;
```

**abhsl packing:**
- Bits 24-31: Alpha (0-255), 255 = opaque
- Bits 16-23: Depth bias (0-255), added to gl_Position.z
- Bits 10-15: Hue (0-63)
- Bits 7-9: Saturation (0-7)
- Bits 0-6: Lightness (0-127)

**tex_info packing:**
- Bits 16-31: U coordinate (8.8 fixed point)
- Bits 0-15: Texture ID + 1 (0 = no texture)

---

## Shader Specification

### Vertex Shader: `osrs_vert.glsl`

**Inputs:**
```glsl
layout(location = 0) in vec3 a_pos;
layout(location = 1) in uint a_abhsl;
layout(location = 2) in uvec2 a_tex;  // (tex_info, v)
```

**Uniforms (UBO):**
```glsl
layout(std140) uniform scene_ubo {
    mat4 u_view_proj;
    vec3 u_camera_pos;
    float u_render_distance;
    vec3 u_sky_color;
    float u_fog_depth;
    float u_brightness;
    float u_color_banding;
    float u_tick;
    int u_use_fog;
};
```

**Per-model uniforms:**
```glsl
uniform vec3 u_model_pos;
uniform vec3 u_model_scale;
uniform vec2 u_model_rot;  // (sin, cos)
uniform ivec4 u_entity_tint; // (hue, sat, lum, amount)
```

**Outputs:**
```glsl
out vec4 v_color;
noperspective centroid out float v_hsl;
flat out int v_tex_id;
out vec2 v_uv;
out float v_fog;
```

**Processing:**
1. Decode HSL from `a_abhsl`
2. Apply entity tint: `hsl += (tint - hsl) * tint_amount / 128`
3. Convert HSL to RGB via lookup table
4. Set `v_color = vec4(rgb, 1.0 - alpha/255.0)`
5. If textured: `v_hsl = float(a_abhsl & 0xFFFF)`, else pack HSL into v_hsl
6. Decode texture ID and UVs
7. Apply texture animation if `u_tick > 0`
8. Transform position: rotate Y, scale, translate
9. Compute `gl_Position = u_view_proj * vec4(world_pos, 1.0)`
10. Add depth bias: `gl_Position.z += float(bias) / 128.0`
11. Compute fog amount based on distance from scene edge with corner rounding

### Fragment Shader: `osrs_frag.glsl`

**Inputs:** Same as vertex outputs

**Uniforms:**
```glsl
uniform sampler2DArray u_textures;
uniform float u_brightness;
uniform float u_texture_light_mode;
uniform vec4 u_fog_color;
uniform float u_color_banding;
```

**Processing:**
1. If textured (`v_tex_id > 0`):
   - Sample `texture(u_textures, vec3(v_uv, v_tex_id - 1)).bgra`
   - Apply brightness: `pow(texture_color, vec4(u_brightness))`
   - Compute light from v_hsl: `light = v_hsl / 127.0`
   - Mix lighting modes: `(1 - mode) * light + mode * v_color.rgb`
   - `out_color = texture_color * vec4(mul, v_color.a)`
2. Else (flat color):
   - Decode HSL from `v_hsl`
   - `rgb = mix(v_color.rgb, hsl_to_rgb(hsl), u_color_banding > 0)`
   - Apply color banding if enabled
   - `out_color = vec4(rgb, v_color.a)`
3. Apply fog: `out_color.rgb = mix(out_color.rgb, u_fog_color.rgb, v_fog)`
4. Alpha test: if `out_color.a < 0.01`, discard

---

## Rendering Pipeline

### Frame Loop

```
1. Clear framebuffer (color + depth)
2. Update camera UBO
3. Draw terrain (opaque, back-to-front by region)
4. Sort and draw entities:
   a. For each visible entity:
      - Compute animation frame (if animated)
      - Compute normals (if animated)
      - Bucket-sort faces by depth
      - Upload to VBO
   b. Draw opaque faces (priority 0-9, back-to-front)
   c. Draw transparent faces (priority 10, back-to-front)
5. Draw UI overlay
6. Swap buffers
```

### Terrain Rendering

**Current:** Single VBO per region, 22k-34k indices
**Recommended:** Keep current approach but:
- Pack vertices tighter (20 bytes → 12 bytes)
- Use indexed drawing (already doing this)
- Add frustum culling per region
- Consider GPU height map for smoother terrain (future)

### Model Rendering

**Critical: Face Sorting**

Every model with >1 face must have faces sorted back-to-front before upload:

```c
void sort_model_faces(osrs_model_t *model, vec3_t camera_pos) {
    // 1. Project vertices to screen space
    // 2. For each face, compute average depth
    // 3. Bucket sort faces into buckets [0, diameter)
    // 4. Emit vertices in back-to-front order
}
```

**Transparency Handling:**
- Faces with alpha = 0: skip entirely
- Faces with alpha = 255: opaque path
- Faces with 0 < alpha < 255: transparent path (draw after all opaque)

---

## Data Structures

### Render Context

```c
typedef struct {
    // GPU resources
    GLuint terrain_vao;
    GLuint terrain_vbo;
    GLuint terrain_ibo;
    
    GLuint model_vao;
    GLuint model_vbo;      // Streamed every frame
    GLuint model_vbo_tex;  // Streamed every frame
    
    GLuint tex_array;
    
    // Shaders
    GLuint shader_terrain;
    GLuint shader_model;
    GLuint shader_ui;
    
    // UBO
    GLuint scene_ubo;
    scene_ubo_t scene_data;
    
    // State
    int screen_w, screen_h;
    vec3_t camera_pos;
    mat4_t view_proj;
    
    // Caches
    model_vbo_cache_t *model_cache;
    int model_cache_count;
} osrs_render_ctx_t;
```

### Scene UBO (std140 aligned)

```c
typedef struct {
    float view_proj[16];      // 64 bytes
    float camera_pos[3];      // 12 bytes
    float render_distance;    // 4 bytes
    float sky_color[3];       // 12 bytes
    float fog_depth;          // 4 bytes
    float brightness;         // 4 bytes
    float color_banding;      // 4 bytes
    float tick;               // 4 bytes
    int use_fog;              // 4 bytes
    // padding to 16-byte alignment: 4 bytes
} scene_ubo_t;  // Total: 112 bytes
```

---

## Implementation Plan

### Sprint 1: Shader-Centric Pipeline
- [ ] Pack HSL into vertex attributes
- [ ] Write HSL→RGB shader function
- [ ] Move brightness to shader
- [ ] Add per-face depth bias
- [ ] Verify terrain renders identically

### Sprint 2: Model Sorting & Correctness
- [ ] Implement bucket sort for model faces
- [ ] Separate opaque/transparent draw paths
- [ ] Fix all model rendering issues (player, NPC, objects)
- [ ] Add texture animation support

### Sprint 3: Fog & Polish
- [ ] Implement scene-edge fog with corner rounding
- [ ] Add sky color support
- [ ] Color banding toggle
- [ ] Performance profiling and optimization

### Sprint 4: GPU-Driven Terrain (Future)
- [ ] Height map textures per region
- [ ] GPU vertex displacement
- [ ] Frustum culling
- [ ] LOD system

---

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Frame time | < 16.6ms (60 FPS) | On AMD RX Vega 56 |
| Draw calls | < 500/frame | Terrain: 1 per visible region |
| VBO uploads | < 10MB/frame | Models only, terrain static |
| CPU time | < 8ms/frame | Animation + sorting on CPU |
| GPU time | < 8ms/frame | Vertex + fragment shading |

---

## Compatibility

- **OpenGL ES 3.0** minimum (our current target)
- **OpenGL 3.3 Core** for desktop builds (future)
- **No extensions required** for base functionality
- Optional: `GL_OES_texture_float_linear` for HDR (future)

---

## Testing Checklist

- [ ] Terrain renders without gaps or flickering
- [ ] Terrain colors match software renderer
- [ ] Player model visible with correct appearance
- [ ] NPC models animate correctly
- [ ] Object models render with correct orientation
- [ ] Textures display with correct UV mapping
- [ ] Texture animation works (water, fire, etc.)
- [ ] Transparency renders correctly (no sorting artifacts)
- [ ] Fog blends correctly at scene edges
- [ ] No z-fighting on overlapping faces
- [ ] Performance: 60+ FPS in crowded areas

---

## Appendix: HSL to RGB Algorithm

```glsl
vec3 hsl_to_rgb(vec3 hsl) {
    float h = hsl.x;
    float s = hsl.y;
    float l = hsl.z;
    
    if (s == 0.0) {
        return vec3(l / 127.0);
    }
    
    float q = l < 64.0 ? l * (127.0 + s) / 127.0 / 2.0
                       : l + s - l * s / 127.0;
    float p = 2.0 * l - q;
    
    float hr = h / 63.0;
    vec3 rgb;
    rgb.r = hue_to_rgb(p, q, hr + 1.0/3.0);
    rgb.g = hue_to_rgb(p, q, hr);
    rgb.b = hue_to_rgb(p, q, hr - 1.0/3.0);
    return rgb;
}

float hue_to_rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}
```

Note: OSRS uses a specific 64K color palette. For exact matching, use a 128x512 lookup texture or compute mathematically.

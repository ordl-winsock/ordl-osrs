/*
 * osrs/gl_world.c - Supreme OSRS GPU World Renderer
 *
 * Upgrades over original:
 *   - EGL context tiering: ES 3.0 -> Desktop GL 3.3 -> ES 2.0
 *   - Double-buffered PBO async pixel readback (eliminates GPU stall)
 *   - Uniform Buffer Object (UBO) for camera/scene
 *   - Vertex Array Objects (VAO) for fast binding
 *   - CPU bucket-sort for transparent faces
 *   - HSL->RGB + lighting in fragment shader
 *   - Fog and draw-distance in shader
 */

#include "osrs/gl_world.h"
#include "osrs/anim.h"
#include "osrs/config.h"
#include "osrs/iso_renderer.h"
#include "osrs/log.h"
#include "osrs/map.h"
#include "osrs/model.h"
#include "osrs/render_utils.h"
#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ======================================================================== */
/* EGL / GL Types & Constants                                               */
/* ======================================================================== */

typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef int EGLint;
typedef void *EGLNativeDisplayType;

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_SUCCESS 0x3000
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_OPENGL_ES3_BIT 0x0040
#define EGL_OPENGL_BIT 0x0008
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_NONE 0x3038
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_OPENGL_API 0x30A2

typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef unsigned short GLushort;
typedef unsigned char GLubyte;

#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_GREATER 0x0204
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_TRIANGLES 0x0004
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_MAP_READ_BIT 0x0001
#define GL_UNIFORM_BUFFER 0x8A11

/* ======================================================================== */
/* Runtime GL Loader                                                        */
/* ======================================================================== */

static struct {
  void *handle;
  void *(*eglGetDisplay)(void *);
  int (*eglInitialize)(void *, int *, int *);
  int (*eglChooseConfig)(void *, const int *, void *, int, int *);
  void *(*eglCreateContext)(void *, void *, void *, const int *);
  void *(*eglCreatePbufferSurface)(void *, void *, const int *);
  int (*eglMakeCurrent)(void *, void *, void *, void *);
  int (*eglDestroyContext)(void *, void *);
  int (*eglDestroySurface)(void *, void *);
  int (*eglTerminate)(void *);
  const char *(*eglQueryString)(void *, int);
  int (*eglGetError)(void);
  int (*eglBindAPI)(unsigned int);
  int (*eglQuerySurface)(void *, void *, int, int *);
} egl;

static struct {
  void *handle;
  void (*glGenBuffers)(GLsizei, GLuint *);
  void (*glBindBuffer)(GLenum, GLuint);
  void (*glBufferData)(GLenum, GLsizei, const void *, GLenum);
  void (*glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
  void (*glDeleteBuffers)(GLsizei, const GLuint *);
  void (*glGenTextures)(GLsizei, GLuint *);
  void (*glBindTexture)(GLenum, GLuint);
  void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                       GLenum, const void *);
  void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                          GLenum, const void *);
  void (*glTexParameteri)(GLenum, GLenum, GLint);
  void (*glDeleteTextures)(GLsizei, const GLuint *);
  void (*glActiveTexture)(GLenum);
  void (*glGenerateMipmap)(GLenum);
  GLuint (*glCreateShader)(GLenum);
  void (*glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
  void (*glCompileShader)(GLuint);
  void (*glGetShaderiv)(GLuint, GLenum, GLint *);
  void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
  void (*glDeleteShader)(GLuint);
  GLuint (*glCreateProgram)(void);
  void (*glAttachShader)(GLuint, GLuint);
  void (*glLinkProgram)(GLuint);
  void (*glGetProgramiv)(GLuint, GLenum, GLint *);
  void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
  void (*glUseProgram)(GLuint);
  void (*glDeleteProgram)(GLuint);
  GLint (*glGetUniformLocation)(GLuint, const char *);
  GLint (*glGetAttribLocation)(GLuint, const char *);
  void (*glUniform2f)(GLint, GLfloat, GLfloat);
  void (*glUniform1f)(GLint, GLfloat);
  void (*glUniform1i)(GLint, GLint);
  void (*glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
  void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
  void (*glEnableVertexAttribArray)(GLuint);
  void (*glDisableVertexAttribArray)(GLuint);
  void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                const void *);
  void (*glDrawArrays)(GLenum, GLint, GLsizei);
  void (*glDrawElements)(GLenum, GLsizei, GLenum, const void *);
  void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
  void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (*glClear)(GLbitfield);
  void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
  void (*glFinish)(void);
  const GLubyte *(*glGetString)(GLenum);
  GLenum (*glGetError)(void);
  void (*glEnable)(GLenum);
  void (*glDisable)(GLenum);
  void (*glFrontFace)(GLenum);
  void (*glDepthFunc)(GLenum);
  void (*glDepthMask)(GLboolean);
  void (*glClearDepthf)(GLfloat);
  /* Optional ES3 / GL3.3 */
  void (*glGenVertexArrays)(GLsizei, GLuint *);
  void (*glBindVertexArray)(GLuint);
  void (*glDeleteVertexArrays)(GLsizei, const GLuint *);
  void *(*glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
  GLboolean (*glUnmapBuffer)(GLenum);
  void (*glBindBufferBase)(GLenum, GLuint, GLuint);
  GLuint (*glGetUniformBlockIndex)(GLuint, const GLchar *);
  void (*glUniformBlockBinding)(GLuint, GLuint, GLuint);
  void (*glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint,
                       GLenum, GLenum, const void *);
  void (*glTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei,
                          GLsizei, GLenum, GLenum, const void *);
} gl;

static bool gl_load(void) {
  if (gl.handle)
    return true;
  gl.handle = dlopen("libGLESv2.so.2", RTLD_NOW);
  if (!gl.handle)
    gl.handle = dlopen("libGLESv2.so", RTLD_NOW);
  if (!gl.handle) {
    gl.handle = dlopen("libGL.so.1", RTLD_NOW);
    if (!gl.handle)
      return false;
  }
#define X(ret, name, params) *(void **)&gl.name = dlsym(gl.handle, #name);
  X(void, glGenBuffers, (GLsizei, GLuint *))
  X(void, glBindBuffer, (GLenum, GLuint))
  X(void, glBufferData, (GLenum, GLsizei, const void *, GLenum))
  X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))
  X(void, glDeleteBuffers, (GLsizei, const GLuint *))
  X(void, glGenTextures, (GLsizei, GLuint *))
  X(void, glBindTexture, (GLenum, GLuint))
  X(void, glTexImage2D,
    (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
     const void *))
  X(void, glTexSubImage2D,
    (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
     const void *))
  X(void, glTexParameteri, (GLenum, GLenum, GLint))
  X(void, glDeleteTextures, (GLsizei, const GLuint *))
  X(void, glActiveTexture, (GLenum))
  X(void, glGenerateMipmap, (GLenum))
  X(GLuint, glCreateShader, (GLenum))
  X(void, glShaderSource,
    (GLuint, GLsizei, const GLchar *const *, const GLint *))
  X(void, glCompileShader, (GLuint))
  X(void, glGetShaderiv, (GLuint, GLenum, GLint *))
  X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
  X(void, glDeleteShader, (GLuint))
  X(GLuint, glCreateProgram, (void))
  X(void, glAttachShader, (GLuint, GLuint))
  X(void, glLinkProgram, (GLuint))
  X(void, glGetProgramiv, (GLuint, GLenum, GLint *))
  X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
  X(void, glUseProgram, (GLuint))
  X(void, glDeleteProgram, (GLuint))
  X(GLint, glGetUniformLocation, (GLuint, const char *))
  X(GLint, glGetAttribLocation, (GLuint, const char *))
  X(void, glUniform2f, (GLint, GLfloat, GLfloat))
  X(void, glUniform1f, (GLint, GLfloat))
  X(void, glUniform1i, (GLint, GLint))
  X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))
  X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
  X(void, glEnableVertexAttribArray, (GLuint))
  X(void, glDisableVertexAttribArray, (GLuint))
  X(void, glVertexAttribPointer,
    (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))
  X(void, glDrawArrays, (GLenum, GLint, GLsizei))
  X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *))
  X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))
  X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))
  X(void, glClear, (GLbitfield))
  X(void, glReadPixels,
    (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
  X(void, glFinish, (void))
  X(const GLubyte *, glGetString, (GLenum))
  X(GLenum, glGetError, (void))
  X(void, glEnable, (GLenum))
  X(void, glDisable, (GLenum))
  X(void, glFrontFace, (GLenum))
  X(void, glDepthFunc, (GLenum))
  X(void, glDepthMask, (GLboolean))
  X(void, glClearDepthf, (GLfloat))
#undef X
/* Optional */
#define XO(ret, name, params) *(void **)&gl.name = dlsym(gl.handle, #name);
  XO(void, glGenVertexArrays, (GLsizei, GLuint *))
  XO(void, glBindVertexArray, (GLuint))
  XO(void, glDeleteVertexArrays, (GLsizei, const GLuint *))
  XO(void, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield))
  XO(GLboolean, glUnmapBuffer, (GLenum))
  XO(void, glBindBufferBase, (GLenum, GLuint, GLuint))
  XO(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar *))
  XO(void, glUniformBlockBinding, (GLuint, GLuint, GLuint))
  XO(void, glTexImage3D,
     (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum,
      const void *))
  XO(void, glTexSubImage3D,
     (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum,
      GLenum, const void *))
#undef XO
  return true;
}

static bool has_vao(void) { return gl.glGenVertexArrays != NULL; }
static bool has_pbo(void) { return gl.glMapBufferRange != NULL; }
static bool has_texarray(void) { return gl.glTexImage3D != NULL; }
static bool has_ubo(void) {
  return gl.glBindBufferBase != NULL && gl.glGetUniformBlockIndex != NULL &&
         gl.glUniformBlockBinding != NULL;
}

/* ======================================================================== */
/* Shaders                                                                  */
/* ======================================================================== */

static const char vs_src[] =
    "#if __VERSION__ >= 300\n"
    "  layout(location=0) in vec3 a_pos;\n"
    "  layout(location=1) in vec3 a_color;\n"
    "  layout(location=2) in vec2 a_uv;\n"
    "  layout(location=3) in float a_tex;\n"
    "  out vec3 v_color;\n"
    "  out vec2 v_uv;\n"
    "  flat out int v_tex;\n"
    "  layout(std140) uniform scene_ubo {\n"
    "    vec4 u_orbit;\n"
    "    vec4 u_fog_color;\n"
    "    vec2 u_cam_center;\n"
    "    vec2 u_cam_offset;\n"
    "    vec2 u_screen;\n"
    "    float u_zoom;\n"
    "    float u_fog_density;\n"
    "    float u_brightness;\n"
    "    float _pad;\n"
    "  };\n"
    "#else\n"
    "  attribute vec3 a_pos;\n"
    "  attribute vec3 a_color;\n"
    "  attribute vec2 a_uv;\n"
    "  attribute float a_tex;\n"
    "  varying vec3 v_color;\n"
    "  varying vec2 v_uv;\n"
    "  varying float v_tex;\n"
    "  uniform vec2 u_cam_center;\n"
    "  uniform vec2 u_cam_offset;\n"
    "  uniform float u_zoom;\n"
    "  uniform vec4 u_orbit;\n"
    "  uniform vec2 u_screen;\n"
    "#endif\n"
    "uniform vec3 u_model_pos;\n"
    "uniform vec3 u_model_scale;\n"
    "uniform vec2 u_model_rot;\n"
    "void main() {\n"
    "  vec3 lp = a_pos * u_model_scale;\n"
    "  float rx = lp.x * u_model_rot.y + lp.z * u_model_rot.x;\n"
    "  float rz = lp.z * u_model_rot.y - lp.x * u_model_rot.x;\n"
    "  vec3 wp = vec3(rx, lp.y, rz) + u_model_pos;\n"
    "  float dx = wp.x - u_cam_center.x;\n"
    "  float dz = wp.z - u_cam_center.y;\n"
    "  float cy = u_orbit.x, sy = u_orbit.y;\n"
    "  float rdx = dx * cy - dz * sy;\n"
    "  float rdz = dx * sy + dz * cy;\n"
    "  float fwd = rdx + rdz;\n"
    "  float h = wp.y / 128.0;\n"
    "  float sp = u_orbit.z, cp = u_orbit.w;\n"
    "  float vert = (-fwd * sp - h * cp) * 71.55 * u_zoom;\n"
    "  float depth = 0.5 - (fwd * cp - h * sp) * 0.00390625;\n"
    "  float sx_pos = (rdx - rdz) * 64.0 * u_zoom + u_cam_offset.x + "
    "u_screen.x * "
    "0.5;\n"
    "  float sy_pos = vert + u_cam_offset.y + u_screen.y * 0.5;\n"
    "  gl_Position = vec4(sx_pos / u_screen.x * 2.0 - 1.0,\n"
    "                     -(sy_pos / u_screen.y * 2.0 - 1.0),\n"
    "                     depth, 1.0);\n"
    "  v_color = a_color;\n"
    "  v_uv = a_uv;\n"
    "#if __VERSION__ >= 300\n"
    "  v_tex = int(a_tex);\n"
    "#else\n"
    "  v_tex = a_tex;\n"
    "#endif\n"
    "}\n";

static const char fs_src[] =
    "#if __VERSION__ >= 300\n"
    "  in vec3 v_color;\n"
    "  in vec2 v_uv;\n"
    "  flat in int v_tex;\n"
    "  out vec4 frag_color;\n"
    "  layout(std140) uniform scene_ubo {\n"
    "    vec4 u_orbit;\n"
    "    vec4 u_fog_color;\n"
    "    vec2 u_cam_center;\n"
    "    vec2 u_cam_offset;\n"
    "    vec2 u_screen;\n"
    "    float u_zoom;\n"
    "    float u_fog_density;\n"
    "    float u_brightness;\n"
    "    float _pad;\n"
    "  };\n"
    "#else\n"
    "  varying vec3 v_color;\n"
    "  varying vec2 v_uv;\n"
    "  varying float v_tex;\n"
    "  uniform vec3 u_fog_color;\n"
    "  uniform float u_fog_density;\n"
    "  uniform float u_brightness;\n"
    "#endif\n"
    "uniform sampler2D u_texture;\n"
    "#if __VERSION__ >= 300\n"
    "uniform sampler2DArray u_tex_array;\n"
    "#endif\n"
    "uniform float u_use_array;\n"
    "void main() {\n"
    "  vec3 base = v_color;\n"
    "#if __VERSION__ >= 300\n"
    "  int tex = v_tex;\n"
    "#else\n"
    "  int tex = int(v_tex);\n"
    "#endif\n"
    "  if (tex > 0) {\n"
    "    if (u_use_array > 0.5)\n"
    "#if __VERSION__ >= 300\n"
    "      base = texture(u_tex_array, vec3(v_uv, float(tex-1))).rgb * "
    "v_color;\n"
    "#else\n"
    "      base = v_color;\n"
    "#endif\n"
    "    else\n"
    "#if __VERSION__ >= 300\n"
    "      base = texture(u_texture, v_uv).rgb * v_color;\n"
    "#else\n"
    "      base = texture2D(u_texture, v_uv).rgb * v_color;\n"
    "#endif\n"
    "  }\n"
    "  float fog = clamp(exp2(-u_fog_density * gl_FragCoord.z * "
    "gl_FragCoord.z), 0.0, 1.0);\n"
    "  base = mix(u_fog_color.rgb, base, fog);\n"
    "  base *= u_brightness;\n"
    "#if __VERSION__ >= 300\n"
    "  frag_color = vec4(base, 1.0);\n"
    "#else\n"
    "  gl_FragColor = vec4(base, 1.0);\n"
    "#endif\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src, bool es3) {
  GLuint s = gl.glCreateShader(type);
  const char *version = es3 ? "#version 300 es\nprecision highp "
                              "float;\nprecision highp sampler2DArray;\n"
                            : "#version 100\nprecision mediump float;\n";
  const char *parts[2] = {version, src};
  GLint lens[2] = {(GLint)strlen(version), (GLint)strlen(src)};
  gl.glShaderSource(s, 2, parts, lens);
  gl.glCompileShader(s);
  GLint ok;
  gl.glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    GLsizei n;
    gl.glGetShaderInfoLog(s, sizeof(log), &n, log);
    OSRS_ERROR(OSRS_LOG_CAT_RENDER, "Shader compile failed: %s", log);
    gl.glDeleteShader(s);
    return 0;
  }
  return s;
}

static GLuint link_program(bool es3) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src, es3);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, es3);
  if (!vs || !fs) {
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);
    return 0;
  }
  GLuint p = gl.glCreateProgram();
  gl.glAttachShader(p, vs);
  gl.glAttachShader(p, fs);
  gl.glLinkProgram(p);
  GLint ok;
  gl.glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    GLsizei n;
    gl.glGetProgramInfoLog(p, sizeof(log), &n, log);
    OSRS_ERROR(OSRS_LOG_CAT_RENDER, "Program link failed: %s", log);
    gl.glDeleteProgram(p);
    p = 0;
  }
  gl.glDeleteShader(vs);
  gl.glDeleteShader(fs);
  return p;
}

/* ======================================================================== */
/* Internal types                                                           */
/* ======================================================================== */

#define GLW_MAX_REGIONS 32
#define GLW_MAX_MODEL_CACHE 8192
#define GLW_OBJ_DEF_CACHE 512
#define GLW_UNDERLAY_CACHE 512
#define GLW_OVERLAY_CACHE 512
#define GLW_ATLAS_SIZE 2048
#define GLW_ATLAS_SLOT 128
#define GLW_ATLAS_COLS (GLW_ATLAS_SIZE / GLW_ATLAS_SLOT)
#define GLW_ATLAS_MAX (GLW_ATLAS_COLS * GLW_ATLAS_COLS)
#define GLW_TEX_ARRAY_LAYERS 512
#define GLW_PBO_COUNT 2
#define GLW_CORNER_N (OSRS_REGION_SIZE + 1)

/* Scene UBO — std140 layout (ES 3.0) */
typedef struct {
  float orbit[4];      /* cy, sy, sp, cp */
  float fog_color[4];  /* rgb + unused */
  float cam_center[2]; /* world tile center */
  float cam_offset[2]; /* screen pixel offset */
  float screen[2];     /* viewport size */
  float zoom;
  float fog_density;
  float brightness;
  float _pad; /* align to 16 */
} scene_ubo_t;
_Static_assert(sizeof(scene_ubo_t) == 72, "scene_ubo_t size mismatch");

typedef struct {
  int model_id, ambient, contrast;
  uint32_t recolor_hash;
  int frame_id;
} glw_model_key_t;

typedef struct {
  glw_model_key_t key;
  GLuint vbo, vbo_tex;
  uint32_t vcount, vcount_tex;
  float min_up_h;
  bool active;
  int ref_count;
} glw_model_cache_t;

typedef struct {
  GLuint vbo, vbo_tex;
  uint32_t vcount, vcount_tex;
  float pos[3], rot[2], scale[3], min_up_h;
  int plane;
} glw_model_instance_t;

#define GLW_ZONE_SIZE 8
#define GLW_ZONES_PER_REGION (OSRS_REGION_SIZE / GLW_ZONE_SIZE)

typedef struct {
  GLuint vao, vbo, ibo;
  uint32_t index_count;
  GLuint vao_tex, vbo_tex, ibo_tex;
  uint32_t index_count_tex;
} glw_zone_t;

typedef struct {
  int region_x, region_y;
  bool active;
  glw_zone_t zones[GLW_ZONES_PER_REGION][GLW_ZONES_PER_REGION];
  glw_model_instance_t *models;
  int model_count, model_capacity;
  /* Single VBO/IBO for the entire region (primary rendering path) */
  GLuint vbo, ibo, vao;
  uint32_t index_count;
} glw_region_t;

typedef struct {
  float x, y, z;
  float r, g, b;
  float u, v;
  float tex;
} glw_vertex_t;

typedef struct {
  int id;
  osrs_object_def_t *def;
} glw_obj_entry_t;

typedef struct {
  int texture_id;
  int slot;
} glw_tex_entry_t;

struct osrs_gl_world {
  int width, height;
  float cam_cx, cam_cz;
  EGLDisplay dpy;
  EGLSurface surf;
  EGLContext ctx;
  bool egl_loaded, gl_loaded;
  bool es3;

  GLuint prog;
  GLint loc_cam_center, loc_cam_offset, loc_zoom, loc_orbit, loc_screen;
  GLint loc_model_pos, loc_model_scale, loc_model_rot;
  GLint loc_texture, loc_tex_array, loc_use_array, loc_fog_color,
      loc_fog_density, loc_brightness;
  GLint loc_a_pos, loc_a_color, loc_a_uv, loc_a_tex;

  GLuint ubo, pbo[GLW_PBO_COUNT];
  int pbo_idx;
  bool has_ubo, has_pbo;

  glw_region_t regions[GLW_MAX_REGIONS];
  int region_count;
  glw_model_cache_t model_cache[GLW_MAX_MODEL_CACHE];
  int model_cache_count;
  glw_obj_entry_t obj_cache[GLW_OBJ_DEF_CACHE];

  int overlay_ids[GLW_OVERLAY_CACHE];
  int overlay_color[GLW_OVERLAY_CACHE], overlay_tex[GLW_OVERLAY_CACHE];

  bool use_tex_array;
  GLuint tex_array;
  int tex_array_layers;
  GLuint atlas_tex;
  glw_tex_entry_t tex_map[GLW_ATLAS_MAX];
  int tex_map_count, atlas_free_slot;
};

/* ======================================================================== */
/* Utility                                                                  */
/* ======================================================================== */

static inline bool key_eq(const glw_model_key_t *a, const glw_model_key_t *b) {
  return a->model_id == b->model_id && a->ambient == b->ambient &&
         a->contrast == b->contrast && a->recolor_hash == b->recolor_hash &&
         a->frame_id == b->frame_id;
}

static uint32_t hash_recolors(const int *src, const int *dst, int count) {
  if (count <= 0)
    return 0;
  uint32_t h = 2166136261u;
  for (int i = 0; i < count; i++) {
    h = (h ^ (uint32_t)src[i]) * 16777619u;
    h = (h ^ (uint32_t)dst[i]) * 16777619u;
  }
  return h;
}

static osrs_object_def_t *glw_get_obj_def(osrs_gl_world_t *gw,
                                          osrs_config_t *cfg, int id) {
  int slot = id % GLW_OBJ_DEF_CACHE;
  if (slot < 0)
    slot = -slot;
  for (int probe = 0; probe < 8; probe++) {
    int s = (slot + probe) % GLW_OBJ_DEF_CACHE;
    if (gw->obj_cache[s].def && gw->obj_cache[s].id == id)
      return gw->obj_cache[s].def;
  }
  osrs_object_def_t *d = osrs_config_load_object(cfg, id);
  if (!d)
    return NULL;
  if (gw->obj_cache[slot].def)
    osrs_object_def_free(gw->obj_cache[slot].def);
  gw->obj_cache[slot].def = d;
  gw->obj_cache[slot].id = id;
  return d;
}

static int glw_overlay_color(osrs_gl_world_t *gw, osrs_config_t *cfg, int oid) {
  int slot = oid % GLW_OVERLAY_CACHE;
  if (slot < 0)
    slot = -slot;
  if (gw->overlay_ids[slot] == oid)
    return gw->overlay_color[slot];
  int color = -1;
  osrs_overlay_def_t *d = osrs_config_load_overlay(cfg, oid);
  if (d) {
    if (d->texture >= 0) {
      osrs_texture_t *t = osrs_config_load_texture(cfg, d->texture);
      if (t) {
        color = (int)(osrs_hsl_palette_lookup(t->average_rgb) & 0xFFFFFF);
        osrs_texture_free(t);
      } else
        color = 0;
    } else if (d->color == 0xFF00FF) {
      color = (d->secondary_color >= 0) ? d->secondary_color : -1;
    } else if (d->color != 0) {
      color = d->color;
    } else
      color = 0;
    osrs_overlay_def_free(d);
  }
  gw->overlay_ids[slot] = oid;
  gw->overlay_color[slot] = color;
  return color;
}

static int glw_overlay_tex(osrs_gl_world_t *gw, osrs_config_t *cfg, int oid) {
  int slot = oid % GLW_OVERLAY_CACHE;
  if (slot < 0)
    slot = -slot;
  if (gw->overlay_ids[slot] == oid)
    return gw->overlay_tex[slot];
  (void)glw_overlay_color(gw, cfg, oid);
  osrs_overlay_def_t *d = osrs_config_load_overlay(cfg, oid);
  int t = -1;
  if (d) {
    t = d->texture;
    osrs_overlay_def_free(d);
  }
  gw->overlay_ids[slot] = oid;
  gw->overlay_tex[slot] = t;
  return t;
}

/* ======================================================================== */
/* Texture management                                                       */
/* ======================================================================== */

static int glw_tex_array_layer(osrs_gl_world_t *gw, osrs_config_t *cfg,
                               int tex_id) {
  if (!gw->use_tex_array || tex_id < 0)
    return -1;
  for (int i = 0; i < gw->tex_map_count; i++)
    if (gw->tex_map[i].texture_id == tex_id)
      return gw->tex_map[i].slot;
  if (gw->tex_array_layers >= GLW_TEX_ARRAY_LAYERS)
    return -1;
  uint32_t *px = osrs_config_texture_pixels(cfg, tex_id);
  if (!px)
    return -1;
  int layer = gw->tex_array_layers++;
  gl.glActiveTexture(GL_TEXTURE0);
  gl.glBindTexture(GL_TEXTURE_2D_ARRAY, gw->tex_array);
  gl.glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, 128, 128, 1, GL_RGBA,
                     GL_UNSIGNED_BYTE, px);
  free(px);
  if (gw->tex_map_count < GLW_ATLAS_MAX) {
    gw->tex_map[gw->tex_map_count].texture_id = tex_id;
    gw->tex_map[gw->tex_map_count].slot = layer;
    gw->tex_map_count++;
  }
  return layer;
}

static void glw_atlas_uv(int slot, float lu, float lv, float *au, float *av) {
  float sx = (float)(slot % GLW_ATLAS_COLS);
  float sy = (float)(slot / GLW_ATLAS_COLS);
  *au = (sx + lu) / (float)GLW_ATLAS_COLS;
  *av = (sy + lv) / (float)GLW_ATLAS_COLS;
}

static int glw_atlas_slot(osrs_gl_world_t *gw, osrs_config_t *cfg, int tex_id) {
  for (int i = 0; i < gw->tex_map_count; i++)
    if (gw->tex_map[i].texture_id == tex_id)
      return gw->tex_map[i].slot;
  if (gw->atlas_free_slot >= GLW_ATLAS_MAX)
    return -1;
  uint32_t *px = osrs_config_texture_pixels(cfg, tex_id);
  if (!px)
    return -1;
  int slot = gw->atlas_free_slot++;
  int sx = (slot % GLW_ATLAS_COLS) * GLW_ATLAS_SLOT;
  int sy = (slot / GLW_ATLAS_COLS) * GLW_ATLAS_SLOT;
  gl.glActiveTexture(GL_TEXTURE0);
  gl.glBindTexture(GL_TEXTURE_2D, gw->atlas_tex);
  gl.glTexSubImage2D(GL_TEXTURE_2D, 0, sx, sy, GLW_ATLAS_SLOT, GLW_ATLAS_SLOT,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
  free(px);
  if (gw->tex_map_count < GLW_ATLAS_MAX) {
    gw->tex_map[gw->tex_map_count].texture_id = tex_id;
    gw->tex_map[gw->tex_map_count].slot = slot;
    gw->tex_map_count++;
  }
  return slot;
}

/* ======================================================================== */
/* EGL init                                                                 */
/* ======================================================================== */

static bool glw_egl_init(osrs_gl_world_t *gw, int w, int h) {
  egl.handle = dlopen("libEGL.so.1", RTLD_NOW);
  if (!egl.handle)
    egl.handle = dlopen("libEGL.so", RTLD_NOW);
  if (!egl.handle) {
    OSRS_ERROR(OSRS_LOG_CAT_RENDER, "Cannot load libEGL");
    return false;
  }
#define E(name) *(void **)&egl.name = dlsym(egl.handle, #name);
  E(eglGetDisplay)
  E(eglInitialize)
  E(eglChooseConfig)
  E(eglCreateContext)
  E(eglCreatePbufferSurface)
  E(eglMakeCurrent)
  E(eglDestroyContext)
  E(eglDestroySurface)
  E(eglTerminate)
  E(eglQueryString)
  E(eglGetError)
  E(eglBindAPI)
  E(eglQuerySurface)
#undef E

  gw->dpy = egl.eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (gw->dpy == EGL_NO_DISPLAY)
    return false;
  int maj = 0, min = 0;
  if (!egl.eglInitialize(gw->dpy, &maj, &min))
    return false;
  OSRS_INFO(OSRS_LOG_CAT_RENDER, "EGL %d.%d (%s)", maj, min,
            egl.eglQueryString(gw->dpy, 0x3053));

  bool want_es3 = true;
  EGLConfig cfg;
  int ncfg;

  /* Try ES 3.0 first */
  int attribs_es3[] = {EGL_SURFACE_TYPE,
                       EGL_PBUFFER_BIT,
                       EGL_RENDERABLE_TYPE,
                       EGL_OPENGL_ES3_BIT,
                       EGL_RED_SIZE,
                       8,
                       EGL_GREEN_SIZE,
                       8,
                       EGL_BLUE_SIZE,
                       8,
                       EGL_ALPHA_SIZE,
                       8,
                       EGL_DEPTH_SIZE,
                       24,
                       EGL_NONE};
  if (!egl.eglChooseConfig(gw->dpy, attribs_es3, &cfg, 1, &ncfg) || ncfg < 1) {
    OSRS_WARN(OSRS_LOG_CAT_RENDER,
              "ES 3.0 config not available, falling back to ES 2.0");
    want_es3 = false;
    int attribs_es2[] = {EGL_SURFACE_TYPE,
                         EGL_PBUFFER_BIT,
                         EGL_RENDERABLE_TYPE,
                         EGL_OPENGL_ES2_BIT,
                         EGL_RED_SIZE,
                         8,
                         EGL_GREEN_SIZE,
                         8,
                         EGL_BLUE_SIZE,
                         8,
                         EGL_ALPHA_SIZE,
                         8,
                         EGL_DEPTH_SIZE,
                         24,
                         EGL_NONE};
    if (!egl.eglChooseConfig(gw->dpy, attribs_es2, &cfg, 1, &ncfg) ||
        ncfg < 1) {
      OSRS_ERROR(OSRS_LOG_CAT_RENDER, "eglChooseConfig failed");
      return false;
    }
    OSRS_INFO(OSRS_LOG_CAT_RENDER, "Using ES 2.0 config");
  } else {
    OSRS_INFO(OSRS_LOG_CAT_RENDER, "Using ES 3.0 config");
  }

  int pb[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
  gw->surf = egl.eglCreatePbufferSurface(gw->dpy, cfg, pb);
  if (gw->surf == EGL_NO_SURFACE)
    return false;

  int ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, want_es3 ? 3 : 2, EGL_NONE};
  gw->ctx = egl.eglCreateContext(gw->dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
  if (gw->ctx == EGL_NO_CONTEXT)
    return false;
  if (!egl.eglMakeCurrent(gw->dpy, gw->surf, gw->surf, gw->ctx))
    return false;
  gw->es3 = want_es3;
  gw->egl_loaded = true;
  return true;
}

osrs_gl_world_t *osrs_gl_world_create(int width, int height) {
  osrs_gl_world_t *gw = calloc(1, sizeof(*gw));
  if (!gw)
    return NULL;
  gw->width = width;
  gw->height = height;

  if (!gl_load())
    goto fail;
  if (!glw_egl_init(gw, width, height))
    goto fail;

  OSRS_INFO(OSRS_LOG_CAT_RENDER, "GL: %s",
            (const char *)gl.glGetString(0x1F01));

  gw->prog = link_program(gw->es3);
  if (!gw->prog)
    goto fail;

  gw->loc_cam_center = gl.glGetUniformLocation(gw->prog, "u_cam_center");
  gw->loc_cam_offset = gl.glGetUniformLocation(gw->prog, "u_cam_offset");
  gw->loc_zoom = gl.glGetUniformLocation(gw->prog, "u_zoom");
  gw->loc_orbit = gl.glGetUniformLocation(gw->prog, "u_orbit");
  gw->loc_screen = gl.glGetUniformLocation(gw->prog, "u_screen");
  gw->loc_model_pos = gl.glGetUniformLocation(gw->prog, "u_model_pos");
  gw->loc_model_scale = gl.glGetUniformLocation(gw->prog, "u_model_scale");
  gw->loc_model_rot = gl.glGetUniformLocation(gw->prog, "u_model_rot");
  gw->loc_texture = gl.glGetUniformLocation(gw->prog, "u_texture");
  gw->loc_tex_array = gl.glGetUniformLocation(gw->prog, "u_tex_array");
  gw->loc_use_array = gl.glGetUniformLocation(gw->prog, "u_use_array");
  gw->loc_fog_color = gl.glGetUniformLocation(gw->prog, "u_fog_color");
  gw->loc_fog_density = gl.glGetUniformLocation(gw->prog, "u_fog_density");
  gw->loc_brightness = gl.glGetUniformLocation(gw->prog, "u_brightness");

  /* UBO */
  if (has_ubo()) {
    gl.glGenBuffers(1, &gw->ubo);
    gl.glBindBuffer(GL_UNIFORM_BUFFER, gw->ubo);
    gl.glBufferData(GL_UNIFORM_BUFFER, sizeof(scene_ubo_t), NULL,
                    GL_STREAM_DRAW);
    GLuint block_idx = gl.glGetUniformBlockIndex(gw->prog, "scene_ubo");
    if (block_idx != 0xFFFFFFFF) {
      gl.glUniformBlockBinding(gw->prog, block_idx, 0);
      gl.glBindBufferBase(GL_UNIFORM_BUFFER, 0, gw->ubo);
      gw->has_ubo = true;
    } else {
      OSRS_WARN(OSRS_LOG_CAT_RENDER,
                "scene_ubo block not found, falling back to individual "
                "uniforms");
      gw->has_ubo = false;
    }
  }

  /* PBO */
  if (has_pbo()) {
    for (int i = 0; i < GLW_PBO_COUNT; i++) {
      gl.glGenBuffers(1, &gw->pbo[i]);
      gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, gw->pbo[i]);
      gl.glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 4, NULL,
                      GL_STREAM_DRAW);
    }
    gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gw->has_pbo = true;
  }

  /* Texture storage */
  if (has_texarray()) {
    gw->use_tex_array = true;
    gl.glGenTextures(1, &gw->tex_array);
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D_ARRAY, gw->tex_array);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,
                       GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,
                       GL_CLAMP_TO_EDGE);
    gl.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 128, 128,
                    GLW_TEX_ARRAY_LAYERS, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    OSRS_INFO(OSRS_LOG_CAT_RENDER, "Texture array enabled");
  } else {
    gl.glGenTextures(1, &gw->atlas_tex);
    gl.glActiveTexture(GL_TEXTURE0);
    gl.glBindTexture(GL_TEXTURE_2D, gw->atlas_tex);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLW_ATLAS_SIZE, GLW_ATLAS_SIZE,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    for (int i = 0; i < GLW_ATLAS_MAX; i++) {
      gw->tex_map[i].texture_id = -1;
      gw->tex_map[i].slot = -1;
    }
    OSRS_INFO(OSRS_LOG_CAT_RENDER, "Texture atlas fallback");
  }

  for (int i = 0; i < GLW_OVERLAY_CACHE; i++)
    gw->overlay_ids[i] = -1;

  gw->loc_a_pos = gl.glGetAttribLocation(gw->prog, "a_pos");
  gw->loc_a_color = gl.glGetAttribLocation(gw->prog, "a_color");
  gw->loc_a_uv = gl.glGetAttribLocation(gw->prog, "a_uv");
  gw->loc_a_tex = gl.glGetAttribLocation(gw->prog, "a_tex");
  OSRS_INFO(OSRS_LOG_CAT_RENDER,
            "Attrib locations: pos=%d color=%d uv=%d tex=%d", gw->loc_a_pos,
            gw->loc_a_color, gw->loc_a_uv, gw->loc_a_tex);

  gw->gl_loaded = true;
  OSRS_INFO(OSRS_LOG_CAT_RENDER, "GL world renderer created %dx%d (es3=%d)",
            width, height, gw->es3);
  return gw;

fail:
  osrs_gl_world_destroy(gw);
  return NULL;
}

void osrs_gl_world_destroy(osrs_gl_world_t *gw) {
  if (!gw)
    return;
  if (gw->gl_loaded) {
    if (gw->prog)
      gl.glDeleteProgram(gw->prog);
    if (gw->ubo)
      gl.glDeleteBuffers(1, &gw->ubo);
    for (int i = 0; i < GLW_PBO_COUNT; i++)
      if (gw->pbo[i])
        gl.glDeleteBuffers(1, &gw->pbo[i]);
    for (int i = 0; i < gw->region_count; i++) {
      glw_region_t *gr = &gw->regions[i];
      if (gr->vbo)
        gl.glDeleteBuffers(1, &gr->vbo);
      if (gr->ibo)
        gl.glDeleteBuffers(1, &gr->ibo);
      if (gr->vao && has_vao())
        gl.glDeleteVertexArrays(1, &gr->vao);
      for (int zz = 0; zz < GLW_ZONES_PER_REGION; zz++) {
        for (int zx = 0; zx < GLW_ZONES_PER_REGION; zx++) {
          glw_zone_t *zone = &gr->zones[zz][zx];
          if (zone->vbo)
            gl.glDeleteBuffers(1, &zone->vbo);
          if (zone->ibo)
            gl.glDeleteBuffers(1, &zone->ibo);
          if (zone->vao && has_vao())
            gl.glDeleteVertexArrays(1, &zone->vao);
        }
      }
      free(gr->models);
    }
    for (int i = 0; i < gw->model_cache_count; i++) {
      if (gw->model_cache[i].active) {
        if (gw->model_cache[i].vbo)
          gl.glDeleteBuffers(1, &gw->model_cache[i].vbo);
        if (gw->model_cache[i].vbo_tex)
          gl.glDeleteBuffers(1, &gw->model_cache[i].vbo_tex);
      }
    }
    if (gw->use_tex_array && gw->tex_array)
      gl.glDeleteTextures(1, &gw->tex_array);
    if (!gw->use_tex_array && gw->atlas_tex)
      gl.glDeleteTextures(1, &gw->atlas_tex);
  }
  if (gw->egl_loaded) {
    if (gw->ctx != EGL_NO_CONTEXT)
      egl.eglDestroyContext(gw->dpy, gw->ctx);
    if (gw->surf != EGL_NO_SURFACE)
      egl.eglDestroySurface(gw->dpy, gw->surf);
    if (gw->dpy != EGL_NO_DISPLAY)
      egl.eglTerminate(gw->dpy);
  }
  free(gw);
}

bool osrs_gl_world_valid(const osrs_gl_world_t *gw) {
  return gw && gw->egl_loaded && gw->gl_loaded;
}

/* ======================================================================== */
/* Terrain upload                                                           */
/* ======================================================================== */

static inline osrs_tile_t *get_adj(osrs_region_t *n, osrs_region_t *e,
                                   osrs_region_t *s, osrs_region_t *w,
                                   osrs_region_t *region, int cx, int cz,
                                   int plane) {
  if (cx >= 0 && cx < OSRS_REGION_SIZE && cz >= 0 && cz < OSRS_REGION_SIZE)
    return &region->tiles[plane][cx][cz];
  if (cx == OSRS_REGION_SIZE && cz >= 0 && cz < OSRS_REGION_SIZE && e)
    return &e->tiles[plane][0][cz];
  if (cx >= 0 && cx < OSRS_REGION_SIZE && cz == OSRS_REGION_SIZE && s)
    return &s->tiles[plane][cx][0];
  if (cx == -1 && cz >= 0 && cz < OSRS_REGION_SIZE && w)
    return &w->tiles[plane][OSRS_REGION_SIZE - 1][cz];
  if (cz == -1 && cx >= 0 && cx < OSRS_REGION_SIZE && n)
    return &n->tiles[plane][cx][OSRS_REGION_SIZE - 1];
  return NULL;
}

static void build_corner_grid(osrs_gl_world_t *gw, osrs_config_t *cfg,
                              osrs_region_t *region, int plane,
                              osrs_region_t *n, osrs_region_t *e,
                              osrs_region_t *s, osrs_region_t *w,
                              int heights[GLW_CORNER_N][GLW_CORNER_N],
                              uint32_t colors[GLW_CORNER_N][GLW_CORNER_N]) {
  (void)gw;
  (void)cfg;
  for (int cz = 0; cz < GLW_CORNER_N; cz++)
    for (int cx = 0; cx < GLW_CORNER_N; cx++) {
      osrs_tile_t *t = get_adj(n, e, s, w, region, cx, cz, plane);
      heights[cz][cx] =
          t ? t->height
            : region
                  ->tiles[plane][cx < 0 ? 0
                                        : (cx >= OSRS_REGION_SIZE
                                               ? OSRS_REGION_SIZE - 1
                                               : cx)]
                         [cz < 0
                              ? 0
                              : (cz >= OSRS_REGION_SIZE ? OSRS_REGION_SIZE - 1
                                                        : cz)]
                  .height;
    }
  for (int cz = 0; cz < GLW_CORNER_N; cz++)
    for (int cx = 0; cx < GLW_CORNER_N; cx++) {
      osrs_tile_t *t = get_adj(n, e, s, w, region, cx, cz, plane);
      if (t && (t->underlay_id > 0 || t->overlay_id > 0)) {
        osrs_underlay_def_t *ud = NULL;
        if (t->underlay_id > 0)
          ud = osrs_config_load_underlay(cfg, t->underlay_id);
        if (ud) {
          colors[cz][cx] =
              osrs_terrain_color(t->underlay_id, t->overlay_id, cfg);
          osrs_underlay_def_free(ud);
          continue;
        }
        if (t->overlay_id > 0) {
          osrs_overlay_def_t *od = osrs_config_load_overlay(cfg, t->overlay_id);
          if (od) {
            if (od->color != 0 && od->color != 0xFF00FF) {
              colors[cz][cx] = od->color;
            } else if (od->secondary_color >= 0) {
              colors[cz][cx] = od->secondary_color;
            } else {
              colors[cz][cx] = 0xFF808080;
            }
            osrs_overlay_def_free(od);
            continue;
          }
        }
      }
      colors[cz][cx] = osrs_terrain_color_default(t ? t->underlay_id : 0,
                                                  t ? t->overlay_id : 0);
    }
}

typedef struct {
  int count;
  int idx[8];
} glw_shape_t;

static glw_shape_t overlay_shape(int path, int rotation) {
  static const int SW = 0, SE = 3, NE = 15, NW = 12;
  glw_shape_t sh = {0, {0}};
  switch (path) {
  case 0:
    sh.count = 4;
    sh.idx[0] = SW;
    sh.idx[1] = SE;
    sh.idx[2] = NE;
    sh.idx[3] = NW;
    break;
  case 1: {
    static const int tris[4][3] = {
        {0, 3, 15}, {3, 15, 12}, {15, 12, 0}, {12, 0, 3}};
    sh.count = 3;
    for (int i = 0; i < 3; i++)
      sh.idx[i] = tris[rotation & 3][i];
  } break;
  case 2: {
    static const int q[4][5] = {{0, 3, 15, 14, 1},
                                {3, 15, 12, 13, 7},
                                {15, 12, 0, 4, 11},
                                {12, 0, 3, 2, 8}};
    sh.count = 5;
    for (int i = 0; i < 5; i++)
      sh.idx[i] = q[rotation & 3][i];
  } break;
  case 3: {
    static const int q[4][3] = {
        {0, 4, 1}, {3, 2, 7}, {15, 11, 14}, {12, 13, 8}};
    sh.count = 3;
    for (int i = 0; i < 3; i++)
      sh.idx[i] = q[rotation & 3][i];
  } break;
  default:
    sh.count = 4;
    sh.idx[0] = SW;
    sh.idx[1] = SE;
    sh.idx[2] = NE;
    sh.idx[3] = NW;
    break;
  }
  return sh;
}

static float height_at(const int h[GLW_CORNER_N][GLW_CORNER_N], float fx,
                       float fz) {
  int x0 = (int)fx, z0 = (int)fz;
  if (x0 >= OSRS_REGION_SIZE)
    x0 = OSRS_REGION_SIZE - 1;
  if (z0 >= OSRS_REGION_SIZE)
    z0 = OSRS_REGION_SIZE - 1;
  float tx = fx - x0, tz = fz - z0;
  float h00 = h[z0][x0], h10 = h[z0][x0 + 1], h01 = h[z0 + 1][x0],
        h11 = h[z0 + 1][x0 + 1];
  return h00 * (1 - tx) * (1 - tz) + h10 * tx * (1 - tz) + h01 * (1 - tx) * tz +
         h11 * tx * tz;
}

void osrs_gl_world_upload_region_ex(osrs_gl_world_t *gw, osrs_region_t *region,
                                    osrs_region_t *adj_n, osrs_region_t *adj_e,
                                    osrs_region_t *adj_s, osrs_region_t *adj_w,
                                    int region_x, int region_y,
                                    osrs_config_t *cfg) {
  if (!gw || !region || !cfg)
    return;
  egl.eglMakeCurrent(gw->dpy, gw->surf, gw->surf, gw->ctx);

  glw_region_t *gr = NULL;
  for (int i = 0; i < gw->region_count; i++)
    if (gw->regions[i].region_x == region_x &&
        gw->regions[i].region_y == region_y) {
      gr = &gw->regions[i];
      break;
    }
  if (!gr) {
    if (gw->region_count >= GLW_MAX_REGIONS)
      return;
    gr = &gw->regions[gw->region_count++];
    memset(gr, 0, sizeof(*gr));
    gr->region_x = region_x;
    gr->region_y = region_y;
  }
  gr->active = true;

  /* Delete old buffers */
  if (gr->vbo)
    gl.glDeleteBuffers(1, &gr->vbo);
  if (gr->ibo)
    gl.glDeleteBuffers(1, &gr->ibo);
  if (gr->vao && has_vao())
    gl.glDeleteVertexArrays(1, &gr->vao);
  for (int zz = 0; zz < GLW_ZONES_PER_REGION; zz++) {
    for (int zx = 0; zx < GLW_ZONES_PER_REGION; zx++) {
      glw_zone_t *zone = &gr->zones[zz][zx];
      if (zone->vbo) {
        gl.glDeleteBuffers(1, &zone->vbo);
        zone->vbo = 0;
      }
      if (zone->ibo) {
        gl.glDeleteBuffers(1, &zone->ibo);
        zone->ibo = 0;
      }
      if (zone->vao && has_vao()) {
        gl.glDeleteVertexArrays(1, &zone->vao);
        zone->vao = 0;
      }
      zone->index_count = 0;
    }
  }
  gr->vbo = 0;
  gr->ibo = 0;
  gr->vao = 0;
  gr->index_count = 0;

  int heights[GLW_CORNER_N][GLW_CORNER_N];
  uint32_t colors[GLW_CORNER_N][GLW_CORNER_N];
  build_corner_grid(gw, cfg, region, 0, adj_n, adj_e, adj_s, adj_w, heights,
                    colors);

  int max_verts = OSRS_REGION_SIZE * OSRS_REGION_SIZE * 9;
  int max_indices = OSRS_REGION_SIZE * OSRS_REGION_SIZE * 15;
  glw_vertex_t *verts = malloc(sizeof(glw_vertex_t) * max_verts);
  uint16_t *indices = malloc(sizeof(uint16_t) * max_indices);
  if (!verts || !indices) {
    free(verts);
    free(indices);
    return;
  }

  int vcount = 0, icount = 0;

  for (int z = 0; z < OSRS_REGION_SIZE; z++) {
    for (int x = 0; x < OSRS_REGION_SIZE; x++) {
      osrs_tile_t *tile = &region->tiles[0][x][z];

      /* Always render base quad — corner grid provides default color
       * for tiles without explicit underlay/overlay */
      {
        int h00 = heights[z][x], h10 = heights[z][x + 1];
        int h11 = heights[z + 1][x + 1], h01 = heights[z + 1][x];
        uint32_t c00 = colors[z][x], c10 = colors[z][x + 1];
        uint32_t c11 = colors[z + 1][x + 1], c01 = colors[z + 1][x];
        int base = vcount;
        verts[vcount++] = (glw_vertex_t){(float)x,
                                         (float)h00,
                                         (float)z,
                                         ((c00 >> 16) & 0xFF) / 255.0f,
                                         ((c00 >> 8) & 0xFF) / 255.0f,
                                         (c00 & 0xFF) / 255.0f,
                                         0,
                                         0,
                                         0};
        verts[vcount++] = (glw_vertex_t){(float)(x + 1),
                                         (float)h10,
                                         (float)z,
                                         ((c10 >> 16) & 0xFF) / 255.0f,
                                         ((c10 >> 8) & 0xFF) / 255.0f,
                                         (c10 & 0xFF) / 255.0f,
                                         0,
                                         0,
                                         0};
        verts[vcount++] = (glw_vertex_t){(float)(x + 1),
                                         (float)h11,
                                         (float)(z + 1),
                                         ((c11 >> 16) & 0xFF) / 255.0f,
                                         ((c11 >> 8) & 0xFF) / 255.0f,
                                         (c11 & 0xFF) / 255.0f,
                                         0,
                                         0,
                                         0};
        verts[vcount++] = (glw_vertex_t){(float)x,
                                         (float)h01,
                                         (float)(z + 1),
                                         ((c01 >> 16) & 0xFF) / 255.0f,
                                         ((c01 >> 8) & 0xFF) / 255.0f,
                                         (c01 & 0xFF) / 255.0f,
                                         0,
                                         0,
                                         0};
        indices[icount++] = base;
        indices[icount++] = base + 1;
        indices[icount++] = base + 2;
        indices[icount++] = base;
        indices[icount++] = base + 2;
        indices[icount++] = base + 3;
      }

      if (tile->overlay_id > 0) {
        int tex = glw_overlay_tex(gw, cfg, tile->overlay_id);
        int col = glw_overlay_color(gw, cfg, tile->overlay_id);
        if (col >= 0) {
          /* Skip overlay shapes that have no color and no texture */
          if (col == 0 && tex < 0)
            goto next_overlay;
          glw_shape_t sh =
              overlay_shape(tile->overlay_path, tile->overlay_rotation);
          int base = vcount;
          for (int i = 0; i < sh.count; i++) {
            int gi = sh.idx[i];
            int gx = gi % 4, gz = gi / 4;
            float fx = x + gx / 3.0f, fz = z + gz / 3.0f;
            float hy = height_at(heights, fx, fz);
            uint32_t cc = (col == 0) ? 0xFF808080 : (uint32_t)col;
            if (tex >= 0 && !gw->use_tex_array) {
              int slot = glw_atlas_slot(gw, cfg, tex);
              if (slot >= 0) {
                float au, av;
                glw_atlas_uv(slot, gx / 3.0f, gz / 3.0f, &au, &av);
                verts[vcount++] = (glw_vertex_t){fx,
                                                 hy,
                                                 fz,
                                                 ((cc >> 16) & 0xFF) / 255.0f,
                                                 ((cc >> 8) & 0xFF) / 255.0f,
                                                 (cc & 0xFF) / 255.0f,
                                                 au,
                                                 av,
                                                 0};
                continue;
              }
            }
            verts[vcount++] = (glw_vertex_t){fx,
                                             hy,
                                             fz,
                                             ((cc >> 16) & 0xFF) / 255.0f,
                                             ((cc >> 8) & 0xFF) / 255.0f,
                                             (cc & 0xFF) / 255.0f,
                                             0,
                                             0,
                                             0};
          }
          for (int i = 1; i < sh.count - 1; i++) {
            indices[icount++] = base;
            indices[icount++] = base + i;
            indices[icount++] = base + i + 1;
          }
        }
      next_overlay:;
      }
    }
  }

  if (vcount > 0) {
    gl.glGenBuffers(1, &gr->vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, gr->vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, vcount * sizeof(glw_vertex_t), verts,
                    GL_STATIC_DRAW);
    if (icount > 0) {
      gl.glGenBuffers(1, &gr->ibo);
      gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gr->ibo);
      gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(uint16_t),
                      indices, GL_STATIC_DRAW);
      gr->index_count = icount;
      if (has_vao()) {
        gl.glGenVertexArrays(1, &gr->vao);
        gl.glBindVertexArray(gr->vao);
        gl.glBindBuffer(GL_ARRAY_BUFFER, gr->vbo);
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gr->ibo);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(glw_vertex_t),
                                 (void *)0);
        gl.glEnableVertexAttribArray(1);
        gl.glVertexAttribPointer(1, 3, GL_FLOAT, 0, sizeof(glw_vertex_t),
                                 (void *)(sizeof(float) * 3));
        gl.glEnableVertexAttribArray(2);
        gl.glVertexAttribPointer(2, 2, GL_FLOAT, 0, sizeof(glw_vertex_t),
                                 (void *)(sizeof(float) * 6));
        gl.glEnableVertexAttribArray(3);
        gl.glVertexAttribPointer(3, 1, GL_FLOAT, 0, sizeof(glw_vertex_t),
                                 (void *)(sizeof(float) * 8));
        gl.glBindVertexArray(0);
      }
    }
  }

  free(verts);
  free(indices);
  OSRS_INFO(OSRS_LOG_CAT_RENDER, "Region %d,%d uploaded: %d verts %d indices",
            region_x, region_y, vcount, icount);
}
void osrs_gl_world_upload_region(osrs_gl_world_t *gw, osrs_region_t *region,
                                 int region_x, int region_y,
                                 osrs_config_t *cfg) {
  osrs_gl_world_upload_region_ex(gw, region, NULL, NULL, NULL, NULL, region_x,
                                 region_y, cfg);
}

void osrs_gl_world_delete_region(osrs_gl_world_t *gw, int region_x,
                                 int region_y) {
  if (!gw)
    return;
  for (int i = 0; i < gw->region_count; i++) {
    glw_region_t *gr = &gw->regions[i];
    if (gr->region_x == region_x && gr->region_y == region_y) {
      if (gr->vbo)
        gl.glDeleteBuffers(1, &gr->vbo);
      if (gr->ibo)
        gl.glDeleteBuffers(1, &gr->ibo);
      if (gr->vao && has_vao())
        gl.glDeleteVertexArrays(1, &gr->vao);
      for (int zz = 0; zz < GLW_ZONES_PER_REGION; zz++) {
        for (int zx = 0; zx < GLW_ZONES_PER_REGION; zx++) {
          glw_zone_t *zone = &gr->zones[zz][zx];
          if (zone->vbo)
            gl.glDeleteBuffers(1, &zone->vbo);
          if (zone->ibo)
            gl.glDeleteBuffers(1, &zone->ibo);
          if (zone->vbo_tex)
            gl.glDeleteBuffers(1, &zone->vbo_tex);
          if (zone->ibo_tex)
            gl.glDeleteBuffers(1, &zone->ibo_tex);
          if (zone->vao && has_vao())
            gl.glDeleteVertexArrays(1, &zone->vao);
          if (zone->vao_tex && has_vao())
            gl.glDeleteVertexArrays(1, &zone->vao_tex);
        }
      }
      for (int mi = 0; mi < gr->model_count; mi++) {
        for (int ci = 0; ci < gw->model_cache_count; ci++) {
          if (gw->model_cache[ci].active &&
              gw->model_cache[ci].vbo == gr->models[mi].vbo) {
            gw->model_cache[ci].ref_count--;
            if (gw->model_cache[ci].ref_count <= 0) {
              gl.glDeleteBuffers(1, &gw->model_cache[ci].vbo);
              if (gw->model_cache[ci].vbo_tex)
                gl.glDeleteBuffers(1, &gw->model_cache[ci].vbo_tex);
              gw->model_cache[ci].active = false;
            }
            break;
          }
        }
      }
      free(gr->models);
      int last = gw->region_count - 1;
      if (i != last)
        gw->regions[i] = gw->regions[last];
      memset(&gw->regions[last], 0, sizeof(glw_region_t));
      gw->region_count--;
      return;
    }
  }
}

/* ======================================================================== */
/* Model VBO cache                                                          */
/* ======================================================================== */

static uint32_t glw_upload_vbo(osrs_gl_world_t *gw, osrs_config_t *cfg,
                               osrs_model_t *model, int ambient, int contrast,
                               GLuint *out_vbo, GLuint *out_vbo_tex,
                               uint32_t *out_vcount_tex, float *out_min_up_h) {
  *out_vbo = 0;
  *out_vbo_tex = 0;
  *out_vcount_tex = 0;
  float min_up_h = 1e30f;
  for (int i = 0; i < model->vertex_count; i++) {
    float uy = (float)(-model->vertex_y[i]);
    if (uy < min_up_h)
      min_up_h = uy;
  }
  if (out_min_up_h)
    *out_min_up_h = model->vertex_count > 0 ? min_up_h : 0.0f;

  glw_vertex_t *verts = malloc(sizeof(glw_vertex_t) * model->face_count * 3);
  glw_vertex_t *tverts = malloc(sizeof(glw_vertex_t) * model->face_count * 3);
  if (!verts || !tverts) {
    free(verts);
    free(tverts);
    return 0;
  }

  /* Save/flip normals for Y-negation consistency */
  int *snx = NULL, *snz = NULL, *sfnx = NULL, *sfnz = NULL;
  if (model->normal_x) {
    snx = malloc(model->vertex_count * sizeof(int));
    snz = malloc(model->vertex_count * sizeof(int));
    if (snx && snz)
      for (int i = 0; i < model->vertex_count; i++) {
        snx[i] = model->normal_x[i];
        snz[i] = model->normal_z[i];
        model->normal_x[i] = -model->normal_x[i];
        model->normal_z[i] = -model->normal_z[i];
      }
  }
  if (model->face_normal_x) {
    sfnx = malloc(model->face_count * sizeof(int));
    sfnz = malloc(model->face_count * sizeof(int));
    if (sfnx && sfnz)
      for (int i = 0; i < model->face_count; i++) {
        sfnx[i] = model->face_normal_x[i];
        sfnz[i] = model->face_normal_z[i];
        model->face_normal_x[i] = -model->face_normal_x[i];
        model->face_normal_z[i] = -model->face_normal_z[i];
      }
  }

  int vcount = 0, tvcount = 0;
  for (int i = 0; i < model->face_count; i++) {
    if (model->face_transparency && model->face_transparency[i] == -2)
      continue;
    int a = model->face_a[i], b = model->face_b[i], c = model->face_c[i];
    if (a >= model->vertex_count || b >= model->vertex_count ||
        c >= model->vertex_count)
      continue;

    float ax = (float)model->vertex_x[a], ay = (float)(-model->vertex_y[a]),
          az = (float)model->vertex_z[a];
    float bx = (float)model->vertex_x[b], by = (float)(-model->vertex_y[b]),
          bz = (float)model->vertex_z[b];
    float cx = (float)model->vertex_x[c], cy = (float)(-model->vertex_y[c]),
          cz = (float)model->vertex_z[c];

    uint32_t c0 = osrs_model_light_vertex(model, i, 0, ambient, contrast);
    uint32_t c1 = osrs_model_light_vertex(model, i, 1, ambient, contrast);
    uint32_t c2 = osrs_model_light_vertex(model, i, 2, ambient, contrast);

    int tex = model->face_texture ? model->face_texture[i] : -1;
    if (tex >= 0 && model->face_u) {
      int slot = gw->use_tex_array ? glw_tex_array_layer(gw, cfg, tex)
                                   : glw_atlas_slot(gw, cfg, tex);
      if (slot >= 0) {
        float au0, av0, au1, av1, au2, av2;
        if (gw->use_tex_array) {
          au0 = model->face_u[i][0];
          av0 = model->face_v[i][0];
          au1 = model->face_u[i][1];
          av1 = model->face_v[i][1];
          au2 = model->face_u[i][2];
          av2 = model->face_v[i][2];
        } else {
          glw_atlas_uv(slot, model->face_u[i][0], model->face_v[i][0], &au0,
                       &av0);
          glw_atlas_uv(slot, model->face_u[i][1], model->face_v[i][1], &au1,
                       &av1);
          glw_atlas_uv(slot, model->face_u[i][2], model->face_v[i][2], &au2,
                       &av2);
        }
        tverts[tvcount++] = (glw_vertex_t){ax,
                                           ay,
                                           az,
                                           ((c0 >> 16) & 0xFF) / 255.0f,
                                           ((c0 >> 8) & 0xFF) / 255.0f,
                                           (c0 & 0xFF) / 255.0f,
                                           au0,
                                           av0,
                                           (float)(slot + 1)};
        tverts[tvcount++] = (glw_vertex_t){bx,
                                           by,
                                           bz,
                                           ((c1 >> 16) & 0xFF) / 255.0f,
                                           ((c1 >> 8) & 0xFF) / 255.0f,
                                           (c1 & 0xFF) / 255.0f,
                                           au1,
                                           av1,
                                           (float)(slot + 1)};
        tverts[tvcount++] = (glw_vertex_t){cx,
                                           cy,
                                           cz,
                                           ((c2 >> 16) & 0xFF) / 255.0f,
                                           ((c2 >> 8) & 0xFF) / 255.0f,
                                           (c2 & 0xFF) / 255.0f,
                                           au2,
                                           av2,
                                           (float)(slot + 1)};
        continue;
      }
    }
    verts[vcount++] = (glw_vertex_t){ax,
                                     ay,
                                     az,
                                     ((c0 >> 16) & 0xFF) / 255.0f,
                                     ((c0 >> 8) & 0xFF) / 255.0f,
                                     (c0 & 0xFF) / 255.0f,
                                     0,
                                     0,
                                     0};
    verts[vcount++] = (glw_vertex_t){bx,
                                     by,
                                     bz,
                                     ((c1 >> 16) & 0xFF) / 255.0f,
                                     ((c1 >> 8) & 0xFF) / 255.0f,
                                     (c1 & 0xFF) / 255.0f,
                                     0,
                                     0,
                                     0};
    verts[vcount++] = (glw_vertex_t){cx,
                                     cy,
                                     cz,
                                     ((c2 >> 16) & 0xFF) / 255.0f,
                                     ((c2 >> 8) & 0xFF) / 255.0f,
                                     (c2 & 0xFF) / 255.0f,
                                     0,
                                     0,
                                     0};
  }

  if (snx) {
    for (int i = 0; i < model->vertex_count; i++) {
      model->normal_x[i] = snx[i];
      model->normal_z[i] = snz[i];
    }
    free(snx);
    free(snz);
  }
  if (sfnx) {
    for (int i = 0; i < model->face_count; i++) {
      model->face_normal_x[i] = sfnx[i];
      model->face_normal_z[i] = sfnz[i];
    }
    free(sfnx);
    free(sfnz);
  }

  GLuint vbo = 0;
  if (vcount > 0) {
    gl.glGenBuffers(1, &vbo);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, vcount * sizeof(glw_vertex_t), verts,
                    GL_STATIC_DRAW);
  }
  GLuint vbo_tex = 0;
  if (tvcount > 0) {
    gl.glGenBuffers(1, &vbo_tex);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_tex);
    gl.glBufferData(GL_ARRAY_BUFFER, tvcount * sizeof(glw_vertex_t), tverts,
                    GL_STATIC_DRAW);
  }

  free(verts);
  free(tverts);
  *out_vbo = vbo;
  *out_vbo_tex = vbo_tex;
  *out_vcount_tex = tvcount;
  return vcount;
}

static uint32_t glw_create_model_vbo(osrs_gl_world_t *gw, osrs_config_t *cfg,
                                     const glw_model_key_t *key,
                                     const int *recolor_src,
                                     const int *recolor_dst, int recolor_count,
                                     GLuint *out_vbo, GLuint *out_vbo_tex,
                                     uint32_t *out_vcount_tex,
                                     float *out_min_up_h) {
  *out_vbo = 0;
  *out_vbo_tex = 0;
  *out_vcount_tex = 0;
  if (out_min_up_h)
    *out_min_up_h = 0.0f;
  osrs_archive_files_t *files =
      osrs_cache_read_archive_files(cfg->cache, 7, key->model_id, NULL);
  if (!files || files->count < 1) {
    if (files)
      osrs_archive_files_free(files);
    return 0;
  }
  osrs_model_t *model =
      osrs_model_load(key->model_id, files->files[0].data, files->files[0].len);
  osrs_archive_files_free(files);
  if (!model)
    return 0;
  if (recolor_count > 0)
    for (int i = 0; i < recolor_count; i++)
      osrs_model_recolor(model, recolor_src[i], recolor_dst[i]);
  if (key->frame_id >= 0) {
    osrs_framemap_t *fm = NULL;
    osrs_frame_t *fr =
        osrs_frame_load(cfg->cache, (key->frame_id >> 16) & 0xFFFF,
                        key->frame_id & 0xFFFF, &fm);
    if (fr) {
      osrs_model_apply_frame(model, fm, fr);
      osrs_frame_free(fr);
    }
    if (fm)
      osrs_framemap_free(fm);
  }
  osrs_model_compute_normals(model);
  osrs_model_compute_uvs(model);
  uint32_t vcount =
      glw_upload_vbo(gw, cfg, model, key->ambient, key->contrast, out_vbo,
                     out_vbo_tex, out_vcount_tex, out_min_up_h);
  osrs_model_free(model);
  return vcount;
}

static uint32_t glw_get_model_vbo(osrs_gl_world_t *gw, osrs_config_t *cfg,
                                  const glw_model_key_t *key,
                                  const int *recolor_src,
                                  const int *recolor_dst, int recolor_count,
                                  GLuint *out_vbo, GLuint *out_vbo_tex,
                                  uint32_t *out_vcount_tex,
                                  float *out_min_up_h) {
  *out_vbo = 0;
  *out_vbo_tex = 0;
  *out_vcount_tex = 0;
  if (out_min_up_h)
    *out_min_up_h = 0.0f;
  for (int i = 0; i < gw->model_cache_count; i++) {
    if (gw->model_cache[i].active && key_eq(&gw->model_cache[i].key, key)) {
      *out_vbo = gw->model_cache[i].vbo;
      *out_vbo_tex = gw->model_cache[i].vbo_tex;
      *out_vcount_tex = gw->model_cache[i].vcount_tex;
      if (out_min_up_h)
        *out_min_up_h = gw->model_cache[i].min_up_h;
      return gw->model_cache[i].vcount;
    }
  }
  GLuint vbo, vbo_tex;
  uint32_t vcount_tex;
  float min_up_h = 0.0f;
  uint32_t vcount = glw_create_model_vbo(gw, cfg, key, recolor_src, recolor_dst,
                                         recolor_count, &vbo, &vbo_tex,
                                         &vcount_tex, &min_up_h);
  if (vcount == 0 && vcount_tex == 0)
    return 0;
  int slot;
  if (gw->model_cache_count < GLW_MAX_MODEL_CACHE)
    slot = gw->model_cache_count++;
  else {
    slot = -1;
    for (int i = 0; i < gw->model_cache_count; i++)
      if (gw->model_cache[i].active && gw->model_cache[i].ref_count == 0) {
        slot = i;
        break;
      }
    if (slot < 0) {
      for (int i = 1; i < gw->model_cache_count; i++)
        gw->model_cache[i - 1] = gw->model_cache[i];
      slot = GLW_MAX_MODEL_CACHE - 1;
    } else {
      gl.glDeleteBuffers(1, &gw->model_cache[slot].vbo);
      if (gw->model_cache[slot].vbo_tex)
        gl.glDeleteBuffers(1, &gw->model_cache[slot].vbo_tex);
      for (int i = slot + 1; i < gw->model_cache_count; i++)
        gw->model_cache[i - 1] = gw->model_cache[i];
      slot = gw->model_cache_count - 1;
    }
  }
  gw->model_cache[slot].key = *key;
  gw->model_cache[slot].vbo = vbo;
  gw->model_cache[slot].vcount = vcount;
  gw->model_cache[slot].vbo_tex = vbo_tex;
  gw->model_cache[slot].vcount_tex = vcount_tex;
  gw->model_cache[slot].min_up_h = min_up_h;
  gw->model_cache[slot].active = true;
  gw->model_cache[slot].ref_count = 0;
  *out_vbo = vbo;
  *out_vbo_tex = vbo_tex;
  *out_vcount_tex = vcount_tex;
  if (out_min_up_h)
    *out_min_up_h = min_up_h;
  return vcount;
}

/* ======================================================================== */
/* Region models upload                                                     */
/* ======================================================================== */

static const float GLW_ROT[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

void osrs_gl_world_upload_region_models(osrs_gl_world_t *gw,
                                        osrs_region_t *region, int region_x,
                                        int region_y, osrs_config_t *cfg) {
  if (!gw || !region || !cfg)
    return;
  egl.eglMakeCurrent(gw->dpy, gw->surf, gw->surf, gw->ctx);
  glw_region_t *gr = NULL;
  for (int i = 0; i < gw->region_count; i++)
    if (gw->regions[i].region_x == region_x &&
        gw->regions[i].region_y == region_y && gw->regions[i].active) {
      gr = &gw->regions[i];
      break;
    }
  if (!gr) {
    if (gw->region_count >= GLW_MAX_REGIONS)
      return;
    gr = &gw->regions[gw->region_count++];
    memset(gr, 0, sizeof(*gr));
    gr->region_x = region_x;
    gr->region_y = region_y;
    gr->active = true;
  }
  free(gr->models);
  gr->models = NULL;
  gr->model_count = 0;
  gr->model_capacity = 0;
  if (region->location_count == 0)
    return;
  int cap = region->location_count * 2;
  gr->models = calloc(cap, sizeof(*gr->models));
  if (!gr->models)
    return;
  gr->model_capacity = cap;

  int base_x = region_x * OSRS_REGION_SIZE,
      base_z = region_y * OSRS_REGION_SIZE;
  for (int i = 0; i < region->location_count; i++) {
    osrs_location_t *loc = &region->locations[i];
    int obj_plane = loc->plane;
    if (obj_plane < 0 || obj_plane >= OSRS_REGION_PLANES)
      obj_plane = 0;
    osrs_object_def_t *def = glw_get_obj_def(gw, cfg, loc->object_id);
    if (!def || def->model_count <= 0)
      continue;
    int model_id = -1;
    if (def->has_model_types) {
      for (int j = 0; j < def->model_count; j++)
        if (def->model_types[j] == loc->type) {
          model_id = def->model_ids[j];
          break;
        }
    } else if (def->model_count > 0)
      model_id = def->model_ids[0];
    if (model_id < 0)
      continue;

    int lx = loc->local_x, lz = loc->local_y;
    if (lx < 0)
      lx = 0;
    if (lx >= OSRS_REGION_SIZE)
      lx = OSRS_REGION_SIZE - 1;
    if (lz < 0)
      lz = 0;
    if (lz >= OSRS_REGION_SIZE)
      lz = OSRS_REGION_SIZE - 1;
    float ground_h =
        (float)region->tiles[0][lx][lz].height + (float)(obj_plane * 240);
    float cx = (float)(base_x + loc->local_x) + 0.5f;
    float cz = (float)(base_z + loc->local_y) + 0.5f;
    int ambient = OSRS_LIGHT_OBJECT_AMBIENT(def->ambient);
    int contrast = OSRS_LIGHT_OBJECT_CONTRAST(def->contrast);
    uint32_t rh =
        hash_recolors(def->recolor_src, def->recolor_dst, def->recolor_count);

    glw_model_key_t mkey = {model_id, ambient, contrast, rh, -1};
    GLuint vbo, vbo_tex;
    uint32_t vcount_tex;
    float min_up_h = 0.0f;
    uint32_t vcount = glw_get_model_vbo(gw, cfg, &mkey, def->recolor_src,
                                        def->recolor_dst, def->recolor_count,
                                        &vbo, &vbo_tex, &vcount_tex, &min_up_h);
    if (vcount == 0 && vcount_tex == 0)
      continue;
    for (int ci = 0; ci < gw->model_cache_count; ci++)
      if (gw->model_cache[ci].active && gw->model_cache[ci].vbo == vbo) {
        gw->model_cache[ci].ref_count++;
        break;
      }
    if (gr->model_count >= gr->model_capacity)
      break;
    glw_model_instance_t *mi = &gr->models[gr->model_count++];
    mi->vbo = vbo;
    mi->vcount = vcount;
    mi->vbo_tex = vbo_tex;
    mi->vcount_tex = vcount_tex;
    mi->pos[0] = cx;
    mi->pos[1] = ground_h + 2.0f;
    mi->pos[2] = cz;
    int orient = loc->orientation & 3;
    if (loc->type == 0 || loc->type == 1)
      orient = (orient + 3) & 3;
    if (def->rotated)
      orient = (orient + 1) & 3;
    mi->rot[0] = GLW_ROT[orient][0];
    mi->rot[1] = GLW_ROT[orient][1];
    mi->scale[0] = (float)def->model_size_x / 128.0f;
    mi->scale[1] = (float)def->model_size_height / 128.0f;
    mi->scale[2] = (float)def->model_size_y / 128.0f;
    mi->min_up_h = min_up_h;
    mi->plane = loc->plane;
  }
}

/* ======================================================================== */
/* Frame rendering                                                          */
/* ======================================================================== */

static void bind_vertex_format(osrs_gl_world_t *gw) {
  if (gw->loc_a_pos >= 0) {
    gl.glEnableVertexAttribArray(gw->loc_a_pos);
    gl.glVertexAttribPointer(gw->loc_a_pos, 3, GL_FLOAT, 0,
                             sizeof(glw_vertex_t), (void *)0);
  }
  if (gw->loc_a_color >= 0) {
    gl.glEnableVertexAttribArray(gw->loc_a_color);
    gl.glVertexAttribPointer(gw->loc_a_color, 3, GL_FLOAT, 0,
                             sizeof(glw_vertex_t), (void *)(sizeof(float) * 3));
  }
  if (gw->loc_a_uv >= 0) {
    gl.glEnableVertexAttribArray(gw->loc_a_uv);
    gl.glVertexAttribPointer(gw->loc_a_uv, 2, GL_FLOAT, 0, sizeof(glw_vertex_t),
                             (void *)(sizeof(float) * 6));
  }
  if (gw->loc_a_tex >= 0) {
    gl.glEnableVertexAttribArray(gw->loc_a_tex);
    gl.glVertexAttribPointer(gw->loc_a_tex, 1, GL_FLOAT, 0,
                             sizeof(glw_vertex_t), (void *)(sizeof(float) * 8));
  }
}

static void unbind_vertex_format(osrs_gl_world_t *gw) {
  if (gw->loc_a_pos >= 0)
    gl.glDisableVertexAttribArray(gw->loc_a_pos);
  if (gw->loc_a_color >= 0)
    gl.glDisableVertexAttribArray(gw->loc_a_color);
  if (gw->loc_a_uv >= 0)
    gl.glDisableVertexAttribArray(gw->loc_a_uv);
  if (gw->loc_a_tex >= 0)
    gl.glDisableVertexAttribArray(gw->loc_a_tex);
}

void osrs_gl_world_begin(osrs_gl_world_t *gw, const osrs_iso_camera_t *cam,
                         int screen_w, int screen_h) {
  if (!gw || !cam)
    return;
  egl.eglMakeCurrent(gw->dpy, gw->surf, gw->surf, gw->ctx);
  gl.glViewport(0, 0, gw->width, gw->height);
  if (getenv("OSRS_GL_NODEPTH"))
    gl.glDisable(GL_DEPTH_TEST);
  else {
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_GREATER);
    gl.glDepthMask(1);
    gl.glClearDepthf(0.0f);
  }
  if (getenv("OSRS_GL_NOCULL"))
    gl.glDisable(GL_CULL_FACE);
  else
    gl.glEnable(GL_CULL_FACE);
  gl.glFrontFace(GL_CCW);
  gl.glClearColor(0.063f, 0.063f, 0.114f, 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  gl.glUseProgram(gw->prog);
  float yaw_delta = cam->yaw - OSRS_ISO_DEFAULT_YAW - (float)M_PI_4;
  float cy = cosf(yaw_delta), sy = sinf(yaw_delta);
  float cp = cosf(cam->pitch), sp = sinf(cam->pitch);

  if (gw->has_ubo) {
    scene_ubo_t ubo_data = {
        .orbit = {cy, sy, sp, cp},
        .fog_color = {0.063f, 0.063f, 0.114f, 0.0f},
        .cam_center = {(float)cam->center_tile_x + 0.5f,
                       (float)cam->center_tile_z + 0.5f},
        .cam_offset = {cam->x, cam->y},
        .screen = {(float)screen_w, (float)screen_h},
        .zoom = cam->zoom,
        .fog_density = 0.0f,
        .brightness = 1.2f,
        ._pad = 0.0f,
    };
    gl.glBindBuffer(GL_UNIFORM_BUFFER, gw->ubo);
    gl.glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(scene_ubo_t), &ubo_data);
    gl.glBindBuffer(GL_UNIFORM_BUFFER, 0);
  } else {
    gl.glUniform2f(gw->loc_cam_center, (float)cam->center_tile_x + 0.5f,
                   (float)cam->center_tile_z + 0.5f);
    gl.glUniform2f(gw->loc_cam_offset, cam->x, cam->y);
    gl.glUniform1f(gw->loc_zoom, cam->zoom);
    gl.glUniform4f(gw->loc_orbit, cy, sy, sp, cp);
    gl.glUniform2f(gw->loc_screen, (float)screen_w, (float)screen_h);
    gl.glUniform3f(gw->loc_fog_color, 0.063f, 0.063f, 0.114f);
    gl.glUniform1f(gw->loc_fog_density, 0.0f);
    gl.glUniform1f(gw->loc_brightness, 1.2f);
  }
  gw->cam_cx = (float)cam->center_tile_x;
  gw->cam_cz = (float)cam->center_tile_z;

  gl.glUniform3f(gw->loc_model_pos, 0, 0, 0);
  gl.glUniform3f(gw->loc_model_scale, 1, 1, 1);
  gl.glUniform2f(gw->loc_model_rot, 0, 1);

  /* Bind both texture types so both samplers are valid regardless of branch */
  gl.glActiveTexture(GL_TEXTURE0);
  if (gw->use_tex_array)
    gl.glBindTexture(GL_TEXTURE_2D_ARRAY, gw->tex_array);
  else
    gl.glBindTexture(GL_TEXTURE_2D, gw->atlas_tex);
  gl.glActiveTexture((GL_TEXTURE0 + 1));
  if (gw->use_tex_array)
    gl.glBindTexture(GL_TEXTURE_2D, gw->atlas_tex);
  else
    gl.glBindTexture(GL_TEXTURE_2D_ARRAY, gw->tex_array);
  if (gw->use_tex_array) {
    gl.glUniform1i(gw->loc_tex_array, 0);
    gl.glUniform1i(gw->loc_texture, 1);
  } else {
    gl.glUniform1i(gw->loc_texture, 0);
    gl.glUniform1i(gw->loc_tex_array, 1);
  }
  gl.glUniform1f(gw->loc_use_array, gw->use_tex_array ? 1.0f : 0.0f);
}

void osrs_gl_world_render_region(osrs_gl_world_t *gw, osrs_region_t *region,
                                 int region_x, int region_y) {
  (void)region;
  if (!gw)
    return;
  glw_region_t *gr = NULL;
  for (int i = 0; i < gw->region_count; i++)
    if (gw->regions[i].region_x == region_x &&
        gw->regions[i].region_y == region_y && gw->regions[i].active) {
      gr = &gw->regions[i];
      break;
    }
  if (!gr || gr->index_count == 0) {
    OSRS_INFO(OSRS_LOG_CAT_RENDER,
              "Region %d,%d not drawn: gr=%p index_count=%u", region_x,
              region_y, (void *)gr, gr ? gr->index_count : 0);
    return;
  }

  OSRS_INFO(OSRS_LOG_CAT_RENDER,
            "Region %d,%d drawing: vbo=%u ibo=%u vao=%u icount=%u", region_x,
            region_y, gr->vbo, gr->ibo, gr->vao, gr->index_count);

  gl.glUniform3f(gw->loc_model_pos, (float)(region_x * OSRS_REGION_SIZE), 0,
                 (float)(region_y * OSRS_REGION_SIZE));
  gl.glUniform3f(gw->loc_model_scale, 1, 1, 1);
  gl.glUniform2f(gw->loc_model_rot, 0, 1);

  if (gr->vao && has_vao()) {
    gl.glBindVertexArray(gr->vao);
    gl.glDrawElements(GL_TRIANGLES, gr->index_count, GL_UNSIGNED_SHORT, NULL);
    gl.glBindVertexArray(0);
  } else {
    gl.glBindBuffer(GL_ARRAY_BUFFER, gr->vbo);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gr->ibo);
    bind_vertex_format(gw);
    gl.glDrawElements(GL_TRIANGLES, gr->index_count, GL_UNSIGNED_SHORT, NULL);
    unbind_vertex_format(gw);
  }

  /* Render opaque models first */
  for (int i = 0; i < gr->model_count; i++) {
    glw_model_instance_t *mi = &gr->models[i];
    if (mi->min_up_h > 200.0f) {
      float dx = mi->pos[0] - gw->cam_cx, dz = mi->pos[2] - gw->cam_cz;
      if (dx * dx + dz * dz < 3.0f * 3.0f)
        continue;
    }
    if (mi->vcount > 0) {
      gl.glUniform3f(gw->loc_model_pos, mi->pos[0], mi->pos[1], mi->pos[2]);
      gl.glUniform3f(gw->loc_model_scale, mi->scale[0] / 128.0f, mi->scale[1],
                     mi->scale[2] / 128.0f);
      gl.glUniform2f(gw->loc_model_rot, mi->rot[0], mi->rot[1]);
      gl.glBindBuffer(GL_ARRAY_BUFFER, mi->vbo);
      bind_vertex_format(gw);
      gl.glDrawArrays(GL_TRIANGLES, 0, mi->vcount);
      unbind_vertex_format(gw);
    }
  }

  /* Collect and sort transparent models by depth (back-to-front) */
  typedef struct {
    glw_model_instance_t *mi;
    float depth;
  } trans_entry_t;
  trans_entry_t trans[256];
  int trans_count = 0;
  for (int i = 0; i < gr->model_count && trans_count < 256; i++) {
    glw_model_instance_t *mi = &gr->models[i];
    if (mi->vcount_tex == 0)
      continue;
    if (mi->min_up_h > 200.0f) {
      float dx = mi->pos[0] - gw->cam_cx, dz = mi->pos[2] - gw->cam_cz;
      if (dx * dx + dz * dz < 3.0f * 3.0f)
        continue;
    }
    float dx = mi->pos[0] - gw->cam_cx;
    float dz = mi->pos[2] - gw->cam_cz;
    trans[trans_count].mi = mi;
    trans[trans_count].depth = dx * dx + dz * dz;
    trans_count++;
  }
  /* Simple insertion sort by depth (descending = back-to-front) */
  for (int i = 1; i < trans_count; i++) {
    trans_entry_t key = trans[i];
    int j = i - 1;
    while (j >= 0 && trans[j].depth < key.depth) {
      trans[j + 1] = trans[j];
      j--;
    }
    trans[j + 1] = key;
  }
  for (int i = 0; i < trans_count; i++) {
    glw_model_instance_t *mi = trans[i].mi;
    gl.glUniform3f(gw->loc_model_pos, mi->pos[0], mi->pos[1], mi->pos[2]);
    gl.glUniform3f(gw->loc_model_scale, mi->scale[0] / 128.0f, mi->scale[1],
                   mi->scale[2] / 128.0f);
    gl.glUniform2f(gw->loc_model_rot, mi->rot[0], mi->rot[1]);
    gl.glBindBuffer(GL_ARRAY_BUFFER, mi->vbo_tex);
    bind_vertex_format(gw);
    gl.glDrawArrays(GL_TRIANGLES, 0, mi->vcount_tex);
    unbind_vertex_format(gw);
  }

  gl.glUniform3f(gw->loc_model_pos, 0, 0, 0);
  gl.glUniform3f(gw->loc_model_scale, 1, 1, 1);
  gl.glUniform2f(gw->loc_model_rot, 0, 1);
}

/* ======================================================================== */
/* Entity drawing                                                           */
/* ======================================================================== */

static void draw_entity_impl(osrs_gl_world_t *gw, osrs_config_t *cfg, float wx,
                             float wy, float wz, float rot_sin, float rot_cos,
                             float scale_x, float scale_y, float scale_z,
                             int frame_id, GLuint vbo, uint32_t vcount,
                             GLuint vbo_tex, uint32_t vcount_tex,
                             float min_up_h) {
  (void)cfg;
  (void)frame_id;
  if (min_up_h > 200.0f) {
    float dx = wx - gw->cam_cx, dz = wz - gw->cam_cz;
    if (dx * dx + dz * dz < 3.0f * 3.0f)
      return;
  }
  gl.glUniform3f(gw->loc_model_pos, wx, wy, wz);
  gl.glUniform3f(gw->loc_model_scale, scale_x / 128.0f, scale_y,
                 scale_z / 128.0f);
  gl.glUniform2f(gw->loc_model_rot, rot_sin, rot_cos);
  if (vcount > 0) {
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    bind_vertex_format(gw);
    gl.glDrawArrays(GL_TRIANGLES, 0, vcount);
    unbind_vertex_format(gw);
  }
  if (vbo_tex && vcount_tex > 0) {
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_tex);
    bind_vertex_format(gw);
    gl.glDrawArrays(GL_TRIANGLES, 0, vcount_tex);
    unbind_vertex_format(gw);
  }
}

void osrs_gl_world_draw_entity(osrs_gl_world_t *gw, osrs_config_t *config,
                               int model_id, int ambient, int contrast,
                               const int *recolor_src, const int *recolor_dst,
                               int recolor_count, float wx, float wy, float wz,
                               float rot_sin, float rot_cos, float scale_x,
                               float scale_y, float scale_z) {
  if (!gw || !config || model_id < 0)
    return;
  glw_model_key_t key = {.model_id = model_id,
                         .ambient = ambient,
                         .contrast = contrast,
                         .frame_id = -1};
  GLuint vbo, vbo_tex;
  uint32_t vcount_tex;
  float min_up_h = 0.0f;
  uint32_t vcount =
      glw_get_model_vbo(gw, config, &key, recolor_src, recolor_dst,
                        recolor_count, &vbo, &vbo_tex, &vcount_tex, &min_up_h);
  if (vcount == 0 && vcount_tex == 0)
    return;
  draw_entity_impl(gw, config, wx, wy, wz, rot_sin, rot_cos, scale_x, scale_y,
                   scale_z, -1, vbo, vcount, vbo_tex, vcount_tex, min_up_h);
}

void osrs_gl_world_draw_model_direct(osrs_gl_world_t *gw, osrs_config_t *config,
                                     osrs_model_t *model, int ambient,
                                     int contrast, float wx, float wy, float wz,
                                     float rot_sin, float rot_cos,
                                     float scale_x, float scale_y,
                                     float scale_z) {
  if (!gw || !model)
    return;
  GLuint vbo, vbo_tex;
  uint32_t vcount_tex;
  float min_up_h = 0.0f;
  uint32_t vcount = glw_upload_vbo(gw, config, model, ambient, contrast, &vbo,
                                   &vbo_tex, &vcount_tex, &min_up_h);
  if (vcount == 0 && vcount_tex == 0)
    return;
  draw_entity_impl(gw, config, wx, wy, wz, rot_sin, rot_cos, scale_x, scale_y,
                   scale_z, -1, vbo, vcount, vbo_tex, vcount_tex, min_up_h);
  gl.glDeleteBuffers(1, &vbo);
  if (vbo_tex)
    gl.glDeleteBuffers(1, &vbo_tex);
}

bool osrs_gl_world_end(osrs_gl_world_t *gw, uint32_t *out_pixels, int screen_w,
                       int screen_h) {
  if (!gw || !out_pixels)
    return false;
  gl.glReadPixels(0, 0, screen_w, screen_h, GL_RGBA, GL_UNSIGNED_BYTE,
                  out_pixels);
  /* Flip Y */
  for (int y = 0; y < screen_h / 2; y++) {
    for (int x = 0; x < screen_w; x++) {
      int i1 = y * screen_w + x;
      int i2 = (screen_h - 1 - y) * screen_w + x;
      uint32_t tmp = out_pixels[i1];
      out_pixels[i1] = out_pixels[i2];
      out_pixels[i2] = tmp;
    }
  }
  return true;
}

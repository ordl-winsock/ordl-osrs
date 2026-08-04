/* tools/test_gl_triangle.c - minimal EGL pbuffer + triangle + readback test */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef void *EGLNativeDisplayType;

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_NONE 0x3038
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056

#define GL_DEPTH_TEST 0x0B71
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LESS 0x0201
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 0x0004

typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef int GLsizei;
typedef char GLchar;
typedef unsigned int GLbitfield;

static void *(*p_eglGetDisplay)(void *);
static int (*p_eglInitialize)(void *, int *, int *);
static int (*p_eglChooseConfig)(void *, const int *, void *, int, int *);
static void *(*p_eglCreateContext)(void *, void *, void *, const int *);
static void *(*p_eglCreatePbufferSurface)(void *, void *, const int *);
static int (*p_eglMakeCurrent)(void *, void *, void *, void *);
static int (*p_eglGetError)(void);

static GLuint (*p_glCreateShader)(GLenum);
static void (*p_glShaderSource)(GLuint, GLsizei, const GLchar *const *,
                                const GLint *);
static void (*p_glCompileShader)(GLuint);
static void (*p_glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*p_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static GLuint (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint, GLuint);
static void (*p_glLinkProgram)(GLuint);
static void (*p_glGetProgramiv)(GLuint, GLenum, GLint *);
static void (*p_glUseProgram)(GLuint);
static GLint (*p_glGetAttribLocation)(GLuint, const char *);
static void (*p_glGenBuffers)(GLsizei, GLuint *);
static void (*p_glBindBuffer)(GLenum, GLuint);
static void (*p_glBufferData)(GLenum, GLsizei, const void *, GLenum);
static void (*p_glEnableVertexAttribArray)(GLuint);
static void (*p_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                       GLsizei, const void *);
static void (*p_glDrawArrays)(GLenum, GLint, GLsizei);
static void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glClear)(GLbitfield);
static void (*p_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                              void *);
static void (*p_glFinish)(void);
static GLenum (*p_glGetError)(void);
static void (*p_glEnable)(GLenum);
static void (*p_glDepthFunc)(GLenum);
static void (*p_glDepthMask)(GLboolean);

static const char vs_src[] =
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_color;\n"
    "uniform vec2 u_cam_center;\n"
    "uniform vec2 u_cam_offset;\n"
    "uniform float u_zoom;\n"
    "uniform vec2 u_screen;\n"
    "uniform vec3 u_model_pos;\n"
    "uniform vec3 u_model_scale;\n"
    "uniform vec2 u_model_rot;\n"
    "varying vec3 v_color;\n"
    "void main() {\n"
    "  vec3 lp = a_pos * u_model_scale;\n"
    "  float rx = lp.x * u_model_rot.y - lp.z * u_model_rot.x;\n"
    "  float rz = lp.x * u_model_rot.x + lp.z * u_model_rot.y;\n"
    "  vec3 wp = vec3(rx, lp.y, rz) + u_model_pos;\n"
    "  float tw = 128.0 * u_zoom;\n"
    "  float th = 64.0 * u_zoom;\n"
    "  float hs = 4.0 * u_zoom;\n"
    "  float dx = wp.x - u_cam_center.x;\n"
    "  float dz = wp.z - u_cam_center.y;\n"
    "  float px = (dx - dz) * tw * 0.5;\n"
    "  float py = (dx + dz) * th * 0.5 - wp.y * hs;\n"
    "  float sx = px + u_cam_offset.x + u_screen.x * 0.5;\n"
    "  float sy = py + u_cam_offset.y + u_screen.y * 0.5;\n"
    "  float depth = (dx + dz + wp.y * 16.0) * 0.0001220703125;\n"
    "  gl_Position = vec4(\n"
    "    sx / u_screen.x * 2.0 - 1.0,\n"
    "    -(sy / u_screen.y * 2.0 - 1.0),\n"
    "    -depth, 1.0);\n"
    "  v_color = a_color;\n"
    "}\n";
static const char fs_src[] =
    "precision mediump float;\n"
    "varying vec3 v_color;\n"
    "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";

int main(int argc, char **argv) {
  int use_depth = argc > 1 && strcmp(argv[1], "depth") == 0;
  void *eh = dlopen("libEGL.so.1", RTLD_NOW);
  void *gh = dlopen("libGLESv2.so.2", RTLD_NOW);
  if (!eh || !gh) {
    printf("dlopen failed eh=%p gh=%p\n", eh, gh);
    return 1;
  }
  p_eglGetDisplay = dlsym(eh, "eglGetDisplay");
  p_eglInitialize = dlsym(eh, "eglInitialize");
  p_eglChooseConfig = dlsym(eh, "eglChooseConfig");
  p_eglCreateContext = dlsym(eh, "eglCreateContext");
  p_eglCreatePbufferSurface = dlsym(eh, "eglCreatePbufferSurface");
  p_eglMakeCurrent = dlsym(eh, "eglMakeCurrent");
  p_eglGetError = dlsym(eh, "eglGetError");

  EGLDisplay dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
  int maj, min;
  p_eglInitialize(dpy, &maj, &min);
  printf("EGL %d.%d\n", maj, min);

  int attribs[] = {EGL_SURFACE_TYPE,
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
  EGLConfig cfg;
  int ncfg;
  if (!p_eglChooseConfig(dpy, attribs, &cfg, 1, &ncfg) || ncfg < 1) {
    printf("eglChooseConfig failed (err=0x%x)\n", p_eglGetError());
    return 1;
  }

  int pb[] = {EGL_WIDTH, 320, EGL_HEIGHT, 240, EGL_NONE};
  EGLSurface surf = p_eglCreatePbufferSurface(dpy, cfg, pb);
  int ctx_at[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext ctx = p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_at);
  if (!p_eglMakeCurrent(dpy, surf, surf, ctx)) {
    printf("makeCurrent failed\n");
    return 1;
  }

  p_glCreateShader = dlsym(gh, "glCreateShader");
  p_glShaderSource = dlsym(gh, "glShaderSource");
  p_glCompileShader = dlsym(gh, "glCompileShader");
  p_glGetShaderiv = dlsym(gh, "glGetShaderiv");
  p_glGetShaderInfoLog = dlsym(gh, "glGetShaderInfoLog");
  p_glCreateProgram = dlsym(gh, "glCreateProgram");
  p_glAttachShader = dlsym(gh, "glAttachShader");
  p_glLinkProgram = dlsym(gh, "glLinkProgram");
  p_glGetProgramiv = dlsym(gh, "glGetProgramiv");
  p_glUseProgram = dlsym(gh, "glUseProgram");
  p_glGetAttribLocation = dlsym(gh, "glGetAttribLocation");
  p_glGenBuffers = dlsym(gh, "glGenBuffers");
  p_glBindBuffer = dlsym(gh, "glBindBuffer");
  p_glBufferData = dlsym(gh, "glBufferData");
  p_glEnableVertexAttribArray = dlsym(gh, "glEnableVertexAttribArray");
  p_glVertexAttribPointer = dlsym(gh, "glVertexAttribPointer");
  p_glDrawArrays = dlsym(gh, "glDrawArrays");
  p_glViewport = dlsym(gh, "glViewport");
  p_glClearColor = dlsym(gh, "glClearColor");
  p_glClear = dlsym(gh, "glClear");
  p_glReadPixels = dlsym(gh, "glReadPixels");
  p_glFinish = dlsym(gh, "glFinish");
  p_glGetError = dlsym(gh, "glGetError");
  p_glEnable = dlsym(gh, "glEnable");
  p_glDepthFunc = dlsym(gh, "glDepthFunc");
  p_glDepthMask = dlsym(gh, "glDepthMask");

  GLuint vs = p_glCreateShader(GL_VERTEX_SHADER);
  const GLchar *p = vs_src;
  GLint len = (GLint)strlen(vs_src);
  p_glShaderSource(vs, 1, &p, &len);
  p_glCompileShader(vs);
  GLint ok;
  p_glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
  printf("vs compile: %d\n", ok);

  GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER);
  p = fs_src;
  len = (GLint)strlen(fs_src);
  p_glShaderSource(fs, 1, &p, &len);
  p_glCompileShader(fs);
  p_glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
  printf("fs compile: %d\n", ok);

  GLuint prog = p_glCreateProgram();
  p_glAttachShader(prog, vs);
  p_glAttachShader(prog, fs);
  p_glLinkProgram(prog);
  p_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  printf("link: %d\n", ok);

  float verts[] = {
      -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, /* red */
      0.5f,  -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, /* green */
      0.0f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f, /* blue */
  };
  GLuint vbo;
  p_glGenBuffers(1, &vbo);
  p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
  p_glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

  GLint la = p_glGetAttribLocation(prog, "a_pos");
  GLint lb = p_glGetAttribLocation(prog, "a_color");
  printf("a_pos=%d a_color=%d\n", la, lb);

  /* Load uniforms like the client does (region 50,50, zoom 0.25) */
  GLint (*p_glGetUniformLocation)(GLuint, const char *) =
      dlsym(gh, "glGetUniformLocation");
  void (*p_glUniform2f)(GLint, GLfloat, GLfloat) = dlsym(gh, "glUniform2f");
  void (*p_glUniform1f)(GLint, GLfloat) = dlsym(gh, "glUniform1f");
  void (*p_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat) =
      dlsym(gh, "glUniform3f");

  p_glUseProgram(prog);
  p_glUniform2f(p_glGetUniformLocation(prog, "u_cam_center"), 3232.0f, 3232.0f);
  p_glUniform2f(p_glGetUniformLocation(prog, "u_cam_offset"), 0.0f, 0.0f);
  p_glUniform1f(p_glGetUniformLocation(prog, "u_zoom"), 0.25f);
  p_glUniform2f(p_glGetUniformLocation(prog, "u_screen"), 320.0f, 240.0f);
  p_glUniform3f(p_glGetUniformLocation(prog, "u_model_pos"), 0.0f, 0.0f, 0.0f);
  p_glUniform3f(p_glGetUniformLocation(prog, "u_model_scale"), 1.0f, 1.0f,
                1.0f);
  p_glUniform2f(p_glGetUniformLocation(prog, "u_model_rot"), 0.0f, 1.0f);
  printf("uniform err=0x%x\n", p_glGetError());

  /* A single tile quad at world (3232..3233, 3200..3201) — on screen */
  float verts2[] = {
      3232.0f, 0.0f, 3232.0f, 1.0f,    0.0f, 0.0f,    3233.0f, 0.0f, 3232.0f,
      0.0f,    1.0f, 0.0f,    3233.0f, 0.0f, 3233.0f, 0.0f,    0.0f, 1.0f,
      3232.0f, 0.0f, 3232.0f, 1.0f,    0.0f, 0.0f,    3233.0f, 0.0f, 3233.0f,
      0.0f,    0.0f, 1.0f,    3232.0f, 0.0f, 3233.0f, 0.0f,    1.0f, 1.0f,
  };
  p_glGenBuffers(1, &vbo);
  p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
  p_glBufferData(GL_ARRAY_BUFFER, sizeof(verts2), verts2, GL_STATIC_DRAW);

  p_glViewport(0, 0, 320, 240);
  p_glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
  if (use_depth) {
    p_glEnable(GL_DEPTH_TEST);
    p_glDepthFunc(GL_LESS);
    p_glDepthMask(1);
    printf("depth test ENABLED\n");
  }
  p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  p_glUseProgram(prog);
  p_glEnableVertexAttribArray((GLuint)la);
  p_glVertexAttribPointer((GLuint)la, 3, GL_FLOAT, 0, 24, (const void *)0);
  p_glEnableVertexAttribArray((GLuint)lb);
  p_glVertexAttribPointer((GLuint)lb, 3, GL_FLOAT, 0, 24, (const void *)12);
  p_glDrawArrays(GL_TRIANGLES, 0, 6);
  p_glFinish();
  printf("draw err=0x%x\n", p_glGetError());

  unsigned char *px = malloc(320 * 240 * 4);
  p_glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, px);
  printf("read err=0x%x\n", p_glGetError());

  /* Count colors */
  int red = 0, green = 0, blue = 0, bg = 0, other = 0;
  for (int i = 0; i < 320 * 240; i++) {
    unsigned char r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
    if (r > 200 && g < 60 && b < 60)
      red++;
    else if (g > 200 && r < 60 && b < 60)
      green++;
    else if (b > 200 && r < 60 && g < 60)
      blue++;
    else if (r < 40 && g < 40 && b < 70)
      bg++;
    else
      other++;
  }
  printf("pixels: red=%d green=%d blue=%d bg=%d other=%d (total %d)\n", red,
         green, blue, bg, other, 320 * 240);
  printf(red + green + blue > 100 ? "TRIANGLE TEST: PASS\n"
                                  : "TRIANGLE TEST: FAIL\n");
  free(px);
  return 0;
}

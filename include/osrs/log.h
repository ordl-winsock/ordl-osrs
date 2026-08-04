/*
 * osrs/log.h — Structured logging for the OSRS client
 * Pure C23, zero external dependencies.
 *
 * Usage:
 *   OSRS_INFO(OSRS_LOG_CAT_NET, "Connected to %s:%d", host, port);
 *   OSRS_DEBUG(OSRS_LOG_CAT_GAME, "Packet opcode=%d len=%d", op, len);
 *   OSRS_TRACE(OSRS_LOG_CAT_PKT, "Raw bytes:");
 *   osrs_log_hex(OSRS_LOG_TRACE, OSRS_LOG_CAT_PKT, buf, len);
 *
 * Environment: OSRS_LOG_LEVEL=error|warn|info|debug|trace (default: info)
 */

#ifndef OSRS_LOG_H
#define OSRS_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Log levels                                                                 */
/* -------------------------------------------------------------------------- */

typedef enum {
  OSRS_LOG_NONE = 0,
  OSRS_LOG_ERROR = 1,
  OSRS_LOG_WARN = 2,
  OSRS_LOG_INFO = 3,
  OSRS_LOG_DEBUG = 4,
  OSRS_LOG_TRACE = 5,
} osrs_log_level_t;

/* Compile-time ceiling (lower = less output). Override with -DOSRS_LOG_MAX=N */
#ifndef OSRS_LOG_MAX
#define OSRS_LOG_MAX OSRS_LOG_TRACE
#endif

/* -------------------------------------------------------------------------- */
/* Categories                                                                 */
/* -------------------------------------------------------------------------- */

#define OSRS_LOG_CAT_NET "NET"
#define OSRS_LOG_CAT_LOGIN "LOGIN"
#define OSRS_LOG_CAT_GAME "GAME"
#define OSRS_LOG_CAT_CACHE "CACHE"
#define OSRS_LOG_CAT_CONFIG "CONFIG"
#define OSRS_LOG_CAT_MAP "MAP"
#define OSRS_LOG_CAT_RENDER "RENDER"
#define OSRS_LOG_CAT_ENTITY "ENTITY"
#define OSRS_LOG_CAT_UI "UI"
#define OSRS_LOG_CAT_CRYPTO "CRYPTO"
#define OSRS_LOG_CAT_TLS "TLS"
#define OSRS_LOG_CAT_AUTH "AUTH"
#define OSRS_LOG_CAT_PROTO "PROTO"
#define OSRS_LOG_CAT_PKT "PKT"
#define OSRS_LOG_CAT_POW "POW"
#define OSRS_LOG_CAT_HTTP "HTTP"
#define OSRS_LOG_CAT_ISAAC "ISAAC"
#define OSRS_LOG_CAT_PI "PI"
#define OSRS_LOG_CAT_CLIENT "CLIENT"

/* -------------------------------------------------------------------------- */
/* Runtime state                                                              */
/* -------------------------------------------------------------------------- */

extern osrs_log_level_t g_osrs_log_level;
extern bool g_osrs_log_timestamps;
extern bool g_osrs_log_color;

/* Call once at startup (reads OSRS_LOG_LEVEL env var). Idempotent. */
void osrs_log_init(void);

/* Set level programmatically. */
static inline void osrs_log_set_level(osrs_log_level_t level) {
  g_osrs_log_level = level;
}

const char *osrs_log_level_str(osrs_log_level_t level);
osrs_log_level_t osrs_log_level_parse(const char *s);

/* -------------------------------------------------------------------------- */
/* Internal helpers — do not call directly                                    */
/* -------------------------------------------------------------------------- */

#if OSRS_LOG_MAX >= OSRS_LOG_TRACE
#define _OSRS_LOG_ENABLED(level) ((level) <= g_osrs_log_level)
#else
#define _OSRS_LOG_ENABLED(level)                                               \
  ((level) <= OSRS_LOG_MAX && (level) <= g_osrs_log_level)
#endif

static inline void _osrs_log_print(osrs_log_level_t level, const char *cat,
                                   const char *file, int line, const char *fmt,
                                   ...) {
  if (!_OSRS_LOG_ENABLED(level))
    return;

  const char *col = "";
  const char *reset = "";
  if (g_osrs_log_color) {
    col = (level == OSRS_LOG_ERROR)   ? "\x1b[1;31m"
          : (level == OSRS_LOG_WARN)  ? "\x1b[1;33m"
          : (level == OSRS_LOG_INFO)  ? "\x1b[1;32m"
          : (level == OSRS_LOG_DEBUG) ? "\x1b[1;36m"
                                      : "\x1b[1;35m";
    reset = "\x1b[0m";
  }

  fprintf(stderr, "%s[%s][%-7s] %s:%d  %s%s\n", col, osrs_log_level_str(level),
          cat, file, line, fmt, reset);
  (void)col;
  (void)reset;
}

/* Use C23 __VA_OPT__ for clean zero-argument support */
#define _OSRS_LOG_PRINT(level, cat, file, line, fmt, ...)                      \
  do {                                                                         \
    if (_OSRS_LOG_ENABLED(level)) {                                            \
      if (g_osrs_log_color) {                                                  \
        const char *_col = (level == OSRS_LOG_ERROR)   ? "\x1b[1;31m"          \
                           : (level == OSRS_LOG_WARN)  ? "\x1b[1;33m"          \
                           : (level == OSRS_LOG_INFO)  ? "\x1b[1;32m"          \
                           : (level == OSRS_LOG_DEBUG) ? "\x1b[1;36m"          \
                                                       : "\x1b[1;35m";         \
        fprintf(stderr, "%s[%s][%-7s]%s %s:%d  " fmt "\x1b[0m\n", _col,        \
                osrs_log_level_str(level), cat, "", file,                      \
                line __VA_OPT__(,) __VA_ARGS__);                              \
      } else {                                                                 \
        fprintf(stderr, "[%s][%-7s] %s:%d  " fmt "\n",                         \
                osrs_log_level_str(level), cat, file,                          \
                line __VA_OPT__(,) __VA_ARGS__);                              \
      }                                                                        \
    }                                                                          \
  } while (0)

/* -------------------------------------------------------------------------- */
/* Public macros                                                              */
/* -------------------------------------------------------------------------- */

#if OSRS_LOG_MAX >= OSRS_LOG_ERROR
#define OSRS_ERROR(cat, fmt, ...)                                              \
  _OSRS_LOG_PRINT(OSRS_LOG_ERROR, cat, __FILE_NAME__, __LINE__, fmt,           \
                  __VA_ARGS__)
#else
#define OSRS_ERROR(cat, fmt, ...) ((void)0)
#endif

#if OSRS_LOG_MAX >= OSRS_LOG_WARN
#define OSRS_WARN(cat, fmt, ...)                                               \
  _OSRS_LOG_PRINT(OSRS_LOG_WARN, cat, __FILE_NAME__, __LINE__, fmt, __VA_ARGS__)
#else
#define OSRS_WARN(cat, fmt, ...) ((void)0)
#endif

#if OSRS_LOG_MAX >= OSRS_LOG_INFO
#define OSRS_INFO(cat, fmt, ...)                                               \
  _OSRS_LOG_PRINT(OSRS_LOG_INFO, cat, __FILE_NAME__, __LINE__, fmt, __VA_ARGS__)
#else
#define OSRS_INFO(cat, fmt, ...) ((void)0)
#endif

#if OSRS_LOG_MAX >= OSRS_LOG_DEBUG
#define OSRS_DEBUG(cat, fmt, ...)                                              \
  _OSRS_LOG_PRINT(OSRS_LOG_DEBUG, cat, __FILE_NAME__, __LINE__, fmt,           \
                  __VA_ARGS__)
#else
#define OSRS_DEBUG(cat, fmt, ...) ((void)0)
#endif

#if OSRS_LOG_MAX >= OSRS_LOG_TRACE
#define OSRS_TRACE(cat, fmt, ...)                                              \
  _OSRS_LOG_PRINT(OSRS_LOG_TRACE, cat, __FILE_NAME__, __LINE__, fmt,           \
                  __VA_ARGS__)
#else
#define OSRS_TRACE(cat, fmt, ...) ((void)0)
#endif

/* -------------------------------------------------------------------------- */
/* Hex dump                                                                   */
/* -------------------------------------------------------------------------- */

void osrs_log_hex_level(osrs_log_level_t level, const char *cat,
                        const char *label, const uint8_t *data, size_t len);

#if OSRS_LOG_MAX >= OSRS_LOG_TRACE
#define OSRS_LOG_HEX(level, cat, label, data, len)                             \
  osrs_log_hex_level(level, cat, label, data, len)
#else
#define OSRS_LOG_HEX(level, cat, label, data, len) ((void)0)
#endif

/* Convenience shorthands */
#define OSRS_HEX_ERROR(cat, label, data, len)                                  \
  OSRS_LOG_HEX(OSRS_LOG_ERROR, cat, label, data, len)
#define OSRS_HEX_WARN(cat, label, data, len)                                   \
  OSRS_LOG_HEX(OSRS_LOG_WARN, cat, label, data, len)
#define OSRS_HEX_DEBUG(cat, label, data, len)                                  \
  OSRS_LOG_HEX(OSRS_LOG_DEBUG, cat, label, data, len)
#define OSRS_HEX_TRACE(cat, label, data, len)                                  \
  OSRS_LOG_HEX(OSRS_LOG_TRACE, cat, label, data, len)

#ifdef __cplusplus
}
#endif

#endif /* OSRS_LOG_H */

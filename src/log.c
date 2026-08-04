/*
 * osrs/log.c — Logging implementation
 * Pure C23, zero external dependencies.
 */

#include "osrs/log.h"

#include <time.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */
/* Globals                                                                    */
/* -------------------------------------------------------------------------- */

osrs_log_level_t g_osrs_log_level = OSRS_LOG_INFO;
bool g_osrs_log_timestamps = false;
bool g_osrs_log_color = false;
static bool g_log_initialized = false;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

const char *osrs_log_level_str(osrs_log_level_t level) {
  switch (level) {
  case OSRS_LOG_NONE:
    return "NONE";
  case OSRS_LOG_ERROR:
    return "ERROR";
  case OSRS_LOG_WARN:
    return "WARN";
  case OSRS_LOG_INFO:
    return "INFO";
  case OSRS_LOG_DEBUG:
    return "DEBUG";
  case OSRS_LOG_TRACE:
    return "TRACE";
  default:
    return "?";
  }
}

osrs_log_level_t osrs_log_level_parse(const char *s) {
  if (!s || !s[0])
    return OSRS_LOG_INFO;
  if (strcmp(s, "error") == 0 || strcmp(s, "ERROR") == 0)
    return OSRS_LOG_ERROR;
  if (strcmp(s, "warn") == 0 || strcmp(s, "WARN") == 0)
    return OSRS_LOG_WARN;
  if (strcmp(s, "info") == 0 || strcmp(s, "INFO") == 0)
    return OSRS_LOG_INFO;
  if (strcmp(s, "debug") == 0 || strcmp(s, "DEBUG") == 0)
    return OSRS_LOG_DEBUG;
  if (strcmp(s, "trace") == 0 || strcmp(s, "TRACE") == 0)
    return OSRS_LOG_TRACE;
  if (strcmp(s, "none") == 0 || strcmp(s, "NONE") == 0)
    return OSRS_LOG_NONE;
  return OSRS_LOG_INFO;
}

/* -------------------------------------------------------------------------- */
/* Init                                                                       */
/* -------------------------------------------------------------------------- */

void osrs_log_init(void) {
  if (g_log_initialized)
    return;
  g_log_initialized = true;

  const char *env = getenv("OSRS_LOG_LEVEL");
  if (env)
    g_osrs_log_level = osrs_log_level_parse(env);

  const char *color = getenv("OSRS_LOG_COLOR");
  if (color)
    g_osrs_log_color = (strcmp(color, "1") == 0 || strcmp(color, "yes") == 0 ||
                        strcmp(color, "true") == 0);
  else
    g_osrs_log_color = isatty(fileno(stderr));

  const char *ts = getenv("OSRS_LOG_TIMESTAMPS");
  if (ts)
    g_osrs_log_timestamps = (strcmp(ts, "1") == 0 || strcmp(ts, "yes") == 0 ||
                             strcmp(ts, "true") == 0);
}

/* -------------------------------------------------------------------------- */
/* Hex dump                                                                   */
/* -------------------------------------------------------------------------- */

void osrs_log_hex_level(osrs_log_level_t level, const char *cat,
                        const char *label, const uint8_t *data, size_t len) {
  if (!_OSRS_LOG_ENABLED(level))
    return;

  if (label && label[0])
    fprintf(stderr, "[%s][%-7s] %s (%zu bytes):\n", osrs_log_level_str(level),
            cat, label, len);

  char ascii[17];
  ascii[16] = '\0';

  for (size_t i = 0; i < len; i += 16) {
    fprintf(stderr, "  %04zx  ", i);

    for (size_t j = 0; j < 16; j++) {
      if (i + j < len) {
        uint8_t b = data[i + j];
        fprintf(stderr, "%02x ", b);
        ascii[j] = (b >= 32 && b < 127) ? (char)b : '.';
      } else {
        fprintf(stderr, "   ");
        ascii[j] = ' ';
      }
    }

    fprintf(stderr, " |%s|\n", ascii);
  }
}

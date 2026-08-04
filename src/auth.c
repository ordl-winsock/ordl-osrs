/*
 * osrs/auth.c — OSRS login authentication methods
 * Pure C23, zero external dependencies.
 */

#include "osrs/auth.h"

#include <string.h>

osrs_auth_method_t osrs_auth_method_from_string(const char *name) {
  if (name && (strcmp(name, "jagex") == 0 || strcmp(name, "2") == 0))
    return OSRS_AUTH_JAGEX;
  return OSRS_AUTH_LEGACY;
}

const char *osrs_auth_method_str(osrs_auth_method_t method) {
  return method == OSRS_AUTH_JAGEX ? "jagex" : "legacy";
}

/*
 * osrs/auth.h — OSRS login authentication methods
 * Pure C23, zero external dependencies.
 *
 * Two ways to authenticate with the game server:
 *   - LEGACY: username + password (auth_type 0)
 *   - JAGEX:  Jagex Account via a game session token (auth_type 2)
 *
 * The Jagex session token is obtained from the auth.jagex.com OAuth +
 * game-session flow (see jagex.h), or supplied directly by the user.
 */

#ifndef OSRS_AUTH_H
#define OSRS_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Auth type values as they appear on the wire in the login block. */
typedef enum {
  OSRS_AUTH_LEGACY = 0, /* username + password */
  OSRS_AUTH_JAGEX = 2,  /* Jagex Account session token */
} osrs_auth_method_t;

/* Credentials for one login attempt. `secret` holds the password (legacy)
 * or the session token (jagex). Bounded by the RSA login-block capacity. */
typedef struct {
  osrs_auth_method_t method;
  char username[65]; /* legacy username, or Jagex account email */
  char secret[128];  /* password (legacy) or session token (jagex) */
} osrs_auth_t;

/* Parse an auth method name ("legacy" | "jagex"). Defaults to legacy. */
osrs_auth_method_t osrs_auth_method_from_string(const char *name);

const char *osrs_auth_method_str(osrs_auth_method_t method);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_AUTH_H */

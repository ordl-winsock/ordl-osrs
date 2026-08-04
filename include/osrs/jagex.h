/*
 * osrs/jagex.h — Jagex Account authentication (OAuth + game-session flow)
 * Pure C23, zero external dependencies.
 *
 * Obtains a Jagex game session token for the OSRS_AUTH_JAGEX login method.
 * Flow (verified against the open-source Bolt launcher):
 *   1. OAuth authorize (PKCE S256) at account.jagex.com/oauth2/auth
 *   2. Token exchange at account.jagex.com/oauth2/token -> id_token
 *   3. Session creation at auth.jagex.com/game-session/v1/sessions -> sessionId
 *
 * The authorize step is interactive (the user signs in via a browser); the
 * token exchange and session creation are plain HTTPS calls.
 */

#ifndef OSRS_JAGEX_H
#define OSRS_JAGEX_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Jagex OAuth endpoints / client registration. */
#define OSRS_JAGEX_ORIGIN "account.jagex.com"
#define OSRS_JAGEX_AUTH_API "auth.jagex.com"
#define OSRS_JAGEX_AUTH_API_PATH "/game-session/v1/sessions"
#define OSRS_JAGEX_CLIENT_ID "com_jagex_auth_desktop_launcher"
#define OSRS_JAGEX_REDIRECT                                                    \
  "https://secure.runescape.com/m=weblogin/launcher-redirect"
#define OSRS_JAGEX_SCOPE "openid offline gamesso.token.create user.profile.read"

/* Generate a PKCE verifier (random, URL-safe) and its S256 challenge
 * (base64url(SHA256(verifier))). Buffers must be >= 64 / 44 bytes. */
void osrs_jagex_pkce(char *verifier, size_t verifier_cap, char *challenge,
                     size_t challenge_cap);

/* Generate a random URL-safe state string (32 chars). */
void osrs_jagex_random_state(char *out, size_t cap);

/* Build the authorize URL the user must open to sign in.
 * out must be >= 1024 bytes. */
void osrs_jagex_authorize_url(char *out, size_t cap, const char *challenge,
                              const char *state);

/* Extract the "code" and verify "state" from a pasted redirect URL.
 * Returns true and fills code_out on success. */
bool osrs_jagex_parse_redirect(const char *redirect_url,
                               const char *expect_state, char *code_out,
                               size_t code_cap);

/* Exchange an authorization code for OAuth tokens. On success, fills
 * id_token_out (the id_token JWT) and returns true. */
bool osrs_jagex_exchange_code(const char *code, const char *verifier,
                              char *id_token_out, size_t id_token_cap,
                              char *err, size_t err_cap);

/* Create a game session from an id_token. On success, fills session_id_out
 * and returns true. */
bool osrs_jagex_create_session(const char *id_token, char *session_id_out,
                               size_t session_id_cap, char *err,
                               size_t err_cap);

/* Full interactive flow: prints the authorize URL, prompts for the pasted
 * redirect, then performs the token exchange and session creation.
 * On success fills session_id_out and returns true. */
bool osrs_jagex_login_interactive(char *session_id_out, size_t session_id_cap);

/* base64url encode (no padding). */
void osrs_jagex_b64url(const unsigned char *in, size_t len, char *out,
                       size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_JAGEX_H */

/*
 * osrs/jagex.c — Jagex Account authentication (OAuth + game-session flow)
 * Pure C23, zero external dependencies.
 */

#include "osrs/jagex.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "osrs/https.h"
#include "security/sha256.h"

/* -------------------------------------------------------------------------- */
/* Random + base64url */
/* -------------------------------------------------------------------------- */

static void rand_bytes(unsigned char *out, size_t len) {
  ssize_t n = getrandom(out, len, 0);
  if (n != (ssize_t)len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
      size_t got = 0;
      while (got < len) {
        ssize_t r = read(fd, out + got, len - got);
        if (r <= 0)
          break;
        got += (size_t)r;
      }
      close(fd);
    }
  }
}

void osrs_jagex_b64url(const unsigned char *in, size_t len, char *out,
                       size_t cap) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t o = 0;
  size_t i = 0;
  while (i + 3 <= len && o + 4 < cap) {
    unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = tbl[(v >> 6) & 63];
    out[o++] = tbl[v & 63];
    i += 3;
  }
  size_t rem = len - i;
  if (rem == 1 && o + 2 < cap) {
    unsigned v = in[i] << 16;
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
  } else if (rem == 2 && o + 3 < cap) {
    unsigned v = (in[i] << 16) | (in[i + 1] << 8);
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = tbl[(v >> 6) & 63];
  }
  out[o] = '\0';
}

static void random_urlsafe(char *out, size_t cap, size_t nchars) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  unsigned char rnd[128];
  if (nchars > sizeof(rnd))
    nchars = sizeof(rnd);
  rand_bytes(rnd, nchars);
  if (nchars + 1 > cap)
    nchars = cap - 1;
  for (size_t i = 0; i < nchars; i++)
    out[i] = tbl[rnd[i] & 63];
  out[nchars] = '\0';
}

/* -------------------------------------------------------------------------- */
/* PKCE */
/* -------------------------------------------------------------------------- */

void osrs_jagex_pkce(char *verifier, size_t verifier_cap, char *challenge,
                     size_t challenge_cap) {
  /* 64-char verifier from 48 random bytes. */
  random_urlsafe(verifier, verifier_cap, 64);
  uint8_t digest[32];
  gc_sha256((const uint8_t *)verifier, strlen(verifier), digest);
  osrs_jagex_b64url(digest, 32, challenge, challenge_cap);
}

void osrs_jagex_random_state(char *out, size_t cap) {
  random_urlsafe(out, cap, 32);
}

/* -------------------------------------------------------------------------- */
/* URL encoding + authorize URL */
/* -------------------------------------------------------------------------- */

static void url_encode(const char *in, char *out, size_t cap) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < cap;
       p++) {
    unsigned char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out[o++] = (char)c;
    } else if (c == ' ') {
      out[o++] = '+'; /* form encoding, matches URLSearchParams */
    } else {
      o += (size_t)snprintf(out + o, cap - o, "%%%02X", c);
    }
  }
  out[o] = '\0';
}

void osrs_jagex_authorize_url(char *out, size_t cap, const char *challenge,
                              const char *state) {
  char scope_enc[256];
  char redir_enc[512];
  url_encode(OSRS_JAGEX_SCOPE, scope_enc, sizeof(scope_enc));
  url_encode(OSRS_JAGEX_REDIRECT, redir_enc, sizeof(redir_enc));
  snprintf(out, cap,
           "https://" OSRS_JAGEX_ORIGIN "/oauth2/auth"
           "?auth_method=&login_type=&flow=launcher&response_type=code"
           "&client_id=%s&redirect_uri=%s&code_challenge=%s"
           "&code_challenge_method=S256&prompt=login&scope=%s&state=%s",
           OSRS_JAGEX_CLIENT_ID, redir_enc, challenge, scope_enc, state);
}

/* -------------------------------------------------------------------------- */
/* Redirect parsing */
/* -------------------------------------------------------------------------- */

static bool query_get(const char *query, const char *key, char *out,
                      size_t cap) {
  size_t klen = strlen(key);
  const char *p = query;
  while (p && *p) {
    if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
      const char *v = p + klen + 1;
      const char *end = strchr(v, '&');
      size_t n = end ? (size_t)(end - v) : strlen(v);
      if (n >= cap)
        n = cap - 1;
      memcpy(out, v, n);
      out[n] = '\0';
      return true;
    }
    p = strchr(p, '&');
    if (p)
      p++;
  }
  return false;
}

bool osrs_jagex_parse_redirect(const char *redirect_url,
                               const char *expect_state, char *code_out,
                               size_t code_cap) {
  const char *q = strchr(redirect_url, '?');
  if (!q)
    return false;
  q++;
  char state[128] = {0};
  if (!query_get(q, "state", state, sizeof(state)))
    return false;
  if (expect_state && strcmp(state, expect_state) != 0)
    return false;
  return query_get(q, "code", code_out, code_cap);
}

/* -------------------------------------------------------------------------- */
/* Minimal JSON string extractor ("key":"value" or "key": "value")            */
/* -------------------------------------------------------------------------- */

static bool json_get_string(const char *json, const char *key, char *out,
                            size_t cap) {
  char pat[96];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(json, pat);
  if (!p)
    return false;
  p += strlen(pat);
  while (*p == ' ' || *p == '\t' || *p == ':')
    p++;
  if (*p != '"') { /* non-string value: read until , } or whitespace */
    size_t n = 0;
    while (p[n] && p[n] != ',' && p[n] != '}' && p[n] != ' ' && n + 1 < cap)
      n++;
    memcpy(out, p, n);
    out[n] = '\0';
    return n > 0;
  }
  p++;
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < cap) {
    if (*p == '\\' && p[1]) {
      p++;
      char c = *p;
      out[o++] = c == 'n' ? '\n' : (c == 't' ? '\t' : c);
      p++;
    } else {
      out[o++] = *p++;
    }
  }
  out[o] = '\0';
  return true;
}

/* -------------------------------------------------------------------------- */
/* Token exchange + session creation */
/* -------------------------------------------------------------------------- */

bool osrs_jagex_exchange_code(const char *code, const char *verifier,
                              char *id_token_out, size_t id_token_cap,
                              char *err, size_t err_cap) {
  char redir_enc[512];
  url_encode(OSRS_JAGEX_REDIRECT, redir_enc, sizeof(redir_enc));
  char body[2048];
  snprintf(body, sizeof(body),
           "grant_type=authorization_code&client_id=%s&code=%s"
           "&code_verifier=%s&redirect_uri=%s",
           OSRS_JAGEX_CLIENT_ID, code, verifier, redir_enc);

  static char resp[16384];
  osrs_https_response_t r;
  if (!osrs_https_post(OSRS_JAGEX_ORIGIN, 443, "/oauth2/token",
                       "application/x-www-form-urlencoded", body, resp,
                       sizeof(resp) - 1, &r)) {
    snprintf(err, err_cap, "token request transport failed");
    return false;
  }
  if (r.status != 200) {
    snprintf(err, err_cap, "token exchange HTTP %d: %.200s", r.status, resp);
    return false;
  }
  if (!json_get_string(resp, "id_token", id_token_out, id_token_cap)) {
    snprintf(err, err_cap, "no id_token in response: %.200s", resp);
    return false;
  }
  return true;
}

bool osrs_jagex_create_session(const char *id_token, char *session_id_out,
                               size_t session_id_cap, char *err,
                               size_t err_cap) {
  char body[4096];
  snprintf(body, sizeof(body), "{\"idToken\":\"%s\"}", id_token);

  static char resp[16384];
  osrs_https_response_t r;
  if (!osrs_https_post(OSRS_JAGEX_AUTH_API, 443, OSRS_JAGEX_AUTH_API_PATH,
                       "application/json", body, resp, sizeof(resp) - 1, &r)) {
    snprintf(err, err_cap, "session request transport failed");
    return false;
  }
  if (r.status != 200) {
    snprintf(err, err_cap, "session create HTTP %d: %.200s", r.status, resp);
    return false;
  }
  /* Accept several plausible field names for the session id. */
  if (json_get_string(resp, "sessionId", session_id_out, session_id_cap) ||
      json_get_string(resp, "session_id", session_id_out, session_id_cap) ||
      json_get_string(resp, "id", session_id_out, session_id_cap)) {
    return true;
  }
  snprintf(err, err_cap, "no session id in response: %.200s", resp);
  return false;
}

/* -------------------------------------------------------------------------- */
/* Interactive flow */
/* -------------------------------------------------------------------------- */

bool osrs_jagex_login_interactive(char *session_id_out, size_t session_id_cap) {
  char verifier[96];
  char challenge[64];
  char state[64];
  osrs_jagex_pkce(verifier, sizeof(verifier), challenge, sizeof(challenge));
  osrs_jagex_random_state(state, sizeof(state));

  char url[1024];
  osrs_jagex_authorize_url(url, sizeof(url), challenge, state);

  fprintf(stderr, "\n=== Jagex Account sign-in ===\n");
  fprintf(stderr, "Open this URL in your browser and sign in:\n\n%s\n\n", url);
  fprintf(stderr,
          "After signing in, your browser will redirect to a "
          "secure.runescape.com page.\nCopy the FULL URL from the address bar "
          "and paste it here,\nthen press Enter:\n> ");
  fflush(stderr);

  char redirect[2048];
  if (!fgets(redirect, sizeof(redirect), stdin))
    return false;

  char code[1024];
  if (!osrs_jagex_parse_redirect(redirect, state, code, sizeof(code))) {
    fprintf(stderr, "Could not parse code/state from that URL.\n");
    return false;
  }

  char id_token[3072];
  char err[512];
  if (!osrs_jagex_exchange_code(code, verifier, id_token, sizeof(id_token), err,
                                sizeof(err))) {
    fprintf(stderr, "Token exchange failed: %s\n", err);
    return false;
  }

  if (!osrs_jagex_create_session(id_token, session_id_out, session_id_cap, err,
                                 sizeof(err))) {
    fprintf(stderr, "Session creation failed: %s\n", err);
    return false;
  }
  return true;
}

/*
 * tools/jagex_auth.c — drive the Jagex OAuth flow in two steps.
 *
 *   jagex_auth url
 *       Generate PKCE, print the authorize URL, and stash the
 *       verifier+state in /tmp/jagex_pkce.txt for step 2.
 *
 *   jagex_auth exchange "<full redirect url>"
 *       Parse the code from the redirect, do the token exchange and
 *       session creation over HTTPS, and print the game session token.
 */

#include <stdio.h>
#include <string.h>

#include "osrs/jagex.h"

#define PKCE_FILE "/tmp/jagex_pkce.txt"

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <url|exchange>\n", argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "url") == 0) {
    char verifier[96], challenge[64], state[64];
    osrs_jagex_pkce(verifier, sizeof(verifier), challenge, sizeof(challenge));
    osrs_jagex_random_state(state, sizeof(state));

    FILE *f = fopen(PKCE_FILE, "w");
    if (!f) {
      perror("fopen");
      return 1;
    }
    fprintf(f, "%s\n%s\n", verifier, state);
    fclose(f);

    char url[1024];
    osrs_jagex_authorize_url(url, sizeof(url), challenge, state);
    printf("%s\n", url);
    return 0;
  }

  if (strcmp(argv[1], "exchange") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: %s exchange \"<redirect url>\"\n", argv[0]);
      return 1;
    }
    char verifier[96] = {0}, state[64] = {0};
    FILE *f = fopen(PKCE_FILE, "r");
    if (!f) {
      fprintf(stderr, "run '%s url' first\n", argv[0]);
      return 1;
    }
    if (!fgets(verifier, sizeof(verifier), f) ||
        !fgets(state, sizeof(state), f)) {
      fclose(f);
      fprintf(stderr, "bad %s\n", PKCE_FILE);
      return 1;
    }
    fclose(f);
    verifier[strcspn(verifier, "\r\n")] = 0;
    state[strcspn(state, "\r\n")] = 0;

    char code[1024];
    if (!osrs_jagex_parse_redirect(argv[2], state, code, sizeof(code))) {
      fprintf(stderr, "could not parse code/state from redirect url\n");
      return 1;
    }
    fprintf(stderr, "got code %.12s...\n", code);

    char id_token[3072], err[512];
    if (!osrs_jagex_exchange_code(code, verifier, id_token, sizeof(id_token),
                                  err, sizeof(err))) {
      fprintf(stderr, "token exchange failed: %s\n", err);
      return 1;
    }
    fprintf(stderr, "got id_token (%.40s...)\n", id_token);

    char session[256];
    if (!osrs_jagex_create_session(id_token, session, sizeof(session), err,
                                   sizeof(err))) {
      fprintf(stderr, "session creation failed: %s\n", err);
      return 1;
    }
    printf("%s\n", session);
    return 0;
  }

  fprintf(stderr, "unknown mode '%s'\n", argv[1]);
  return 1;
}

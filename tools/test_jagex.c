/*
 * tools/test_jagex.c — exercise the Jagex OAuth HTTPS mechanics.
 * Tests PKCE, URL building, redirect parsing (all offline), and the token
 * exchange against the live endpoint (expecting a clean HTTP 400 for a fake
 * code — which proves the HTTPS POST + JSON path works).
 */

#include <stdio.h>
#include <string.h>

#include "osrs/jagex.h"

static int failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL: %s\n", msg);                                               \
      failures++;                                                              \
    } else {                                                                   \
      printf("ok: %s\n", msg);                                                 \
    }                                                                          \
  } while (0)

int main(void) {
  /* PKCE: verifier is 64 URL-safe chars; challenge is 43 base64url chars. */
  char verifier[96], challenge[64];
  osrs_jagex_pkce(verifier, sizeof(verifier), challenge, sizeof(challenge));
  CHECK(strlen(verifier) == 64, "pkce verifier is 64 chars");
  CHECK(strlen(challenge) == 43, "pkce challenge is 43 chars (b64url sha256)");
  CHECK(strchr(challenge, '+') == NULL && strchr(challenge, '/') == NULL &&
            strchr(challenge, '=') == NULL,
        "challenge is base64url (no +/=)");

  /* Authorize URL contains the key parameters. */
  char url[1024];
  osrs_jagex_authorize_url(url, sizeof(url), challenge, "state123");
  CHECK(strstr(url, "account.jagex.com/oauth2/auth") != NULL,
        "authorize url host/path");
  CHECK(strstr(url, "client_id=com_jagex_auth_desktop_launcher") != NULL,
        "authorize url client_id");
  CHECK(strstr(url, "code_challenge=") != NULL, "authorize url challenge");
  CHECK(strstr(url, "gamesso.token.create") != NULL, "authorize url scope");
  CHECK(strstr(url, "state=state123") != NULL, "authorize url state");

  /* Redirect parsing. */
  char code[256];
  bool ok = osrs_jagex_parse_redirect(
      "https://secure.runescape.com/m=weblogin/launcher-redirect?code=abc123&"
      "state=state123",
      "state123", code, sizeof(code));
  CHECK(ok && strcmp(code, "abc123") == 0, "parse_redirect extracts code");
  ok = osrs_jagex_parse_redirect("https://x/?code=abc&state=WRONG", "state123",
                                 code, sizeof(code));
  CHECK(!ok, "parse_redirect rejects wrong state");

  /* Live token exchange with a fake code: expect HTTP 400 (not a crash,
   * not a transport failure) — proves HTTPS POST + JSON error parsing works. */
  char id_token[512], err[512];
  bool exchanged = osrs_jagex_exchange_code("fakecode123", verifier, id_token,
                                            sizeof(id_token), err, sizeof(err));
  printf("token exchange (fake code) -> %s : %s\n",
         exchanged ? "SUCCESS(unexpected!)" : "rejected(expected)", err);
  CHECK(!exchanged, "fake code is rejected");
  CHECK(strstr(err, "HTTP 4") != NULL || strstr(err, "HTTP 3") != NULL,
        "got a real HTTP status back from account.jagex.com");

  if (failures) {
    printf("\n%d FAILURES\n", failures);
    return 1;
  }
  printf("\nAll jagex tests passed.\n");
  return 0;
}

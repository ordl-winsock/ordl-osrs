/*
 * osrs/https.h — minimal blocking HTTPS (HTTP/1.1 over TLS 1.3) client
 * Pure C23, zero external dependencies (uses the vendored TLS stack).
 *
 * Supports what the Jagex OAuth + game-session flows need: GET and POST
 * with a body, reading the status code and response body. Handles
 * Content-Length and chunked transfer encoding.
 */

#ifndef OSRS_HTTPS_H
#define OSRS_HTTPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int status;      /* HTTP status code, or -1 on transport error */
  char *body;      /* response body (caller-provided buffer) */
  size_t body_len; /* bytes written into body */
} osrs_https_response_t;

/* HTTPS GET. Fills resp->body (up to body_cap). Returns true if a response
 * was received (check resp->status for the HTTP code). */
bool osrs_https_get(const char *host, uint16_t port, const char *path,
                    char *body, size_t body_cap, osrs_https_response_t *resp);

/* HTTPS POST with a body (content_type e.g. "application/x-www-form-urlencoded"
 * or "application/json"). */
bool osrs_https_post(const char *host, uint16_t port, const char *path,
                     const char *content_type, const char *req_body, char *body,
                     size_t body_cap, osrs_https_response_t *resp);

/* Find a header value (case-insensitive) in a raw response head.
 * Returns pointer into the head (not null-terminated copy) or NULL.
 * len receives the value length. */
const char *osrs_https_header(const char *head, const char *name, size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* OSRS_HTTPS_H */

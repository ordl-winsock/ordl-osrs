/*
 * osrs/https.c — minimal blocking HTTPS (HTTP/1.1 over TLS 1.3) client
 * Pure C23, zero external dependencies.
 */

#include "osrs/https.h"
#include "osrs/log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "osrs/net.h"
#include "security/tls13.h"

#define HTTPS_MAX_HEAD 16384
#define HTTPS_UA "osrs-c-client/1.0"

/* Read all available application data until close/EOF, appending to buf. */
static size_t tls_read_all(gc_tls13_client_t *cli, int fd, uint8_t *buf,
                           size_t cap) {
  size_t total = 0;
  while (total < cap) {
    ssize_t n = gc_tls13_client_recv(cli, fd, buf + total, cap - total);
    if (n <= 0)
      break;
    total += (size_t)n;
  }
  return total;
}

static int hexval(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* Decode chunked transfer-encoding in-place. Returns decoded length. */
static size_t dechunk(uint8_t *buf, size_t len) {
  size_t rd = 0, wr = 0;
  for (;;) {
    /* parse chunk size line */
    size_t size = 0;
    bool got = false;
    while (rd < len) {
      uint8_t c = buf[rd];
      if (c == '\r') {
        rd++;
        if (rd < len && buf[rd] == '\n')
          rd++;
        break;
      }
      int hv = hexval(c);
      if (hv >= 0) {
        size = (size << 4) | (unsigned)hv;
        got = true;
      }
      rd++;
    }
    if (!got)
      break;
    if (size == 0)
      break; /* terminal chunk */
    if (rd + size > len)
      size = len - rd;
    memmove(buf + wr, buf + rd, size);
    wr += size;
    rd += size;
    /* skip trailing CRLF */
    if (rd < len && buf[rd] == '\r')
      rd++;
    if (rd < len && buf[rd] == '\n')
      rd++;
  }
  return wr;
}

const char *osrs_https_header(const char *head, const char *name, size_t *len) {
  size_t nlen = strlen(name);
  const char *p = head;
  /* skip the status line */
  const char *eol = strstr(p, "\r\n");
  if (!eol)
    return NULL;
  p = eol + 2;
  while (*p && !(p[0] == '\r' && p[1] == '\n')) {
    const char *line_end = strstr(p, "\r\n");
    if (!line_end)
      line_end = p + strlen(p);
    if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
      const char *v = p + nlen + 1;
      while (*v == ' ' || *v == '\t')
        v++;
      *len = (size_t)(line_end - v);
      return v;
    }
    if (*line_end == '\0')
      break;
    p = line_end + 2;
  }
  return NULL;
}

static bool https_request(const char *host, uint16_t port, const char *method,
                          const char *path, const char *content_type,
                          const char *req_body, char *body, size_t body_cap,
                          osrs_https_response_t *resp) {
  resp->status = -1;
  resp->body = body;
  resp->body_len = 0;
  if (body_cap)
    body[0] = '\0';

  int fd = osrs_tcp_connect(host, port, 10000);
  if (fd < 0) {
    OSRS_ERROR(OSRS_LOG_CAT_HTTP, "HTTPS connect failed: %s:%d", host, port);
    return false;
  }
  OSRS_DEBUG(OSRS_LOG_CAT_HTTP, "HTTPS connected: %s:%d", host, port);

  gc_tls13_client_t cli;
  if (!gc_tls13_client_handshake(&cli, fd, host)) {
    OSRS_ERROR(OSRS_LOG_CAT_HTTP, "HTTPS TLS handshake failed: %s:%d", host,
               port);
    close(fd);
    return false;
  }
  OSRS_DEBUG(OSRS_LOG_CAT_HTTP, "HTTPS TLS handshake OK: %s (cipher 0x%04x)",
             host, cli.cipher_suite);

  /* Build the request. */
  size_t body_len = req_body ? strlen(req_body) : 0;
  char head[2048];
  int hlen;
  if (req_body) {
    hlen = snprintf(head, sizeof(head),
                    "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
                    "Accept: application/json\r\nContent-Type: %s\r\n"
                    "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                    method, path, host, HTTPS_UA, content_type, body_len);
  } else {
    hlen = snprintf(head, sizeof(head),
                    "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
                    "Accept: application/json\r\nConnection: close\r\n\r\n",
                    method, path, host, HTTPS_UA);
  }

  bool ok = gc_tls13_client_send(&cli, fd, (const uint8_t *)head, (size_t)hlen);
  if (ok && req_body)
    ok = gc_tls13_client_send(&cli, fd, (const uint8_t *)req_body, body_len);
  if (!ok) {
    close(fd);
    return false;
  }

  /* Read the full response (Connection: close -> server closes). */
  static uint8_t raw[HTTPS_MAX_HEAD + 65536];
  size_t total = tls_read_all(&cli, fd, raw, sizeof(raw) - 1);
  close(fd);
  if (total == 0) {
    OSRS_ERROR(OSRS_LOG_CAT_HTTP, "HTTPS empty response from %s:%d", host,
               port);
    return false;
  }
  raw[total] = '\0';

  /* Split head / body. */
  char *sep = strstr((char *)raw, "\r\n\r\n");
  if (!sep)
    return false;
  size_t head_len = (size_t)(sep - (char *)raw) + 2; /* keep CRLF lines */
  uint8_t *rbody = (uint8_t *)sep + 4;
  size_t rbody_len = total - (head_len + 2);

  /* Status code. */
  int status = -1;
  if (memcmp(raw, "HTTP/", 5) == 0) {
    const char *sp = strchr((char *)raw, ' ');
    if (sp)
      status = atoi(sp + 1);
  }
  resp->status = status;

  OSRS_DEBUG(OSRS_LOG_CAT_HTTP,
             "HTTPS response from %s:%d: status=%d body=%zuB", host, port,
             status, rbody_len);

  /* Null-terminate the head for header scanning. */
  char head_copy[HTTPS_MAX_HEAD];
  if (head_len >= sizeof(head_copy))
    head_len = sizeof(head_copy) - 1;
  memcpy(head_copy, raw, head_len);
  head_copy[head_len] = '\0';

  /* Chunked? */
  size_t tlen;
  const char *te = osrs_https_header(head_copy, "Transfer-Encoding", &tlen);
  if (te && tlen >= 7 && memcmp(te, "chunked", 7) == 0) {
    rbody_len = dechunk(rbody, rbody_len);
  } else {
    size_t clen;
    const char *cl = osrs_https_header(head_copy, "Content-Length", &clen);
    if (cl) {
      size_t want = (size_t)strtoul(cl, NULL, 10);
      if (want < rbody_len)
        rbody_len = want;
    }
  }

  if (rbody_len > body_cap)
    rbody_len = body_cap;
  if (body_cap) {
    memcpy(body, rbody, rbody_len);
    body[rbody_len] = '\0';
  }
  resp->body_len = rbody_len;
  return true;
}

bool osrs_https_get(const char *host, uint16_t port, const char *path,
                    char *body, size_t body_cap, osrs_https_response_t *resp) {
  return https_request(host, port, "GET", path, NULL, NULL, body, body_cap,
                       resp);
}

bool osrs_https_post(const char *host, uint16_t port, const char *path,
                     const char *content_type, const char *req_body, char *body,
                     size_t body_cap, osrs_https_response_t *resp) {
  return https_request(host, port, "POST", path, content_type, req_body, body,
                       body_cap, resp);
}

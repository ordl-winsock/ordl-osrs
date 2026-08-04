/*
 * tools/test_tls.c — TLS 1.3 client smoke test.
 * Connects to a host:port, does the handshake, sends an HTTPS GET,
 * and prints the response head. Usage:
 *   ./build/test_tls <host> <port> <path>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osrs/net.h"
#include "security/tls13.h"

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <host> <port> <path>\n", argv[0]);
    return 1;
  }
  const char *host = argv[1];
  int port = atoi(argv[2]);
  const char *path = argv[3];

  int fd = osrs_tcp_connect(host, (uint16_t)port, 10000);
  if (fd < 0) {
    fprintf(stderr, "TCP connect to %s:%d failed\n", host, port);
    return 1;
  }
  printf("TCP connected to %s:%d\n", host, port);

  gc_tls13_client_t cli;
  if (!gc_tls13_client_handshake(&cli, fd, host)) {
    fprintf(stderr, "TLS handshake failed\n");
    return 1;
  }
  printf("TLS handshake OK (cipher 0x%04x)\n", cli.cipher_suite);

  char req[1024];
  int rlen =
      snprintf(req, sizeof(req),
               "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: osrs-c/1.0\r\n"
               "Accept: application/json\r\nConnection: close\r\n\r\n",
               path, host);
  if (!gc_tls13_client_send(&cli, fd, (const uint8_t *)req, (size_t)rlen)) {
    fprintf(stderr, "TLS send failed\n");
    return 1;
  }

  uint8_t buf[16384];
  size_t total = 0;
  for (;;) {
    ssize_t n =
        gc_tls13_client_recv(&cli, fd, buf + total, sizeof(buf) - 1 - total);
    if (n <= 0)
      break;
    total += (size_t)n;
    if (total >= sizeof(buf) - 1)
      break;
  }
  buf[total] = '\0';
  printf("--- response (%zu bytes) ---\n", total);
  for (size_t i = 0; i < total && i < 1200; i++) {
    unsigned char c = buf[i];
    if (c >= 32 && c < 127)
      putchar(c);
    else if (c == '\n' || c == '\r' || c == '\t')
      putchar(c);
    else
      printf("\\x%02x", c);
  }
  putchar('\n');
  return 0;
}

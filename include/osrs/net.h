/*
 * osrs/net.h - TCP client for OSRS game connection
 * Pure C23, POSIX sockets, zero external dependencies.
 */

#ifndef OSRS_NET_H
#define OSRS_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OSRS_NET_RECV_CAP 65536
#define OSRS_NET_SEND_CAP 65536

typedef struct {
  int fd;
  bool connected;
  bool peer_closed; /* remote sent FIN; surfaced once recv buffer drains */

  /* Receive ring buffer (linear, compacting) */
  uint8_t recv_buf[OSRS_NET_RECV_CAP];
  size_t recv_len;

  /* Send queue (linear, compacting) */
  uint8_t send_buf[OSRS_NET_SEND_CAP];
  size_t send_len;
} osrs_net_t;

/* Connect to host:port (blocking with timeout). Returns true on success. */
bool osrs_net_connect(osrs_net_t *net, const char *host, uint16_t port,
                      int timeout_ms);

/* Close connection */
void osrs_net_close(osrs_net_t *net);

/* Queue bytes for sending (returns false if queue full) */
bool osrs_net_queue(osrs_net_t *net, const uint8_t *data, size_t len);

/* Flush send queue and drain receive buffer. Call every frame.
 * Returns false on fatal socket error. */
bool osrs_net_poll(osrs_net_t *net);

/* Consume received bytes from the front of the receive buffer */
size_t osrs_net_read(osrs_net_t *net, uint8_t *out, size_t len);

/* Peek at received bytes without consuming */
size_t osrs_net_peek(const osrs_net_t *net, uint8_t *out, size_t len);

/* Number of bytes available to read */
size_t osrs_net_available(const osrs_net_t *net);

/* Discard n bytes from the receive buffer */
void osrs_net_consume(osrs_net_t *net, size_t n);

/* Raw blocking TCP connect for TLS. Returns a blocking-mode fd, or -1. */
int osrs_tcp_connect(const char *host, uint16_t port, int timeout_ms);

#endif /* OSRS_NET_H */

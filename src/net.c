/*
 * osrs/net.c - TCP client for OSRS game connection
 * Pure C23, POSIX sockets, zero external dependencies.
 */

#include "osrs/net.h"
#include "osrs/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

bool osrs_net_connect(osrs_net_t *net, const char *host, uint16_t port,
                      int timeout_ms) {
  memset(net, 0, sizeof(*net));
  net->fd = -1;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", port);

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *res = NULL;
  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    OSRS_ERROR(OSRS_LOG_CAT_NET, "Failed to resolve %s:%u", host, port);
    return false;
  }

  int fd = -1;
  for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0)
      continue;

    /* Non-blocking connect with timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
    if (rc < 0 && errno != EINPROGRESS) {
      close(fd);
      fd = -1;
      continue;
    }

    if (rc < 0) {
      fd_set wfds;
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);
      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      rc = select(fd + 1, NULL, &wfds, NULL, &tv);
      if (rc <= 0) {
        close(fd);
        fd = -1;
        continue;
      }
      int err = 0;
      socklen_t err_len = sizeof(err);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0 ||
          err != 0) {
        close(fd);
        fd = -1;
        continue;
      }
    }
    break;
  }
  freeaddrinfo(res);

  if (fd < 0) {
    OSRS_ERROR(OSRS_LOG_CAT_NET, "Failed to connect to %s:%u", host, port);
    return false;
  }

  /* Leave socket non-blocking for poll-based I/O */
  net->fd = fd;
  net->connected = true;
  OSRS_INFO(OSRS_LOG_CAT_NET, "Connected to %s:%u", host, port);
  return true;
}

void osrs_net_close(osrs_net_t *net) {
  if (net->fd >= 0) {
    OSRS_INFO(OSRS_LOG_CAT_NET, "Disconnected");
    close(net->fd);
    net->fd = -1;
  }
  net->connected = false;
  net->recv_len = 0;
  net->send_len = 0;
}

bool osrs_net_queue(osrs_net_t *net, const uint8_t *data, size_t len) {
  if (net->send_len + len > OSRS_NET_SEND_CAP)
    return false;
  memcpy(net->send_buf + net->send_len, data, len);
  net->send_len += len;
  OSRS_TRACE(OSRS_LOG_CAT_NET, "Queued %zu bytes", len);
  return true;
}

bool osrs_net_poll(osrs_net_t *net) {
  if (net->fd < 0)
    return false;

  /* Flush send queue */
  while (net->send_len > 0) {
    ssize_t n = send(net->fd, net->send_buf, net->send_len, MSG_NOSIGNAL);
    if (n > 0) {
      OSRS_TRACE(OSRS_LOG_CAT_NET, "Sent %zd bytes", n);
      memmove(net->send_buf, net->send_buf + n, net->send_len - (size_t)n);
      net->send_len -= (size_t)n;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    } else {
      OSRS_DEBUG(OSRS_LOG_CAT_NET, "Connection closed due to send error");
      osrs_net_close(net);
      return false;
    }
  }

  /* Drain receive buffer */
  for (;;) {
    if (net->recv_len >= OSRS_NET_RECV_CAP)
      break;
    ssize_t n = recv(net->fd, net->recv_buf + net->recv_len,
                     OSRS_NET_RECV_CAP - net->recv_len, 0);
    if (n > 0) {
      OSRS_TRACE(OSRS_LOG_CAT_NET, "Received %zd bytes", n);
      /* Clean dump of all newly received bytes */
      {
        static FILE *dump = NULL;
        if (!dump)
          dump = fopen("/tmp/osrs_raw_recv.bin", "wb");
        if (dump) {
          fwrite(net->recv_buf + net->recv_len, 1, (size_t)n, dump);
          fflush(dump);
        }
      }
      net->recv_len += (size_t)n;
    } else if (n == 0) {
      /* Remote closed: mark it, but surface any buffered data to the caller
       * first — the close is only fatal once the buffer is drained. */
      OSRS_DEBUG(OSRS_LOG_CAT_NET, "Connection closed by peer");
      net->peer_closed = true;
      break;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else if (errno == EINTR) {
      continue;
    } else {
      OSRS_DEBUG(OSRS_LOG_CAT_NET, "Connection closed due to recv error");
      osrs_net_close(net);
      return false;
    }
  }
  /* Only fail when the peer closed AND there's no data left to process. */
  if (net->peer_closed && net->recv_len == 0) {
    osrs_net_close(net);
    return false;
  }
  return true;
}

size_t osrs_net_available(const osrs_net_t *net) { return net->recv_len; }

size_t osrs_net_peek(const osrs_net_t *net, uint8_t *out, size_t len) {
  if (len > net->recv_len)
    len = net->recv_len;
  memcpy(out, net->recv_buf, len);
  return len;
}

size_t osrs_net_read(osrs_net_t *net, uint8_t *out, size_t len) {
  size_t n = osrs_net_peek(net, out, len);
  osrs_net_consume(net, n);
  return n;
}

void osrs_net_consume(osrs_net_t *net, size_t n) {
  if (n > net->recv_len)
    n = net->recv_len;
  memmove(net->recv_buf, net->recv_buf + n, net->recv_len - n);
  net->recv_len -= n;
}

int osrs_tcp_connect(const char *host, uint16_t port, int timeout_ms) {
  osrs_net_t net;
  if (!osrs_net_connect(&net, host, port, timeout_ms))
    return -1;
  /* Switch to blocking mode for the simple blocking TLS I/O loop. */
  int flags = fcntl(net.fd, F_GETFL, 0);
  fcntl(net.fd, F_SETFL, flags & ~O_NONBLOCK);
  return net.fd;
}

/*
 * tools/packet-logger.c - LD_PRELOAD packet logger for OSRS
 *
 * Usage: LD_PRELOAD=/path/to/libosrs_packets.so java -jar runelite.jar
 *        or: LD_PRELOAD=/path/to/libosrs_packets.so ./your-c-client
 *
 * Captures all TCP traffic to/from port 43594 and writes to
 * /tmp/osrs_packets.raw with a simple framed binary format:
 *
 *   uint8_t  direction   (0 = client->server, 1 = server->client)
 *   uint64_t timestamp   (milliseconds since epoch, little-endian)
 *   uint32_t length      (little-endian)
 *   uint8_t  data[length]
 *
 * Build: gcc -shared -fPIC -o libosrs_packets.so packet-logger.c -ldl
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define OSRS_PORT 43594
#define LOG_PATH "/tmp/osrs_packets.raw"
#define MAX_TRACKED_FDS 1024

/* -------------------------------------------------------------------------- */
/* Original function pointers                                                 */
/* -------------------------------------------------------------------------- */
static ssize_t (*real_read)(int fd, void *buf, size_t count);
static ssize_t (*real_write)(int fd, const void *buf, size_t count);
static ssize_t (*real_recv)(int sockfd, void *buf, size_t len, int flags);
static ssize_t (*real_send)(int sockfd, const void *buf, size_t len, int flags);
static ssize_t (*real_recvfrom)(int sockfd, void *buf, size_t len, int flags,
                                struct sockaddr *src_addr, socklen_t *addrlen);
static ssize_t (*real_sendto)(int sockfd, const void *buf, size_t len,
                              int flags, const struct sockaddr *dest_addr,
                              socklen_t addrlen);
static int (*real_connect)(int sockfd, const struct sockaddr *addr,
                           socklen_t addrlen);
static int (*real_close)(int fd);

/* -------------------------------------------------------------------------- */
/* Tracked socket state                                                       */
/* -------------------------------------------------------------------------- */
typedef struct {
  bool active;
  bool is_osrs;
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
} sock_info_t;

static sock_info_t g_socks[MAX_TRACKED_FDS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_log_fd = -1;
static bool g_initialized = false;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static bool get_port(const struct sockaddr_storage *ss, uint16_t *out) {
  if (ss->ss_family == AF_INET) {
    *out = ntohs(((const struct sockaddr_in *)ss)->sin_port);
    return true;
  }
  if (ss->ss_family == AF_INET6) {
    *out = ntohs(((const struct sockaddr_in6 *)ss)->sin6_port);
    return true;
  }
  return false;
}

static bool is_osrs_port(uint16_t port) { return port == OSRS_PORT; }

static void ensure_log_open(void) {
  if (g_log_fd >= 0)
    return;
  g_log_fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (g_log_fd < 0) {
    fprintf(stderr, "[packet-logger] failed to open %s\n", LOG_PATH);
    return;
  }
  /* Write a small header so decoders can identify the file */
  static const uint8_t header[] = {'O', 'S', 'R', 'S', 'P', 'K', 'T', 1};
  (void)write(g_log_fd, header, sizeof(header));
}

static void log_data(int fd, bool from_server, const void *data, size_t len) {
  if (g_log_fd < 0 || len == 0)
    return;

  uint8_t direction = from_server ? 1 : 0;
  uint64_t ts = now_ms();
  uint32_t ulen = (uint32_t)len;

  /* Write atomically as much as possible */
  uint8_t header[13];
  header[0] = direction;
  memcpy(header + 1, &ts, sizeof(ts));
  memcpy(header + 9, &ulen, sizeof(ulen));

  pthread_mutex_lock(&g_lock);
  (void)write(g_log_fd, header, sizeof(header));
  (void)write(g_log_fd, data, len);
  fsync(g_log_fd);
  pthread_mutex_unlock(&g_lock);
}

static void init(void) {
  if (g_initialized)
    return;
  g_initialized = true;

  real_read = dlsym(RTLD_NEXT, "read");
  real_write = dlsym(RTLD_NEXT, "write");
  real_recv = dlsym(RTLD_NEXT, "recv");
  real_send = dlsym(RTLD_NEXT, "send");
  real_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
  real_sendto = dlsym(RTLD_NEXT, "sendto");
  real_connect = dlsym(RTLD_NEXT, "connect");
  real_close = dlsym(RTLD_NEXT, "close");

  ensure_log_open();

  fprintf(stderr, "[packet-logger] loaded. logging port %d to %s\n", OSRS_PORT,
          LOG_PATH);
}

static bool fd_is_osrs(int fd) {
  if (fd < 0 || fd >= MAX_TRACKED_FDS)
    return false;
  pthread_mutex_lock(&g_lock);
  bool r = g_socks[fd].active && g_socks[fd].is_osrs;
  pthread_mutex_unlock(&g_lock);
  return r;
}

static void track_fd(int fd, bool is_osrs) {
  if (fd < 0 || fd >= MAX_TRACKED_FDS)
    return;
  pthread_mutex_lock(&g_lock);
  g_socks[fd].active = true;
  g_socks[fd].is_osrs = is_osrs;
  pthread_mutex_unlock(&g_lock);
}

static void untrack_fd(int fd) {
  if (fd < 0 || fd >= MAX_TRACKED_FDS)
    return;
  pthread_mutex_lock(&g_lock);
  memset(&g_socks[fd], 0, sizeof(g_socks[fd]));
  pthread_mutex_unlock(&g_lock);
}

/* -------------------------------------------------------------------------- */
/* Intercepted syscalls                                                       */
/* -------------------------------------------------------------------------- */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  init();
  int rc = real_connect(sockfd, addr, addrlen);
  if (rc == 0 && addr != NULL) {
    struct sockaddr_storage ss;
    memcpy(&ss, addr, addrlen < sizeof(ss) ? (size_t)addrlen : sizeof(ss));
    uint16_t port = 0;
    if (get_port(&ss, &port) && is_osrs_port(port)) {
      track_fd(sockfd, true);
      fprintf(stderr, "[packet-logger] OSRS connection on fd=%d\n", sockfd);
    } else {
      track_fd(sockfd, false);
    }
  }
  return rc;
}

int close(int fd) {
  init();
  if (fd >= 0 && fd < MAX_TRACKED_FDS && g_socks[fd].active &&
      g_socks[fd].is_osrs) {
    fprintf(stderr, "[packet-logger] OSRS connection closed on fd=%d\n", fd);
  }
  untrack_fd(fd);
  return real_close(fd);
}

ssize_t read(int fd, void *buf, size_t count) {
  init();
  ssize_t n = real_read(fd, buf, count);
  if (n > 0 && fd_is_osrs(fd))
    log_data(fd, true, buf, (size_t)n);
  return n;
}

ssize_t write(int fd, const void *buf, size_t count) {
  init();
  if (count > 0 && fd_is_osrs(fd))
    log_data(fd, false, buf, count);
  return real_write(fd, buf, count);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  init();
  ssize_t n = real_recv(sockfd, buf, len, flags);
  if (n > 0 && fd_is_osrs(sockfd))
    log_data(sockfd, true, buf, (size_t)n);
  return n;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
  init();
  if (len > 0 && fd_is_osrs(sockfd))
    log_data(sockfd, false, buf, len);
  return real_send(sockfd, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
  init();
  ssize_t n = real_recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
  if (n > 0 && fd_is_osrs(sockfd))
    log_data(sockfd, true, buf, (size_t)n);
  return n;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
  init();
  if (len > 0 && fd_is_osrs(sockfd))
    log_data(sockfd, false, buf, len);
  return real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

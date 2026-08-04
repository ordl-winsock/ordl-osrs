/*
 * Simple TCP proxy for OSRS packet capture.
 * Listens on a local port, forwards to destination, and dumps hex.
 * Usage: proxy <local_port> <dest_host> <dest_port> <dump_file>
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile int running = 1;

static void sighandler(int sig) {
  (void)sig;
  running = 0;
}

static int listen_socket(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  if (listen(fd, 8) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int connect_host(const char *host, int port) {
  struct hostent *he = gethostbyname(host);
  if (!he)
    return -1;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void hexdump(FILE *fp, const char *label, const uint8_t *data,
                    size_t len) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  fprintf(fp, "\n=== %s %zu bytes @ %ld.%09ld ===\n", label, len,
          (long)ts.tv_sec, ts.tv_nsec);
  for (size_t i = 0; i < len; i += 16) {
    fprintf(fp, "%04zx  ", i);
    for (size_t j = 0; j < 16; j++) {
      if (i + j < len)
        fprintf(fp, "%02x ", data[i + j]);
      else
        fprintf(fp, "   ");
    }
    fprintf(fp, " |");
    for (size_t j = 0; j < 16 && i + j < len; j++) {
      uint8_t c = data[i + j];
      fprintf(fp, "%c", (c >= 32 && c < 127) ? c : '.');
    }
    fprintf(fp, "|\n");
  }
  fflush(fp);
}

static void relay(int client, int server, FILE *dump) {
  struct pollfd fds[2];
  fds[0].fd = client;
  fds[0].events = POLLIN;
  fds[1].fd = server;
  fds[1].events = POLLIN;

  uint8_t buf[65536];
  while (running) {
    int rc = poll(fds, 2, 100);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (rc == 0)
      continue;

    if (fds[0].revents & POLLIN) {
      ssize_t n = recv(client, buf, sizeof(buf), 0);
      if (n <= 0)
        break;
      hexdump(dump, "C->S", buf, (size_t)n);
      if (send(server, buf, (size_t)n, MSG_NOSIGNAL) != n)
        break;
    }
    if (fds[1].revents & POLLIN) {
      ssize_t n = recv(server, buf, sizeof(buf), 0);
      if (n <= 0)
        break;
      hexdump(dump, "S->C", buf, (size_t)n);
      if (send(client, buf, (size_t)n, MSG_NOSIGNAL) != n)
        break;
    }
    if ((fds[0].revents & (POLLERR | POLLHUP)) ||
        (fds[1].revents & (POLLERR | POLLHUP)))
      break;
  }
}

int main(int argc, char **argv) {
  if (argc != 5) {
    fprintf(stderr,
            "Usage: %s <local_port> <dest_host> <dest_port> <dump_file>\n",
            argv[0]);
    return 1;
  }
  int local_port = atoi(argv[1]);
  const char *dest_host = argv[2];
  int dest_port = atoi(argv[3]);
  const char *dump_file = argv[4];

  signal(SIGINT, sighandler);
  signal(SIGTERM, sighandler);

  FILE *dump = fopen(dump_file, "w");
  if (!dump) {
    perror("fopen");
    return 1;
  }

  int listener = listen_socket(local_port);
  if (listener < 0) {
    perror("listen_socket");
    return 1;
  }
  printf("Proxy listening on port %d -> %s:%d\n", local_port, dest_host,
         dest_port);
  printf("Dumping to %s\n", dump_file);

  while (running) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client = accept(listener, (struct sockaddr *)&client_addr, &addrlen);
    if (client < 0) {
      if (errno == EINTR)
        continue;
      perror("accept");
      break;
    }
    printf("Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    int server = connect_host(dest_host, dest_port);
    if (server < 0) {
      perror("connect_host");
      close(client);
      continue;
    }
    printf("Connected to %s:%d\n", dest_host, dest_port);

    relay(client, server, dump);

    close(client);
    close(server);
    printf("Connection closed\n");
  }

  fclose(dump);
  close(listener);
  return 0;
}

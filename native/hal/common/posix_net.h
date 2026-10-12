#ifndef KAIRO_POSIX_NET_H
#define KAIRO_POSIX_NET_H

#include "kairo/hal.h"

int posix_net_socket(int type);
int posix_net_connect(int fd, const char *host, uint16_t port);
int posix_net_connect_status(int fd);
int posix_net_listen(int fd, uint16_t port, int backlog);
int posix_net_accept(int fd, char *addr, size_t addr_len, uint16_t *port);
int posix_net_send(int fd, const void *buf, size_t len);
int posix_net_recv(int fd, void *buf, size_t len);
int posix_net_sendto(int fd, const char *host, uint16_t port, const void *buf, size_t len);
int posix_net_recvfrom(int fd, void *buf, size_t len, char *addr, size_t addr_len, uint16_t *port);
int posix_net_bind(int fd, uint16_t port);
int posix_net_close(int fd);
int posix_net_poll(hal_pollfd_t *fds, size_t n);
int posix_net_resolve(const char *host, char *addr, size_t addr_len);

#endif

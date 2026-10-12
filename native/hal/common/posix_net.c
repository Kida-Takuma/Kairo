#include "posix_net.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#else
#include <poll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

static int map_errno(void)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) return HAL_EAGAIN;
    if (errno == EBADF || errno == ENOTCONN || errno == ECONNRESET || errno == EPIPE) return HAL_ECLOSED;
    return HAL_ERR;
}

static int resolve4(const char *host, uint16_t port, struct sockaddr_in *out)
{
    memset(out, 0, sizeof *out);
    out->sin_family = AF_INET;
    out->sin_port = htons(port);
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) return 0;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return HAL_ERR;
    out->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 0;
}

static void addr_to_str(const struct sockaddr_in *sa, char *addr, size_t addr_len, uint16_t *port)
{
    if (addr && addr_len) inet_ntop(AF_INET, &sa->sin_addr, addr, (socklen_t)addr_len);
    if (port) *port = ntohs(sa->sin_port);
}

int posix_net_socket(int type)
{
    int fd = socket(AF_INET, type == HAL_NET_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0) return HAL_ERR;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (type != HAL_NET_UDP) setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

int posix_net_connect(int fd, const char *host, uint16_t port)
{
    struct sockaddr_in sa;
    if (resolve4(host, port, &sa) != 0) return HAL_ERR;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) return 0;
    return map_errno();
}

int posix_net_connect_status(int fd)
{
    struct pollfd p = { .fd = fd, .events = POLLOUT };
    int r = poll(&p, 1, 0);
    if (r == 0) return HAL_EAGAIN;
    if (r < 0) return HAL_ERR;
    int err = 0;
    socklen_t len = sizeof err;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return HAL_ERR;
    if (err == 0) return 0;
    if (err == EINPROGRESS || err == EALREADY) return HAL_EAGAIN;
    return HAL_ERR;
}

int posix_net_bind(int fd, uint16_t port)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    return bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0 ? 0 : HAL_ERR;
}

int posix_net_listen(int fd, uint16_t port, int backlog)
{
    if (posix_net_bind(fd, port) != 0) return HAL_ERR;
    return listen(fd, backlog > 0 ? backlog : 4) == 0 ? 0 : HAL_ERR;
}

int posix_net_accept(int fd, char *addr, size_t addr_len, uint16_t *port)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    int nfd = accept(fd, (struct sockaddr *)&sa, &len);
    if (nfd < 0) return map_errno();
    fcntl(nfd, F_SETFL, fcntl(nfd, F_GETFL, 0) | O_NONBLOCK);
    int one = 1;
    setsockopt(nfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    addr_to_str(&sa, addr, addr_len, port);
    return nfd;
}

int posix_net_send(int fd, const void *buf, size_t len)
{
    ssize_t n = send(fd, buf, len, 0);
    if (n < 0) return map_errno();
    return (int)n;
}

int posix_net_recv(int fd, void *buf, size_t len)
{
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0) return map_errno();
    return (int)n;
}

int posix_net_sendto(int fd, const char *host, uint16_t port, const void *buf, size_t len)
{
    struct sockaddr_in sa;
    if (resolve4(host, port, &sa) != 0) return HAL_ERR;
    ssize_t n = sendto(fd, buf, len, 0, (struct sockaddr *)&sa, sizeof sa);
    if (n < 0) return map_errno();
    return (int)n;
}

int posix_net_recvfrom(int fd, void *buf, size_t len, char *addr, size_t addr_len, uint16_t *port)
{
    struct sockaddr_in sa;
    socklen_t slen = sizeof sa;
    ssize_t n = recvfrom(fd, buf, len, 0, (struct sockaddr *)&sa, &slen);
    if (n < 0) return map_errno();
    addr_to_str(&sa, addr, addr_len, port);
    return (int)n;
}

int posix_net_close(int fd)
{
    return close(fd) == 0 ? 0 : HAL_ERR;
}

int posix_net_poll(hal_pollfd_t *fds, size_t n)
{
    struct pollfd p[32];
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; i++) {
        p[i].fd = fds[i].fd;
        p[i].events = (short)(((fds[i].events & HAL_POLL_IN) ? POLLIN : 0) | ((fds[i].events & HAL_POLL_OUT) ? POLLOUT : 0));
        p[i].revents = 0;
    }
    int r = poll(p, (nfds_t)n, 0);
    if (r <= 0) return r < 0 ? HAL_ERR : 0;
    for (size_t i = 0; i < n; i++) {
        int re = 0;
        if (p[i].revents & POLLIN) re |= HAL_POLL_IN;
        if (p[i].revents & POLLOUT) re |= HAL_POLL_OUT;
        if (p[i].revents & (POLLERR | POLLNVAL)) re |= HAL_POLL_ERR;
        if (p[i].revents & POLLHUP) re |= HAL_POLL_HUP | HAL_POLL_IN;
        fds[i].revents = re;
    }
    return r;
}

int posix_net_resolve(const char *host, char *addr, size_t addr_len)
{
    struct sockaddr_in sa;
    if (resolve4(host, 0, &sa) != 0) return HAL_ERR;
    inet_ntop(AF_INET, &sa.sin_addr, addr, (socklen_t)addr_len);
    return 0;
}

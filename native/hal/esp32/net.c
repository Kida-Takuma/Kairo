#include "kairo/hal.h"
#include "../common/posix_net.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_netif.h"
#include "esp_tls.h"

#define TLS_MAX 4

typedef struct {
    int handle;
    esp_tls_t *tls;
    bool connected;
} tls_conn_t;

static tls_conn_t s_tls[TLS_MAX];

esp_netif_t *kairo_wifi_netif(void);

static tls_conn_t *tls_find(int fd)
{
    for (int i = 0; i < TLS_MAX; i++)
        if (s_tls[i].tls && s_tls[i].handle == fd) return &s_tls[i];
    return NULL;
}

static int tls_map(ssize_t n)
{
    if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) return HAL_EAGAIN;
    if (n < 0) return HAL_ECLOSED;
    return (int)n;
}

int hal_net_socket(int type) { return posix_net_socket(type); }
int hal_net_connect(int fd, const char *host, uint16_t port) { return posix_net_connect(fd, host, port); }
int hal_net_listen(int fd, uint16_t port, int backlog) { return posix_net_listen(fd, port, backlog); }
int hal_net_accept(int fd, char *addr, size_t addr_len, uint16_t *port) { return posix_net_accept(fd, addr, addr_len, port); }
int hal_net_sendto(int fd, const char *host, uint16_t port, const void *buf, size_t len) { return posix_net_sendto(fd, host, port, buf, len); }
int hal_net_recvfrom(int fd, void *buf, size_t len, char *addr, size_t addr_len, uint16_t *port) { return posix_net_recvfrom(fd, buf, len, addr, addr_len, port); }
int hal_net_bind(int fd, uint16_t port) { return posix_net_bind(fd, port); }
int hal_net_resolve(const char *host, char *addr, size_t addr_len) { return posix_net_resolve(host, addr, addr_len); }

int hal_net_tls_connect(int fd, const char *host, uint16_t port)
{
    tls_conn_t *c = NULL;
    for (int i = 0; i < TLS_MAX; i++) {
        if (!s_tls[i].tls) {
            c = &s_tls[i];
            break;
        }
    }
    if (!c) return HAL_ERR;
    c->tls = esp_tls_init();
    if (!c->tls) return HAL_ERR;
    c->handle = fd;
    c->connected = false;
    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .non_block = true,
        .timeout_ms = 15000,
    };
    int r = esp_tls_conn_new_async(host, (int)strlen(host), port, &cfg, c->tls);
    if (r < 0) {
        esp_tls_conn_destroy(c->tls);
        c->tls = NULL;
        return HAL_ERR;
    }
    if (r == 1) c->connected = true;
    return c->connected ? 0 : HAL_EAGAIN;
}

int hal_net_connect_status(int fd)
{
    tls_conn_t *c = tls_find(fd);
    if (!c) return posix_net_connect_status(fd);
    if (c->connected) return 0;
    int r = esp_tls_conn_new_async(NULL, 0, 0, NULL, c->tls);
    if (r < 0) return HAL_ERR;
    if (r == 0) return HAL_EAGAIN;
    c->connected = true;
    return 0;
}

int hal_net_send(int fd, const void *buf, size_t len)
{
    tls_conn_t *c = tls_find(fd);
    if (!c) return posix_net_send(fd, buf, len);
    return tls_map(esp_tls_conn_write(c->tls, buf, len));
}

int hal_net_recv(int fd, void *buf, size_t len)
{
    tls_conn_t *c = tls_find(fd);
    if (!c) return posix_net_recv(fd, buf, len);
    ssize_t n = esp_tls_conn_read(c->tls, buf, len);
    if (n == 0) return 0;
    return tls_map(n);
}

int hal_net_close(int fd)
{
    tls_conn_t *c = tls_find(fd);
    if (c) {
        esp_tls_conn_destroy(c->tls);
        c->tls = NULL;
    }
    return posix_net_close(fd);
}

int hal_net_poll(hal_pollfd_t *fds, size_t n)
{
    hal_pollfd_t mapped[32];
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; i++) {
        mapped[i] = fds[i];
        tls_conn_t *c = tls_find(fds[i].fd);
        if (c) {
            int sock = -1;
            esp_tls_get_conn_sockfd(c->tls, &sock);
            mapped[i].fd = sock;
        }
    }
    int r = posix_net_poll(mapped, n);
    for (size_t i = 0; i < n; i++) fds[i].revents = mapped[i].revents;
    return r;
}

int hal_net_local_ip(char *addr, size_t addr_len)
{
    esp_netif_t *netif = kairo_wifi_netif();
    if (!netif) return HAL_ERR;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) return HAL_ERR;
    snprintf(addr, addr_len, IPSTR, IP2STR(&info.ip));
    return 0;
}

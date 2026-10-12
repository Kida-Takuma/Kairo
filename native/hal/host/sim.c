#include "kairo/hal.h"
#include "../common/posix_net.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int64_t s_rtc_offset_ms;
static bool s_rtc_set;

int hal_random_bytes(uint8_t *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return HAL_ERR;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n == len ? 0 : HAL_ERR;
}

static int64_t wall_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int64_t hal_rtc_get_ms(void)
{
    return wall_ms() + (s_rtc_set ? s_rtc_offset_ms : 0);
}

int hal_rtc_set_ms(int64_t epoch_ms)
{
    s_rtc_offset_ms = epoch_ms - wall_ms();
    s_rtc_set = true;
    return 0;
}

int hal_net_socket(int type) { return posix_net_socket(type); }
int hal_net_connect(int fd, const char *host, uint16_t port) { return posix_net_connect(fd, host, port); }
int hal_net_tls_connect(int fd, const char *host, uint16_t port) { (void)fd; (void)host; (void)port; return HAL_ENOSYS; }
int hal_net_connect_status(int fd) { return posix_net_connect_status(fd); }
int hal_net_listen(int fd, uint16_t port, int backlog) { return posix_net_listen(fd, port, backlog); }
int hal_net_accept(int fd, char *addr, size_t addr_len, uint16_t *port) { return posix_net_accept(fd, addr, addr_len, port); }
int hal_net_send(int fd, const void *buf, size_t len) { return posix_net_send(fd, buf, len); }
int hal_net_recv(int fd, void *buf, size_t len) { return posix_net_recv(fd, buf, len); }
int hal_net_sendto(int fd, const char *host, uint16_t port, const void *buf, size_t len) { return posix_net_sendto(fd, host, port, buf, len); }
int hal_net_recvfrom(int fd, void *buf, size_t len, char *addr, size_t addr_len, uint16_t *port) { return posix_net_recvfrom(fd, buf, len, addr, addr_len, port); }
int hal_net_bind(int fd, uint16_t port) { return posix_net_bind(fd, port); }
int hal_net_close(int fd) { return posix_net_close(fd); }
int hal_net_poll(hal_pollfd_t *fds, size_t n) { return posix_net_poll(fds, n); }
int hal_net_resolve(const char *host, char *addr, size_t addr_len) { return posix_net_resolve(host, addr, addr_len); }

int hal_net_local_ip(char *addr, size_t addr_len)
{
    snprintf(addr, addr_len, "127.0.0.1");
    return 0;
}

static int s_wifi_status = HAL_WIFI_IDLE;
static uint64_t s_wifi_t0;
static uint64_t s_wifi_scan_t0;
static char s_wifi_ssid[33];

int hal_wifi_connect(const char *ssid, const char *password)
{
    (void)password;
    snprintf(s_wifi_ssid, sizeof s_wifi_ssid, "%s", ssid);
    s_wifi_status = HAL_WIFI_CONNECTING;
    s_wifi_t0 = hal_time_us();
    return 0;
}

int hal_wifi_status(void)
{
    if (s_wifi_status == HAL_WIFI_CONNECTING && hal_time_us() - s_wifi_t0 > 300000) {
        s_wifi_status = strcmp(s_wifi_ssid, "wrong") == 0 ? HAL_WIFI_FAILED : HAL_WIFI_CONNECTED;
    }
    return s_wifi_status;
}

int hal_wifi_disconnect(void)
{
    s_wifi_status = HAL_WIFI_IDLE;
    return 0;
}

int hal_wifi_scan_start(void)
{
    s_wifi_scan_t0 = hal_time_us();
    return 0;
}

int hal_wifi_scan_results(hal_wifi_ap_t *out, size_t max)
{
    if (!s_wifi_scan_t0) return HAL_ERR;
    if (hal_time_us() - s_wifi_scan_t0 < 400000) return HAL_EAGAIN;
    s_wifi_scan_t0 = 0;
    static const hal_wifi_ap_t fake[] = {
        { "kairo-lab", -48, 6, true },
        { "guest", -71, 11, false },
        { "neighbor-2.4G", -83, 1, true },
    };
    size_t n = sizeof fake / sizeof fake[0];
    if (n > max) n = max;
    memcpy(out, fake, n * sizeof fake[0]);
    return (int)n;
}

int hal_wifi_rssi(void)
{
    return s_wifi_status == HAL_WIFI_CONNECTED ? -52 : 0;
}

static bool s_ble_scanning;
static uint64_t s_ble_scan_t0;
static uint32_t s_ble_scan_ms;
static struct {
    bool used;
    int status;
    int pair;
    uint64_t t0;
} s_ble_conn[4];

int hal_ble_init(void)
{
    return 0;
}

int hal_ble_scan_start(uint32_t duration_ms)
{
    s_ble_scanning = true;
    s_ble_scan_t0 = hal_time_us();
    s_ble_scan_ms = duration_ms;
    return 0;
}

int hal_ble_scan_stop(void)
{
    s_ble_scanning = false;
    return 0;
}

int hal_ble_scan_results(hal_ble_device_t *out, size_t max, bool *done)
{
    static const hal_ble_device_t fake[] = {
        { "AA:BB:CC:DD:EE:01", "Kairo Sensor", -55, true },
        { "AA:BB:CC:DD:EE:02", "Heart Rate", -67, true },
        { "AA:BB:CC:DD:EE:03", "", -88, false },
    };
    uint64_t elapsed = (hal_time_us() - s_ble_scan_t0) / 1000;
    size_t n = elapsed < 100 ? 0 : elapsed < 300 ? 1 : elapsed < 600 ? 2 : 3;
    if (n > max) n = max;
    memcpy(out, fake, n * sizeof fake[0]);
    if (s_ble_scanning && elapsed >= s_ble_scan_ms) s_ble_scanning = false;
    *done = !s_ble_scanning;
    return (int)n;
}

int hal_ble_connect(const char *addr)
{
    if (strncmp(addr, "AA:BB:CC:DD:EE:", 15) != 0) return HAL_ERR;
    for (int i = 0; i < 4; i++) {
        if (s_ble_conn[i].used) continue;
        s_ble_conn[i].used = true;
        s_ble_conn[i].status = HAL_BLE_CONNECTING;
        s_ble_conn[i].pair = -1;
        s_ble_conn[i].t0 = hal_time_us();
        return i;
    }
    return HAL_ERR;
}

int hal_ble_conn_status(int conn)
{
    if (conn < 0 || conn >= 4 || !s_ble_conn[conn].used) return HAL_ERR;
    if (s_ble_conn[conn].status == HAL_BLE_CONNECTING && hal_time_us() - s_ble_conn[conn].t0 > 200000) s_ble_conn[conn].status = HAL_BLE_CONNECTED;
    return s_ble_conn[conn].status;
}

int hal_ble_pair(int conn)
{
    if (conn < 0 || conn >= 4 || !s_ble_conn[conn].used) return HAL_ERR;
    s_ble_conn[conn].pair = HAL_BLE_PAIRING;
    s_ble_conn[conn].t0 = hal_time_us();
    return 0;
}

int hal_ble_pair_status(int conn)
{
    if (conn < 0 || conn >= 4 || !s_ble_conn[conn].used || s_ble_conn[conn].pair < 0) return HAL_ERR;
    if (s_ble_conn[conn].pair == HAL_BLE_PAIRING && hal_time_us() - s_ble_conn[conn].t0 > 300000) s_ble_conn[conn].pair = HAL_BLE_PAIRED;
    return s_ble_conn[conn].pair;
}

int hal_ble_disconnect(int conn)
{
    if (conn < 0 || conn >= 4 || !s_ble_conn[conn].used) return HAL_ERR;
    s_ble_conn[conn].used = false;
    return 0;
}

static struct {
    FILE *f;
    uint64_t last_us;
} s_uart[4];

int hal_uart_open(int id, int tx_pin, int rx_pin, uint32_t baud)
{
    (void)tx_pin; (void)rx_pin; (void)baud;
    if (id < 0 || id >= 4) return HAL_ERR;
    char env[32];
    snprintf(env, sizeof env, "KAIRO_SIM_UART%d", id);
    const char *path = getenv(env);
    if (s_uart[id].f) fclose(s_uart[id].f);
    s_uart[id].f = path ? fopen(path, "rb") : NULL;
    s_uart[id].last_us = 0;
    return 0;
}

int hal_uart_write(int id, const void *buf, size_t len)
{
    if (id < 0 || id >= 4) return HAL_ERR;
    (void)buf;
    return (int)len;
}

int hal_uart_read(int id, void *buf, size_t len)
{
    if (id < 0 || id >= 4) return HAL_ERR;
    if (!s_uart[id].f) return HAL_EAGAIN;
    uint64_t now = hal_time_us();
    if (now - s_uart[id].last_us < 100000) return HAL_EAGAIN;
    s_uart[id].last_us = now;
    char line[256];
    if (!fgets(line, sizeof line, s_uart[id].f)) return HAL_EAGAIN;
    size_t n = strlen(line);
    if (n > len) n = len;
    memcpy(buf, line, n);
    return (int)n;
}

int hal_uart_close(int id)
{
    if (id < 0 || id >= 4) return HAL_ERR;
    if (s_uart[id].f) fclose(s_uart[id].f);
    s_uart[id].f = NULL;
    return 0;
}

int hal_adc_read_mv(int channel)
{
    if (channel < 0 || channel > 9) return HAL_ERR;
    char env[32];
    snprintf(env, sizeof env, "KAIRO_SIM_ADC%d", channel);
    const char *v = getenv(env);
    return v ? atoi(v) : 2100;
}

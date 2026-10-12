#include "kairo/hal.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/adc.h"
#include "hardware/uart.h"

static int64_t s_rtc_offset_ms;
static bool s_rtc_set;

int hal_random_bytes(uint8_t *buf, size_t len)
{
    while (len) {
        uint32_t r = get_rand_32();
        size_t n = len < 4 ? len : 4;
        memcpy(buf, &r, n);
        buf += n;
        len -= n;
    }
    return 0;
}

int64_t hal_rtc_get_ms(void)
{
    int64_t up = (int64_t)(time_us_64() / 1000);
    return s_rtc_set ? up + s_rtc_offset_ms : 0;
}

int hal_rtc_set_ms(int64_t epoch_ms)
{
    s_rtc_offset_ms = epoch_ms - (int64_t)(time_us_64() / 1000);
    s_rtc_set = true;
    return 0;
}

static uart_inst_t *uart_of(int id)
{
    return id == 0 ? uart0 : id == 1 ? uart1 : NULL;
}

int hal_uart_open(int id, int tx_pin, int rx_pin, uint32_t baud)
{
    uart_inst_t *u = uart_of(id);
    if (!u) return HAL_ERR;
    uart_init(u, baud);
    if (tx_pin >= 0) gpio_set_function((uint)tx_pin, GPIO_FUNC_UART);
    if (rx_pin >= 0) gpio_set_function((uint)rx_pin, GPIO_FUNC_UART);
    uart_set_fifo_enabled(u, true);
    return 0;
}

int hal_uart_write(int id, const void *buf, size_t len)
{
    uart_inst_t *u = uart_of(id);
    if (!u) return HAL_ERR;
    uart_write_blocking(u, buf, len);
    return (int)len;
}

int hal_uart_read(int id, void *buf, size_t len)
{
    uart_inst_t *u = uart_of(id);
    if (!u) return HAL_ERR;
    uint8_t *p = buf;
    size_t n = 0;
    while (n < len && uart_is_readable(u)) p[n++] = (uint8_t)uart_getc(u);
    return n ? (int)n : HAL_EAGAIN;
}

int hal_uart_close(int id)
{
    uart_inst_t *u = uart_of(id);
    if (!u) return HAL_ERR;
    uart_deinit(u);
    return 0;
}

static bool s_adc_init;

int hal_adc_read_mv(int channel)
{
    if (channel < 0 || channel > 3) return HAL_ERR;
    if (!s_adc_init) {
        adc_init();
        s_adc_init = true;
    }
    adc_gpio_init((uint)(26 + channel));
    adc_select_input((uint)channel);
    uint16_t raw = adc_read();
    return (int)((uint32_t)raw * 3300u / 4095u);
}

int hal_net_socket(int type) { (void)type; return HAL_ENOSYS; }
int hal_net_connect(int fd, const char *host, uint16_t port) { (void)fd; (void)host; (void)port; return HAL_ENOSYS; }
int hal_net_tls_connect(int fd, const char *host, uint16_t port) { (void)fd; (void)host; (void)port; return HAL_ENOSYS; }
int hal_net_connect_status(int fd) { (void)fd; return HAL_ENOSYS; }
int hal_net_listen(int fd, uint16_t port, int backlog) { (void)fd; (void)port; (void)backlog; return HAL_ENOSYS; }
int hal_net_accept(int fd, char *addr, size_t addr_len, uint16_t *port) { (void)fd; (void)addr; (void)addr_len; (void)port; return HAL_ENOSYS; }
int hal_net_send(int fd, const void *buf, size_t len) { (void)fd; (void)buf; (void)len; return HAL_ENOSYS; }
int hal_net_recv(int fd, void *buf, size_t len) { (void)fd; (void)buf; (void)len; return HAL_ENOSYS; }
int hal_net_sendto(int fd, const char *host, uint16_t port, const void *buf, size_t len) { (void)fd; (void)host; (void)port; (void)buf; (void)len; return HAL_ENOSYS; }
int hal_net_recvfrom(int fd, void *buf, size_t len, char *addr, size_t addr_len, uint16_t *port) { (void)fd; (void)buf; (void)len; (void)addr; (void)addr_len; (void)port; return HAL_ENOSYS; }
int hal_net_bind(int fd, uint16_t port) { (void)fd; (void)port; return HAL_ENOSYS; }
int hal_net_close(int fd) { (void)fd; return HAL_ENOSYS; }
int hal_net_poll(hal_pollfd_t *fds, size_t n) { (void)fds; (void)n; return 0; }
int hal_net_resolve(const char *host, char *addr, size_t addr_len) { (void)host; (void)addr; (void)addr_len; return HAL_ENOSYS; }
int hal_net_local_ip(char *addr, size_t addr_len) { (void)addr; (void)addr_len; return HAL_ENOSYS; }
int hal_wifi_connect(const char *ssid, const char *password) { (void)ssid; (void)password; return HAL_ENOSYS; }
int hal_wifi_status(void) { return HAL_ENOSYS; }
int hal_wifi_disconnect(void) { return HAL_ENOSYS; }
int hal_wifi_scan_start(void) { return HAL_ENOSYS; }
int hal_wifi_scan_results(hal_wifi_ap_t *out, size_t max) { (void)out; (void)max; return HAL_ENOSYS; }
int hal_wifi_rssi(void) { return 0; }
int hal_ble_init(void) { return HAL_ENOSYS; }
int hal_ble_scan_start(uint32_t duration_ms) { (void)duration_ms; return HAL_ENOSYS; }
int hal_ble_scan_stop(void) { return HAL_ENOSYS; }
int hal_ble_scan_results(hal_ble_device_t *out, size_t max, bool *done) { (void)out; (void)max; *done = true; return HAL_ENOSYS; }
int hal_ble_connect(const char *addr) { (void)addr; return HAL_ENOSYS; }
int hal_ble_conn_status(int conn) { (void)conn; return HAL_ENOSYS; }
int hal_ble_pair(int conn) { (void)conn; return HAL_ENOSYS; }
int hal_ble_pair_status(int conn) { (void)conn; return HAL_ENOSYS; }
int hal_ble_disconnect(int conn) { (void)conn; return HAL_ENOSYS; }

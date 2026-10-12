#ifndef KAIRO_HAL_H
#define KAIRO_HAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_OK 0
#define HAL_ERR -1
#define HAL_EAGAIN -2
#define HAL_ENOSYS -3
#define HAL_ECLOSED -4

typedef enum {
    HAL_GPIO_INPUT = 0,
    HAL_GPIO_OUTPUT = 1,
    HAL_GPIO_INPUT_PULLUP = 2,
    HAL_GPIO_INPUT_PULLDOWN = 3,
} hal_gpio_mode_t;

int hal_init(void);
const char *hal_board_name(void);

uint64_t hal_time_us(void);
void hal_idle_until_us(uint64_t deadline_us);

int hal_gpio_set_mode(uint32_t pin, hal_gpio_mode_t mode);
int hal_gpio_write(uint32_t pin, bool level);
int hal_gpio_read(uint32_t pin);

void hal_console_write(const char *buf, size_t len);
int hal_console_read(uint8_t *buf, size_t max);

int hal_app_capacity(size_t *bytes);
int hal_app_erase(size_t bytes);
int hal_app_write(size_t offset, const void *data, size_t len);
int hal_app_finish(void);
const void *hal_app_map(size_t *bytes);

size_t hal_js_heap_bytes(void);
size_t hal_js_stack_bytes(void);

void hal_reboot(void);
void hal_panic(const char *msg) __attribute__((noreturn));

int hal_random_bytes(uint8_t *buf, size_t len);

int64_t hal_rtc_get_ms(void);
int hal_rtc_set_ms(int64_t epoch_ms);

#define HAL_NET_TCP 0
#define HAL_NET_UDP 1
#define HAL_POLL_IN 1
#define HAL_POLL_OUT 2
#define HAL_POLL_ERR 4
#define HAL_POLL_HUP 8

typedef struct {
    int fd;
    int events;
    int revents;
} hal_pollfd_t;

int hal_net_socket(int type);
int hal_net_connect(int fd, const char *host, uint16_t port);
int hal_net_tls_connect(int fd, const char *host, uint16_t port);
int hal_net_connect_status(int fd);
int hal_net_listen(int fd, uint16_t port, int backlog);
int hal_net_accept(int fd, char *addr, size_t addr_len, uint16_t *port);
int hal_net_send(int fd, const void *buf, size_t len);
int hal_net_recv(int fd, void *buf, size_t len);
int hal_net_sendto(int fd, const char *host, uint16_t port, const void *buf, size_t len);
int hal_net_recvfrom(int fd, void *buf, size_t len, char *addr, size_t addr_len, uint16_t *port);
int hal_net_bind(int fd, uint16_t port);
int hal_net_close(int fd);
int hal_net_poll(hal_pollfd_t *fds, size_t n);
int hal_net_resolve(const char *host, char *addr, size_t addr_len);
int hal_net_local_ip(char *addr, size_t addr_len);

#define HAL_WIFI_IDLE 0
#define HAL_WIFI_CONNECTING 1
#define HAL_WIFI_CONNECTED 2
#define HAL_WIFI_FAILED 3

typedef struct {
    char ssid[33];
    int rssi;
    int channel;
    bool secure;
} hal_wifi_ap_t;

int hal_wifi_connect(const char *ssid, const char *password);
int hal_wifi_status(void);
int hal_wifi_disconnect(void);
int hal_wifi_scan_start(void);
int hal_wifi_scan_results(hal_wifi_ap_t *out, size_t max);
int hal_wifi_rssi(void);

#define HAL_BLE_CONNECTING 0
#define HAL_BLE_CONNECTED 1
#define HAL_BLE_DISCONNECTED 2
#define HAL_BLE_PAIRING 0
#define HAL_BLE_PAIRED 1
#define HAL_BLE_PAIR_FAILED 2

typedef struct {
    char addr[18];
    char name[32];
    int rssi;
    bool connectable;
} hal_ble_device_t;

int hal_ble_init(void);
int hal_ble_scan_start(uint32_t duration_ms);
int hal_ble_scan_stop(void);
int hal_ble_scan_results(hal_ble_device_t *out, size_t max, bool *done);
int hal_ble_connect(const char *addr);
int hal_ble_conn_status(int conn);
int hal_ble_pair(int conn);
int hal_ble_pair_status(int conn);
int hal_ble_disconnect(int conn);

int hal_uart_open(int id, int tx_pin, int rx_pin, uint32_t baud);
int hal_uart_write(int id, const void *buf, size_t len);
int hal_uart_read(int id, void *buf, size_t len);
int hal_uart_close(int id);

int hal_adc_read_mv(int channel);

int hal_pwm_set(uint32_t pin, uint32_t freq_hz, float duty);
int hal_pwm_stop(uint32_t pin);

#define HAL_EDGE_RISE 1
#define HAL_EDGE_FALL 2
#define HAL_EDGE_BOTH 3

typedef struct {
    uint32_t pin;
    uint8_t level;
    uint64_t time_us;
} hal_gpio_event_t;

int hal_gpio_watch(uint32_t pin, int edge);
int hal_gpio_unwatch(uint32_t pin);
int hal_gpio_poll_events(hal_gpio_event_t *out, size_t max);
int64_t hal_gpio_pulse_in(uint32_t pin, bool level, uint32_t timeout_us);

int hal_i2c_open(int bus, int sda, int scl, uint32_t freq_hz);
int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len);
int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len);
int hal_i2c_write_read(int bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rbuf, size_t rlen);
int hal_i2c_close(int bus);

int hal_spi_open(int bus, int sck, int mosi, int miso, uint32_t freq_hz, int mode);
int hal_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t len);
int hal_spi_close(int bus);

int hal_kv_get(const char *key, uint8_t *buf, size_t max);
int hal_kv_set(const char *key, const uint8_t *data, size_t len);
int hal_kv_delete(const char *key);
int hal_kv_keys(char *buf, size_t max);

int hal_deep_sleep(uint64_t us);

void hal_delay_us(uint32_t us);
uint32_t hal_critical_enter(void);
void hal_critical_exit(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif

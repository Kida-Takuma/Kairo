#include "kairo/hal.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rom/ets_sys.h"

#define PWM_CH_MAX 8
#define PWM_TIMER_MAX 4
#define PWM_RES LEDC_TIMER_12_BIT

static struct {
    bool used;
    uint32_t pin;
    int timer;
} s_pwm[PWM_CH_MAX];
static struct {
    bool used;
    uint32_t freq;
} s_pwm_timer[PWM_TIMER_MAX];

static int pwm_timer_for(uint32_t freq)
{
    for (int i = 0; i < PWM_TIMER_MAX; i++)
        if (s_pwm_timer[i].used && s_pwm_timer[i].freq == freq) return i;
    for (int i = 0; i < PWM_TIMER_MAX; i++) {
        if (s_pwm_timer[i].used) continue;
        ledc_timer_config_t tc = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = PWM_RES,
            .timer_num = (ledc_timer_t)i,
            .freq_hz = freq,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        if (ledc_timer_config(&tc) != ESP_OK) return -1;
        s_pwm_timer[i].used = true;
        s_pwm_timer[i].freq = freq;
        return i;
    }
    return -1;
}

int hal_pwm_set(uint32_t pin, uint32_t freq_hz, float duty)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || freq_hz == 0) return HAL_ERR;
    int ch = -1;
    for (int i = 0; i < PWM_CH_MAX; i++) {
        if (s_pwm[i].used && s_pwm[i].pin == pin) {
            ch = i;
            break;
        }
    }
    int timer = pwm_timer_for(freq_hz);
    if (timer < 0) return HAL_ERR;
    if (ch < 0) {
        for (int i = 0; i < PWM_CH_MAX; i++) {
            if (!s_pwm[i].used) {
                ch = i;
                break;
            }
        }
        if (ch < 0) return HAL_ERR;
        s_pwm[ch].used = true;
        s_pwm[ch].pin = pin;
        s_pwm[ch].timer = -1;
    }
    uint32_t max = (1u << 12) - 1;
    uint32_t d = (uint32_t)(duty * (float)max + 0.5f);
    if (s_pwm[ch].timer != timer) {
        ledc_channel_config_t cc = {
            .gpio_num = (int)pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)ch,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = (ledc_timer_t)timer,
            .duty = d,
            .hpoint = 0,
        };
        if (ledc_channel_config(&cc) != ESP_OK) return HAL_ERR;
        s_pwm[ch].timer = timer;
        return 0;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, d);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    return 0;
}

int hal_pwm_stop(uint32_t pin)
{
    for (int i = 0; i < PWM_CH_MAX; i++) {
        if (!s_pwm[i].used || s_pwm[i].pin != pin) continue;
        ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, 0);
        s_pwm[i].used = false;
        gpio_reset_pin((gpio_num_t)pin);
        return 0;
    }
    return HAL_ERR;
}

#define GPIO_EVQ 64

static hal_gpio_event_t s_evq[GPIO_EVQ];
static volatile uint32_t s_evq_head, s_evq_tail;
static bool s_isr_installed;

static void IRAM_ATTR gpio_isr(void *arg)
{
    uint32_t pin = (uint32_t)(uintptr_t)arg;
    uint32_t next = (s_evq_tail + 1) % GPIO_EVQ;
    if (next == s_evq_head) return;
    s_evq[s_evq_tail].pin = pin;
    s_evq[s_evq_tail].level = (uint8_t)gpio_get_level((gpio_num_t)pin);
    s_evq[s_evq_tail].time_us = (uint64_t)esp_timer_get_time();
    s_evq_tail = next;
}

int hal_gpio_watch(uint32_t pin, int edge)
{
    if (!GPIO_IS_VALID_GPIO(pin)) return HAL_ERR;
    if (!s_isr_installed) {
        if (gpio_install_isr_service(0) != ESP_OK) return HAL_ERR;
        s_isr_installed = true;
    }
    gpio_int_type_t type = edge == HAL_EDGE_RISE ? GPIO_INTR_POSEDGE : edge == HAL_EDGE_FALL ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE;
    gpio_set_intr_type((gpio_num_t)pin, type);
    gpio_isr_handler_remove((gpio_num_t)pin);
    if (gpio_isr_handler_add((gpio_num_t)pin, gpio_isr, (void *)(uintptr_t)pin) != ESP_OK) return HAL_ERR;
    return gpio_intr_enable((gpio_num_t)pin) == ESP_OK ? 0 : HAL_ERR;
}

int hal_gpio_unwatch(uint32_t pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) return HAL_ERR;
    gpio_intr_disable((gpio_num_t)pin);
    gpio_isr_handler_remove((gpio_num_t)pin);
    return 0;
}

int hal_gpio_poll_events(hal_gpio_event_t *out, size_t max)
{
    size_t n = 0;
    while (n < max && s_evq_head != s_evq_tail) {
        out[n++] = s_evq[s_evq_head];
        s_evq_head = (s_evq_head + 1) % GPIO_EVQ;
    }
    return (int)n;
}

int64_t hal_gpio_pulse_in(uint32_t pin, bool level, uint32_t timeout_us)
{
    if (!GPIO_IS_VALID_GPIO(pin)) return HAL_ERR;
    gpio_num_t g = (gpio_num_t)pin;
    int64_t deadline = esp_timer_get_time() + timeout_us;
    while (gpio_get_level(g) == (int)level)
        if (esp_timer_get_time() > deadline) return HAL_ERR;
    while (gpio_get_level(g) != (int)level)
        if (esp_timer_get_time() > deadline) return HAL_ERR;
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(g) == (int)level)
        if (esp_timer_get_time() > deadline) return HAL_ERR;
    return esp_timer_get_time() - start;
}

#define I2C_BUS_MAX 2
#define I2C_DEV_MAX 8

static struct {
    i2c_master_bus_handle_t bus;
    uint32_t freq;
    struct {
        bool used;
        uint8_t addr;
        i2c_master_dev_handle_t dev;
    } devs[I2C_DEV_MAX];
} s_i2c[I2C_BUS_MAX];

int hal_i2c_open(int bus, int sda, int scl, uint32_t freq_hz)
{
    if (bus < 0 || bus >= I2C_BUS_MAX) return HAL_ERR;
    if (s_i2c[bus].bus) hal_i2c_close(bus);
    i2c_master_bus_config_t cfg = {
        .i2c_port = bus,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg, &s_i2c[bus].bus) != ESP_OK) return HAL_ERR;
    s_i2c[bus].freq = freq_hz ? freq_hz : 100000;
    return 0;
}

static i2c_master_dev_handle_t i2c_dev(int bus, uint8_t addr)
{
    for (int i = 0; i < I2C_DEV_MAX; i++)
        if (s_i2c[bus].devs[i].used && s_i2c[bus].devs[i].addr == addr) return s_i2c[bus].devs[i].dev;
    int slot = -1;
    for (int i = 0; i < I2C_DEV_MAX; i++) {
        if (!s_i2c[bus].devs[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        i2c_master_bus_rm_device(s_i2c[bus].devs[0].dev);
        s_i2c[bus].devs[0].used = false;
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = s_i2c[bus].freq,
    };
    if (i2c_master_bus_add_device(s_i2c[bus].bus, &dc, &s_i2c[bus].devs[slot].dev) != ESP_OK) return NULL;
    s_i2c[bus].devs[slot].used = true;
    s_i2c[bus].devs[slot].addr = addr;
    return s_i2c[bus].devs[slot].dev;
}

static int i2c_map(esp_err_t e)
{
    if (e == ESP_OK) return 0;
    if (e == ESP_ERR_NOT_FOUND || e == ESP_ERR_INVALID_RESPONSE) return HAL_ECLOSED;
    return HAL_ERR;
}

int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len)
{
    if (bus < 0 || bus >= I2C_BUS_MAX || !s_i2c[bus].bus) return HAL_ERR;
    if (len == 0) return i2c_map(i2c_master_probe(s_i2c[bus].bus, addr, 50));
    i2c_master_dev_handle_t d = i2c_dev(bus, addr);
    if (!d) return HAL_ERR;
    return i2c_map(i2c_master_transmit(d, data, len, 100));
}

int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len)
{
    if (bus < 0 || bus >= I2C_BUS_MAX || !s_i2c[bus].bus) return HAL_ERR;
    i2c_master_dev_handle_t d = i2c_dev(bus, addr);
    if (!d) return HAL_ERR;
    return i2c_map(i2c_master_receive(d, buf, len, 100));
}

int hal_i2c_write_read(int bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    if (bus < 0 || bus >= I2C_BUS_MAX || !s_i2c[bus].bus) return HAL_ERR;
    i2c_master_dev_handle_t d = i2c_dev(bus, addr);
    if (!d) return HAL_ERR;
    return i2c_map(i2c_master_transmit_receive(d, wdata, wlen, rbuf, rlen, 100));
}

int hal_i2c_close(int bus)
{
    if (bus < 0 || bus >= I2C_BUS_MAX) return HAL_ERR;
    if (!s_i2c[bus].bus) return 0;
    for (int i = 0; i < I2C_DEV_MAX; i++) {
        if (s_i2c[bus].devs[i].used) i2c_master_bus_rm_device(s_i2c[bus].devs[i].dev);
        s_i2c[bus].devs[i].used = false;
    }
    i2c_del_master_bus(s_i2c[bus].bus);
    s_i2c[bus].bus = NULL;
    return 0;
}

#define SPI_BUS_MAX 2

static struct {
    bool used;
    spi_host_device_t host;
    spi_device_handle_t dev;
} s_spi[SPI_BUS_MAX];

int hal_spi_open(int bus, int sck, int mosi, int miso, uint32_t freq_hz, int mode)
{
    if (bus < 0 || bus >= SPI_BUS_MAX) return HAL_ERR;
    if (s_spi[bus].used) hal_spi_close(bus);
    spi_host_device_t host = bus == 0 ? SPI2_HOST : SPI3_HOST;
    spi_bus_config_t bc = {
        .sclk_io_num = sck,
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    if (spi_bus_initialize(host, &bc, SPI_DMA_CH_AUTO) != ESP_OK) return HAL_ERR;
    spi_device_interface_config_t dc = {
        .clock_speed_hz = (int)(freq_hz ? freq_hz : 1000000),
        .mode = (uint8_t)mode,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    if (spi_bus_add_device(host, &dc, &s_spi[bus].dev) != ESP_OK) {
        spi_bus_free(host);
        return HAL_ERR;
    }
    s_spi[bus].used = true;
    s_spi[bus].host = host;
    return 0;
}

int hal_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (bus < 0 || bus >= SPI_BUS_MAX || !s_spi[bus].used) return HAL_ERR;
    if (len == 0) return 0;
    spi_transaction_t t;
    memset(&t, 0, sizeof t);
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(s_spi[bus].dev, &t) == ESP_OK ? 0 : HAL_ERR;
}

int hal_spi_close(int bus)
{
    if (bus < 0 || bus >= SPI_BUS_MAX) return HAL_ERR;
    if (!s_spi[bus].used) return 0;
    spi_bus_remove_device(s_spi[bus].dev);
    spi_bus_free(s_spi[bus].host);
    s_spi[bus].used = false;
    return 0;
}

static nvs_handle_t s_nvs;
static bool s_nvs_open;

static int nvs_ensure(void)
{
    if (s_nvs_open) return 0;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return HAL_ERR;
    if (nvs_open("kairo", NVS_READWRITE, &s_nvs) != ESP_OK) return HAL_ERR;
    s_nvs_open = true;
    return 0;
}

int hal_kv_get(const char *key, uint8_t *buf, size_t max)
{
    if (nvs_ensure() != 0) return HAL_ERR;
    size_t len = max;
    esp_err_t e = nvs_get_blob(s_nvs, key, buf, &len);
    if (e == ESP_ERR_NVS_NOT_FOUND) return HAL_ECLOSED;
    if (e != ESP_OK) return HAL_ERR;
    return (int)len;
}

int hal_kv_set(const char *key, const uint8_t *data, size_t len)
{
    if (nvs_ensure() != 0) return HAL_ERR;
    if (nvs_set_blob(s_nvs, key, data, len) != ESP_OK) return HAL_ERR;
    return nvs_commit(s_nvs) == ESP_OK ? 0 : HAL_ERR;
}

int hal_kv_delete(const char *key)
{
    if (nvs_ensure() != 0) return HAL_ERR;
    esp_err_t e = nvs_erase_key(s_nvs, key);
    if (e == ESP_ERR_NVS_NOT_FOUND) return HAL_ECLOSED;
    if (e != ESP_OK) return HAL_ERR;
    return nvs_commit(s_nvs) == ESP_OK ? 0 : HAL_ERR;
}

int hal_kv_keys(char *buf, size_t max)
{
    if (nvs_ensure() != 0) return HAL_ERR;
    size_t off = 0;
    nvs_iterator_t it = NULL;
    esp_err_t e = nvs_entry_find("nvs", "kairo", NVS_TYPE_BLOB, &it);
    while (e == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        size_t n = strlen(info.key) + 1;
        if (off + n + 1 > max) break;
        memcpy(buf + off, info.key, n);
        off += n;
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    buf[off] = 0;
    return (int)off;
}

int hal_deep_sleep(uint64_t us)
{
    esp_deep_sleep(us ? us : 1);
    return 0;
}

static portMUX_TYPE s_critical = portMUX_INITIALIZER_UNLOCKED;

void hal_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

uint32_t hal_critical_enter(void)
{
    portENTER_CRITICAL(&s_critical);
    return 0;
}

void hal_critical_exit(uint32_t state)
{
    (void)state;
    portEXIT_CRITICAL(&s_critical);
}

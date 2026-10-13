#include "kairo/hal.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/sync.h"

#ifndef KAIRO_APP_FLASH_BYTES
#define KAIRO_APP_FLASH_BYTES (512 * 1024)
#endif
#define KV_FLASH_BYTES FLASH_SECTOR_SIZE
#define KV_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - KAIRO_APP_FLASH_BYTES - KV_FLASH_BYTES)
#define KV_MAGIC 0x564B414Bu

int hal_pwm_set(uint32_t pin, uint32_t freq_hz, float duty)
{
    if (pin >= NUM_BANK0_GPIOS || freq_hz == 0) return HAL_ERR;
    uint slice = pwm_gpio_to_slice_num(pin);
    uint32_t clk = clock_get_hz(clk_sys);
    uint32_t wrap = 65535;
    float div = (float)clk / ((float)freq_hz * (float)(wrap + 1));
    if (div < 1.0f) {
        div = 1.0f;
        wrap = clk / freq_hz - 1;
    }
    if (div > 255.0f) div = 255.0f;
    gpio_set_function(pin, GPIO_FUNC_PWM);
    pwm_set_clkdiv(slice, div);
    pwm_set_wrap(slice, (uint16_t)wrap);
    pwm_set_gpio_level(pin, (uint16_t)(duty * (float)wrap + 0.5f));
    pwm_set_enabled(slice, true);
    return 0;
}

int hal_pwm_stop(uint32_t pin)
{
    if (pin >= NUM_BANK0_GPIOS) return HAL_ERR;
    pwm_set_gpio_level(pin, 0);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    return 0;
}

#define GPIO_EVQ 64

static hal_gpio_event_t s_evq[GPIO_EVQ];
static volatile uint32_t s_evq_head, s_evq_tail;
static bool s_irq_installed;

static void gpio_irq(uint gpio, uint32_t events)
{
    uint32_t next = (s_evq_tail + 1) % GPIO_EVQ;
    if (next == s_evq_head) return;
    s_evq[s_evq_tail].pin = gpio;
    s_evq[s_evq_tail].level = (events & GPIO_IRQ_EDGE_RISE) ? 1 : 0;
    s_evq[s_evq_tail].time_us = time_us_64();
    s_evq_tail = next;
}

int hal_gpio_watch(uint32_t pin, int edge)
{
    if (pin >= NUM_BANK0_GPIOS) return HAL_ERR;
    uint32_t mask = ((edge & HAL_EDGE_RISE) ? GPIO_IRQ_EDGE_RISE : 0) | ((edge & HAL_EDGE_FALL) ? GPIO_IRQ_EDGE_FALL : 0);
    if (!s_irq_installed) {
        gpio_set_irq_enabled_with_callback(pin, mask, true, gpio_irq);
        s_irq_installed = true;
    } else {
        gpio_set_irq_enabled(pin, mask, true);
    }
    return 0;
}

int hal_gpio_unwatch(uint32_t pin)
{
    if (pin >= NUM_BANK0_GPIOS) return HAL_ERR;
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
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
    if (pin >= NUM_BANK0_GPIOS) return HAL_ERR;
    uint64_t deadline = time_us_64() + timeout_us;
    while (gpio_get(pin) == level)
        if (time_us_64() > deadline) return HAL_ERR;
    while (gpio_get(pin) != level)
        if (time_us_64() > deadline) return HAL_ERR;
    uint64_t start = time_us_64();
    while (gpio_get(pin) == level)
        if (time_us_64() > deadline) return HAL_ERR;
    return (int64_t)(time_us_64() - start);
}

static i2c_inst_t *i2c_of(int bus)
{
    return bus == 0 ? i2c0 : bus == 1 ? i2c1 : NULL;
}

static int i2c_map(int rc, size_t expected)
{
    if (rc == PICO_ERROR_GENERIC) return HAL_ECLOSED;
    if (rc < 0) return HAL_ERR;
    return (size_t)rc == expected ? 0 : HAL_ERR;
}

int hal_i2c_open(int bus, int sda, int scl, uint32_t freq_hz)
{
    i2c_inst_t *i = i2c_of(bus);
    if (!i) return HAL_ERR;
    i2c_init(i, freq_hz ? freq_hz : 100000);
    gpio_set_function((uint)sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)scl, GPIO_FUNC_I2C);
    gpio_pull_up((uint)sda);
    gpio_pull_up((uint)scl);
    return 0;
}

int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_inst_t *i = i2c_of(bus);
    if (!i) return HAL_ERR;
    if (len == 0) {
        uint8_t dummy;
        int rc = i2c_read_timeout_us(i, addr, &dummy, 1, false, 20000);
        return rc < 0 ? HAL_ECLOSED : 0;
    }
    return i2c_map(i2c_write_timeout_us(i, addr, data, len, false, 50000), len);
}

int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len)
{
    i2c_inst_t *i = i2c_of(bus);
    if (!i) return HAL_ERR;
    return i2c_map(i2c_read_timeout_us(i, addr, buf, len, false, 50000), len);
}

int hal_i2c_write_read(int bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    i2c_inst_t *i = i2c_of(bus);
    if (!i) return HAL_ERR;
    int rc = i2c_map(i2c_write_timeout_us(i, addr, wdata, wlen, true, 50000), wlen);
    if (rc != 0) return rc;
    return i2c_map(i2c_read_timeout_us(i, addr, rbuf, rlen, false, 50000), rlen);
}

int hal_i2c_close(int bus)
{
    i2c_inst_t *i = i2c_of(bus);
    if (!i) return HAL_ERR;
    i2c_deinit(i);
    return 0;
}

static spi_inst_t *spi_of(int bus)
{
    return bus == 0 ? spi0 : bus == 1 ? spi1 : NULL;
}

int hal_spi_open(int bus, int sck, int mosi, int miso, uint32_t freq_hz, int mode)
{
    spi_inst_t *s = spi_of(bus);
    if (!s) return HAL_ERR;
    spi_init(s, freq_hz ? freq_hz : 1000000);
    spi_set_format(s, 8, (mode & 2) ? SPI_CPOL_1 : SPI_CPOL_0, (mode & 1) ? SPI_CPHA_1 : SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function((uint)sck, GPIO_FUNC_SPI);
    if (mosi >= 0) gpio_set_function((uint)mosi, GPIO_FUNC_SPI);
    if (miso >= 0) gpio_set_function((uint)miso, GPIO_FUNC_SPI);
    return 0;
}

int hal_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_inst_t *s = spi_of(bus);
    if (!s) return HAL_ERR;
    return spi_write_read_blocking(s, tx, rx, len) == (int)len ? 0 : HAL_ERR;
}

int hal_spi_close(int bus)
{
    spi_inst_t *s = spi_of(bus);
    if (!s) return HAL_ERR;
    spi_deinit(s);
    return 0;
}

static uint8_t s_kv[KV_FLASH_BYTES];
static bool s_kv_loaded;

static void kv_load(void)
{
    if (s_kv_loaded) return;
    memcpy(s_kv, (const void *)(XIP_BASE + KV_FLASH_OFFSET), KV_FLASH_BYTES);
    uint32_t magic;
    memcpy(&magic, s_kv, 4);
    if (magic != KV_MAGIC) {
        memset(s_kv, 0xff, KV_FLASH_BYTES);
        memcpy(s_kv, &(uint32_t){ KV_MAGIC }, 4);
        s_kv[4] = 0;
    }
    s_kv_loaded = true;
}

static int kv_save(void)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(KV_FLASH_OFFSET, KV_FLASH_BYTES);
    flash_range_program(KV_FLASH_OFFSET, s_kv, KV_FLASH_BYTES);
    restore_interrupts(ints);
    return 0;
}

static size_t kv_record(size_t off, uint8_t *klen, uint16_t *vlen)
{
    if (off + 3 > KV_FLASH_BYTES) return 0;
    *klen = s_kv[off];
    memcpy(vlen, s_kv + off + 1, 2);
    size_t rec = 3 + *klen + *vlen;
    if (*klen == 0 || off + rec > KV_FLASH_BYTES) return 0;
    return rec;
}

static size_t kv_find(const char *key, size_t *rec_len)
{
    size_t off = 4;
    uint8_t klen;
    uint16_t vlen;
    size_t rec;
    while (off < KV_FLASH_BYTES && s_kv[off] != 0 && s_kv[off] != 0xff && (rec = kv_record(off, &klen, &vlen)) != 0) {
        if (klen == strlen(key) && memcmp(s_kv + off + 3, key, klen) == 0) {
            *rec_len = rec;
            return off;
        }
        off += rec;
    }
    *rec_len = 0;
    return off;
}

int hal_kv_get(const char *key, uint8_t *buf, size_t max)
{
    kv_load();
    size_t rec;
    size_t off = kv_find(key, &rec);
    if (!rec) return HAL_ECLOSED;
    uint8_t klen;
    uint16_t vlen;
    kv_record(off, &klen, &vlen);
    size_t n = vlen < max ? vlen : max;
    memcpy(buf, s_kv + off + 3 + klen, n);
    return (int)n;
}

static size_t kv_end(void)
{
    size_t off = 4;
    uint8_t klen;
    uint16_t vlen;
    size_t rec;
    while (off < KV_FLASH_BYTES && s_kv[off] != 0 && s_kv[off] != 0xff && (rec = kv_record(off, &klen, &vlen)) != 0) off += rec;
    return off;
}

int hal_kv_delete(const char *key)
{
    kv_load();
    size_t rec;
    size_t off = kv_find(key, &rec);
    if (!rec) return HAL_ECLOSED;
    size_t end = kv_end();
    memmove(s_kv + off, s_kv + off + rec, end - off - rec);
    memset(s_kv + end - rec, 0, rec);
    return kv_save();
}

int hal_kv_set(const char *key, const uint8_t *data, size_t len)
{
    kv_load();
    size_t klen = strlen(key);
    if (klen == 0 || klen > 255 || len > 65535) return HAL_ERR;
    size_t rec;
    size_t off = kv_find(key, &rec);
    size_t end = kv_end();
    if (rec) {
        memmove(s_kv + off, s_kv + off + rec, end - off - rec);
        end -= rec;
    }
    size_t need = 3 + klen + len;
    if (end + need + 1 > KV_FLASH_BYTES) return HAL_ERR;
    s_kv[end] = (uint8_t)klen;
    uint16_t vlen = (uint16_t)len;
    memcpy(s_kv + end + 1, &vlen, 2);
    memcpy(s_kv + end + 3, key, klen);
    memcpy(s_kv + end + 3 + klen, data, len);
    s_kv[end + need] = 0;
    return kv_save();
}

int hal_kv_keys(char *buf, size_t max)
{
    kv_load();
    size_t off = 4, out = 0;
    uint8_t klen;
    uint16_t vlen;
    size_t rec;
    while (off < KV_FLASH_BYTES && s_kv[off] != 0 && s_kv[off] != 0xff && (rec = kv_record(off, &klen, &vlen)) != 0) {
        if (out + klen + 2 > max) break;
        memcpy(buf + out, s_kv + off + 3, klen);
        out += klen;
        buf[out++] = 0;
        off += rec;
    }
    buf[out] = 0;
    return (int)out;
}

int hal_deep_sleep(uint64_t us)
{
    (void)us;
    return HAL_ENOSYS;
}

void hal_delay_us(uint32_t us)
{
    busy_wait_us_32(us);
}

uint32_t hal_critical_enter(void)
{
    return save_and_disable_interrupts();
}

void hal_critical_exit(uint32_t state)
{
    restore_interrupts(state);
}

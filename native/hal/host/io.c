#include "kairo/hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void trace(const char *fmt, ...)
{
    char buf[200];
    uint64_t t = hal_time_us();
    int n = snprintf(buf, sizeof buf, "[%7llu.%03llu ms] ", (unsigned long long)(t / 1000), (unsigned long long)(t % 1000));
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    hal_console_write(buf, (size_t)n);
}

static float s_pwm_duty[48];
static uint32_t s_pwm_freq[48];

int hal_pwm_set(uint32_t pin, uint32_t freq_hz, float duty)
{
    if (pin >= 48 || freq_hz == 0) return HAL_ERR;
    if (s_pwm_freq[pin] != freq_hz || (int)(s_pwm_duty[pin] * 1000) != (int)(duty * 1000)) {
        s_pwm_freq[pin] = freq_hz;
        s_pwm_duty[pin] = duty;
        trace("pwm %u %u Hz duty=%.1f%%", pin, freq_hz, duty * 100.0);
    }
    return 0;
}

int hal_pwm_stop(uint32_t pin)
{
    if (pin >= 48) return HAL_ERR;
    s_pwm_freq[pin] = 0;
    trace("pwm %u stopped", pin);
    return 0;
}

static struct {
    bool used;
    int edge;
    uint64_t period_us;
    uint64_t next_us;
    bool level;
} s_watch[48];

int hal_gpio_watch(uint32_t pin, int edge)
{
    if (pin >= 48) return HAL_ERR;
    s_watch[pin].used = true;
    s_watch[pin].edge = edge;
    char env[32];
    snprintf(env, sizeof env, "KAIRO_SIM_GPIO%u_TOGGLE_MS", pin);
    const char *v = getenv(env);
    s_watch[pin].period_us = v ? (uint64_t)atoi(v) * 1000 : 0;
    s_watch[pin].next_us = hal_time_us() + s_watch[pin].period_us;
    s_watch[pin].level = false;
    return 0;
}

int hal_gpio_unwatch(uint32_t pin)
{
    if (pin >= 48) return HAL_ERR;
    s_watch[pin].used = false;
    return 0;
}

int hal_gpio_poll_events(hal_gpio_event_t *out, size_t max)
{
    size_t n = 0;
    uint64_t now = hal_time_us();
    for (uint32_t pin = 0; pin < 48 && n < max; pin++) {
        if (!s_watch[pin].used || !s_watch[pin].period_us || now < s_watch[pin].next_us) continue;
        s_watch[pin].next_us += s_watch[pin].period_us;
        s_watch[pin].level = !s_watch[pin].level;
        int edge = s_watch[pin].level ? HAL_EDGE_RISE : HAL_EDGE_FALL;
        if (!(s_watch[pin].edge & edge)) continue;
        out[n].pin = pin;
        out[n].level = s_watch[pin].level;
        out[n].time_us = now;
        n++;
    }
    return (int)n;
}

int64_t hal_gpio_pulse_in(uint32_t pin, bool level, uint32_t timeout_us)
{
    (void)level; (void)timeout_us;
    if (pin >= 48) return HAL_ERR;
    char env[32];
    snprintf(env, sizeof env, "KAIRO_SIM_GPIO%u_PULSE_US", pin);
    const char *v = getenv(env);
    return v ? atoll(v) : HAL_ERR;
}

static bool s_i2c_open[2];

int hal_i2c_open(int bus, int sda, int scl, uint32_t freq_hz)
{
    if (bus < 0 || bus > 1) return HAL_ERR;
    s_i2c_open[bus] = true;
    trace("i2c%d open sda=%d scl=%d %u Hz", bus, sda, scl, freq_hz);
    return 0;
}

static uint8_t s_fake_reg;

int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len)
{
    if (bus < 0 || bus > 1 || !s_i2c_open[bus]) return HAL_ERR;
    if (addr != 0x48) return HAL_ECLOSED;
    if (len) s_fake_reg = data[0];
    return 0;
}

int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len)
{
    if (bus < 0 || bus > 1 || !s_i2c_open[bus]) return HAL_ERR;
    if (addr != 0x48) return HAL_ECLOSED;
    static const uint8_t temp[2] = { 0x19, 0x80 };
    static const uint8_t config[2] = { 0x60, 0xa0 };
    const uint8_t *src = s_fake_reg == 0x01 ? config : temp;
    for (size_t i = 0; i < len; i++) buf[i] = i < 2 ? src[i] : 0;
    return 0;
}

int hal_i2c_write_read(int bus, uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    int rc = hal_i2c_write(bus, addr, wdata, wlen);
    if (rc != 0) return rc;
    return hal_i2c_read(bus, addr, rbuf, rlen);
}

int hal_i2c_close(int bus)
{
    if (bus < 0 || bus > 1) return HAL_ERR;
    s_i2c_open[bus] = false;
    return 0;
}

static bool s_spi_open[2];

int hal_spi_open(int bus, int sck, int mosi, int miso, uint32_t freq_hz, int mode)
{
    if (bus < 0 || bus > 1) return HAL_ERR;
    s_spi_open[bus] = true;
    trace("spi%d open sck=%d mosi=%d miso=%d %u Hz mode=%d", bus, sck, mosi, miso, freq_hz, mode);
    return 0;
}

int hal_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (bus < 0 || bus > 1 || !s_spi_open[bus]) return HAL_ERR;
    memcpy(rx, tx, len);
    return 0;
}

int hal_spi_close(int bus)
{
    if (bus < 0 || bus > 1) return HAL_ERR;
    s_spi_open[bus] = false;
    return 0;
}

#define KV_MAX 64

static struct {
    char key[32];
    uint8_t *data;
    size_t len;
} s_kv[KV_MAX];
static int s_kv_count;
static bool s_kv_loaded;

static const char *kv_path(void)
{
    return getenv("KAIRO_SIM_KV");
}

static void kv_load(void)
{
    if (s_kv_loaded) return;
    s_kv_loaded = true;
    const char *path = kv_path();
    if (!path) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    while (s_kv_count < KV_MAX) {
        uint8_t klen;
        uint16_t vlen;
        if (fread(&klen, 1, 1, f) != 1 || fread(&vlen, 2, 1, f) != 1) break;
        if (klen == 0 || klen >= 32) break;
        if (fread(s_kv[s_kv_count].key, 1, klen, f) != klen) break;
        s_kv[s_kv_count].key[klen] = 0;
        s_kv[s_kv_count].data = malloc(vlen ? vlen : 1);
        if (fread(s_kv[s_kv_count].data, 1, vlen, f) != vlen) break;
        s_kv[s_kv_count].len = vlen;
        s_kv_count++;
    }
    fclose(f);
}

static int kv_save(void)
{
    const char *path = kv_path();
    if (!path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return HAL_ERR;
    for (int i = 0; i < s_kv_count; i++) {
        uint8_t klen = (uint8_t)strlen(s_kv[i].key);
        uint16_t vlen = (uint16_t)s_kv[i].len;
        fwrite(&klen, 1, 1, f);
        fwrite(&vlen, 2, 1, f);
        fwrite(s_kv[i].key, 1, klen, f);
        fwrite(s_kv[i].data, 1, vlen, f);
    }
    fclose(f);
    return 0;
}

static int kv_find(const char *key)
{
    for (int i = 0; i < s_kv_count; i++)
        if (strcmp(s_kv[i].key, key) == 0) return i;
    return -1;
}

int hal_kv_get(const char *key, uint8_t *buf, size_t max)
{
    kv_load();
    int i = kv_find(key);
    if (i < 0) return HAL_ECLOSED;
    size_t n = s_kv[i].len < max ? s_kv[i].len : max;
    memcpy(buf, s_kv[i].data, n);
    return (int)n;
}

int hal_kv_set(const char *key, const uint8_t *data, size_t len)
{
    kv_load();
    if (strlen(key) == 0 || strlen(key) >= 32) return HAL_ERR;
    int i = kv_find(key);
    if (i < 0) {
        if (s_kv_count >= KV_MAX) return HAL_ERR;
        i = s_kv_count++;
        snprintf(s_kv[i].key, sizeof s_kv[i].key, "%s", key);
        s_kv[i].data = NULL;
    }
    free(s_kv[i].data);
    s_kv[i].data = malloc(len ? len : 1);
    memcpy(s_kv[i].data, data, len);
    s_kv[i].len = len;
    return kv_save();
}

int hal_kv_delete(const char *key)
{
    kv_load();
    int i = kv_find(key);
    if (i < 0) return HAL_ECLOSED;
    free(s_kv[i].data);
    s_kv[i] = s_kv[--s_kv_count];
    return kv_save();
}

int hal_kv_keys(char *buf, size_t max)
{
    kv_load();
    size_t off = 0;
    for (int i = 0; i < s_kv_count; i++) {
        size_t n = strlen(s_kv[i].key) + 1;
        if (off + n + 1 > max) break;
        memcpy(buf + off, s_kv[i].key, n);
        off += n;
    }
    buf[off] = 0;
    return (int)off;
}

int hal_deep_sleep(uint64_t us)
{
    trace("deep sleep for %llu ms (simulator exits)", (unsigned long long)(us / 1000));
    fflush(stdout);
    exit(0);
}

void hal_delay_us(uint32_t us)
{
    uint64_t until = hal_time_us() + us;
    while (hal_time_us() < until) {}
}

uint32_t hal_critical_enter(void)
{
    return 0;
}

void hal_critical_exit(uint32_t state)
{
    (void)state;
}

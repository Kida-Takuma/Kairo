#include "kairo/hal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define HOST_GPIO_COUNT 48

#define HOST_APP_CAPACITY (256 * 1024)

static uint64_t s_t0_us;
static struct {
    hal_gpio_mode_t mode;
    bool level;
} s_pins[HOST_GPIO_COUNT];
static const char *s_app_file;
static uint8_t s_app[HOST_APP_CAPACITY];

void hal_host_set_app_file(const char *path)
{
    s_app_file = path;
}

static void app_load(void)
{
    memset(s_app, 0xff, sizeof s_app);
    FILE *f = fopen(s_app_file, "rb");
    if (!f) return;
    fread(s_app, 1, sizeof s_app, f);
    fclose(f);
}

static int app_save(void)
{
    FILE *f = fopen(s_app_file, "wb");
    if (!f) return -1;
    size_t n = fwrite(s_app, 1, sizeof s_app, f);
    fclose(f);
    return n == sizeof s_app ? 0 : -1;
}

static uint64_t monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

int hal_init(void)
{
    s_t0_us = monotonic_us();
    memset(s_pins, 0, sizeof s_pins);
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (s_app_file) {
        app_load();
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    }
    return 0;
}

const char *hal_board_name(void)
{
    return "host";
}

uint64_t hal_time_us(void)
{
    return monotonic_us() - s_t0_us;
}

void hal_idle_until_us(uint64_t deadline_us)
{
    uint64_t now = hal_time_us();
    if (deadline_us <= now) return;
    uint64_t d = deadline_us - now;
    struct timespec ts = { .tv_sec = (time_t)(d / 1000000ull), .tv_nsec = (long)((d % 1000000ull) * 1000ull) };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void trace(const char *fmt, ...)
{
    char buf[160];
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

int hal_gpio_set_mode(uint32_t pin, hal_gpio_mode_t mode)
{
    if (pin >= HOST_GPIO_COUNT) return -1;
    s_pins[pin].mode = mode;
    if (mode == HAL_GPIO_INPUT_PULLUP) s_pins[pin].level = true;
    if (mode == HAL_GPIO_INPUT_PULLDOWN) s_pins[pin].level = false;
    static const char *names[] = { "input", "output", "input-pullup", "input-pulldown" };
    trace("gpio %u mode=%s", pin, names[mode & 3]);
    return 0;
}

int hal_gpio_write(uint32_t pin, bool level)
{
    if (pin >= HOST_GPIO_COUNT) return -1;
    if (s_pins[pin].level != level) {
        s_pins[pin].level = level;
        trace("gpio %u -> %s", pin, level ? "HIGH" : "LOW");
    }
    return 0;
}

int hal_gpio_read(uint32_t pin)
{
    if (pin >= HOST_GPIO_COUNT) return -1;
    return s_pins[pin].level ? 1 : 0;
}

void hal_console_write(const char *buf, size_t len)
{
    fwrite(buf, 1, len, stdout);
    if (s_app_file) fflush(stdout);
}

int hal_console_read(uint8_t *buf, size_t max)
{
    if (!s_app_file) return 0;
    ssize_t n = read(STDIN_FILENO, buf, max);
    return n < 0 ? 0 : (int)n;
}

int hal_app_capacity(size_t *bytes)
{
    if (!s_app_file) return -1;
    *bytes = HOST_APP_CAPACITY;
    return 0;
}

int hal_app_erase(size_t bytes)
{
    if (!s_app_file || bytes > HOST_APP_CAPACITY) return -1;
    memset(s_app, 0xff, sizeof s_app);
    return 0;
}

int hal_app_write(size_t offset, const void *data, size_t len)
{
    if (!s_app_file || offset + len > HOST_APP_CAPACITY) return -1;
    memcpy(s_app + offset, data, len);
    return 0;
}

int hal_app_finish(void)
{
    if (!s_app_file) return -1;
    return app_save();
}

const void *hal_app_map(size_t *bytes)
{
    if (!s_app_file) {
        *bytes = 0;
        return NULL;
    }
    *bytes = HOST_APP_CAPACITY;
    return s_app;
}

size_t hal_js_heap_bytes(void)
{
    return 1024 * 1024;
}

size_t hal_js_stack_bytes(void)
{
    return 0;
}

void hal_reboot(void)
{
    fflush(stdout);
    exit(0);
}

void hal_panic(const char *msg)
{
    fprintf(stderr, "\n*** PANIC: %s\n", msg);
    fflush(stderr);
    abort();
}

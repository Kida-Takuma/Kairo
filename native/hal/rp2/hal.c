#include "kairo/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#ifndef KAIRO_APP_FLASH_BYTES
#define KAIRO_APP_FLASH_BYTES (512 * 1024)
#endif
#define APP_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - KAIRO_APP_FLASH_BYTES)

static uint8_t s_page[FLASH_PAGE_SIZE];
static size_t s_page_base;
static size_t s_page_len;
static bool s_page_dirty;

int hal_init(void)
{
    stdio_init_all();
    return 0;
}

const char *hal_board_name(void)
{
    return PICO_BOARD;
}

uint64_t hal_time_us(void)
{
    return time_us_64();
}

void hal_idle_until_us(uint64_t deadline_us)
{
    uint64_t now = time_us_64();
    if (deadline_us <= now) return;
    sleep_until(from_us_since_boot(deadline_us));
}

int hal_gpio_set_mode(uint32_t pin, hal_gpio_mode_t mode)
{
    if (pin >= NUM_BANK0_GPIOS) return -1;
    gpio_init(pin);
    switch (mode) {
    case HAL_GPIO_OUTPUT:
        gpio_set_dir(pin, GPIO_OUT);
        break;
    case HAL_GPIO_INPUT_PULLUP:
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        break;
    case HAL_GPIO_INPUT_PULLDOWN:
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
        break;
    default:
        gpio_set_dir(pin, GPIO_IN);
        gpio_disable_pulls(pin);
        break;
    }
    return 0;
}

int hal_gpio_write(uint32_t pin, bool level)
{
    if (pin >= NUM_BANK0_GPIOS) return -1;
    gpio_put(pin, level);
    return 0;
}

int hal_gpio_read(uint32_t pin)
{
    if (pin >= NUM_BANK0_GPIOS) return -1;
    return gpio_get(pin) ? 1 : 0;
}

void hal_console_write(const char *buf, size_t len)
{
    fwrite(buf, 1, len, stdout);
    stdio_flush();
}

int hal_console_read(uint8_t *buf, size_t max)
{
    size_t n = 0;
    while (n < max) {
        int c = getchar_timeout_us(0);
        if (c < 0) break;
        buf[n++] = (uint8_t)c;
    }
    return (int)n;
}

int hal_app_capacity(size_t *bytes)
{
    *bytes = KAIRO_APP_FLASH_BYTES;
    return 0;
}

static void flash_page_flush(void)
{
    if (!s_page_dirty) return;
    memset(s_page + s_page_len, 0xff, FLASH_PAGE_SIZE - s_page_len);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(APP_FLASH_OFFSET + s_page_base, s_page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    s_page_dirty = false;
    s_page_len = 0;
}

int hal_app_erase(size_t bytes)
{
    size_t n = (bytes + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE * FLASH_SECTOR_SIZE;
    if (n > KAIRO_APP_FLASH_BYTES) return -1;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(APP_FLASH_OFFSET, n);
    restore_interrupts(ints);
    s_page_base = 0;
    s_page_len = 0;
    s_page_dirty = false;
    return 0;
}

int hal_app_write(size_t offset, const void *data, size_t len)
{
    if (offset + len > KAIRO_APP_FLASH_BYTES) return -1;
    const uint8_t *p = data;
    while (len) {
        size_t page_base = offset / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
        if (s_page_dirty && page_base != s_page_base) flash_page_flush();
        if (!s_page_dirty) {
            s_page_base = page_base;
            s_page_len = 0;
            memset(s_page, 0xff, FLASH_PAGE_SIZE);
        }
        size_t in_page = offset - page_base;
        size_t n = FLASH_PAGE_SIZE - in_page;
        if (n > len) n = len;
        memcpy(s_page + in_page, p, n);
        if (in_page + n > s_page_len) s_page_len = in_page + n;
        s_page_dirty = true;
        if (s_page_len == FLASH_PAGE_SIZE) flash_page_flush();
        offset += n;
        p += n;
        len -= n;
    }
    return 0;
}

int hal_app_finish(void)
{
    flash_page_flush();
    return 0;
}

const void *hal_app_map(size_t *bytes)
{
    *bytes = KAIRO_APP_FLASH_BYTES;
    return (const void *)(XIP_BASE + APP_FLASH_OFFSET);
}

size_t hal_js_heap_bytes(void)
{
#if PICO_RP2350
    return 384 * 1024;
#else
    return 160 * 1024;
#endif
}

size_t hal_js_stack_bytes(void)
{
    return PICO_STACK_SIZE - 8 * 1024;
}

void hal_reboot(void)
{
    stdio_flush();
    watchdog_reboot(0, 0, 10);
    for (;;) tight_loop_contents();
}

void hal_panic(const char *msg)
{
    printf("\n*** PANIC: %s\n", msg);
    stdio_flush();
    abort();
}

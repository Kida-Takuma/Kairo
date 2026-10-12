#include "kairo/hal.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

#define APP_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x40)
#define APP_PARTITION_LABEL "kairo_app"
#define FLASH_SECTOR 4096

static const esp_partition_t *s_app;
static const void *s_app_map;
static esp_partition_mmap_handle_t s_app_map_handle;

static void console_init(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 4096;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
#else
    uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 4096, 0, 0, NULL, 0);
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    setvbuf(stdout, NULL, _IONBF, 0);
}

int hal_init(void)
{
    console_init();
    s_app = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, APP_PARTITION_SUBTYPE, APP_PARTITION_LABEL);
    return 0;
}

const char *hal_board_name(void)
{
    return CONFIG_IDF_TARGET;
}

uint64_t hal_time_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

void hal_idle_until_us(uint64_t deadline_us)
{
    uint64_t now = hal_time_us();
    if (deadline_us <= now) {
        taskYIELD();
        return;
    }
    uint64_t d = deadline_us - now;
    TickType_t ticks = (TickType_t)(d / (1000000 / configTICK_RATE_HZ));
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
}

int hal_gpio_set_mode(uint32_t pin, hal_gpio_mode_t mode)
{
    if (!GPIO_IS_VALID_GPIO(pin)) return -1;
    if (mode == HAL_GPIO_OUTPUT && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) return -1;
    gpio_num_t g = (gpio_num_t)pin;
    gpio_reset_pin(g);
    switch (mode) {
    case HAL_GPIO_OUTPUT:
        gpio_set_direction(g, GPIO_MODE_OUTPUT);
        break;
    case HAL_GPIO_INPUT_PULLUP:
        gpio_set_direction(g, GPIO_MODE_INPUT);
        gpio_set_pull_mode(g, GPIO_PULLUP_ONLY);
        break;
    case HAL_GPIO_INPUT_PULLDOWN:
        gpio_set_direction(g, GPIO_MODE_INPUT);
        gpio_set_pull_mode(g, GPIO_PULLDOWN_ONLY);
        break;
    default:
        gpio_set_direction(g, GPIO_MODE_INPUT);
        gpio_set_pull_mode(g, GPIO_FLOATING);
        break;
    }
    return 0;
}

int hal_gpio_write(uint32_t pin, bool level)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return -1;
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
    return 0;
}

int hal_gpio_read(uint32_t pin)
{
    if (!GPIO_IS_VALID_GPIO(pin)) return -1;
    return gpio_get_level((gpio_num_t)pin);
}

void hal_console_write(const char *buf, size_t len)
{
    fwrite(buf, 1, len, stdout);
}

int hal_console_read(uint8_t *buf, size_t max)
{
    int n = (int)read(STDIN_FILENO, buf, max);
    return n < 0 ? 0 : n;
}

int hal_app_capacity(size_t *bytes)
{
    if (!s_app) return -1;
    *bytes = s_app->size;
    return 0;
}

static void unmap_app(void)
{
    if (s_app_map) {
        esp_partition_munmap(s_app_map_handle);
        s_app_map = NULL;
    }
}

int hal_app_erase(size_t bytes)
{
    if (!s_app) return -1;
    unmap_app();
    size_t n = (bytes + FLASH_SECTOR - 1) / FLASH_SECTOR * FLASH_SECTOR;
    if (n > s_app->size) return -1;
    return esp_partition_erase_range(s_app, 0, n) == ESP_OK ? 0 : -1;
}

int hal_app_write(size_t offset, const void *data, size_t len)
{
    if (!s_app || offset + len > s_app->size) return -1;
    return esp_partition_write(s_app, offset, data, len) == ESP_OK ? 0 : -1;
}

int hal_app_finish(void)
{
    return s_app ? 0 : -1;
}

const void *hal_app_map(size_t *bytes)
{
    if (!s_app) return NULL;
    if (!s_app_map) {
        if (esp_partition_mmap(s_app, 0, s_app->size, ESP_PARTITION_MMAP_DATA, &s_app_map, &s_app_map_handle) != ESP_OK) return NULL;
    }
    *bytes = s_app->size;
    return s_app_map;
}

size_t hal_js_heap_bytes(void)
{
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t reserve = 64 * 1024;
    return free_bytes > reserve * 2 ? free_bytes - reserve : free_bytes / 2;
}

size_t hal_js_stack_bytes(void)
{
    return KAIRO_TASK_STACK_BYTES - 16 * 1024;
}

void hal_reboot(void)
{
    fflush(stdout);
    esp_restart();
}

void hal_panic(const char *msg)
{
    printf("\n*** PANIC: %s\n", msg);
    fflush(stdout);
    abort();
}

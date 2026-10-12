#include "kairo/hal.h"

#include <string.h>
#include <sys/time.h>

#include "driver/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_random.h"

int hal_random_bytes(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
    return 0;
}

int64_t hal_rtc_get_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int hal_rtc_set_ms(int64_t epoch_ms)
{
    struct timeval tv = { .tv_sec = (time_t)(epoch_ms / 1000), .tv_usec = (suseconds_t)((epoch_ms % 1000) * 1000) };
    return settimeofday(&tv, NULL) == 0 ? 0 : HAL_ERR;
}

static bool s_uart_open[UART_NUM_MAX];

int hal_uart_open(int id, int tx_pin, int rx_pin, uint32_t baud)
{
    if (id < 0 || id >= UART_NUM_MAX) return HAL_ERR;
    if (s_uart_open[id]) uart_driver_delete(id);
    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(id, 2048, 0, 0, NULL, 0) != ESP_OK) return HAL_ERR;
    if (uart_param_config(id, &cfg) != ESP_OK) return HAL_ERR;
    if (uart_set_pin(id, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return HAL_ERR;
    s_uart_open[id] = true;
    return 0;
}

int hal_uart_write(int id, const void *buf, size_t len)
{
    if (id < 0 || id >= UART_NUM_MAX || !s_uart_open[id]) return HAL_ERR;
    int n = uart_write_bytes(id, buf, len);
    return n < 0 ? HAL_ERR : n;
}

int hal_uart_read(int id, void *buf, size_t len)
{
    if (id < 0 || id >= UART_NUM_MAX || !s_uart_open[id]) return HAL_ERR;
    int n = uart_read_bytes(id, buf, (uint32_t)len, 0);
    if (n < 0) return HAL_ERR;
    return n == 0 ? HAL_EAGAIN : n;
}

int hal_uart_close(int id)
{
    if (id < 0 || id >= UART_NUM_MAX) return HAL_ERR;
    if (s_uart_open[id]) uart_driver_delete(id);
    s_uart_open[id] = false;
    return 0;
}

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_adc_chan[16];

static int adc_ensure(int channel)
{
    if (!s_adc) {
        adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
        if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) return HAL_ERR;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t ccfg = { .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        adc_cali_create_scheme_curve_fitting(&ccfg, &s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t lcfg = { .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        adc_cali_create_scheme_line_fitting(&lcfg, &s_cali);
#endif
    }
    if (!s_adc_chan[channel]) {
        adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        if (adc_oneshot_config_channel(s_adc, (adc_channel_t)channel, &ccfg) != ESP_OK) return HAL_ERR;
        s_adc_chan[channel] = true;
    }
    return 0;
}

int hal_adc_read_mv(int channel)
{
    if (channel < 0 || channel >= 16) return HAL_ERR;
    if (adc_ensure(channel) != 0) return HAL_ERR;
    int raw = 0;
    if (adc_oneshot_read(s_adc, (adc_channel_t)channel, &raw) != ESP_OK) return HAL_ERR;
    int mv = 0;
    if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) return mv;
    return raw * 3100 / 4095;
}

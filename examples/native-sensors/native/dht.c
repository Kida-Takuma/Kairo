#include "kairo/bridge.h"

static int wait_level(uint32_t pin, bool level, uint32_t timeout_us)
{
    uint64_t start = kairo_time_us();
    while (hal_gpio_read(pin) != (int)level) {
        if (kairo_time_us() - start > timeout_us) return -1;
    }
    return (int)(kairo_time_us() - start);
}

static JSValue read_dht(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    uint32_t pin;
    bool dht22 = true;
    if (kairo_arg_uint(ctx, argc, argv, 0, &pin)) return JS_EXCEPTION;
    if (argc > 1 && kairo_arg_bool(ctx, argc, argv, 1, &dht22)) return JS_EXCEPTION;

    hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT);
    hal_gpio_write(pin, false);
    kairo_delay_us(dht22 ? 1100 : 18000);
    hal_gpio_write(pin, true);
    kairo_delay_us(30);
    hal_gpio_set_mode(pin, HAL_GPIO_INPUT_PULLUP);

    uint8_t data[5] = { 0 };
    uint32_t state = kairo_critical_enter();
    int ok = wait_level(pin, false, 100) >= 0 && wait_level(pin, true, 100) >= 0 && wait_level(pin, false, 100) >= 0;
    for (int i = 0; ok && i < 40; i++) {
        if (wait_level(pin, true, 100) < 0) ok = 0;
        int high = wait_level(pin, false, 100);
        if (high < 0) ok = 0;
        if (high > 40) data[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    }
    kairo_critical_exit(state);

    if (!ok) return kairo_throw(ctx, "dht: no response on pin %u", pin);
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) return kairo_throw(ctx, "dht: checksum mismatch");

    double humidity, temperature;
    if (dht22) {
        humidity = ((data[0] << 8) | data[1]) / 10.0;
        int16_t t = (int16_t)(((data[2] & 0x7f) << 8) | data[3]);
        temperature = (data[2] & 0x80 ? -t : t) / 10.0;
    } else {
        humidity = data[0];
        temperature = data[2];
    }
    JSValue o = kairo_object(ctx);
    kairo_set(ctx, o, "temperature", kairo_number(ctx, temperature));
    kairo_set(ctx, o, "humidity", kairo_number(ctx, humidity));
    return o;
}

KAIRO_NATIVE_MODULE(dht)
{
    kairo_export(m, "read", read_dht, 2);
}

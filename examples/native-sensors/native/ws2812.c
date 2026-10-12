#include "kairo/bridge.h"

static inline void send_byte(uint32_t pin, uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        bool one = (b >> i) & 1;
        hal_gpio_write(pin, true);
        kairo_delay_us(one ? 1 : 0);
        hal_gpio_write(pin, false);
        kairo_delay_us(one ? 0 : 1);
    }
}

static JSValue show(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    uint32_t pin;
    const uint8_t *grb;
    size_t len;
    kairo_bytes_t ref;
    if (kairo_arg_uint(ctx, argc, argv, 0, &pin)) return JS_EXCEPTION;
    if (kairo_arg_bytes(ctx, argc, argv, 1, &grb, &len, &ref)) return JS_EXCEPTION;
    if (hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT) != 0) {
        kairo_release_bytes(ctx, &ref);
        return kairo_throw(ctx, "ws2812: pin %u is not available", pin);
    }
    uint32_t state = kairo_critical_enter();
    for (size_t i = 0; i < len; i++) send_byte(pin, grb[i]);
    kairo_critical_exit(state);
    hal_gpio_write(pin, false);
    kairo_delay_us(80);
    kairo_release_bytes(ctx, &ref);
    return kairo_int(ctx, (int32_t)(len / 3));
}

KAIRO_NATIVE_MODULE(ws2812)
{
    kairo_export(m, "show", show, 2);
}

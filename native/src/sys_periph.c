#include "rt_internal.h"
#include "kairo/hal.h"
#include "kairo/loop.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static JSValue ok_or_throw(JSContext *ctx, const char *what, int rc)
{
    if (rc < 0) return rt_throw_hal(ctx, what, rc);
    return JS_UNDEFINED;
}

static JSValue wifi_connect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "wifi_connect(ssid, password?)");
    const char *ssid = JS_ToCString(ctx, argv[0]);
    if (!ssid) return JS_EXCEPTION;
    const char *pass = argc > 1 && !JS_IsUndefined(argv[1]) ? JS_ToCString(ctx, argv[1]) : NULL;
    int rc = hal_wifi_connect(ssid, pass ? pass : "");
    JS_FreeCString(ctx, ssid);
    if (pass) JS_FreeCString(ctx, pass);
    return ok_or_throw(ctx, "wifi_connect", rc);
}

static JSValue wifi_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    int rc = hal_wifi_status();
    if (rc < 0) return rt_throw_hal(ctx, "wifi_status", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue wifi_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ok_or_throw(ctx, "wifi_disconnect", hal_wifi_disconnect());
}

static JSValue wifi_scan_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ok_or_throw(ctx, "wifi_scan", hal_wifi_scan_start());
}

static JSValue wifi_scan_results(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    hal_wifi_ap_t aps[24];
    int n = hal_wifi_scan_results(aps, 24);
    if (n == HAL_EAGAIN) return JS_UNDEFINED;
    if (n < 0) return rt_throw_hal(ctx, "wifi_scan", n);
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "ssid", JS_NewString(ctx, aps[i].ssid));
        JS_SetPropertyStr(ctx, o, "rssi", JS_NewInt32(ctx, aps[i].rssi));
        JS_SetPropertyStr(ctx, o, "channel", JS_NewInt32(ctx, aps[i].channel));
        JS_SetPropertyStr(ctx, o, "secure", JS_NewBool(ctx, aps[i].secure));
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
    }
    return arr;
}

static JSValue wifi_rssi(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, hal_wifi_rssi());
}

static JSValue ble_init(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ok_or_throw(ctx, "ble_init", hal_ble_init());
}

static JSValue ble_scan_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t ms = 5000;
    if (argc > 0 && rt_arg_int(ctx, argv[0], &ms)) return JS_EXCEPTION;
    return ok_or_throw(ctx, "ble_scan", hal_ble_scan_start((uint32_t)ms));
}

static JSValue ble_scan_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ok_or_throw(ctx, "ble_scan_stop", hal_ble_scan_stop());
}

static JSValue ble_scan_results(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    hal_ble_device_t devs[32];
    bool done = false;
    int n = hal_ble_scan_results(devs, 32, &done);
    if (n < 0) return rt_throw_hal(ctx, "ble_scan", n);
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "addr", JS_NewString(ctx, devs[i].addr));
        JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, devs[i].name));
        JS_SetPropertyStr(ctx, o, "rssi", JS_NewInt32(ctx, devs[i].rssi));
        JS_SetPropertyStr(ctx, o, "connectable", JS_NewBool(ctx, devs[i].connectable));
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, o);
    }
    JSValue r = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, r, "devices", arr);
    JS_SetPropertyStr(ctx, r, "done", JS_NewBool(ctx, done));
    return r;
}

static JSValue ble_connect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "ble_connect(addr)");
    const char *addr = JS_ToCString(ctx, argv[0]);
    if (!addr) return JS_EXCEPTION;
    int rc = hal_ble_connect(addr);
    JS_FreeCString(ctx, addr);
    if (rc < 0) return rt_throw_hal(ctx, "ble_connect", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue ble_conn_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t conn;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &conn)) return JS_ThrowTypeError(ctx, "ble_conn_status(conn)");
    int rc = hal_ble_conn_status(conn);
    if (rc < 0) return rt_throw_hal(ctx, "ble_conn_status", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue ble_pair(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t conn;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &conn)) return JS_ThrowTypeError(ctx, "ble_pair(conn)");
    return ok_or_throw(ctx, "ble_pair", hal_ble_pair(conn));
}

static JSValue ble_pair_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t conn;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &conn)) return JS_ThrowTypeError(ctx, "ble_pair_status(conn)");
    int rc = hal_ble_pair_status(conn);
    if (rc < 0) return rt_throw_hal(ctx, "ble_pair_status", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue ble_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t conn;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &conn)) return JS_ThrowTypeError(ctx, "ble_disconnect(conn)");
    return ok_or_throw(ctx, "ble_disconnect", hal_ble_disconnect(conn));
}

static JSValue uart_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t id, tx, rx, baud;
    if (argc < 4 || rt_arg_int(ctx, argv[0], &id) || rt_arg_int(ctx, argv[1], &tx) || rt_arg_int(ctx, argv[2], &rx) || rt_arg_int(ctx, argv[3], &baud))
        return JS_ThrowTypeError(ctx, "uart_open(id, tx, rx, baud)");
    return ok_or_throw(ctx, "uart_open", hal_uart_open(id, tx, rx, (uint32_t)baud));
}

static JSValue uart_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t id;
    if (argc < 2 || rt_arg_int(ctx, argv[0], &id)) return JS_ThrowTypeError(ctx, "uart_write(id, data)");
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[1], &data, &len, &hold);
    if (kind < 0) return JS_EXCEPTION;
    int rc = hal_uart_write(id, data, len);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    if (rc < 0) return rt_throw_hal(ctx, "uart_write", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue uart_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t id, max = 512;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &id)) return JS_ThrowTypeError(ctx, "uart_read(id, max?)");
    if (argc > 1 && rt_arg_int(ctx, argv[1], &max)) return JS_EXCEPTION;
    if (max <= 0 || max > 4096) max = 512;
    uint8_t buf[4096];
    int rc = hal_uart_read(id, buf, (size_t)max);
    if (rc == HAL_EAGAIN || rc == 0) return JS_UNDEFINED;
    if (rc < 0) return rt_throw_hal(ctx, "uart_read", rc);
    return rt_new_uint8array(ctx, buf, (size_t)rc);
}

static JSValue uart_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t id;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &id)) return JS_ThrowTypeError(ctx, "uart_close(id)");
    return ok_or_throw(ctx, "uart_close", hal_uart_close(id));
}

static JSValue adc_read_mv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t ch;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &ch)) return JS_ThrowTypeError(ctx, "adc_read_mv(channel)");
    int rc = hal_adc_read_mv(ch);
    if (rc < 0) return rt_throw_hal(ctx, "adc_read", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue pwm_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin, freq;
    double duty;
    if (argc < 3 || rt_arg_int(ctx, argv[0], &pin) || rt_arg_int(ctx, argv[1], &freq) || JS_ToFloat64(ctx, &duty, argv[2]))
        return JS_ThrowTypeError(ctx, "pwm_set(pin, freqHz, duty)");
    if (duty < 0) duty = 0;
    if (duty > 1) duty = 1;
    return ok_or_throw(ctx, "pwm_set", hal_pwm_set((uint32_t)pin, (uint32_t)freq, (float)duty));
}

static JSValue pwm_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &pin)) return JS_ThrowTypeError(ctx, "pwm_stop(pin)");
    return ok_or_throw(ctx, "pwm_stop", hal_pwm_stop((uint32_t)pin));
}

#define GPIO_WATCH_MAX 16

static struct {
    bool used;
    uint32_t pin;
    JSValue cb;
} s_gpio_watch[GPIO_WATCH_MAX];
static int s_gpio_watch_count;

static void gpio_events_hook(void)
{
    hal_gpio_event_t ev[16];
    int n = hal_gpio_poll_events(ev, 16);
    if (n <= 0) return;
    JSContext *ctx = g_rt.ctx;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < GPIO_WATCH_MAX; j++) {
            if (!s_gpio_watch[j].used || s_gpio_watch[j].pin != ev[i].pin) continue;
            JSValue cb = JS_DupValue(ctx, s_gpio_watch[j].cb);
            JSValue args[2] = { JS_NewInt32(ctx, ev[i].level), JS_NewFloat64(ctx, (double)ev[i].time_us) };
            JSValue r = JS_Call(ctx, cb, JS_UNDEFINED, 2, args);
            if (JS_IsException(r)) rt_dump_exception(ctx);
            JS_FreeValue(ctx, r);
            JS_FreeValue(ctx, cb);
            rt_drain_jobs();
            break;
        }
    }
}

static JSValue gpio_watch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin, edge;
    if (argc < 3 || rt_arg_int(ctx, argv[0], &pin) || rt_arg_int(ctx, argv[1], &edge) || !JS_IsFunction(ctx, argv[2]))
        return JS_ThrowTypeError(ctx, "gpio_watch(pin, edge, cb)");
    int slot = -1;
    for (int i = 0; i < GPIO_WATCH_MAX; i++) {
        if (s_gpio_watch[i].used && s_gpio_watch[i].pin == (uint32_t)pin) {
            JS_FreeValue(ctx, s_gpio_watch[i].cb);
            s_gpio_watch[i].cb = JS_DupValue(ctx, argv[2]);
            return ok_or_throw(ctx, "gpio_watch", hal_gpio_watch((uint32_t)pin, edge));
        }
        if (!s_gpio_watch[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return JS_ThrowInternalError(ctx, "gpio_watch: too many watched pins (max %d)", GPIO_WATCH_MAX);
    int rc = hal_gpio_watch((uint32_t)pin, edge);
    if (rc < 0) return rt_throw_hal(ctx, "gpio_watch", rc);
    s_gpio_watch[slot].used = true;
    s_gpio_watch[slot].pin = (uint32_t)pin;
    s_gpio_watch[slot].cb = JS_DupValue(ctx, argv[2]);
    s_gpio_watch_count++;
    k_add_idle_hook(gpio_events_hook);
    return JS_UNDEFINED;
}

static JSValue gpio_unwatch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &pin)) return JS_ThrowTypeError(ctx, "gpio_unwatch(pin)");
    for (int i = 0; i < GPIO_WATCH_MAX; i++) {
        if (!s_gpio_watch[i].used || s_gpio_watch[i].pin != (uint32_t)pin) continue;
        JS_FreeValue(ctx, s_gpio_watch[i].cb);
        s_gpio_watch[i].used = false;
        s_gpio_watch_count--;
    }
    hal_gpio_unwatch((uint32_t)pin);
    if (s_gpio_watch_count == 0) k_remove_idle_hook(gpio_events_hook);
    return JS_UNDEFINED;
}

static JSValue gpio_pulse_in(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin, timeout = 1000000;
    if (argc < 2 || rt_arg_int(ctx, argv[0], &pin)) return JS_ThrowTypeError(ctx, "gpio_pulse_in(pin, level, timeoutUs?)");
    bool level = JS_ToBool(ctx, argv[1]);
    if (argc > 2 && rt_arg_int(ctx, argv[2], &timeout)) return JS_EXCEPTION;
    if (timeout > 2000000) timeout = 2000000;
    int64_t r = hal_gpio_pulse_in((uint32_t)pin, level, (uint32_t)timeout);
    if (r == HAL_ENOSYS) return rt_throw_hal(ctx, "gpio_pulse_in", HAL_ENOSYS);
    if (r < 0) return JS_NewInt32(ctx, 0);
    return JS_NewFloat64(ctx, (double)r);
}

static JSValue i2c_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus, sda, scl, freq = 100000;
    if (argc < 3 || rt_arg_int(ctx, argv[0], &bus) || rt_arg_int(ctx, argv[1], &sda) || rt_arg_int(ctx, argv[2], &scl))
        return JS_ThrowTypeError(ctx, "i2c_open(bus, sda, scl, freqHz?)");
    if (argc > 3 && rt_arg_int(ctx, argv[3], &freq)) return JS_EXCEPTION;
    return ok_or_throw(ctx, "i2c_open", hal_i2c_open(bus, sda, scl, (uint32_t)freq));
}

static JSValue i2c_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus, addr;
    if (argc < 3 || rt_arg_int(ctx, argv[0], &bus) || rt_arg_int(ctx, argv[1], &addr)) return JS_ThrowTypeError(ctx, "i2c_write(bus, addr, data)");
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[2], &data, &len, &hold);
    if (kind < 0) return JS_EXCEPTION;
    int rc = hal_i2c_write(bus, (uint8_t)addr, data, len);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    if (rc == HAL_ECLOSED) return JS_FALSE;
    if (rc < 0) return rt_throw_hal(ctx, "i2c_write", rc);
    return JS_TRUE;
}

static JSValue i2c_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus, addr, n;
    if (argc < 3 || rt_arg_int(ctx, argv[0], &bus) || rt_arg_int(ctx, argv[1], &addr) || rt_arg_int(ctx, argv[2], &n))
        return JS_ThrowTypeError(ctx, "i2c_read(bus, addr, length)");
    if (n <= 0 || n > 1024) return JS_ThrowRangeError(ctx, "i2c_read: bad length");
    uint8_t buf[1024];
    int rc = hal_i2c_read(bus, (uint8_t)addr, buf, (size_t)n);
    if (rc == HAL_ECLOSED) return JS_NULL;
    if (rc < 0) return rt_throw_hal(ctx, "i2c_read", rc);
    return rt_new_uint8array(ctx, buf, (size_t)n);
}

static JSValue i2c_write_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus, addr, n;
    if (argc < 4 || rt_arg_int(ctx, argv[0], &bus) || rt_arg_int(ctx, argv[1], &addr) || rt_arg_int(ctx, argv[3], &n))
        return JS_ThrowTypeError(ctx, "i2c_write_read(bus, addr, data, length)");
    if (n <= 0 || n > 1024) return JS_ThrowRangeError(ctx, "i2c_write_read: bad length");
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[2], &data, &len, &hold);
    if (kind < 0) return JS_EXCEPTION;
    uint8_t buf[1024];
    int rc = hal_i2c_write_read(bus, (uint8_t)addr, data, len, buf, (size_t)n);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    if (rc == HAL_ECLOSED) return JS_NULL;
    if (rc < 0) return rt_throw_hal(ctx, "i2c_write_read", rc);
    return rt_new_uint8array(ctx, buf, (size_t)n);
}

static JSValue i2c_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &bus)) return JS_ThrowTypeError(ctx, "i2c_close(bus)");
    return ok_or_throw(ctx, "i2c_close", hal_i2c_close(bus));
}

static JSValue spi_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus, sck, mosi, miso, freq = 1000000, mode = 0;
    if (argc < 4 || rt_arg_int(ctx, argv[0], &bus) || rt_arg_int(ctx, argv[1], &sck) || rt_arg_int(ctx, argv[2], &mosi) || rt_arg_int(ctx, argv[3], &miso))
        return JS_ThrowTypeError(ctx, "spi_open(bus, sck, mosi, miso, freqHz?, mode?)");
    if (argc > 4 && rt_arg_int(ctx, argv[4], &freq)) return JS_EXCEPTION;
    if (argc > 5 && rt_arg_int(ctx, argv[5], &mode)) return JS_EXCEPTION;
    return ok_or_throw(ctx, "spi_open", hal_spi_open(bus, sck, mosi, miso, (uint32_t)freq, mode & 3));
}

static JSValue spi_transfer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus;
    if (argc < 2 || rt_arg_int(ctx, argv[0], &bus)) return JS_ThrowTypeError(ctx, "spi_transfer(bus, data)");
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[1], &data, &len, &hold);
    if (kind < 0) return JS_EXCEPTION;
    if (len > 4096) {
        if (kind == 1) JS_FreeCString(ctx, (const char *)data);
        JS_FreeValue(ctx, hold);
        return JS_ThrowRangeError(ctx, "spi_transfer: max 4096 bytes");
    }
    uint8_t *rx = malloc(len ? len : 1);
    if (!rx) return JS_ThrowOutOfMemory(ctx);
    int rc = hal_spi_transfer(bus, data, rx, len);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    JSValue r = rc < 0 ? rt_throw_hal(ctx, "spi_transfer", rc) : rt_new_uint8array(ctx, rx, len);
    free(rx);
    return r;
}

static JSValue spi_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t bus;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &bus)) return JS_ThrowTypeError(ctx, "spi_close(bus)");
    return ok_or_throw(ctx, "spi_close", hal_spi_close(bus));
}

static JSValue kv_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "kv_get(key)");
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    uint8_t buf[4096];
    int rc = hal_kv_get(key, buf, sizeof buf);
    JS_FreeCString(ctx, key);
    if (rc == HAL_ECLOSED) return JS_NULL;
    if (rc < 0) return rt_throw_hal(ctx, "kv_get", rc);
    return rt_new_uint8array(ctx, buf, (size_t)rc);
}

static JSValue kv_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "kv_set(key, data)");
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[1], &data, &len, &hold);
    if (kind < 0) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }
    int rc = len > 4096 ? HAL_ERR : hal_kv_set(key, data, len);
    JS_FreeCString(ctx, key);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    return ok_or_throw(ctx, "kv_set", rc);
}

static JSValue kv_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "kv_delete(key)");
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    int rc = hal_kv_delete(key);
    JS_FreeCString(ctx, key);
    if (rc == HAL_ECLOSED) return JS_FALSE;
    if (rc < 0) return rt_throw_hal(ctx, "kv_delete", rc);
    return JS_TRUE;
}

static JSValue kv_keys(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    char buf[2048];
    int rc = hal_kv_keys(buf, sizeof buf);
    if (rc < 0) return rt_throw_hal(ctx, "kv_keys", rc);
    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    for (char *p = buf; p < buf + rc && *p; p += strlen(p) + 1) JS_SetPropertyUint32(ctx, arr, idx++, JS_NewString(ctx, p));
    return arr;
}

static JSValue deep_sleep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    double ms;
    if (argc < 1 || JS_ToFloat64(ctx, &ms, argv[0])) return JS_ThrowTypeError(ctx, "deep_sleep(ms)");
    if (ms < 0) ms = 0;
    return ok_or_throw(ctx, "deep_sleep", hal_deep_sleep((uint64_t)(ms * 1000.0)));
}

static JSValue reboot(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    hal_reboot();
    return JS_UNDEFINED;
}

void sys_periph_release(JSContext *ctx)
{
    for (int i = 0; i < GPIO_WATCH_MAX; i++) {
        if (!s_gpio_watch[i].used) continue;
        JS_FreeValue(ctx, s_gpio_watch[i].cb);
        s_gpio_watch[i].used = false;
    }
    s_gpio_watch_count = 0;
    k_remove_idle_hook(gpio_events_hook);
}

const JSCFunctionListEntry sys_periph_funcs[] = {
    JS_CFUNC_DEF("pwm_set", 3, pwm_set),
    JS_CFUNC_DEF("pwm_stop", 1, pwm_stop),
    JS_CFUNC_DEF("gpio_watch", 3, gpio_watch),
    JS_CFUNC_DEF("gpio_unwatch", 1, gpio_unwatch),
    JS_CFUNC_DEF("gpio_pulse_in", 3, gpio_pulse_in),
    JS_CFUNC_DEF("i2c_open", 4, i2c_open),
    JS_CFUNC_DEF("i2c_write", 3, i2c_write),
    JS_CFUNC_DEF("i2c_read", 3, i2c_read),
    JS_CFUNC_DEF("i2c_write_read", 4, i2c_write_read),
    JS_CFUNC_DEF("i2c_close", 1, i2c_close),
    JS_CFUNC_DEF("spi_open", 6, spi_open),
    JS_CFUNC_DEF("spi_transfer", 2, spi_transfer),
    JS_CFUNC_DEF("spi_close", 1, spi_close),
    JS_CFUNC_DEF("kv_get", 1, kv_get),
    JS_CFUNC_DEF("kv_set", 2, kv_set),
    JS_CFUNC_DEF("kv_delete", 1, kv_delete),
    JS_CFUNC_DEF("kv_keys", 0, kv_keys),
    JS_CFUNC_DEF("deep_sleep", 1, deep_sleep),
    JS_CFUNC_DEF("reboot", 0, reboot),
    JS_CFUNC_DEF("wifi_connect", 2, wifi_connect),
    JS_CFUNC_DEF("wifi_status", 0, wifi_status),
    JS_CFUNC_DEF("wifi_disconnect", 0, wifi_disconnect),
    JS_CFUNC_DEF("wifi_scan_start", 0, wifi_scan_start),
    JS_CFUNC_DEF("wifi_scan_results", 0, wifi_scan_results),
    JS_CFUNC_DEF("wifi_rssi", 0, wifi_rssi),
    JS_CFUNC_DEF("ble_init", 0, ble_init),
    JS_CFUNC_DEF("ble_scan_start", 1, ble_scan_start),
    JS_CFUNC_DEF("ble_scan_stop", 0, ble_scan_stop),
    JS_CFUNC_DEF("ble_scan_results", 0, ble_scan_results),
    JS_CFUNC_DEF("ble_connect", 1, ble_connect),
    JS_CFUNC_DEF("ble_conn_status", 1, ble_conn_status),
    JS_CFUNC_DEF("ble_pair", 1, ble_pair),
    JS_CFUNC_DEF("ble_pair_status", 1, ble_pair_status),
    JS_CFUNC_DEF("ble_disconnect", 1, ble_disconnect),
    JS_CFUNC_DEF("uart_open", 4, uart_open),
    JS_CFUNC_DEF("uart_write", 2, uart_write),
    JS_CFUNC_DEF("uart_read", 2, uart_read),
    JS_CFUNC_DEF("uart_close", 1, uart_close),
    JS_CFUNC_DEF("adc_read_mv", 1, adc_read_mv),
};
const int sys_periph_funcs_count = (int)(sizeof sys_periph_funcs / sizeof sys_periph_funcs[0]);

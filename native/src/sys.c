#include "rt_internal.h"
#include "kairo/hal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARG_INT(ctx, out, v)                                      \
    do {                                                          \
        if (rt_arg_int((ctx), (v), &(out))) return JS_EXCEPTION; \
    } while (0)

static JSValue sys_gpio_mode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin, mode;
    if (argc < 2) return JS_ThrowTypeError(ctx, "gpio_mode(pin, mode)");
    ARG_INT(ctx, pin, argv[0]);
    ARG_INT(ctx, mode, argv[1]);
    if (mode < 0 || mode > 3) return JS_ThrowRangeError(ctx, "gpio_mode: invalid mode %d", mode);
    if (hal_gpio_set_mode((uint32_t)pin, (hal_gpio_mode_t)mode) != 0)
        return JS_ThrowRangeError(ctx, "gpio: pin %d is not available on this board", pin);
    return JS_UNDEFINED;
}

static JSValue sys_gpio_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin;
    if (argc < 2) return JS_ThrowTypeError(ctx, "gpio_write(pin, level)");
    ARG_INT(ctx, pin, argv[0]);
    bool level = JS_ToBool(ctx, argv[1]);
    if (hal_gpio_write((uint32_t)pin, level) != 0)
        return JS_ThrowRangeError(ctx, "gpio: pin %d is not available on this board", pin);
    return JS_UNDEFINED;
}

static JSValue sys_gpio_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t pin;
    if (argc < 1) return JS_ThrowTypeError(ctx, "gpio_read(pin)");
    ARG_INT(ctx, pin, argv[0]);
    int r = hal_gpio_read((uint32_t)pin);
    if (r < 0) return JS_ThrowRangeError(ctx, "gpio: pin %d is not available on this board", pin);
    return JS_NewInt32(ctx, r);
}

static JSValue sys_time_us(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(ctx, (double)hal_time_us());
}

static void timer_fire(void *arg)
{
    k_timer_t id = (k_timer_t)(uintptr_t)arg;
    int slot = k_timer_slot(id);
    if (slot < 0) return;
    JSContext *ctx = g_rt.ctx;

    JSValue fn = JS_DupValue(ctx, g_rt.timer_fn[slot]);
    if (!k_timer_active(id)) {

        JS_FreeValue(ctx, g_rt.timer_fn[slot]);
        g_rt.timer_fn[slot] = JS_UNDEFINED;
    }
    if (JS_IsFunction(ctx, fn)) {
        JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 0, NULL);
        if (JS_IsException(ret)) rt_dump_exception(ctx);
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, fn);
    rt_drain_jobs();
}

static JSValue sys_timer_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    double delay_ms, period_ms;
    if (argc < 3 || !JS_IsFunction(ctx, argv[2]))
        return JS_ThrowTypeError(ctx, "timer_start(delayMs, periodMs, fn)");
    if (JS_ToFloat64(ctx, &delay_ms, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &period_ms, argv[1])) return JS_EXCEPTION;
    if (delay_ms < 0) delay_ms = 0;
    if (period_ms < 0) period_ms = 0;

    k_timer_t id = k_timer_add((uint64_t)(delay_ms * 1000.0), (uint64_t)(period_ms * 1000.0), timer_fire, NULL);
    if (id == K_TIMER_INVALID) return JS_ThrowInternalError(ctx, "timer_start: no free timer slots (max %d)", K_MAX_TIMERS);
    int slot = k_timer_slot(id);

    g_rt.timer_fn[slot] = JS_DupValue(ctx, argv[2]);

    k_timer_set_arg(id, (void *)(uintptr_t)id);
    return JS_NewFloat64(ctx, (double)id);
}

static JSValue sys_timer_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    double idf;
    if (argc < 1) return JS_ThrowTypeError(ctx, "timer_cancel(id)");
    if (JS_ToFloat64(ctx, &idf, argv[0])) return JS_EXCEPTION;
    k_timer_t id = (k_timer_t)idf;
    if (k_timer_cancel(id)) {
        int slot = k_timer_slot(id);
        JS_FreeValue(ctx, g_rt.timer_fn[slot]);
        g_rt.timer_fn[slot] = JS_UNDEFINED;
        return JS_TRUE;
    }
    return JS_FALSE;
}

static JSValue sys_console_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    size_t len;
    const char *s = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!s) return JS_EXCEPTION;
    hal_console_write(s, len);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue sys_halt(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    k_stop();
    return JS_UNDEFINED;
}

static JSValue sys_board(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewString(ctx, hal_board_name());
}

static JSValue sys_random_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t n;
    if (argc < 1) return JS_ThrowTypeError(ctx, "random_bytes(n)");
    ARG_INT(ctx, n, argv[0]);
    if (n < 0 || n > 65536) return JS_ThrowRangeError(ctx, "random_bytes: bad length");
    uint8_t buf[256];
    uint8_t *p = (size_t)n <= sizeof buf ? buf : malloc((size_t)n);
    if (!p) return JS_ThrowOutOfMemory(ctx);
    int rc = hal_random_bytes(p, (size_t)n);
    JSValue r = rc == 0 ? rt_new_uint8array(ctx, p, (size_t)n) : rt_throw_hal(ctx, "random_bytes", rc);
    if (p != buf) free(p);
    return r;
}

static JSValue sys_rtc_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(ctx, (double)hal_rtc_get_ms());
}

static JSValue sys_rtc_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    double ms;
    if (argc < 1 || JS_ToFloat64(ctx, &ms, argv[0])) return JS_ThrowTypeError(ctx, "rtc_set(epochMs)");
    int rc = hal_rtc_set_ms((int64_t)ms);
    if (rc != 0) return rt_throw_hal(ctx, "rtc_set", rc);
    return JS_UNDEFINED;
}

JSValue rt_throw_hal(JSContext *ctx, const char *what, int rc)
{
    switch (rc) {
    case HAL_ENOSYS: return JS_ThrowReferenceError(ctx, "%s is not supported on board '%s'", what, hal_board_name());
    case HAL_ECLOSED: return JS_ThrowReferenceError(ctx, "%s: closed", what);
    case HAL_EAGAIN: return JS_ThrowInternalError(ctx, "%s: would block", what);
    default: return JS_ThrowInternalError(ctx, "%s failed (%d)", what, rc);
    }
}

JSValue rt_new_uint8array(JSContext *ctx, const void *data, size_t len)
{
    JSValue ab = JS_NewArrayBufferCopy(ctx, data, len);
    if (JS_IsException(ab)) return ab;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, g, "Uint8Array");
    JS_FreeValue(ctx, g);
    JSValue arr = JS_CallConstructor(ctx, ctor, 1, &ab);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, ab);
    return arr;
}

int rt_get_bytes(JSContext *ctx, JSValueConst v, const uint8_t **data, size_t *len, JSValue *hold)
{
    *hold = JS_UNDEFINED;
    if (JS_IsString(v)) {
        const char *s = JS_ToCStringLen(ctx, len, v);
        if (!s) return -1;
        *data = (const uint8_t *)s;
        *hold = JS_DupValue(ctx, v);
        return 1;
    }
    size_t off, blen, elem;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, v, &off, &blen, &elem);
    if (!JS_IsException(ab)) {
        uint8_t *p = JS_GetArrayBuffer(ctx, &blen, ab);
        *data = p + off;
        *len = blen;
        *hold = ab;
        return 0;
    }
    JS_FreeValue(ctx, JS_GetException(ctx));
    uint8_t *p = JS_GetArrayBuffer(ctx, len, v);
    if (p) {
        *data = p;
        return 0;
    }
    JS_FreeValue(ctx, JS_GetException(ctx));
    JS_ThrowTypeError(ctx, "expected string, Uint8Array or ArrayBuffer");
    return -1;
}

static const JSCFunctionListEntry sys_funcs[] = {
    JS_CFUNC_DEF("gpio_mode", 2, sys_gpio_mode),
    JS_CFUNC_DEF("gpio_write", 2, sys_gpio_write),
    JS_CFUNC_DEF("gpio_read", 1, sys_gpio_read),
    JS_CFUNC_DEF("time_us", 0, sys_time_us),
    JS_CFUNC_DEF("timer_start", 3, sys_timer_start),
    JS_CFUNC_DEF("timer_cancel", 1, sys_timer_cancel),
    JS_CFUNC_DEF("console_write", 1, sys_console_write),
    JS_CFUNC_DEF("halt", 0, sys_halt),
    JS_CFUNC_DEF("board", 0, sys_board),
    JS_CFUNC_DEF("random_bytes", 1, sys_random_bytes),
    JS_CFUNC_DEF("rtc_get", 0, sys_rtc_get),
    JS_CFUNC_DEF("rtc_set", 1, sys_rtc_set),
};

static int sys_module_init(JSContext *ctx, JSModuleDef *m)
{
    if (JS_SetModuleExportList(ctx, m, sys_funcs, (int)(sizeof sys_funcs / sizeof sys_funcs[0]))) return -1;
    if (JS_SetModuleExportList(ctx, m, sys_net_funcs, sys_net_funcs_count)) return -1;
    return JS_SetModuleExportList(ctx, m, sys_periph_funcs, sys_periph_funcs_count);
}

JSModuleDef *rt_init_sys_module(JSContext *ctx)
{
    JSModuleDef *m = JS_NewCModule(ctx, "kairo:sys", sys_module_init);
    if (!m) return NULL;
    JS_AddModuleExportList(ctx, m, sys_funcs, (int)(sizeof sys_funcs / sizeof sys_funcs[0]));
    JS_AddModuleExportList(ctx, m, sys_net_funcs, sys_net_funcs_count);
    JS_AddModuleExportList(ctx, m, sys_periph_funcs, sys_periph_funcs_count);
    return m;
}

void rt_sys_release(void)
{
    if (!g_rt.ctx) return;
    sys_net_release(g_rt.ctx);
    sys_periph_release(g_rt.ctx);
    for (int i = 0; i < K_MAX_TIMERS; i++) {
        JS_FreeValue(g_rt.ctx, g_rt.timer_fn[i]);
        g_rt.timer_fn[i] = JS_UNDEFINED;
    }
}

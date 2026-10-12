#ifndef KAIRO_RT_INTERNAL_H
#define KAIRO_RT_INTERNAL_H

#include "quickjs.h"
#include "kairo/loop.h"

typedef struct {
    JSRuntime *rt;
    JSContext *ctx;
    JSValue timer_fn[K_MAX_TIMERS];
    int error_count;
} rt_state_t;

extern rt_state_t g_rt;

void rt_dump_exception(JSContext *ctx);
void rt_dump_value_as_error(JSContext *ctx, JSValueConst exc, const char *prefix);
void rt_drain_jobs(void);
void rt_console_puts(const char *s);

JSModuleDef *rt_init_sys_module(JSContext *ctx);
int rt_init_native_modules(JSContext *ctx);
void rt_sys_release(void);

extern const JSCFunctionListEntry sys_net_funcs[];
extern const int sys_net_funcs_count;
void sys_net_release(JSContext *ctx);

extern const JSCFunctionListEntry sys_periph_funcs[];
extern const int sys_periph_funcs_count;
void sys_periph_release(JSContext *ctx);

JSValue rt_throw_hal(JSContext *ctx, const char *what, int rc);
static inline int rt_arg_int(JSContext *ctx, JSValueConst v, int32_t *out)
{
    return JS_ToInt32(ctx, out, v);
}
JSValue rt_new_uint8array(JSContext *ctx, const void *data, size_t len);
int rt_get_bytes(JSContext *ctx, JSValueConst v, const uint8_t **data, size_t *len, JSValue *hold);

#endif

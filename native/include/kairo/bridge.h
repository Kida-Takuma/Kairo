#ifndef KAIRO_BRIDGE_H
#define KAIRO_BRIDGE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "quickjs.h"
#include "kairo/hal.h"
#include "kairo/loop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kairo_module kairo_module_t;
typedef JSValue (*kairo_fn_t)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
typedef void (*kairo_module_init_t)(kairo_module_t *m);

#define KAIRO_NATIVE_MODULE(name) void kairo_native_init_##name(kairo_module_t *m)

void kairo_native_module(const char *name, kairo_module_init_t init);
void kairo_native_register_all(void);

void kairo_export(kairo_module_t *m, const char *name, kairo_fn_t fn, int nargs);
void kairo_export_number(kairo_module_t *m, const char *name, double value);
void kairo_export_string(kairo_module_t *m, const char *name, const char *value);

int kairo_arg_int(JSContext *ctx, int argc, JSValueConst *argv, int i, int32_t *out);
int kairo_arg_uint(JSContext *ctx, int argc, JSValueConst *argv, int i, uint32_t *out);
int kairo_arg_number(JSContext *ctx, int argc, JSValueConst *argv, int i, double *out);
int kairo_arg_bool(JSContext *ctx, int argc, JSValueConst *argv, int i, bool *out);
const char *kairo_arg_string(JSContext *ctx, int argc, JSValueConst *argv, int i);
void kairo_free_string(JSContext *ctx, const char *s);

typedef struct {
    JSValue hold;
    int kind;
    const uint8_t *data;
} kairo_bytes_t;

int kairo_arg_bytes(JSContext *ctx, int argc, JSValueConst *argv, int i, const uint8_t **data, size_t *len, kairo_bytes_t *ref);
void kairo_release_bytes(JSContext *ctx, kairo_bytes_t *ref);
bool kairo_arg_is_function(JSContext *ctx, int argc, JSValueConst *argv, int i);

JSValue kairo_undefined(void);
JSValue kairo_null(void);
JSValue kairo_bool(JSContext *ctx, bool v);
JSValue kairo_int(JSContext *ctx, int32_t v);
JSValue kairo_number(JSContext *ctx, double v);
JSValue kairo_string(JSContext *ctx, const char *s);
JSValue kairo_bytes(JSContext *ctx, const void *data, size_t len);
JSValue kairo_object(JSContext *ctx);
void kairo_set(JSContext *ctx, JSValue obj, const char *key, JSValue value);
JSValue kairo_array(JSContext *ctx);
void kairo_push(JSContext *ctx, JSValue arr, JSValue value);
JSValue kairo_throw(JSContext *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
JSValue kairo_throw_type(JSContext *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
JSValue kairo_throw_hal(JSContext *ctx, const char *what, int rc);

typedef struct kairo_promise kairo_promise_t;
JSValue kairo_promise_new(JSContext *ctx, kairo_promise_t **out);
void kairo_promise_resolve(kairo_promise_t *p, JSValue value);
void kairo_promise_reject(kairo_promise_t *p, JSValue error);
void kairo_promise_reject_msg(kairo_promise_t *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

typedef struct kairo_callback kairo_callback_t;
kairo_callback_t *kairo_callback_hold(JSContext *ctx, JSValueConst fn);
void kairo_callback_call(kairo_callback_t *cb, int argc, JSValue *argv);
void kairo_callback_release(kairo_callback_t *cb);
JSContext *kairo_callback_ctx(kairo_callback_t *cb);

typedef void (*kairo_event_fn_t)(uint32_t id, uint32_t a, uint32_t b, void *user);
bool kairo_event_post(uint32_t id, uint32_t a, uint32_t b);
int kairo_event_handler(uint32_t id, kairo_event_fn_t fn, void *user);
void kairo_event_handler_remove(uint32_t id);

JSContext *kairo_ctx(void);
uint64_t kairo_time_us(void);
void kairo_delay_us(uint32_t us);
uint32_t kairo_critical_enter(void);
void kairo_critical_exit(uint32_t state);

#ifdef __cplusplus
}
#endif

#endif

#include "kairo/bridge.h"
#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_MODULES_MAX 16
#define NATIVE_EXPORTS_MAX 48
#define EVENT_QUEUE 64
#define EVENT_HANDLERS 16

enum { EXPORT_FN, EXPORT_NUMBER, EXPORT_STRING };

struct kairo_module {
    const char *name;
    char qualified[64];
    kairo_module_init_t init;
    JSModuleDef *def;
    int count;
    struct {
        const char *name;
        int kind;
        kairo_fn_t fn;
        int nargs;
        double num;
        const char *str;
    } exports[NATIVE_EXPORTS_MAX];
};

static kairo_module_t s_modules[NATIVE_MODULES_MAX];
static int s_module_count;

__attribute__((weak)) void kairo_native_register_all(void)
{
}

void kairo_native_module(const char *name, kairo_module_init_t init)
{
    if (s_module_count >= NATIVE_MODULES_MAX) return;
    kairo_module_t *m = &s_modules[s_module_count++];
    memset(m, 0, sizeof *m);
    m->name = name;
    m->init = init;
    snprintf(m->qualified, sizeof m->qualified, "kairo:native/%s", name);
}

void kairo_export(kairo_module_t *m, const char *name, kairo_fn_t fn, int nargs)
{
    if (m->count >= NATIVE_EXPORTS_MAX) return;
    m->exports[m->count].name = name;
    m->exports[m->count].kind = EXPORT_FN;
    m->exports[m->count].fn = fn;
    m->exports[m->count].nargs = nargs;
    m->count++;
}

void kairo_export_number(kairo_module_t *m, const char *name, double value)
{
    if (m->count >= NATIVE_EXPORTS_MAX) return;
    m->exports[m->count].name = name;
    m->exports[m->count].kind = EXPORT_NUMBER;
    m->exports[m->count].num = value;
    m->count++;
}

void kairo_export_string(kairo_module_t *m, const char *name, const char *value)
{
    if (m->count >= NATIVE_EXPORTS_MAX) return;
    m->exports[m->count].name = name;
    m->exports[m->count].kind = EXPORT_STRING;
    m->exports[m->count].str = value;
    m->count++;
}

static int native_module_init(JSContext *ctx, JSModuleDef *def)
{
    for (int i = 0; i < s_module_count; i++) {
        kairo_module_t *m = &s_modules[i];
        if (m->def != def) continue;
        for (int j = 0; j < m->count; j++) {
            JSValue v;
            switch (m->exports[j].kind) {
            case EXPORT_FN: v = JS_NewCFunction(ctx, m->exports[j].fn, m->exports[j].name, m->exports[j].nargs); break;
            case EXPORT_NUMBER: v = JS_NewFloat64(ctx, m->exports[j].num); break;
            default: v = JS_NewString(ctx, m->exports[j].str); break;
            }
            if (JS_SetModuleExport(ctx, def, m->exports[j].name, v)) return -1;
        }
        return 0;
    }
    return -1;
}

int rt_init_native_modules(JSContext *ctx)
{
    s_module_count = 0;
    kairo_native_register_all();
    for (int i = 0; i < s_module_count; i++) {
        kairo_module_t *m = &s_modules[i];
        m->count = 0;
        m->init(m);
        m->def = JS_NewCModule(ctx, m->qualified, native_module_init);
        if (!m->def) return -1;
        for (int j = 0; j < m->count; j++) JS_AddModuleExport(ctx, m->def, m->exports[j].name);
    }
    return 0;
}

int kairo_arg_int(JSContext *ctx, int argc, JSValueConst *argv, int i, int32_t *out)
{
    if (i >= argc) {
        JS_ThrowTypeError(ctx, "argument %d is required", i + 1);
        return -1;
    }
    return JS_ToInt32(ctx, out, argv[i]) ? -1 : 0;
}

int kairo_arg_uint(JSContext *ctx, int argc, JSValueConst *argv, int i, uint32_t *out)
{
    int32_t v;
    if (kairo_arg_int(ctx, argc, argv, i, &v)) return -1;
    if (v < 0) {
        JS_ThrowRangeError(ctx, "argument %d must not be negative", i + 1);
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

int kairo_arg_number(JSContext *ctx, int argc, JSValueConst *argv, int i, double *out)
{
    if (i >= argc) {
        JS_ThrowTypeError(ctx, "argument %d is required", i + 1);
        return -1;
    }
    return JS_ToFloat64(ctx, out, argv[i]) ? -1 : 0;
}

int kairo_arg_bool(JSContext *ctx, int argc, JSValueConst *argv, int i, bool *out)
{
    if (i >= argc) {
        JS_ThrowTypeError(ctx, "argument %d is required", i + 1);
        return -1;
    }
    *out = JS_ToBool(ctx, argv[i]) > 0;
    return 0;
}

const char *kairo_arg_string(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
    if (i >= argc) {
        JS_ThrowTypeError(ctx, "argument %d is required", i + 1);
        return NULL;
    }
    return JS_ToCString(ctx, argv[i]);
}

void kairo_free_string(JSContext *ctx, const char *s)
{
    if (s) JS_FreeCString(ctx, s);
}

int kairo_arg_bytes(JSContext *ctx, int argc, JSValueConst *argv, int i, const uint8_t **data, size_t *len, kairo_bytes_t *ref)
{
    ref->hold = JS_UNDEFINED;
    ref->kind = -1;
    ref->data = NULL;
    if (i >= argc) {
        JS_ThrowTypeError(ctx, "argument %d is required", i + 1);
        return -1;
    }
    int kind = rt_get_bytes(ctx, argv[i], data, len, &ref->hold);
    if (kind < 0) return -1;
    ref->kind = kind;
    ref->data = *data;
    return 0;
}

void kairo_release_bytes(JSContext *ctx, kairo_bytes_t *ref)
{
    if (ref->kind == 1) JS_FreeCString(ctx, (const char *)ref->data);
    if (ref->kind >= 0) JS_FreeValue(ctx, ref->hold);
    ref->kind = -1;
}

bool kairo_arg_is_function(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
    return i < argc && JS_IsFunction(ctx, argv[i]);
}

JSValue kairo_undefined(void) { return JS_UNDEFINED; }
JSValue kairo_null(void) { return JS_NULL; }
JSValue kairo_bool(JSContext *ctx, bool v) { return JS_NewBool(ctx, v); }
JSValue kairo_int(JSContext *ctx, int32_t v) { return JS_NewInt32(ctx, v); }
JSValue kairo_number(JSContext *ctx, double v) { return JS_NewFloat64(ctx, v); }
JSValue kairo_string(JSContext *ctx, const char *s) { return JS_NewString(ctx, s ? s : ""); }
JSValue kairo_bytes(JSContext *ctx, const void *data, size_t len) { return rt_new_uint8array(ctx, data, len); }
JSValue kairo_object(JSContext *ctx) { return JS_NewObject(ctx); }
void kairo_set(JSContext *ctx, JSValue obj, const char *key, JSValue value) { JS_SetPropertyStr(ctx, obj, key, value); }
JSValue kairo_array(JSContext *ctx) { return JS_NewArray(ctx); }

void kairo_push(JSContext *ctx, JSValue arr, JSValue value)
{
    JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
    int32_t len = 0;
    JS_ToInt32(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);
    JS_SetPropertyUint32(ctx, arr, (uint32_t)len, value);
}

JSValue kairo_throw(JSContext *ctx, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return JS_ThrowInternalError(ctx, "%s", buf);
}

JSValue kairo_throw_type(JSContext *ctx, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return JS_ThrowTypeError(ctx, "%s", buf);
}

JSValue kairo_throw_hal(JSContext *ctx, const char *what, int rc)
{
    return rt_throw_hal(ctx, what, rc);
}

struct kairo_promise {
    JSContext *ctx;
    JSValue funcs[2];
};

JSValue kairo_promise_new(JSContext *ctx, kairo_promise_t **out)
{
    kairo_promise_t *p = calloc(1, sizeof *p);
    if (!p) return JS_ThrowOutOfMemory(ctx);
    p->ctx = ctx;
    JSValue promise = JS_NewPromiseCapability(ctx, p->funcs);
    if (JS_IsException(promise)) {
        free(p);
        return promise;
    }
    *out = p;
    return promise;
}

static void promise_settle(kairo_promise_t *p, int which, JSValue value)
{
    JSContext *ctx = p->ctx;
    JSValue r = JS_Call(ctx, p->funcs[which], JS_UNDEFINED, 1, &value);
    if (JS_IsException(r)) rt_dump_exception(ctx);
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, p->funcs[0]);
    JS_FreeValue(ctx, p->funcs[1]);
    free(p);
    rt_drain_jobs();
}

void kairo_promise_resolve(kairo_promise_t *p, JSValue value)
{
    promise_settle(p, 0, value);
}

void kairo_promise_reject(kairo_promise_t *p, JSValue error)
{
    promise_settle(p, 1, error);
}

void kairo_promise_reject_msg(kairo_promise_t *p, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    JSValue err = JS_NewError(p->ctx);
    JS_SetPropertyStr(p->ctx, err, "message", JS_NewString(p->ctx, buf));
    promise_settle(p, 1, err);
}

struct kairo_callback {
    JSContext *ctx;
    JSValue fn;
};

kairo_callback_t *kairo_callback_hold(JSContext *ctx, JSValueConst fn)
{
    kairo_callback_t *cb = calloc(1, sizeof *cb);
    if (!cb) return NULL;
    cb->ctx = ctx;
    cb->fn = JS_DupValue(ctx, fn);
    return cb;
}

void kairo_callback_call(kairo_callback_t *cb, int argc, JSValue *argv)
{
    if (!cb) return;
    JSContext *ctx = cb->ctx;
    JSValue r = JS_Call(ctx, cb->fn, JS_UNDEFINED, argc, argv);
    if (JS_IsException(r)) rt_dump_exception(ctx);
    JS_FreeValue(ctx, r);
    for (int i = 0; i < argc; i++) JS_FreeValue(ctx, argv[i]);
    rt_drain_jobs();
}

void kairo_callback_release(kairo_callback_t *cb)
{
    if (!cb) return;
    JS_FreeValue(cb->ctx, cb->fn);
    free(cb);
}

JSContext *kairo_callback_ctx(kairo_callback_t *cb)
{
    return cb ? cb->ctx : NULL;
}

typedef struct {
    uint32_t id, a, b;
} event_t;

static event_t s_events[EVENT_QUEUE];
static volatile uint32_t s_ev_head, s_ev_tail;
static struct {
    bool used;
    uint32_t id;
    kairo_event_fn_t fn;
    void *user;
} s_handlers[EVENT_HANDLERS];

static void events_hook(void)
{
    while (s_ev_head != s_ev_tail) {
        event_t e = s_events[s_ev_head];
        s_ev_head = (s_ev_head + 1) % EVENT_QUEUE;
        for (int i = 0; i < EVENT_HANDLERS; i++) {
            if (s_handlers[i].used && s_handlers[i].id == e.id) {
                s_handlers[i].fn(e.id, e.a, e.b, s_handlers[i].user);
                break;
            }
        }
    }
}

bool kairo_event_post(uint32_t id, uint32_t a, uint32_t b)
{
    uint32_t next = (s_ev_tail + 1) % EVENT_QUEUE;
    if (next == s_ev_head) return false;
    s_events[s_ev_tail].id = id;
    s_events[s_ev_tail].a = a;
    s_events[s_ev_tail].b = b;
    s_ev_tail = next;
    return true;
}

int kairo_event_handler(uint32_t id, kairo_event_fn_t fn, void *user)
{
    int slot = -1;
    for (int i = 0; i < EVENT_HANDLERS; i++) {
        if (s_handlers[i].used && s_handlers[i].id == id) {
            slot = i;
            break;
        }
        if (!s_handlers[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return -1;
    s_handlers[slot].used = true;
    s_handlers[slot].id = id;
    s_handlers[slot].fn = fn;
    s_handlers[slot].user = user;
    k_add_idle_hook(events_hook);
    return 0;
}

void kairo_event_handler_remove(uint32_t id)
{
    bool any = false;
    for (int i = 0; i < EVENT_HANDLERS; i++) {
        if (s_handlers[i].used && s_handlers[i].id == id) s_handlers[i].used = false;
        any |= s_handlers[i].used;
    }
    if (!any) k_remove_idle_hook(events_hook);
}

JSContext *kairo_ctx(void)
{
    return g_rt.ctx;
}

uint64_t kairo_time_us(void)
{
    return hal_time_us();
}

void kairo_delay_us(uint32_t us)
{
    hal_delay_us(us);
}

uint32_t kairo_critical_enter(void)
{
    return hal_critical_enter();
}

void kairo_critical_exit(uint32_t state)
{
    hal_critical_exit(state);
}

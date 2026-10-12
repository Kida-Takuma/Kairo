#include "rt_internal.h"
#include "kairo/rt.h"
#include "kairo/hal.h"

#include <stdio.h>
#include <string.h>

#ifndef KAIRO_JS_HEAP_BYTES
#define KAIRO_JS_HEAP_BYTES (1024 * 1024)
#endif

rt_state_t g_rt;
static size_t s_heap_limit = KAIRO_JS_HEAP_BYTES;
static size_t s_stack_limit;

void rt_set_heap_limit(size_t bytes)
{
    s_heap_limit = bytes;
}

void rt_set_stack_limit(size_t bytes)
{
    s_stack_limit = bytes;
}

static void rt_report_rejections(JSContext *ctx);

void rt_console_puts(const char *s)
{
    hal_console_write(s, strlen(s));
}

void rt_dump_value_as_error(JSContext *ctx, JSValueConst exc, const char *prefix)
{
    const char *msg = JS_ToCString(ctx, exc);
    char buf[512];
    snprintf(buf, sizeof buf, "%s%s\n", prefix, msg ? msg : "<unprintable>");
    rt_console_puts(buf);
    if (msg) JS_FreeCString(ctx, msg);

    if (JS_IsError(exc)) {
        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *s = JS_ToCString(ctx, stack);
            if (s) {
                rt_console_puts(s);
                JS_FreeCString(ctx, s);
            }
        }
        JS_FreeValue(ctx, stack);
    }
    g_rt.error_count++;
}

void rt_dump_exception(JSContext *ctx)
{
    JSValue exc = JS_GetException(ctx);
    if (JS_IsNull(exc) || JS_IsUninitialized(exc)) {

        char buf[160];
        snprintf(buf, sizeof buf, "Uncaught exception without an error object: JS heap exhausted? (limit %zu bytes)\n", s_heap_limit);
        rt_console_puts(buf);
        g_rt.error_count++;
    } else {
        rt_dump_value_as_error(ctx, exc, "Uncaught ");
    }
    JS_FreeValue(ctx, exc);
}

void rt_drain_jobs(void)
{
    JSContext *ctx;
    for (;;) {
        int r = JS_ExecutePendingJob(g_rt.rt, &ctx);
        if (r == 0) break;
        if (r < 0) rt_dump_exception(ctx);
    }
    rt_report_rejections(g_rt.ctx);
}

#define RT_MAX_PENDING_REJECTIONS 16
static struct {
    JSValue promise;
    JSValue reason;
} s_rejections[RT_MAX_PENDING_REJECTIONS];
static int s_rejection_count;

static void rt_rejection_tracker(JSContext *ctx, JSValueConst promise, JSValueConst reason,
                                 bool is_handled, void *opaque)
{
    (void)opaque;
    if (is_handled) {
        for (int i = 0; i < s_rejection_count; i++) {
            if (JS_VALUE_GET_PTR(s_rejections[i].promise) != JS_VALUE_GET_PTR(promise)) continue;
            JS_FreeValue(ctx, s_rejections[i].promise);
            JS_FreeValue(ctx, s_rejections[i].reason);
            s_rejections[i] = s_rejections[--s_rejection_count];
            return;
        }
        return;
    }
    if (s_rejection_count >= RT_MAX_PENDING_REJECTIONS) {
        rt_dump_value_as_error(ctx, reason, "Unhandled promise rejection: ");
        return;
    }
    s_rejections[s_rejection_count].promise = JS_DupValue(ctx, promise);
    s_rejections[s_rejection_count].reason = JS_DupValue(ctx, reason);
    s_rejection_count++;
}

static void rt_report_rejections(JSContext *ctx)
{
    for (int i = 0; i < s_rejection_count; i++) {
        rt_dump_value_as_error(ctx, s_rejections[i].reason, "Unhandled promise rejection: ");
        JS_FreeValue(ctx, s_rejections[i].promise);
        JS_FreeValue(ctx, s_rejections[i].reason);
    }
    s_rejection_count = 0;
}

static JSModuleDef *rt_module_loader(JSContext *ctx, const char *name, void *opaque)
{
    (void)opaque;

    JS_ThrowReferenceError(ctx, "module '%s' not found: bundle it with the kairo CLI, or add a native module under native/ if it starts with kairo:native/", name);
    return NULL;
}

static int eval_module(const char *src, size_t len, const char *name)
{
    JSContext *ctx = g_rt.ctx;
    JSValue ret = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_MODULE);
    if (JS_IsException(ret)) {
        rt_dump_exception(ctx);
        return -1;
    }
    if (JS_IsPromise(ret)) JS_PromiseMarkAsHandled(ctx, ret);
    rt_drain_jobs();
    int rc = 0;
    if (JS_IsPromise(ret) && JS_PromiseState(ctx, ret) == JS_PROMISE_REJECTED) {
        JSValue reason = JS_PromiseResult(ctx, ret);
        rt_dump_value_as_error(ctx, reason, "Uncaught (in module) ");
        JS_FreeValue(ctx, reason);
        rc = -1;
    }
    JS_FreeValue(ctx, ret);
    return rc;
}

int rt_boot(const char *app_src, size_t app_len, const char *app_name)
{
    memset(&g_rt, 0, sizeof g_rt);
    for (int i = 0; i < K_MAX_TIMERS; i++) g_rt.timer_fn[i] = JS_UNDEFINED;

    k_init();

    g_rt.rt = JS_NewRuntime();
    if (!g_rt.rt) return -1;
    JS_SetMemoryLimit(g_rt.rt, s_heap_limit);
    if (s_stack_limit) JS_SetMaxStackSize(g_rt.rt, s_stack_limit);
    JS_SetModuleLoaderFunc(g_rt.rt, NULL, rt_module_loader, NULL);
    JS_SetHostPromiseRejectionTracker(g_rt.rt, rt_rejection_tracker, NULL);

    g_rt.ctx = JS_NewContext(g_rt.rt);
    if (!g_rt.ctx) return -1;

    if (!rt_init_sys_module(g_rt.ctx)) return -1;
    if (rt_init_native_modules(g_rt.ctx) != 0) return -1;

    return eval_module(app_src, app_len, app_name);
}

void rt_run(void)
{
    k_run();
}

void rt_print_memory_stats(void)
{
    if (!g_rt.rt) return;
    JSMemoryUsage u;
    JS_ComputeMemoryUsage(g_rt.rt, &u);
    char buf[256];
    snprintf(buf, sizeof buf,
             "[js heap] used %lld bytes / limit %zu bytes (%lld objects, %lld functions, %lld strings)\n",
             (long long)u.memory_used_size, s_heap_limit, (long long)u.obj_count, (long long)u.js_func_count,
             (long long)u.str_count);
    rt_console_puts(buf);
}

int rt_shutdown(void)
{
    if (g_rt.ctx) rt_report_rejections(g_rt.ctx);
    rt_sys_release();
    if (g_rt.ctx) JS_FreeContext(g_rt.ctx);
    if (g_rt.rt) JS_FreeRuntime(g_rt.rt);
    g_rt.ctx = NULL;
    g_rt.rt = NULL;
    return g_rt.error_count ? 1 : 0;
}

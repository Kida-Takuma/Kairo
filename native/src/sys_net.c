#include "rt_internal.h"
#include "kairo/hal.h"
#include "kairo/loop.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NET_MAX_WATCH 16
#define NET_RECV_MAX 4096

typedef struct {
    int fd;
    int events;
    JSValue cb;
} net_watch_t;

static net_watch_t s_watch[NET_MAX_WATCH];
static int s_watch_count;

static JSValue net_socket(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t type = HAL_NET_TCP;
    if (argc > 0 && rt_arg_int(ctx, argv[0], &type)) return JS_EXCEPTION;
    int fd = hal_net_socket(type);
    if (fd < 0) return rt_throw_hal(ctx, "net_socket", fd);
    return JS_NewInt32(ctx, fd);
}

static JSValue connect_common(JSContext *ctx, int argc, JSValueConst *argv, bool tls)
{
    int32_t fd, port;
    if (argc < 3) return JS_ThrowTypeError(ctx, "net_connect(fd, host, port)");
    if (rt_arg_int(ctx, argv[0], &fd) || rt_arg_int(ctx, argv[2], &port)) return JS_EXCEPTION;
    const char *host = JS_ToCString(ctx, argv[1]);
    if (!host) return JS_EXCEPTION;
    int rc = tls ? hal_net_tls_connect(fd, host, (uint16_t)port) : hal_net_connect(fd, host, (uint16_t)port);
    JS_FreeCString(ctx, host);
    if (rc == HAL_EAGAIN) return JS_FALSE;
    if (rc < 0) return rt_throw_hal(ctx, tls ? "tls_connect" : "net_connect", rc);
    return JS_TRUE;
}

static JSValue net_connect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    return connect_common(ctx, argc, argv, false);
}

static JSValue net_tls_connect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    return connect_common(ctx, argc, argv, true);
}

static JSValue net_connect_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_connect_status(fd)");
    int rc = hal_net_connect_status(fd);
    if (rc == HAL_EAGAIN) return JS_FALSE;
    if (rc < 0) return rt_throw_hal(ctx, "net_connect", rc);
    return JS_TRUE;
}

static JSValue net_bind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd, port;
    if (argc < 2 || rt_arg_int(ctx, argv[0], &fd) || rt_arg_int(ctx, argv[1], &port)) return JS_ThrowTypeError(ctx, "net_bind(fd, port)");
    int rc = hal_net_bind(fd, (uint16_t)port);
    if (rc < 0) return rt_throw_hal(ctx, "net_bind", rc);
    return JS_UNDEFINED;
}

static JSValue net_listen(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd, port, backlog = 4;
    if (argc < 2 || rt_arg_int(ctx, argv[0], &fd) || rt_arg_int(ctx, argv[1], &port)) return JS_ThrowTypeError(ctx, "net_listen(fd, port, backlog?)");
    if (argc > 2 && rt_arg_int(ctx, argv[2], &backlog)) return JS_EXCEPTION;
    int rc = hal_net_listen(fd, (uint16_t)port, backlog);
    if (rc < 0) return rt_throw_hal(ctx, "net_listen", rc);
    return JS_UNDEFINED;
}

static JSValue net_accept(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_accept(fd)");
    char addr[48] = "";
    uint16_t port = 0;
    int nfd = hal_net_accept(fd, addr, sizeof addr, &port);
    if (nfd == HAL_EAGAIN) return JS_UNDEFINED;
    if (nfd < 0) return rt_throw_hal(ctx, "net_accept", nfd);
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "fd", JS_NewInt32(ctx, nfd));
    JS_SetPropertyStr(ctx, o, "addr", JS_NewString(ctx, addr));
    JS_SetPropertyStr(ctx, o, "port", JS_NewInt32(ctx, port));
    return o;
}

static JSValue net_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd;
    if (argc < 2 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_send(fd, data)");
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[1], &data, &len, &hold);
    if (kind < 0) return JS_EXCEPTION;
    int rc = hal_net_send(fd, data, len);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    if (rc == HAL_EAGAIN) return JS_NewInt32(ctx, 0);
    if (rc < 0) return rt_throw_hal(ctx, "net_send", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue net_recv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd, max = NET_RECV_MAX;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_recv(fd, max?)");
    if (argc > 1 && rt_arg_int(ctx, argv[1], &max)) return JS_EXCEPTION;
    if (max <= 0 || max > 65536) max = NET_RECV_MAX;
    uint8_t *buf = malloc((size_t)max);
    if (!buf) return JS_ThrowOutOfMemory(ctx);
    int rc = hal_net_recv(fd, buf, (size_t)max);
    JSValue r;
    if (rc == HAL_EAGAIN) r = JS_UNDEFINED;
    else if (rc == 0 || rc == HAL_ECLOSED) r = JS_NULL;
    else if (rc < 0) r = rt_throw_hal(ctx, "net_recv", rc);
    else r = rt_new_uint8array(ctx, buf, (size_t)rc);
    free(buf);
    return r;
}

static JSValue net_sendto(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd, port;
    if (argc < 4 || rt_arg_int(ctx, argv[0], &fd) || rt_arg_int(ctx, argv[2], &port)) return JS_ThrowTypeError(ctx, "net_sendto(fd, host, port, data)");
    const char *host = JS_ToCString(ctx, argv[1]);
    if (!host) return JS_EXCEPTION;
    const uint8_t *data;
    size_t len;
    JSValue hold;
    int kind = rt_get_bytes(ctx, argv[3], &data, &len, &hold);
    if (kind < 0) {
        JS_FreeCString(ctx, host);
        return JS_EXCEPTION;
    }
    int rc = hal_net_sendto(fd, host, (uint16_t)port, data, len);
    JS_FreeCString(ctx, host);
    if (kind == 1) JS_FreeCString(ctx, (const char *)data);
    JS_FreeValue(ctx, hold);
    if (rc == HAL_EAGAIN) return JS_NewInt32(ctx, 0);
    if (rc < 0) return rt_throw_hal(ctx, "net_sendto", rc);
    return JS_NewInt32(ctx, rc);
}

static JSValue net_recvfrom(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd, max = NET_RECV_MAX;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_recvfrom(fd, max?)");
    if (argc > 1 && rt_arg_int(ctx, argv[1], &max)) return JS_EXCEPTION;
    if (max <= 0 || max > 65536) max = NET_RECV_MAX;
    uint8_t *buf = malloc((size_t)max);
    if (!buf) return JS_ThrowOutOfMemory(ctx);
    char addr[48] = "";
    uint16_t port = 0;
    int rc = hal_net_recvfrom(fd, buf, (size_t)max, addr, sizeof addr, &port);
    if (rc == HAL_EAGAIN) {
        free(buf);
        return JS_UNDEFINED;
    }
    if (rc < 0) {
        free(buf);
        return rt_throw_hal(ctx, "net_recvfrom", rc);
    }
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "data", rt_new_uint8array(ctx, buf, (size_t)rc));
    JS_SetPropertyStr(ctx, o, "addr", JS_NewString(ctx, addr));
    JS_SetPropertyStr(ctx, o, "port", JS_NewInt32(ctx, port));
    free(buf);
    return o;
}

static void unwatch_fd(JSContext *ctx, int fd)
{
    for (int i = 0; i < s_watch_count; i++) {
        if (s_watch[i].fd != fd) continue;
        JS_FreeValue(ctx, s_watch[i].cb);
        s_watch[i] = s_watch[--s_watch_count];
        break;
    }
}

static void net_poll_hook(void);

static JSValue net_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_close(fd)");
    unwatch_fd(ctx, fd);
    if (s_watch_count == 0) k_remove_idle_hook(net_poll_hook);
    hal_net_close(fd);
    return JS_UNDEFINED;
}

static JSValue net_watch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd, events;
    if (argc < 3 || rt_arg_int(ctx, argv[0], &fd) || rt_arg_int(ctx, argv[1], &events) || !JS_IsFunction(ctx, argv[2]))
        return JS_ThrowTypeError(ctx, "net_watch(fd, events, cb)");
    for (int i = 0; i < s_watch_count; i++) {
        if (s_watch[i].fd != fd) continue;
        s_watch[i].events = events;
        JS_FreeValue(ctx, s_watch[i].cb);
        s_watch[i].cb = JS_DupValue(ctx, argv[2]);
        return JS_UNDEFINED;
    }
    if (s_watch_count >= NET_MAX_WATCH) return JS_ThrowInternalError(ctx, "net_watch: too many sockets (max %d)", NET_MAX_WATCH);
    s_watch[s_watch_count].fd = fd;
    s_watch[s_watch_count].events = events;
    s_watch[s_watch_count].cb = JS_DupValue(ctx, argv[2]);
    s_watch_count++;
    k_add_idle_hook(net_poll_hook);
    return JS_UNDEFINED;
}

static JSValue net_unwatch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t fd;
    if (argc < 1 || rt_arg_int(ctx, argv[0], &fd)) return JS_ThrowTypeError(ctx, "net_unwatch(fd)");
    unwatch_fd(ctx, fd);
    if (s_watch_count == 0) k_remove_idle_hook(net_poll_hook);
    return JS_UNDEFINED;
}

static JSValue net_resolve(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "net_resolve(host)");
    const char *host = JS_ToCString(ctx, argv[0]);
    if (!host) return JS_EXCEPTION;
    char addr[48];
    int rc = hal_net_resolve(host, addr, sizeof addr);
    JS_FreeCString(ctx, host);
    if (rc < 0) return rt_throw_hal(ctx, "net_resolve", rc);
    return JS_NewString(ctx, addr);
}

static JSValue net_local_ip(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    char addr[48];
    int rc = hal_net_local_ip(addr, sizeof addr);
    if (rc < 0) return JS_NULL;
    return JS_NewString(ctx, addr);
}

static void net_poll_hook(void)
{
    if (!s_watch_count) return;
    JSContext *ctx = g_rt.ctx;
    hal_pollfd_t fds[NET_MAX_WATCH];
    int n = s_watch_count;
    for (int i = 0; i < n; i++) {
        fds[i].fd = s_watch[i].fd;
        fds[i].events = s_watch[i].events;
        fds[i].revents = 0;
    }
    int rc = hal_net_poll(fds, (size_t)n);
    if (rc <= 0) return;
    for (int i = 0; i < n; i++) {
        if (!fds[i].revents) continue;
        JSValue cb = JS_UNDEFINED;
        for (int j = 0; j < s_watch_count; j++) {
            if (s_watch[j].fd == fds[i].fd) {
                cb = JS_DupValue(ctx, s_watch[j].cb);
                break;
            }
        }
        if (JS_IsUndefined(cb)) continue;
        JSValue ev = JS_NewInt32(ctx, fds[i].revents);
        JSValue r = JS_Call(ctx, cb, JS_UNDEFINED, 1, &ev);
        if (JS_IsException(r)) rt_dump_exception(ctx);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, cb);
        rt_drain_jobs();
    }
}

void sys_net_release(JSContext *ctx)
{
    for (int i = 0; i < s_watch_count; i++) JS_FreeValue(ctx, s_watch[i].cb);
    s_watch_count = 0;
    k_remove_idle_hook(net_poll_hook);
}

const JSCFunctionListEntry sys_net_funcs[] = {
    JS_CFUNC_DEF("net_socket", 1, net_socket),
    JS_CFUNC_DEF("net_connect", 3, net_connect),
    JS_CFUNC_DEF("net_tls_connect", 3, net_tls_connect),
    JS_CFUNC_DEF("net_connect_status", 1, net_connect_status),
    JS_CFUNC_DEF("net_bind", 2, net_bind),
    JS_CFUNC_DEF("net_listen", 3, net_listen),
    JS_CFUNC_DEF("net_accept", 1, net_accept),
    JS_CFUNC_DEF("net_send", 2, net_send),
    JS_CFUNC_DEF("net_recv", 2, net_recv),
    JS_CFUNC_DEF("net_sendto", 4, net_sendto),
    JS_CFUNC_DEF("net_recvfrom", 2, net_recvfrom),
    JS_CFUNC_DEF("net_close", 1, net_close),
    JS_CFUNC_DEF("net_watch", 3, net_watch),
    JS_CFUNC_DEF("net_unwatch", 1, net_unwatch),
    JS_CFUNC_DEF("net_resolve", 1, net_resolve),
    JS_CFUNC_DEF("net_local_ip", 0, net_local_ip),
};
const int sys_net_funcs_count = (int)(sizeof sys_net_funcs / sizeof sys_net_funcs[0]);

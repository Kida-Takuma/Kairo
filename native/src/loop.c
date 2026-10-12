#include "kairo/loop.h"
#include "kairo/hal.h"

#include <string.h>

typedef struct {
    bool used;
    uint16_t gen;
    uint64_t deadline_us;
    uint64_t period_us;
    k_callback_t cb;
    void *arg;
} k_timer_slot_t;

typedef struct {
    k_callback_t cb;
    void *arg;
} k_ready_item_t;

static k_timer_slot_t s_timers[K_MAX_TIMERS];
static k_ready_item_t s_ready[K_READY_QUEUE_SIZE];
static uint32_t s_ready_head, s_ready_tail;
static bool s_stop;
static k_idle_hook_t s_idle_hooks[K_MAX_IDLE_HOOKS];
static int s_idle_hook_count;

static inline k_timer_t make_id(int slot, uint16_t gen)
{
    return ((k_timer_t)gen << 16) | (k_timer_t)(slot + 1);
}

int k_timer_slot(k_timer_t id)
{
    uint32_t low = id & 0xffffu;
    if (low == 0 || low > K_MAX_TIMERS) return -1;
    return (int)low - 1;
}

static k_timer_slot_t *lookup(k_timer_t id)
{
    int slot = k_timer_slot(id);
    if (slot < 0) return NULL;
    k_timer_slot_t *t = &s_timers[slot];
    if (!t->used || t->gen != (uint16_t)(id >> 16)) return NULL;
    return t;
}

void k_init(void)
{
    memset(s_timers, 0, sizeof s_timers);
    s_ready_head = s_ready_tail = 0;
    s_stop = false;
}

k_timer_t k_timer_add(uint64_t delay_us, uint64_t period_us, k_callback_t cb, void *arg)
{
    for (int i = 0; i < K_MAX_TIMERS; i++) {
        k_timer_slot_t *t = &s_timers[i];
        if (t->used) continue;
        t->used = true;
        t->gen = (uint16_t)(t->gen + 1);
        if (t->gen == 0) t->gen = 1;
        t->deadline_us = hal_time_us() + delay_us;
        t->period_us = period_us;
        t->cb = cb;
        t->arg = arg;
        return make_id(i, t->gen);
    }
    return K_TIMER_INVALID;
}

bool k_timer_cancel(k_timer_t id)
{
    k_timer_slot_t *t = lookup(id);
    if (!t) return false;
    t->used = false;
    return true;
}

bool k_timer_active(k_timer_t id)
{
    return lookup(id) != NULL;
}

void k_timer_set_arg(k_timer_t id, void *arg)
{
    k_timer_slot_t *t = lookup(id);
    if (t) t->arg = arg;
}

bool k_post(k_callback_t cb, void *arg)
{
    uint32_t next = (s_ready_tail + 1) % K_READY_QUEUE_SIZE;
    if (next == s_ready_head) return false;
    s_ready[s_ready_tail].cb = cb;
    s_ready[s_ready_tail].arg = arg;
    s_ready_tail = next;
    return true;
}

static bool ready_pop(k_ready_item_t *out)
{
    if (s_ready_head == s_ready_tail) return false;
    *out = s_ready[s_ready_head];
    s_ready_head = (s_ready_head + 1) % K_READY_QUEUE_SIZE;
    return true;
}

static bool ready_empty(void)
{
    return s_ready_head == s_ready_tail;
}

static bool timers_pending(void)
{
    for (int i = 0; i < K_MAX_TIMERS; i++)
        if (s_timers[i].used) return true;
    return false;
}

bool k_has_pending(void)
{
    return !ready_empty() || timers_pending();
}

bool k_add_idle_hook(k_idle_hook_t hook)
{
    for (int i = 0; i < s_idle_hook_count; i++)
        if (s_idle_hooks[i] == hook) return true;
    if (s_idle_hook_count >= K_MAX_IDLE_HOOKS) return false;
    s_idle_hooks[s_idle_hook_count++] = hook;
    return true;
}

void k_remove_idle_hook(k_idle_hook_t hook)
{
    for (int i = 0; i < s_idle_hook_count; i++) {
        if (s_idle_hooks[i] != hook) continue;
        s_idle_hooks[i] = s_idle_hooks[--s_idle_hook_count];
        return;
    }
}

static void run_idle_hooks(void)
{
    for (int i = 0; i < s_idle_hook_count; i++) s_idle_hooks[i]();
}

void k_stop(void)
{
    s_stop = true;
}

static int earliest_timer(void)
{
    int best = -1;
    for (int i = 0; i < K_MAX_TIMERS; i++) {
        if (!s_timers[i].used) continue;
        if (best < 0 || s_timers[i].deadline_us < s_timers[best].deadline_us) best = i;
    }
    return best;
}

static void idle(uint64_t now, uint64_t deadline)
{
    if (s_idle_hook_count && deadline > now + K_IDLE_SLICE_US) deadline = now + K_IDLE_SLICE_US;
    hal_idle_until_us(deadline);
}

void k_run(void)
{
    s_stop = false;
    while (!s_stop) {
        k_ready_item_t item;
        while (!s_stop && ready_pop(&item)) item.cb(item.arg);
        if (s_stop) break;
        run_idle_hooks();
        if (s_stop || !ready_empty()) continue;

        int idx = earliest_timer();
        uint64_t now = hal_time_us();
        if (idx < 0) {
            if (!s_idle_hook_count) break;
            idle(now, now + K_IDLE_SLICE_US);
            continue;
        }
        k_timer_slot_t *t = &s_timers[idx];
        if (t->deadline_us > now) {
            idle(now, t->deadline_us);
            continue;
        }

        k_callback_t cb = t->cb;
        void *arg = t->arg;
        if (t->period_us == 0) {
            t->used = false;
        } else {
            t->deadline_us += t->period_us;
            if (t->deadline_us <= now) t->deadline_us = now + t->period_us;
        }
        cb(arg);
    }
}

#ifndef KAIRO_LOOP_H
#define KAIRO_LOOP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef K_MAX_TIMERS
#define K_MAX_TIMERS 64
#endif
#ifndef K_READY_QUEUE_SIZE
#define K_READY_QUEUE_SIZE 64
#endif
#ifndef K_IDLE_SLICE_US
#define K_IDLE_SLICE_US 20000
#endif
#ifndef K_MAX_IDLE_HOOKS
#define K_MAX_IDLE_HOOKS 4
#endif

typedef void (*k_callback_t)(void *arg);
typedef void (*k_idle_hook_t)(void);

typedef uint32_t k_timer_t;
#define K_TIMER_INVALID ((k_timer_t)0)

void k_init(void);

k_timer_t k_timer_add(uint64_t delay_us, uint64_t period_us, k_callback_t cb, void *arg);
bool k_timer_cancel(k_timer_t id);
bool k_timer_active(k_timer_t id);
void k_timer_set_arg(k_timer_t id, void *arg);
int k_timer_slot(k_timer_t id);

bool k_post(k_callback_t cb, void *arg);

bool k_add_idle_hook(k_idle_hook_t hook);
void k_remove_idle_hook(k_idle_hook_t hook);
void k_run(void);
void k_stop(void);
bool k_has_pending(void);

#ifdef __cplusplus
}
#endif

#endif

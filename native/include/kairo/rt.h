#ifndef KAIRO_RT_H
#define KAIRO_RT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void rt_set_heap_limit(size_t bytes);
void rt_set_stack_limit(size_t bytes);
int rt_boot(const char *app_src, size_t app_len, const char *app_name);
void rt_run(void);
void rt_print_memory_stats(void);
int rt_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif

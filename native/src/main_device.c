#include "kairo/app_image.h"
#include "kairo/hal.h"
#include "kairo/loop.h"
#include "kairo/rt.h"

#include <stdio.h>
#include <string.h>

static void say(const char *s)
{
    hal_console_write(s, strlen(s));
}

void kairo_device_main(void)
{
    hal_init();
    rt_set_heap_limit(hal_js_heap_bytes());
    rt_set_stack_limit(hal_js_stack_bytes());

    char banner[128];
    snprintf(banner, sizeof banner, "\nkairo runtime on %s\n", hal_board_name());
    say(banner);

    size_t len = 0;
    const char *src = app_image_locate(&len);
    if (!src) {
        say("no app installed; run: kairo flash <entry.ts>\n");
    } else if (rt_boot(src, len, "app.js") != 0) {
        say("app failed to start\n");
    }

    k_add_idle_hook(upload_poll);
    for (;;) {
        k_run();
        hal_idle_until_us(hal_time_us() + K_IDLE_SLICE_US);
    }
}

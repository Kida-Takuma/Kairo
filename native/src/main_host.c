#include "kairo/rt.h"
#include "kairo/hal.h"
#include "kairo/app_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <stdbool.h>

void hal_host_set_app_file(const char *path);

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[n] = 0;
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *device_app_file = NULL;
    bool stats = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stats") == 0) {
            stats = true;
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device_app_file = argv[++i];
        } else if (strcmp(argv[i], "--heap") == 0 && i + 1 < argc) {
            rt_set_heap_limit((size_t)strtoull(argv[++i], NULL, 10));
        } else {
            path = argv[i];
        }
    }
    if (device_app_file) {
        hal_host_set_app_file(device_app_file);
        kairo_device_main();
        return 0;
    }
    if (!path) {
        fprintf(stderr, "usage: %s [--heap BYTES] [--stats] <app-bundle.js>\n       %s --device <app-image-file>\n", argv[0], argv[0]);
        return 2;
    }
    size_t len;
    char *src = read_file(path, &len);
    if (!src) {
        perror(path);
        return 2;
    }

    char *name = basename((char *)path);
    hal_init();
    int rc = rt_boot(src, len, name);
    if (rc == 0) rt_run();
    if (stats) rt_print_memory_stats();
    int status = rt_shutdown();
    free(src);
    return rc != 0 ? 1 : status;
}

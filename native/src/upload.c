#include "kairo/app_image.h"
#include "kairo/hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPLOAD_CHUNK 1024
#define UPLOAD_TIMEOUT_US 5000000ull

static char s_line[96];
static size_t s_line_len;
static const uint8_t *s_prefetch;
static size_t s_prefetch_len;

static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    hal_console_write(buf, (size_t)n);
}

static int read_exact(uint8_t *dst, size_t want)
{
    size_t got = 0;
    if (s_prefetch_len) {
        size_t n = s_prefetch_len < want ? s_prefetch_len : want;
        memcpy(dst, s_prefetch, n);
        s_prefetch += n;
        s_prefetch_len -= n;
        got = n;
    }
    uint64_t deadline = hal_time_us() + UPLOAD_TIMEOUT_US;
    while (got < want) {
        int n = hal_console_read(dst + got, want - got);
        if (n > 0) {
            got += (size_t)n;
            deadline = hal_time_us() + UPLOAD_TIMEOUT_US;
            continue;
        }
        uint64_t now = hal_time_us();
        if (now >= deadline) return -1;
        hal_idle_until_us(now + 1000);
    }
    return 0;
}

static void do_flash(size_t length, uint32_t expect_crc)
{
    size_t cap;
    if (hal_app_capacity(&cap) != 0) {
        reply("KAIRO ERR no app storage on this board\n");
        return;
    }
    if (length == 0 || length + APP_IMAGE_HEADER_SIZE + 1 > cap) {
        reply("KAIRO ERR app too large: %u bytes, capacity %u\n", (unsigned)length, (unsigned)(cap - APP_IMAGE_HEADER_SIZE - 1));
        return;
    }
    if (hal_app_erase(length + APP_IMAGE_HEADER_SIZE + 1) != 0) {
        reply("KAIRO ERR erase failed\n");
        return;
    }
    app_image_header_t h = { APP_IMAGE_MAGIC, (uint32_t)length, expect_crc, 0 };
    if (hal_app_write(0, &h, sizeof h) != 0) {
        reply("KAIRO ERR write failed\n");
        return;
    }
    reply("KAIRO READY %d\n", UPLOAD_CHUNK);

    static uint8_t chunk[UPLOAD_CHUNK];
    size_t written = 0;
    uint32_t crc = 0;
    while (written < length) {
        size_t want = length - written;
        if (want > UPLOAD_CHUNK) want = UPLOAD_CHUNK;
        if (read_exact(chunk, want) != 0) {
            reply("KAIRO ERR timeout after %u bytes\n", (unsigned)written);
            return;
        }
        crc = crc32_update(crc, chunk, want);
        if (hal_app_write(APP_IMAGE_HEADER_SIZE + written, chunk, want) != 0) {
            reply("KAIRO ERR write failed at %u\n", (unsigned)written);
            return;
        }
        written += want;
        reply("KAIRO ACK %u\n", (unsigned)written);
    }
    static const uint8_t nul = 0;
    if (hal_app_write(APP_IMAGE_HEADER_SIZE + written, &nul, 1) != 0 || hal_app_finish() != 0) {
        reply("KAIRO ERR finish failed\n");
        return;
    }
    if (crc != expect_crc) {
        reply("KAIRO ERR crc mismatch: got %08x want %08x\n", (unsigned)crc, (unsigned)expect_crc);
        return;
    }
    reply("KAIRO DONE %u\n", (unsigned)written);
    hal_idle_until_us(hal_time_us() + 200000);
    hal_reboot();
}

static void handle_line(const char *line)
{
    if (strncmp(line, "KAIRO ", 6) != 0) return;
    const char *cmd = line + 6;
    if (strcmp(cmd, "INFO") == 0) {
        size_t cap = 0, len = 0;
        hal_app_capacity(&cap);
        app_image_locate(&len);
        reply("KAIRO INFO board=%s capacity=%u app=%u\n", hal_board_name(), (unsigned)(cap > APP_IMAGE_HEADER_SIZE + 1 ? cap - APP_IMAGE_HEADER_SIZE - 1 : 0), (unsigned)len);
        return;
    }
    if (strcmp(cmd, "REBOOT") == 0) {
        reply("KAIRO DONE 0\n");
        hal_idle_until_us(hal_time_us() + 100000);
        hal_reboot();
        return;
    }
    if (strcmp(cmd, "ERASE") == 0) {
        if (hal_app_erase(APP_IMAGE_HEADER_SIZE) == 0 && hal_app_finish() == 0) reply("KAIRO DONE 0\n");
        else reply("KAIRO ERR erase failed\n");
        return;
    }
    if (strncmp(cmd, "FLASH ", 6) == 0) {
        char *end;
        unsigned long length = strtoul(cmd + 6, &end, 10);
        unsigned long crc = strtoul(end, NULL, 16);
        do_flash((size_t)length, (uint32_t)crc);
        return;
    }
    reply("KAIRO ERR unknown command\n");
}

void upload_poll(void)
{
    uint8_t buf[64];
    int n;
    while ((n = hal_console_read(buf, sizeof buf)) > 0) {
        for (int i = 0; i < n; i++) {
            uint8_t c = buf[i];
            if (c == '\n' || c == '\r') {
                if (s_line_len) {
                    s_line[s_line_len] = 0;
                    s_line_len = 0;
                    s_prefetch = buf + i + 1;
                    s_prefetch_len = (size_t)(n - i - 1);
                    handle_line(s_line);
                    i = n - 1 - (int)s_prefetch_len;
                    s_prefetch_len = 0;
                }
            } else if (s_line_len < sizeof s_line - 1) {
                s_line[s_line_len++] = (char)c;
            } else {
                s_line_len = 0;
            }
        }
    }
}

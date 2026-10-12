#include "kairo/app_image.h"
#include "kairo/hal.h"

#include <string.h>

const char *app_image_locate(size_t *len)
{
    size_t cap;
    const uint8_t *base = hal_app_map(&cap);
    if (!base || cap < APP_IMAGE_HEADER_SIZE) return NULL;
    app_image_header_t h;
    memcpy(&h, base, sizeof h);
    if (h.magic != APP_IMAGE_MAGIC) return NULL;
    if (h.length == 0 || h.length + 1 > cap - APP_IMAGE_HEADER_SIZE) return NULL;
    const char *src = (const char *)base + APP_IMAGE_HEADER_SIZE;
    if (src[h.length] != 0) return NULL;
    if (crc32_of(src, h.length) != h.crc32) return NULL;
    *len = h.length;
    return src;
}

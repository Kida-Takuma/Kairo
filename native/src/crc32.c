#include "kairo/app_image.h"

static const uint32_t s_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
};

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        crc = s_table[crc & 15] ^ (crc >> 4);
        crc = s_table[crc & 15] ^ (crc >> 4);
    }
    return ~crc;
}

uint32_t crc32_of(const void *data, size_t len)
{
    return crc32_update(0, data, len);
}

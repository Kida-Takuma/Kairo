#ifndef KAIRO_APP_IMAGE_H
#define KAIRO_APP_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_IMAGE_MAGIC 0x5050414Bu
#define APP_IMAGE_HEADER_SIZE 16

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t crc32;
    uint32_t flags;
} app_image_header_t;

const char *app_image_locate(size_t *len);

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32_of(const void *data, size_t len);

void upload_poll(void);
void kairo_device_main(void);

#ifdef __cplusplus
}
#endif

#endif

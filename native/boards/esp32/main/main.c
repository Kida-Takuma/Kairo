#include "kairo/app_image.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void kairo_task(void *arg)
{
    (void)arg;
    kairo_device_main();
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreate(kairo_task, "kairo", KAIRO_TASK_STACK_BYTES, NULL, 5, NULL);
}

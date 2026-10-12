set(KAIRO_NATIVE ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(QJS_DIR ${KAIRO_NATIVE}/third_party/quickjs-ng)

set(KAIRO_NATIVE_SOURCES "${KAIRO_NATIVE_SOURCES}" CACHE STRING "extra native module sources")
set(KAIRO_NATIVE_INCLUDE "${KAIRO_NATIVE_INCLUDE}" CACHE STRING "extra include dirs")

set(QJS_SRCS
    ${QJS_DIR}/quickjs.c
    ${QJS_DIR}/libregexp.c
    ${QJS_DIR}/libunicode.c
    ${QJS_DIR}/dtoa.c)

idf_component_register(
    SRCS
        main.c
        ${KAIRO_NATIVE}/src/loop.c
        ${KAIRO_NATIVE}/src/rt.c
        ${KAIRO_NATIVE}/src/sys.c
        ${KAIRO_NATIVE}/src/sys_net.c
        ${KAIRO_NATIVE}/src/sys_periph.c
        ${KAIRO_NATIVE}/src/bridge.c
        ${KAIRO_NATIVE}/src/crc32.c
        ${KAIRO_NATIVE}/src/app_image.c
        ${KAIRO_NATIVE}/src/upload.c
        ${KAIRO_NATIVE}/src/main_device.c
        ${KAIRO_NATIVE}/hal/esp32/hal.c
        ${KAIRO_NATIVE}/hal/esp32/net.c
        ${KAIRO_NATIVE}/hal/esp32/wifi.c
        ${KAIRO_NATIVE}/hal/esp32/ble.c
        ${KAIRO_NATIVE}/hal/esp32/periph.c
        ${KAIRO_NATIVE}/hal/esp32/io.c
        ${KAIRO_NATIVE}/hal/common/posix_net.c
        ${QJS_SRCS}
        ${KAIRO_NATIVE_SOURCES}
    INCLUDE_DIRS ${KAIRO_NATIVE}/include ${QJS_DIR} ${KAIRO_NATIVE_INCLUDE}
    REQUIRES driver esp_timer esp_partition spi_flash esp_system esp_wifi esp_netif esp_event nvs_flash lwip esp-tls mbedtls bt esp_adc esp_driver_ledc esp_driver_i2c esp_driver_spi esp_driver_gpio)

target_compile_definitions(${COMPONENT_LIB} PRIVATE QUICKJS_NG_BUILD _GNU_SOURCE KAIRO_TASK_STACK_BYTES=49152)
set_source_files_properties(${QJS_SRCS} PROPERTIES COMPILE_OPTIONS "-w")

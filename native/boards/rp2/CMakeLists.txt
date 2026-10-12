cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(kairo C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(KAIRO_NATIVE ${CMAKE_CURRENT_LIST_DIR}/../..)
set(QJS_DIR ${KAIRO_NATIVE}/third_party/quickjs-ng)
set(KAIRO_NATIVE_SOURCES "${KAIRO_NATIVE_SOURCES}" CACHE STRING "extra native module sources")
set(KAIRO_NATIVE_INCLUDE "${KAIRO_NATIVE_INCLUDE}" CACHE STRING "extra include dirs")
set(QJS_SRCS ${QJS_DIR}/quickjs.c ${QJS_DIR}/libregexp.c ${QJS_DIR}/libunicode.c ${QJS_DIR}/dtoa.c)

add_executable(kairo
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
    ${KAIRO_NATIVE}/hal/rp2/hal.c
    ${KAIRO_NATIVE}/hal/rp2/periph.c
    ${KAIRO_NATIVE}/hal/rp2/io.c
    ${QJS_SRCS}
    ${KAIRO_NATIVE_SOURCES})

target_include_directories(kairo PRIVATE ${KAIRO_NATIVE}/include ${QJS_DIR} ${KAIRO_NATIVE_INCLUDE})
target_compile_definitions(kairo PRIVATE QUICKJS_NG_BUILD _GNU_SOURCE PICO_STACK_SIZE=0x8000)
target_compile_options(kairo PRIVATE -Os)
set_source_files_properties(${QJS_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(kairo pico_stdlib pico_rand hardware_flash hardware_watchdog hardware_sync hardware_uart hardware_adc hardware_pwm hardware_i2c hardware_spi hardware_clocks)

pico_enable_stdio_usb(kairo 1)
pico_enable_stdio_uart(kairo 0)
pico_add_extra_outputs(kairo)

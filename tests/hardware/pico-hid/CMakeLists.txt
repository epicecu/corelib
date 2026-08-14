cmake_minimum_required(VERSION 3.20)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(corelib_pico_hid C CXX ASM)
pico_sdk_init()
if(PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
  message(FATAL_ERROR "The Pico HID fixture requires Pico SDK 2.3.0 or newer")
endif()

add_subdirectory(../../.. epicecu-corelib)

add_executable(corelib_pico_hid
  src/main.c
  src/corelib_self_test.c
  src/test_device.c
  src/usb_descriptors.c
  src/generated/common.pb.c
  src/generated/shares.pb.c
)
target_include_directories(corelib_pico_hid PRIVATE src)
target_compile_definitions(corelib_pico_hid PRIVATE
  CFG_TUSB_CONFIG_FILE="tusb_config.h"
)
target_compile_options(corelib_pico_hid PRIVATE
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)
target_link_libraries(corelib_pico_hid PRIVATE
  corelib_device
  pico_stdlib
  pico_unique_id
  tinyusb_device
  tinyusb_board
)
pico_enable_stdio_usb(corelib_pico_hid 0)
pico_enable_stdio_uart(corelib_pico_hid 0)
pico_add_extra_outputs(corelib_pico_hid)

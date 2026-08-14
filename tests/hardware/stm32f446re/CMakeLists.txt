cmake_minimum_required(VERSION 3.20)
project(corelib_stm32f446re C CXX ASM)
set(STM32CUBE_F4_PATH "" CACHE PATH "STM32CubeF4 1.28.1 root")
if(NOT EXISTS "${STM32CUBE_F4_PATH}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c")
  message(FATAL_ERROR "Set STM32CUBE_F4_PATH to an STM32CubeF4 1.28.1 checkout")
endif()
add_subdirectory(../../.. epicecu-corelib)
set(HAL_ROOT "${STM32CUBE_F4_PATH}/Drivers/STM32F4xx_HAL_Driver")
set(CMSIS_DEVICE "${STM32CUBE_F4_PATH}/Drivers/CMSIS/Device/ST/STM32F4xx")
add_executable(corelib_stm32f446re
  src/main.c
  "${CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f446xx.s"
  "${CMSIS_DEVICE}/Source/Templates/system_stm32f4xx.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_cortex.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_flash.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_flash_ex.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_gpio.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_pwr.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_pwr_ex.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_rcc.c"
  "${HAL_ROOT}/Src/stm32f4xx_hal_rcc_ex.c")
set_target_properties(corelib_stm32f446re PROPERTIES OUTPUT_NAME corelib_stm32f446re SUFFIX .elf)
target_compile_definitions(corelib_stm32f446re PRIVATE STM32F446xx USE_HAL_DRIVER)
target_include_directories(corelib_stm32f446re PRIVATE src "${HAL_ROOT}/Inc" "${CMSIS_DEVICE}/Include" "${STM32CUBE_F4_PATH}/Drivers/CMSIS/Include")
target_compile_options(corelib_stm32f446re PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(corelib_stm32f446re PRIVATE corelib_device)
target_link_options(corelib_stm32f446re PRIVATE
  -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
  --specs=nano.specs --specs=nosys.specs
  -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F446RETX_FLASH.ld
  -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/corelib_stm32f446re.map)
add_custom_command(TARGET corelib_stm32f446re POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:corelib_stm32f446re> corelib_stm32f446re.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:corelib_stm32f446re> corelib_stm32f446re.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:corelib_stm32f446re> VERBATIM)

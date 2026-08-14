# STM32F446RE CMake fixture

This fixture links Corelib into a complete NUCLEO-F446RE firmware image using
the GNU Arm toolchain and STM32CubeF4 1.28.1, without a framework build layer.

```sh
task stm32:build STM32CUBE_F4_PATH=/path/to/STM32CubeF4
```

The build emits ELF, HEX, BIN, and map files and verifies the startup, HAL tick,
and Corelib symbols while rejecting references to heap allocators.

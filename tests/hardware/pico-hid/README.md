# Pico HID test device

This hardware fixture targets the original RP2040 Raspberry Pi Pico. It runs a
public-API Corelib self-test at boot, signals success with three short flashes of
the GPIO 25 LED, and then enumerates as a 64-byte vendor HID device.

The USB VID/PID (`cafe:4010`) and fixed node UUID are development-only values.
Production firmware must use an assigned USB identity and a provisioned,
persistent UUIDv4.

The Pico SDK already contains TinyUSB. Point the build at a Pico SDK checkout
and build the UF2 from the repository root:

```sh
task pico:build PICO_SDK_PATH=/path/to/pico-device
```

To flash a Pico already in BOOTSEL mode, explicitly identify its mount:

```sh
task pico:flash PICO_SDK_PATH=/path/to/pico-device \
  PICO_UF2_MOUNT=/run/media/$USER/RPI-RP2
```

The flash task verifies `INFO_UF2.TXT` before copying the UF2. A repeating LED
blink count indicates the self-test failure code. Successful firmware enters
HID service after the three startup flashes.

For the adapter test, first prepare the private adapter test-client virtual
environment, then provide its checkout without committing the path:

```sh
task pico:e2e PROGRAMMOR_ADAPTERS_DIR=/path/to/programmor-adapters
```

On Linux, the user running the test must have read/write access to the HID
device. The fixture mirrors Common 1 and Shares 1-6 from the deterministic test
adapter device model.

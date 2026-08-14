# Test layout

This directory contains Corelib's host, packaging, fuzzing, and target-hardware
tests. Run commands from the repository root so Task can apply the correct
toolchain and build configuration.

The top level is deliberately limited to four test categories:

```text
tests/
|-- unit/          Host behavioural tests
|-- integration/   Runtime and consumer-boundary tests
|-- fuzz/          Parser and gateway fuzz targets
`-- hardware/      Board-specific firmware fixtures
```

## `unit/`

GoogleTest and GoogleMock suites exercise the device and gateway C APIs and C++
facades. They cover protocol behaviour, resource limits, failure paths,
callbacks, routing, and lifecycle state. These host tests require no hardware.

```sh
task native:unit
```

GoogleTest is fetched only when native tests are enabled and is never installed
or exposed to Corelib consumers.

## `integration/`

Integration tests verify boundaries that are broader than an individual unit.

### `integration/runtime/`

Small framework-free executables preserve guarantees that a C++ test framework
cannot provide. They compile public APIs directly as C11 and C++14, check the
no-exceptions/no-RTTI runtime surface, validate normative protocol bytes, and
run the deterministic Pico device model on the host.

```sh
task native:runtime
task native:conformance
```

Run both groups with `task native:test`. Use `task native:check` for strict GCC
and Clang builds and `task native:sanitize` for supported runtime sanitisers.
Use `task quality:coverage` to generate line and branch coverage reports for
the owned Corelib sources. The coverage task enforces an 85% line-coverage floor.

### `integration/consumer/`

Consumer tests ensure that Corelib can be used through its supported CMake
interfaces rather than by relying on private implementation details.

#### `integration/consumer/build-tree/`

This is a minimal C application linked against Corelib from the repository's
CMake build. It verifies that an ordinary consumer can include the public
device header, initialise a context, and link the expected API symbols. It is
built and executed by `task native:test`.

#### `integration/consumer/installed-package/`

These C and C++ programs consume an installed Corelib package rather than reaching
into the source tree. They cover both device and gateway APIs and protect the
CMake export, installed header layout, target names, and transitive dependency
contract.

```sh
task package:cmake
```

## `fuzz/`

Fuzz targets feed arbitrary input into PFP frame handling and gateway paths to
check validation and memory safety. They may include private Corelib headers because
they intentionally test below the public API boundary.

```sh
task quality:fuzz
```

## `hardware/`

Firmware fixtures verify that the portable Corelib builds and operates on supported
microcontroller toolchains. Each target directory contains its own setup and
usage notes.

### `hardware/pico-hid/`

Raspberry Pi Pico firmware using USB HID. It supports firmware builds, flashing,
and adapter end-to-end testing. See
[pico-hid/README.md](hardware/pico-hid/README.md).

### `hardware/teensy41-rawhid/`

Teensy 4.1 firmware using RawHID. Its end-to-end manifest exercises discovery,
connection, and PFP traffic through the HID adapter. See
[teensy41-rawhid/README.md](hardware/teensy41-rawhid/README.md).

### `hardware/stm32f446re/`

Bare-metal NUCLEO-F446RE build fixture using the GNU Arm toolchain and
STM32CubeF4. It verifies linking, expected firmware symbols, and the absence of
heap allocator references. See
[stm32f446re/README.md](hardware/stm32f446re/README.md).

Hardware builds form part of the release gate, but flashing and adapter
end-to-end tests require the corresponding connected board and local tools.

## Choosing where a test belongs

- Put host behavioural assertions in `unit/`.
- Put language, linkage, runtime, and normative-vector checks in
  `integration/runtime/`.
- Put build-tree and installed-package compatibility checks in the matching
  directory below `integration/consumer/`.
- Put parser robustness entry points in `fuzz/`.
- Put board-specific firmware and manifests below `hardware/<target>/`.
- Keep generated build output under `build/`; `.pio/` directories are local
  PlatformIO artefacts and are not test source.

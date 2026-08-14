<div align="center">

<img src="docs/images/corelib-logo.png" alt="EpicECU Corelib" width="400" />

##### Portable C11 middleware for Programmor-compatible microcontroller devices

</div>

[![CI](https://github.com/epicecu/corelib/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/epicecu/corelib/actions/workflows/ci.yml)

Version 0.1.0 implements Programmor Frame Protocol (PFP) v1 and Transaction
Protocol v2 for Arduino, bare-metal firmware, and MCU projects with an
application-controlled scheduler.

The core has no heap, hardware abstraction layer, scheduler, filesystem,
network stack, or background-work dependency. It receives and emits complete
64-byte PFP frames and treats device-specific Common and Share payloads as
opaque bytes.

## Install

For CMake firmware, add this repository with `add_subdirectory()` and link
`Corelib::Device`. C++14 consumers enable `CORELIB_BUILD_CPP`,
provide ETL 20.x through `CORELIB_ETL_ROOT`, and link
`Corelib::DeviceCpp`. Installed-package consumers request the `Cpp`
component. The C target remains ETL-independent.

Gateway-capable firmware additionally enables `CORELIB_BUILD_GATEWAY`
and links `Corelib::Gateway` or `Corelib::GatewayCpp`.
Gateway code and storage are absent from the default standard-device target.
For example, configure CMake with
`-DCORELIB_BUILD_GATEWAY=ON`; linking a gateway target then supplies the
required compile definition to consumers.

The repository also follows the Arduino 1.5 library layout. Install it from a
ZIP or clone it into the Arduino libraries directory, then include:

```cpp
#include <Corelib.h>
```

Arduino gateway sketches define `CORELIB_ENABLE_GATEWAY=1` before that
include. This exposes the optional gateway facade through the umbrella header;
it is distinct from the CMake build option.

Framework-neutral consumers may include the role-specific API directly:

```c
#include <corelib/device.h>
```

```cpp
#include <corelib/device.hpp>
#include <corelib/gateway.hpp>
```

## Repository layout

- `src/corelib` keeps each device and gateway implementation beside its
  public C and C++ headers.
- `src/protocol` contains owned PFP and Transaction Protocol codecs.
- `src/internal` contains private implementation contracts.
- `src/vendor` and generated protocol directories retain upstream formatting.
- [`examples`](examples), `docs`, `tests`, and `tasks` contain integration examples,
  guidance, validation, and developer workflows.

The source conventions and documentation policy are defined in
[`docs/style.md`](docs/style.md).

## Quick start: Arduino/C++

The C++ facade owns Corelib's fixed storage, so a small Arduino application can
be integrated without heap allocation. This complete example uses `Serial` as
a transport for 64-byte PFP frames:

```cpp
#include <Corelib.h>

constexpr corelib_link_id_t kSerialLink = 1;

uint8_t incoming_frame[CORELIB_FRAME_SIZE];
size_t incoming_size = 0;
bool response_pending = false;
corelib::TransactionId pending_request{};

class DeviceHandler final : public corelib::Handler {
 public:
  corelib::SendResult sendFrame(corelib::LinkId, void *,
                                   corelib::FrameView frame) override {
    if (static_cast<size_t>(Serial.availableForWrite()) < frame.size()) {
      return corelib::SendResult::Busy;
    }
    return Serial.write(frame.data(), frame.size()) == frame.size()
               ? corelib::SendResult::Accepted
               : corelib::SendResult::Failed;
  }

  void onTransaction(const corelib::TransactionView &transaction) override {
    // Callback data is borrowed, so copy what the application needs.
    pending_request = transaction.id;
    response_pending = true;
  }
};

DeviceHandler handler;
corelib::Device<512, 2, 16, 4> device;

bool init_sdk() {
  corelib::Config config;
  // Replace this with the device's provisioned, persistent UUIDv4.
  config.nodeUuid[0] = 0x12;
  config.nodeUuid[6] = 0x40;
  config.nodeUuid[8] = 0x80;
  config.maximumTransactionDataSize = 256;

  return device.init(config, handler) == corelib::Status::Ok &&
         device.addLink(kSerialLink) == corelib::Status::Ok;
}

void setup() {
  Serial.begin(115200);
  (void)init_sdk();
}

void loop() {
  while (Serial.available() > 0 && incoming_size < sizeof(incoming_frame)) {
    const int value = Serial.read();
    if (value >= 0) {
      incoming_frame[incoming_size++] = static_cast<uint8_t>(value);
    }
  }

  if (incoming_size == sizeof(incoming_frame)) {
    (void)device.receive(kSerialLink, incoming_frame, millis());
    incoming_size = 0;
  }

  if (response_pending) {
    response_pending = false;
    (void)device.respond(pending_request, corelib::Result::Unsupported);
  }

  (void)device.tick(millis());
}
```

This example deliberately returns `UNSUPPORTED` for application requests.
Replace that response with the device's Common or Share handling. Keep response
work outside the transaction callback, because callback data is borrowed and
Corelib calls for one context must be serialized.

Your transport may be UART, USB CDC, HID, or another packet link, but it must
deliver and accept complete 64-byte frames. Production firmware must also use a
persistent UUIDv4 and pass a non-decreasing monotonic time to `receive()` and
`tick()`. See the complete [`DeviceSerial`](examples/DeviceSerial) sketch and
the [integration](docs/integration.md) and [concurrency](docs/concurrency.md)
guides when adapting the example to a real device.

## Build and validate

Install [Task](https://taskfile.dev/), CMake, GCC or Clang, and run:

```sh
task quick  # Configure, build, and run the portable test suite.
task test   # Equivalent native test entry point.
task check  # Build with strict GCC and Clang warnings.
task quality:format CLANG_FORMAT=clang-format-18
task quality:format-check CLANG_FORMAT=clang-format-18
task quality:docs
```

Tasks are grouped by purpose under `native:`, `protocol:`, `package:`,
`arduino:`, `quality:`, and the `pico:`, `teensy:`, and `stm32:` hardware
namespaces. Their definitions live in focused files under [`tasks`](tasks),
while the root Taskfile contains only shared configuration and common
workflows. Run `task --list` for the complete command catalogue.

`task all` is the complete release gate. It also requires Arduino Lint, the Arm
GNU toolchain, and an STM32CubeF4 1.28.1 checkout supplied as
`STM32CUBE_F4_PATH`; pinned Arduino CLI and board cores are installed locally
when needed.

## Test with a Teensy 4.1

The Teensy fixture exercises Corelib, USB RawHID transport, and Programmor HID
adapter together on physical hardware. The following walkthrough targets
Ubuntu and other Debian-based Linux systems.

Install the host tools. Install [Task](https://taskfile.dev/installation/)
using its official instructions rather than the unrelated `task` package that
some distributions provide:

```sh
sudo apt update
sudo apt install cmake curl tar python3-venv teensy-loader-cli
```

Install PJRC's USB permissions, reload the rules, and reconnect the Teensy:

```sh
curl -fL https://www.pjrc.com/teensy/00-teensy.rules \
  -o /tmp/00-teensy.rules
sudo cp /tmp/00-teensy.rules /etc/udev/rules.d/00-teensy.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Clone the core library and adapter repositories beside each other, then prepare
the adapter's private Python test environment:

```sh
git clone <corelib-repository-url> corelib
git clone <adapter-repository-url> programmor-adapters
task --dir programmor-adapters/tests/test-client setup
cd corelib
```

Connect a Teensy 4.1 over USB and run the deterministic physical test:

```sh
task teensy:test \
  PROGRAMMOR_ADAPTERS_DIR=../programmor-adapters
```

The first run downloads Arduino CLI 1.5.1 and Teensy core 1.62.0 into the
ignored `build/tooling` directory. No global Arduino installation is needed.
The task builds fresh firmware, uploads it, waits for RawHID to re-enumerate,
and runs the adapter manifest. Press the Teensy PROGRAM button once if the
loader asks for it. Success ends with output similar to:

```text
PASS .../adapter-e2e.json: Teensy 4.1 RawHID device end-to-end
```

The test covers adapter authentication, USB discovery, PFP session and
topology establishment, Common and Share requests, publication and read-back,
unsupported-share handling, subscription polling, and clean disconnect. The
firmware also runs an on-device Corelib self-test before serving requests.

Use the component tasks when diagnosing a failure:

```sh
task teensy:setup   # Install or verify pinned local build tooling.
task teensy:build   # Compile without accessing the board.
task teensy:upload  # Build and program the connected Teensy.
task teensy:wait    # Wait for RawHID enumeration.
task teensy:e2e PROGRAMMOR_ADAPTERS_DIR=../programmor-adapters
```

If `teensy_loader_cli` is installed outside `PATH`, pass it explicitly as
`TEENSY_LOADER_CLI=/path/to/teensy_loader_cli`. A loader that waits indefinitely
usually needs one PROGRAM-button press. `Permission denied` on `hidraw` or USB
means the PJRC udev rule is missing or the board has not been reconnected. If
the adapter test client reports a missing `.venv`, rerun its `task setup` step.

See the [Teensy fixture guide](tests/hardware/teensy41-rawhid) for fixture
implementation details.

## Integrate

Choose `Device` for a single downstream device or `Gateway` for an ECU
that is also a normal addressable device and routes one or more downstream PFP
links. Every registered gateway link is dedicated to PFP; USB, CAN, serial,
discovery, and bootstrap carriage remain developer-owned integrations. See the
[gateway integration guide](docs/gateway.md) for the complete boundary.

1. Provision a persistent RFC 4122 UUIDv4 in device-owned storage.
2. Allocate Corelib context and the configured storage pools.
3. Register transport, transaction, lifecycle, and diagnostic callbacks.
4. Initialise the context and register its transport link.
5. Pass each complete received frame to `corelib_receive_frame()`.
6. Call `corelib_tick()` regularly with non-decreasing monotonic time.
7. Complete requests with `corelib_respond()` or publish unsolicited
   data with `corelib_publish()`.
8. Remove the link or reset the context when the connection is invalidated.

Transport drivers and device application handlers remain outside Corelib:

```text
device transport -> Corelib -> device transaction handler
device transport <- Corelib <- response or publication
```

See [integration](docs/integration.md) and
[concurrency](docs/concurrency.md) for the complete ownership contract.
The [C++ and Arduino guide](docs/cpp.md) documents ETL setup, typed callbacks,
fixed-storage sizing, and facade lifetime rules.

An RP2040 Pico SDK 2.3.0/TinyUSB hardware fixture is available under
[`tests/hardware/pico-hid`](tests/hardware/pico-hid). It runs an on-device Corelib
self-test and can be exercised through the Programmor HID adapter. It remains a
manual hardware check because CI has no attached Pico. A fully linked
NUCLEO-F446RE CMake fixture is under
[`tests/hardware/stm32f446re`](tests/hardware/stm32f446re).
A Teensy 4.1 RawHID fixture with the same deterministic Common/Share model is
available under
[`tests/hardware/teensy41-rawhid`](tests/hardware/teensy41-rawhid).

## Execution model

All calls for one context are serialized and callbacks are synchronous. An
interrupt handler should place complete frames into an application-owned queue
or ring buffer; a main loop or owning task then calls Corelib. Corelib does not
create tasks, queues, locks, interrupts, or timers.

The send callback must attempt one complete non-blocking frame write.
`CORELIB_SEND_BUSY` retains the queued frame for a later Corelib call;
`CORELIB_SEND_FAILED` terminates that attempt and reports a diagnostic.

## Storage

Use `corelib_context_size()`, `corelib_context_alignment()`, and
`corelib_pending_request_entry_size()` instead of duplicating internal
sizes. Reassembly storage requires `maximum_message_size * slot_count` message
bytes and `255 * slot_count` fragment markers. Outbound storage requires
`64 * capacity` bytes, and transaction scratch requires
`maximum_message_size` bytes.

## Compatibility

| Corelib | PFP | Transaction | C | Optional C++ facade |
| --- | --- | --- | --- | --- |
| 0.1.0 | 1 | 2 | C11 | C++14 with ETL 20.x |

Version 0.1.0 provides the standard-node core and the separately enabled
portable gateway component. Native verification uses GCC and Clang; package
builds cover Arduino/Teensy, Raspberry Pi Pico SDK, and STM32Cube integrations.
The legacy `Comm`-based Corelib API is a migration reference, not a compatibility
constraint.

## License

MIT

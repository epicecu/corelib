<div align="center">

<img src="docs/public/corelib-logo.png" alt="EpicECU Corelib" width="400" />

### Portable, heap-free middleware for Programmor-compatible embedded devices

</div>

[![CI](https://github.com/epicecu/corelib/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/epicecu/corelib/actions/workflows/ci.yml)
[![Documentation](https://github.com/epicecu/corelib/actions/workflows/docs.yml/badge.svg)](https://epicecu.github.io/corelib/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Corelib implements Portable Frame Protocol (PFP) v1 and Transaction Protocol
v2 for Arduino, bare-metal firmware, and MCU projects with an
application-controlled scheduler. The canonical core is portable C11, with an
optional fixed-storage C++14 facade.

Documentation: https://epicecu.github.io/corelib/

## Highlights

- Standard Device endpoints and an optional multi-link Gateway component.
- Complete 64-byte PFP frames over an application-owned transport.
- Common and Share requests, responses, and publications.
- Caller-owned C storage and compile-time-sized C++ storage.
- No heap, scheduler, hardware abstraction layer, filesystem, or background
  work.
- Explicit resource limits, transport back-pressure, diagnostics, and
  lifecycle callbacks.
- CMake packages plus Arduino, Pico SDK, STM32Cube, and Teensy compatibility.

## Choose a role

Use `corelib_context_t` or `corelib::Device<>` for a standard addressable
endpoint with one upstream PFP link.

Use `corelib_gateway_context_t` or `corelib::Gateway<>` for an ECU that is both
an addressable endpoint and a router for bounded downstream links. Gateway
support is optional and absent from standard-only builds.

See the [architecture](docs/architecture.md),
[Device guide](docs/device.md), and [Gateway guide](docs/gateway.md) for the
complete integration boundaries.

## Quick start: C

The C11 API uses opaque contexts and caller-owned fixed storage. This skeleton
shows the complete initialisation shape; production firmware supplies the
transport implementation, persistent UUIDv4, and application handlers.

```c
#include <corelib/device.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

enum {
  MAXIMUM_MESSAGE_BYTES = 512,
  REASSEMBLY_SLOTS = 2,
  OUTBOUND_FRAMES = 16,
  PENDING_REQUESTS = 4
};

alignas(max_align_t) static uint8_t context_memory[CORELIB_CONTEXT_STORAGE_SIZE];
static uint8_t messages[MAXIMUM_MESSAGE_BYTES * REASSEMBLY_SLOTS];
static uint8_t received[255 * REASSEMBLY_SLOTS];
static uint8_t scratch[MAXIMUM_MESSAGE_BYTES];
static uint8_t outbound[CORELIB_FRAME_SIZE * OUTBOUND_FRAMES];
alignas(max_align_t) static uint8_t
    pending[CORELIB_PENDING_REQUEST_STORAGE_SIZE * PENDING_REQUESTS];
static corelib_context_t *device;

static corelib_send_result_t send_frame(void *user, corelib_link_id_t link, void *transport, const uint8_t frame[CORELIB_FRAME_SIZE]) {
  /* Buffer the complete frame without blocking or partially consuming it. */
  return application_send_frame(user, link, transport, frame);
}

corelib_status_t initialise_corelib(void *application, void *transport) {
  corelib_config_t config = {0};

  application_load_uuid(config.node_uuid);
  config.heartbeat_interval_ms = 2000u;
  config.application_response_timeout_ms = 1000u;
  config.maximum_transaction_data_size = 256u;
  config.callbacks.send_frame = send_frame;
  config.callbacks.user = application;
  config.storage.reassembly.message = messages;
  config.storage.reassembly.received = received;
  config.storage.reassembly_slot_count = REASSEMBLY_SLOTS;
  config.storage.maximum_message_size = MAXIMUM_MESSAGE_BYTES;
  config.storage.transaction_scratch = scratch;
  config.storage.outbound.frames = outbound;
  config.storage.outbound.capacity = OUTBOUND_FRAMES;
  config.storage.pending_requests.entries = pending;
  config.storage.pending_requests.capacity = PENDING_REQUESTS;
  config.storage.pending_requests.entry_size =
      corelib_pending_request_entry_size();

  corelib_status_t status = corelib_init(
      context_memory, sizeof(context_memory), &config, &device);
  if (status == CORELIB_OK) {
    status = corelib_add_link(device, 1u, transport);
  }
  return status;
}
```

Pass each complete received frame to `corelib_receive_frame()`, process
application work outside callbacks, and call `corelib_tick()` regularly with a
non-decreasing monotonic time. The [C Device guide](docs/device.md) covers the
full lifecycle, transactions, storage, and failure handling.

## Installation

Add Corelib directly to CMake and link the required role:

```cmake
add_subdirectory(path/to/corelib)
target_link_libraries(firmware PRIVATE Corelib::Device)
```

Enable `CORELIB_BUILD_GATEWAY` and link `Corelib::Gateway` for the C gateway.
Enable `CORELIB_BUILD_CPP`, provide ETL 20.x through `CORELIB_ETL_ROOT`, and
link `Corelib::DeviceCpp` or `Corelib::GatewayCpp` for the C++14 facade.

Installed-package consumers use `find_package(Corelib 1 CONFIG REQUIRED)` and
request the `Cpp`, `Gateway`, or `GatewayCpp` component when required.

Arduino users can install a release archive or repository checkout and include
`<Corelib.h>`. Gateway sketches define `CORELIB_ENABLE_GATEWAY=1` before the
include. See [installation](docs/installation.md), the
[C++ guide](docs/cpp.md), and the [Arduino guide](docs/arduino.md).

## Documentation

- [Introduction](docs/introduction.md)
- [Architecture](docs/architecture.md)
- [Transactions](docs/transactions.md)
- [Transport and scheduling](docs/transport.md)
- [Storage and capacity](docs/storage.md)
- [Examples](docs/examples.md)
- [C API reference](docs/reference/c/index.md)
- [C++ API reference](docs/reference/cpp/index.md)

The published site labels `main` as **Latest**. Numeric stable SemVer tags are
published separately, retaining the newest patch from every minor release
line.

## Development

Use Taskfile as the repository entry point:

```sh
task quick                 # Build and run the portable native test suite.
task check                 # Strict GCC and Clang builds.
task quality:format-check  # Verify owned-source formatting.
task quality:docs          # Verify comments and generated API pages.
task docs:build            # Build the public documentation site.
task docs:serve            # Serve documentation at http://localhost:8000.
task all                   # Run the complete software release gate.
```

## Compatibility

| Corelib | Portable Frame Protocol | Transaction Protocol | C | Optional C++ facade |
| --- | --- | --- | --- | --- |
| 1.0.x | 1 | 2 | C11 | C++14 with ETL 20.x |

## Licence

Corelib is available under the [MIT Licence](LICENSE).

# C++ facade

The optional C++14 facade owns Corelib's fixed storage and delegates every
operation to the C11 core. It adds type-safe values and callback bridging; it
does not contain another protocol implementation.

## Requirements

- A modern 32-bit MCU toolchain with C++14 support.
- Embedded Template Library 20.32.1 or newer within major version 20.
- No exceptions, RTTI, heap, operating system, or C++ standard-library runtime.

For CMake source builds, set `CORELIB_ETL_ROOT`, enable `CORELIB_BUILD_CPP`,
and link `Corelib::DeviceCpp`. Installed consumers request the `Cpp` package
component and make ETL discoverable through `ETL_ROOT` when required.

## Ownership

`corelib::Device` and `corelib::Gateway` cannot be copied or moved because the
C context points into storage owned by the facade object. Create each object in
storage that remains valid for the complete connection lifetime. The handler
supplied to `init()` is borrowed and must outlive the facade.

Callback frames, UUIDs, and transaction payloads are borrowed `etl::span`
views. Copy data needed after the callback returns. Callbacks are synchronous
and must not call the same context.

Template parameters set memory limits at compile time:

```cpp
corelib::Device<512, 2, 16, 4> device;
```

The parameters are maximum message bytes, reassembly slots, outbound frames,
and pending requests. Choose the smallest limits that satisfy the device.

## Device integration

Derive one long-lived handler and initialise the fixed-storage owner:

```cpp
class Handler final : public corelib::Handler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *,
                                corelib::FrameView frame) override {
    return transportWrite(frame);
  }

  void onTransaction(const corelib::TransactionView &value) override {
    queueRequest(value.id, value.data);
  }
};

Handler handler;
corelib::Device<512, 2, 16, 4> device;
corelib::Config config;

loadProvisionedUuid(config.nodeUuid);
config.maximumTransactionDataSize = 256;
const corelib::Status status = device.init(config, handler);
```

The same receive, tick, response, publication, and serialisation rules as the C
API apply. Operations return `corelib::Status`; normal failures do not throw or
set hidden global state.

## Gateway facade

`corelib::Gateway<>` owns the local Device storage and all gateway pools.
`corelib::GatewayHandler` extends the Device handler with discovery, bootstrap,
and topology callbacks. Template arguments bound links, routes, candidates,
assignments, forwarding frames, and control reassembly.

Use `nativeHandle()` only when interoperating with C-only application code.
See the [C++ API reference](./reference/cpp/) for the complete facade. Arduino
packaging and the umbrella header are covered in the [Arduino guide](./arduino).

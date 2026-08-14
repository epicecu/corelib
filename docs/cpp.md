# C++ and Arduino integration

The C++14 facade owns Corelib's fixed storage and delegates every operation to
the C11 core. It adds types and callback bridging; it does not contain another
protocol implementation.

## Requirements

- A modern 32-bit MCU toolchain with C++14 support.
- Embedded Template Library (ETL) 20.32.1 or newer within major version 20.
- No exceptions, RTTI, heap, operating system, or C++ standard-library runtime
  is required by the facade.

Arduino Library Manager installs ETL from the dependency declared in
`library.properties`. CMake source builds set `CORELIB_ETL_ROOT`, enable
`CORELIB_BUILD_CPP`, and link `Corelib::DeviceCpp`. Installed CMake
consumers request `find_package(Corelib COMPONENTS Cpp)` and make
ETL discoverable through `ETL_ROOT` when it is not on the normal include path.

## Ownership

`corelib::Device` cannot be copied or moved because the C context points
into storage owned by that object. Create it in storage that remains valid for
the complete connection lifetime. The `corelib::Handler` supplied to
`init()` is borrowed and must outlive Corelib.

Callback frames, UUIDs, and transaction payloads are borrowed `etl::span`
views. Copy data needed after the callback returns. Callbacks execute
synchronously and must not call the same Corelib context; defer responses and
publications until after the callback.

Template parameters set the memory limits at compile time:

```cpp
corelib::Device<512, 2, 16, 4> device;
```

The parameters are maximum message bytes, reassembly slots, outbound frames,
and pending requests. Board compile output reports the resulting flash and RAM
usage. Choose the smallest limits that satisfy the device protocol.

## Error handling

Operations return `corelib::Status`. Normal errors never throw and do not
set hidden global state. `corelib::SendResult::Busy` retains a complete frame
for retry; `Failed` reports a permanent transport failure.

Use `nativeHandle()` only when interoperating with C-only application code.
Normal C++ applications should use `addLink()`, `receive()`, `tick()`,
`respond()`, `publish()`, `reset()`, `usage()`, and `limits()` directly.

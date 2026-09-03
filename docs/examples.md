# Examples

The examples below focus on the boundary between firmware and Corelib. They are
short integration fragments rather than complete board support packages.

## C Device loop

```c
while (driver_has_frame()) {
  uint8_t frame[CORELIB_FRAME_SIZE];
  driver_read_frame(frame);
  (void)corelib_receive_frame(device, link_id, frame, monotonic_ms());
}

process_deferred_application_work();
(void)corelib_tick(device, monotonic_ms());
```

## C publication

```c
uint8_t encoded[128];
size_t encoded_size = encode_engine_status(encoded, sizeof(encoded));

corelib_status_t status = corelib_publish(
    device, false, ENGINE_STATUS_SHARE_ID, encoded, encoded_size);
if (status == CORELIB_CAPACITY_EXCEEDED) {
  schedule_publication_retry();
}
```

## C Gateway profile completion

```c
/* Called by the gateway's discovery callback. */
profile_start_discovery(link_id, profile_id, discovery_token);

/* Called later by the gateway-owning loop. */
(void)corelib_gateway_report_candidate(gateway, &candidate);
(void)corelib_gateway_complete_discovery(
    gateway, candidate.link_id, candidate.discovery_token, CORELIB_OK);
```

When a subsequent bootstrap callback completes, report its protocol control
result through `corelib_gateway_complete_assignment()`.

## C++ Device

```cpp
corelib::Device<512, 2, 16, 4> device;
corelib::Config config;
Handler handler;

loadProvisionedUuid(config.nodeUuid);
config.maximumTransactionDataSize = 256;
if (device.init(config, handler) == corelib::Status::Ok) {
  (void)device.addLink(1, &transport);
}
```

## Arduino sketches

The repository includes complete Arduino sketches that are compiled by
`task arduino:check`:

- [`DeviceSerial`](https://github.com/epicecu/corelib/tree/main/examples/DeviceSerial)
- [`DeviceTeensyRawHID`](https://github.com/epicecu/corelib/tree/main/examples/DeviceTeensyRawHID)
- [`GatewayIntegration`](https://github.com/epicecu/corelib/tree/main/examples/GatewayIntegration)

Use the [Device guide](./device), [Gateway guide](./gateway), and
[transport contract](./transport) when adapting them.

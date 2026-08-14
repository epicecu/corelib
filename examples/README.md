# Examples

These self-contained Arduino sketches show where application-owned transports
and device behaviour connect to the portable Corelib. Each sketch directory and
`.ino` file have the same name so they can be opened directly in Arduino IDE.

| Example | Role | Integration shown | Validation target |
| --- | --- | --- | --- |
| [`DeviceSerial`](DeviceSerial) | Standard device | Complete 64-byte PFP frames over Arduino `Serial` | Nano RP2040 Connect |
| [`DeviceTeensyRawHID`](DeviceTeensyRawHID) | Standard device | One PFP frame per Teensyduino RawHID report | Teensy 4.1 |
| [`GatewayIntegration`](GatewayIntegration) | Gateway ECU | Upstream/downstream links and profile callback boundaries | Nano RP2040 Connect |

Compile every example against the repository's pinned Arduino cores with:

```sh
task arduino:check
```

## Device examples

The device sketches demonstrate a single dedicated PFP link. They receive and
emit complete 64-byte frames, call `tick()` with monotonic time, and defer
application responses outside the synchronous transaction callback.

Replace each example UUID with a persistent UUIDv4 provisioned by the device.
Corelib does not derive device identity from the transport or populate
application Common and Share payloads.

## Gateway example

`GatewayIntegration` demonstrates an ECU that is both an addressable device and
a router. Define `CORELIB_ENABLE_GATEWAY=1` before including the Arduino
umbrella header, register exactly one upstream link and one or more downstream
links, and dedicate those links to PFP.

The example's frame-send, discovery, and bootstrap hooks deliberately return
failure until the application supplies its transport profile. Corelib does not
define how USB, CAN, serial, or another driver discovers an unassigned device
or carries its bootstrap assignment. See the
[gateway integration guide](../docs/gateway.md) for that boundary.

## Hardware tests

Examples are small integration guides, not device certification firmware. The
fixtures under [`tests/hardware`](../tests/hardware) add on-device self-tests,
deterministic Common/Share behaviour, upload tasks, and adapter end-to-end
manifests.

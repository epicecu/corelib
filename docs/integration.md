# Device integration

Corelib is a protocol service inside device firmware. It does not own the
transport driver, scheduler, clock, persistent storage, or device data model.

## Component boundary

```text
transport driver -> complete 64-byte frame -> Corelib
Corelib -> transaction callback -> device application handler
device handler -> respond or publish -> Corelib
Corelib -> send callback -> transport driver
```

This is the same useful separation used by the towing ECU firmware's USB task
and communication handler, without requiring firmware classes to inherit from
a Corelib task or communication base class.

## Application-owned data

Corelib decodes the shared Transaction Protocol envelope. Common and Share
payload bytes remain opaque. Device firmware owns its payload schemas, generated
sources, validation, storage updates, and business rules.

Callback data is borrowed until the callback returns. Copy data needed for
deferred work into application storage and retain the supplied transaction
identity. Call `corelib_respond()` after the callback has returned.

## Transport contract

Each registered link has an application-owned transport context. Received
reports must be converted to exactly one complete PFP frame before they enter
Corelib. Transport metadata such as a HID report ID is not part of the frame.

The send callback attempts one complete frame without waiting. Return:

- `CORELIB_SEND_ACCEPTED` when the whole frame was buffered.
- `CORELIB_SEND_BUSY` when no frame was consumed and retry is safe.
- `CORELIB_SEND_FAILED` after a permanent transport failure.

Never partially consume a frame and then return `BUSY`.

## Downstream bootstrap

A transport profile can discover a node before that node has a PFP address.
After receiving the profile-specific assignment, its integration calls
`corelib_accept_bootstrap_assignment()`. Corelib validates the UUID,
session, address, parent, heartbeat interval, link state, and inactive-session
precondition before activating the node. The integration relays the returned
status to its parent gateway using the profile's bootstrap mechanism.

Gateway-capable firmware uses the separate optional component described in
[gateway integration](gateway.md). The standard context remains endpoint-only.

## Migration from legacy Corelib

- Replace `Comm::processRead()` with driver receive plus
  `corelib_receive_frame()`.
- Replace `Comm::loop()` with the owning device loop or task plus
  `corelib_tick()`.
- Replace blocking `Comm::write()` implementations with the send callback.
- Replace `CommsHandler::usingProto()` with the transaction callback and
  explicit response or publication operations.

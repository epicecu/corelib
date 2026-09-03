# Transport and scheduling

Each registered link is dedicated to Portable Frame Protocol while it remains
registered. Corelib does not inspect whether the link is USB, CAN, serial, HID,
or another packet transport.

## Frame contract

The driver must assemble exactly one complete 64-byte PFP frame before calling
`corelib_receive_frame()`. Transport metadata such as a HID report identifier
is not part of the frame.

The send callback attempts one complete frame without blocking:

- `CORELIB_SEND_ACCEPTED` means the entire frame was buffered.
- `CORELIB_SEND_BUSY` means no bytes were consumed and retry is safe.
- `CORELIB_SEND_FAILED` means the attempt failed permanently.

Never partially consume a frame and then return `CORELIB_SEND_BUSY`.

## Execution ownership

One application execution context owns each Corelib context. Calls for that
context are serialised, and callbacks execute synchronously from the operation
that caused them. A callback must not call the same context.

In a main loop, poll the transport, submit complete frames, process deferred
responses, and call `corelib_tick()` every iteration.

In scheduled firmware, an interrupt or driver can place complete frames and
completion events into an application-owned bounded queue. One task drains the
queue and calls Corelib. Queue waits must remain bounded so heartbeats and
deadlines continue to advance.

Pass a non-decreasing monotonic millisecond value to every receive and tick
operation. Wall-clock time and clocks that can move backwards are unsuitable.

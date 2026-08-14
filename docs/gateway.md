# Gateway integration

Gateway support is an optional portable component for an ECU or other device
that is both a normal PFP endpoint and a router for downstream PFP devices.
Enabling it does not select or include a USB, CAN, serial, RTOS, or board
driver.

## Choose a firmware role

Use `corelib_context_t` or `corelib::Device<>` for a standard
downstream device. It owns one upstream link, accepts a topology session or a
profile-delivered bootstrap assignment, and never forwards frames.

Use `corelib_gateway_context_t` or `corelib::Gateway<>` for a
gateway-capable ECU. It has exactly one upstream link and bounded downstream
links, while still receiving transactions addressed to the ECU itself.

Gateway support is disabled by default. CMake projects enable
`CORELIB_BUILD_GATEWAY` and link `Corelib::Gateway` or
`Corelib::GatewayCpp`. Arduino builds define
`CORELIB_ENABLE_GATEWAY=1` before including `Corelib.h`.

## Dedicated PFP links

Every link registered with `Gateway` is dedicated exclusively to PFP until
it is removed. The portable Corelib stores the opaque link and profile identifiers
but does not inspect them or switch behaviour based on USB, CAN, serial, or
another transport.

The integration owns:

- complete 64-byte frame carriage;
- discovery of unassigned candidates;
- bootstrap delivery to one candidate;
- transport retry and error handling; and
- link and directly attached node loss detection.

The gateway owns sessions, addresses, discovery correlation, bounded routes,
frame forwarding, hop limits, topology controls, timeouts, and subtree
removal.

## Integration sequence

1. Allocate the local-device and gateway storage pools.
2. Initialise the gateway with frame-send, discovery, bootstrap, application,
   lifecycle, and diagnostic callbacks.
3. Register one available upstream link with profile ID zero.
4. Register available downstream links with non-zero public or private profile
   IDs.
5. Pass every complete received frame with its Corelib-local link ID.
6. In a discovery callback, start profile-specific discovery without blocking.
7. Report candidates with the supplied discovery token, then complete the
   discovery round.
8. In a bootstrap callback, deliver the assignment through the selected
   profile. The remote standard node calls
   `corelib_accept_bootstrap_assignment()`; report the resulting status
   with `corelib_gateway_complete_assignment()`.
9. Report link or direct-node loss and call `tick()` regularly with monotonic
   time.

Callbacks are synchronous and borrowed. They must not call the same gateway
context. `BUSY` means no work was consumed and Corelib retries from a later
operation without extending the original deadline.

Assignment completion identifies both the root transaction and candidate UUID
and uses `corelib_control_status_t`, not a local Corelib result. Before a
successful acknowledgement is exposed, the gateway reserves the provisional
route and every frame required for `addressAck` and `nodeReady`. Failure to
reserve all resources leaves the assignment pending and emits no success.

Reported candidates remain bounded cache entries after a discovery round ends
so the root can issue `addressAssign`. They are removed when consumed, rejected,
expired, their link is lost, or the topology session is cleared. Discovery,
assignment, and candidate-retention timeouts default to one second.

## Storage and execution

All gateway pools are caller-owned and independently bounded. The C++ facade
turns the capacities into template arguments and owns the corresponding fixed
arrays. A standard build does not link the gateway target and therefore
contains no gateway route, candidate, discovery, assignment, or forwarding
storage.

The gateway is serialized, non-blocking, and heap-free. An ISR should place
received frames or completion events into an application queue. A main loop or
integrator-owned RTOS task calls Corelib.

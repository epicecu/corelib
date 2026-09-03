# C Gateway integration

Gateway support is an optional portable component for an ECU or other device
that is both a normal Portable Frame Protocol (PFP) endpoint and a router for
downstream PFP devices. It does not select or include a USB, CAN, serial, RTOS,
or board driver.

## Choose a firmware role

Use `corelib_context_t` for a standard downstream Device. It owns one upstream
link, accepts a topology session or profile-delivered bootstrap assignment, and
never forwards frames.

Use `corelib_gateway_context_t` for a gateway-capable ECU. It has exactly one
upstream link and bounded downstream links, while still receiving transactions
addressed to the ECU itself.

Gateway support is disabled by default. CMake projects enable
`CORELIB_BUILD_GATEWAY` and link `Corelib::Gateway`. Standard builds therefore
contain no gateway routing implementation or storage.

## Dedicated Portable Frame Protocol links

Every link registered with a Gateway is dedicated exclusively to PFP until it
is removed. Corelib stores opaque link and profile identifiers but does not
switch behaviour based on the underlying transport.

The integration owns:

- complete 64-byte frame carriage;
- discovery of unassigned candidates;
- bootstrap delivery to one candidate;
- transport retry and error handling; and
- link and directly attached node loss detection.

The Gateway owns sessions, addresses, discovery correlation, bounded routes,
frame forwarding, hop limits, topology controls, timeouts, and subtree removal.

## Integration sequence

1. Allocate the local-Device and gateway storage pools.
2. Initialise the Gateway with frame-send, discovery, bootstrap, application,
   lifecycle, and diagnostic callbacks.
3. Register one available upstream link with profile ID zero.
4. Register downstream links with non-zero public or private profile IDs.
5. Pass every complete received frame with its Corelib-local link ID.
6. In a discovery callback, start profile-specific discovery without blocking.
7. Report candidates with the supplied token, then complete the discovery round.
8. In a bootstrap callback, deliver the assignment through the selected
   profile. The remote Device calls `corelib_accept_bootstrap_assignment()`;
   report its control status with `corelib_gateway_complete_assignment()`.
9. Report link or direct-node loss and call `corelib_gateway_tick()` regularly
   with monotonic time.

Callbacks are synchronous and borrowed. They must not call the same Gateway
context. `CORELIB_SEND_BUSY` means no work was consumed and Corelib retries
later without extending the original deadline.

## Assignment behaviour

Assignment completion identifies both the root transaction and candidate UUID
and uses `corelib_control_status_t`, not a local Corelib operation status.
Before exposing success, the Gateway reserves the provisional route and every
frame required for `addressAck` and `nodeReady`. Failure to reserve all
resources leaves the assignment pending and emits no success.

Reported candidates remain bounded cache entries after discovery ends so the
root can issue `addressAssign`. They are removed when consumed, rejected,
expired, their link is lost, or the topology session is cleared. Discovery,
assignment, and candidate-retention timeouts default to one second.

## Register links

Exactly one available upstream link connects towards the root adapter. Each
downstream link has a non-zero application-defined profile identifier:

```c
corelib_link_config_t upstream = {
    .link_id = 1u,
    .role = CORELIB_LINK_UPSTREAM,
    .available = true,
};
corelib_link_config_t downstream = {
    .link_id = 2u,
    .profile_id = 1u,
    .role = CORELIB_LINK_DOWNSTREAM,
    .transport_context = &downstream_transport,
    .available = true,
};

(void)corelib_gateway_add_link(gateway, &upstream);
(void)corelib_gateway_add_link(gateway, &downstream);
```

Pass every received frame to `corelib_gateway_receive_frame()` with its ingress
link. Complete discovery and bootstrap work later from the same serialised
execution context that owns the Gateway.

## Storage and execution

All gateway pools are caller-owned and independently bounded. The Gateway is
non-blocking, serialised, and heap-free. An ISR should place received frames or
completion events into an application queue; a main loop or integrator-owned
RTOS task calls Corelib.

See the [Gateway C API reference](./reference/c/gateway) and
[storage guide](./storage) for the complete configuration contract.

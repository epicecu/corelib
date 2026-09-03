# Troubleshooting

## No frames leave the device

Call `corelib_tick()` regularly and check the send callback result. Returning
`CORELIB_SEND_BUSY` retains a complete frame for retry; returning
`CORELIB_SEND_FAILED` discards that attempt and emits a diagnostic.

## Received frames are rejected

Submit exactly 64 PFP bytes without transport report identifiers or length
prefixes. Verify that all calls use the registered link ID and non-decreasing
monotonic time. Observe the diagnostic callback for rejected frames or
messages.

## Responses expire

Copy the transaction identity during the callback and respond from the owning
execution context before `application_response_timeout_ms`. Do not call back
into the same Corelib context from inside a callback.

## Capacity is exceeded

Inspect `corelib_usage()` and `corelib_limits()`, or their Gateway equivalents,
under representative worst-case traffic. Increase only the pool that reaches
its configured limit and account for transport back-pressure.

## A downstream node never becomes reachable

For a Gateway, correlate discovery completion with the supplied link and token.
Report the candidate before completing discovery, carry the supplied bootstrap
assignment through the correct profile, and pass the resulting control status
to `corelib_gateway_complete_assignment()`. A route is not published until the
whole assignment can commit.

## Arduino cannot see gateway types

Define `CORELIB_ENABLE_GATEWAY=1` before including `<Corelib.h>`. This controls
the Arduino umbrella header and is distinct from the CMake
`CORELIB_BUILD_GATEWAY` option.

# Concurrency and scheduling

The core is non-blocking, serialized, and non-reentrant. One application
execution context owns each Corelib context.

## Main-loop firmware

Poll the transport, submit every complete frame, and call
`corelib_tick()` on every loop iteration using a non-decreasing
monotonic millisecond clock.

## Scheduled firmware

A driver or interrupt may place complete frames into an application-owned
bounded queue. One owning task removes frames, calls
`corelib_receive_frame()`, processes deferred application responses, and
calls `corelib_tick()`.

Queue waits must be bounded so an idle transport cannot prevent heartbeats,
request expiry, or other timed protocol work. Queue capacity, overflow policy,
task priority, stack size, locking, and interrupt-safe queue operations remain
firmware decisions.

## Callback rules

Callbacks execute synchronously from Corelib call that caused them. They must
not call the same Corelib context. Copy required data and defer response or
publication until the callback returns. Calls for different contexts may be
serialized independently by the application.

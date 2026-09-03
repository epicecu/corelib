# Introduction

Corelib is portable middleware for microcontroller firmware that communicates
with Programmor. It implements Portable Frame Protocol (PFP) v1 and
Transaction Protocol v2 without owning a transport driver, scheduler, clock,
filesystem, or application data model.

The public C11 API is the canonical integration. An optional C++14 facade owns
the same fixed storage at compile time and delegates to the C implementation.
Neither path allocates heap memory.

## Choose a role

A **Device** is one addressable endpoint with one upstream PFP link. It accepts
transactions for its own Common and Share data and never forwards frames.

A **Gateway** is both an addressable Device and a router. It has exactly one
upstream link plus bounded downstream links, discovers candidates through
application-defined transport profiles, assigns addresses, and forwards PFP
traffic through the resulting topology.

Gateway support is optional. Standard Device builds do not contain gateway
routes, discovery state, assignment state, or forwarding queues.

## What the application owns

Corelib operates on complete 64-byte PFP frames. Firmware remains responsible
for:

- carrying complete frames over USB, CAN, serial, HID, or another transport;
- providing a non-decreasing monotonic millisecond clock;
- provisioning and retaining a persistent UUIDv4;
- encoding, validating, and storing Common and Share payloads; and
- choosing fixed capacities, scheduling policy, and transport error recovery.

Continue with [installation](./installation), learn the
[architecture](./architecture), or integrate a [C Device](./device).

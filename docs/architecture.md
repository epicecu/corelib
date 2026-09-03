# Architecture

Corelib sits between complete transport frames and application-owned device
behaviour:

```text
transport driver -> complete 64-byte PFP frame -> Corelib
transport driver <- complete 64-byte PFP frame <- Corelib
                                             |
                                             v
                              Common and Share handler
```

## Protocol layers

Portable Frame Protocol provides fixed-size frames, addressing, sessions,
fragmentation, reassembly, hop limits, and gateway carriage. Corelib accepts
and emits exactly one complete 64-byte PFP frame at a time.

Transaction Protocol carries Common and Share requests, responses, and
publications inside reassembled PFP messages. Corelib decodes the transaction
envelope but deliberately treats the application payload as opaque bytes.

The version numbers and framing behaviour are compatibility contracts. This
guide explains how firmware integrates them; it is not a substitute for their
normative wire specifications.

## Device data flow

An active Device receives a transaction through its synchronous callback. The
callback copies any data required later and retains the transaction identity.
After the callback returns, application code calls `corelib_respond()` or
publishes independent data with `corelib_publish()`.

## Gateway data flow

A Gateway embeds the same local endpoint and adds bounded routing state. Frames
addressed to the gateway reach its local transaction callback. Other valid
frames are forwarded between the single upstream link and an appropriate
downstream route.

Discovery and bootstrap carriage are transport-profile responsibilities.
Corelib supplies correlation tokens and assignments, then commits topology
only after the integration reports successful completion.

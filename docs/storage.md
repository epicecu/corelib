# Storage and capacity

Corelib allocates no heap memory. C applications provide every storage pool;
the C++ facade turns the same capacities into template arguments and owns the
arrays inside its Device or Gateway object.

## Device storage

Use `corelib_context_size()`, `corelib_context_alignment()`, and
`corelib_pending_request_entry_size()` instead of duplicating private sizes.
The public conservative constants are suitable for static allocation.

The variable pools require:

| Pool | Required bytes |
| --- | --- |
| Reassembled messages | `maximum_message_size * reassembly_slot_count` |
| Fragment markers | `255 * reassembly_slot_count` |
| Transaction scratch | `maximum_message_size` |
| Outbound frames | `CORELIB_FRAME_SIZE * outbound_capacity` |
| Pending requests | `corelib_pending_request_entry_size() * pending_capacity` |

`maximum_transaction_data_size` describes application bytes. The complete
transaction envelope also occupies message storage, so configuration
validation requires additional protocol overhead.

## Gateway storage

A Gateway contains Device storage plus independent fixed-entry pools for
links, routes, discovery rounds, retained candidates, pending assignments, and
forwarding frames. It also has local control-message reassembly storage.

Use `corelib_gateway_context_size()`,
`corelib_gateway_context_alignment()`, and `corelib_gateway_entry_size()` for
portable C allocation. A standard Device build does not link or reserve these
gateway pools.

## Choosing capacities

Start with the smallest limits that cover expected concurrent messages,
pending application work, and transport back-pressure. Exercise the worst-case
traffic pattern, then inspect `corelib_usage()` or `corelib_gateway_usage()`.
Capacity exhaustion is explicit and never causes hidden allocation.

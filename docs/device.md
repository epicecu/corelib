# C Device integration

Use `corelib_context_t` for a standard endpoint with one dedicated upstream
Portable Frame Protocol link. The context is opaque; firmware owns its backing
memory and every variable-capacity pool.

## 1. Allocate fixed storage

```c
#include <corelib/device.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

enum {
  MAXIMUM_MESSAGE_BYTES = 512,
  REASSEMBLY_SLOTS = 2,
  OUTBOUND_FRAMES = 16,
  PENDING_REQUESTS = 4
};

alignas(max_align_t) static uint8_t context_memory[CORELIB_CONTEXT_STORAGE_SIZE];
static uint8_t messages[MAXIMUM_MESSAGE_BYTES * REASSEMBLY_SLOTS];
static uint8_t received[255 * REASSEMBLY_SLOTS];
static uint8_t scratch[MAXIMUM_MESSAGE_BYTES];
static uint8_t outbound[CORELIB_FRAME_SIZE * OUTBOUND_FRAMES];
alignas(max_align_t) static uint8_t
    pending[CORELIB_PENDING_REQUEST_STORAGE_SIZE * PENDING_REQUESTS];
static corelib_context_t *device;
```

The size constants are conservative. Runtime size and alignment queries are
available for integrations that use platform-specific static allocators.

## 2. Implement callbacks

```c
static corelib_send_result_t send_frame(
    void *user, corelib_link_id_t link, void *transport,
    const uint8_t frame[CORELIB_FRAME_SIZE]) {
  (void)user;
  (void)link;
  return transport_try_write(transport, frame, CORELIB_FRAME_SIZE);
}

static void on_transaction(void *user,
                           const corelib_transaction_t *transaction) {
  application_queue_request(user, transaction->id,
                            transaction->data, transaction->data_size);
}
```

`transport_try_write()` must map its result to the
`corelib_send_result_t` contract. `application_queue_request()` must copy any
payload bytes it needs after the callback returns.

Lifecycle and diagnostic callbacks are optional. They are the appropriate
places to observe session changes, local reachability, rejected input, expired
requests, and resource pressure.

## 3. Initialise and register the link

```c
static corelib_status_t initialise_corelib(void *application,
                                           void *transport) {
  corelib_config_t config = {0};

  load_provisioned_uuid(config.node_uuid);
  config.heartbeat_interval_ms = 2000u;
  config.application_response_timeout_ms = 1000u;
  config.maximum_transaction_data_size = 256u;
  config.callbacks.send_frame = send_frame;
  config.callbacks.transaction = on_transaction;
  config.callbacks.user = application;
  config.storage.reassembly.message = messages;
  config.storage.reassembly.received = received;
  config.storage.reassembly_slot_count = REASSEMBLY_SLOTS;
  config.storage.maximum_message_size = MAXIMUM_MESSAGE_BYTES;
  config.storage.transaction_scratch = scratch;
  config.storage.outbound.frames = outbound;
  config.storage.outbound.capacity = OUTBOUND_FRAMES;
  config.storage.pending_requests.entries = pending;
  config.storage.pending_requests.capacity = PENDING_REQUESTS;
  config.storage.pending_requests.entry_size =
      corelib_pending_request_entry_size();

  corelib_status_t status = corelib_init(
      context_memory, sizeof(context_memory), &config, &device);
  if (status == CORELIB_OK) {
    status = corelib_add_link(device, 1u, transport);
  }
  return status;
}
```

Use a persistent RFC 4122 UUIDv4 provisioned for the device. Do not derive a
new identity on every boot.

## 4. Drive the context

```c
void communication_step(uint64_t now_ms) {
  uint8_t frame[CORELIB_FRAME_SIZE];

  while (transport_read_complete_frame(frame)) {
    (void)corelib_receive_frame(device, 1u, frame, now_ms);
  }

  application_complete_pending_requests(device);
  (void)corelib_tick(device, now_ms);
}
```

Serialise all calls for this context. On disconnect, remove the link or reset
the context according to the transport lifecycle. A downstream transport
profile can activate an unaddressed Device with
`corelib_accept_bootstrap_assignment()`.

See the [C API reference](./reference/c/device) for every operation.

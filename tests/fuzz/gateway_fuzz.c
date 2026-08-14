#include "internal/corelib_internal.h"
#include "corelib/gateway.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static corelib_send_result_t send_frame(void *user, uint16_t link,
                                                void *transport,
                                                const uint8_t frame[64]) {
  (void)user; (void)link; (void)transport; (void)frame;
  return CORELIB_SEND_ACCEPTED;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  enum { ENTRY = CORELIB_GATEWAY_ENTRY_STORAGE_SIZE };
  alignas(max_align_t) uint8_t gateway_memory[CORELIB_GATEWAY_CONTEXT_STORAGE_SIZE];
  alignas(max_align_t) uint8_t device_memory[CORELIB_CONTEXT_STORAGE_SIZE];
  alignas(max_align_t) uint8_t entries[6][4 * ENTRY];
  alignas(max_align_t) uint8_t pending[2 * CORELIB_PENDING_REQUEST_STORAGE_SIZE];
  uint8_t messages[2 * 256], received[2 * 255], scratch[256], outbound[8 * 64];
  uint8_t controls[2 * 128], control_received[2 * 255];
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = NULL;
  corelib_link_config_t link;
  corelib_bootstrap_assignment_t bootstrap;
  corelib_pfp_frame_t frame;
  size_t count, part;
  if (size == 0 || size > 128) return 0;
  memset(&config, 0, sizeof(config));
  config.device.node_uuid[6] = 0x40; config.device.node_uuid[8] = 0x80;
  config.device.heartbeat_interval_ms = 2000;
  config.device.application_response_timeout_ms = 1000;
  config.device.maximum_transaction_data_size = 128;
  config.device.callbacks.send_frame = send_frame;
  config.device.storage.reassembly.message = messages;
  config.device.storage.reassembly.received = received;
  config.device.storage.reassembly_slot_count = 2;
  config.device.storage.maximum_message_size = 256;
  config.device.storage.transaction_scratch = scratch;
  config.device.storage.outbound.frames = outbound;
  config.device.storage.outbound.capacity = 8;
  config.device.storage.pending_requests.entries = pending;
  config.device.storage.pending_requests.capacity = 2;
  config.device.storage.pending_requests.entry_size = CORELIB_PENDING_REQUEST_STORAGE_SIZE;
  config.storage.links = (corelib_entry_storage_t){entries[0], 4, ENTRY};
  config.storage.routes = (corelib_entry_storage_t){entries[1], 4, ENTRY};
  config.storage.discoveries = (corelib_entry_storage_t){entries[2], 4, ENTRY};
  config.storage.candidates = (corelib_entry_storage_t){entries[3], 4, ENTRY};
  config.storage.assignments = (corelib_entry_storage_t){entries[4], 4, ENTRY};
  config.storage.forwarding = (corelib_entry_storage_t){entries[5], 4, ENTRY};
  config.storage.control_reassembly.message = controls;
  config.storage.control_reassembly.received = control_received;
  config.storage.control_reassembly_slots = 2;
  config.storage.maximum_control_message_size = 128;
  config.storage.device_context_memory = device_memory;
  config.storage.device_context_memory_size = sizeof(device_memory);
  config.discovery_timeout_ms = 1000;
  config.assignment_timeout_ms = 1000;
  config.candidate_retention_timeout_ms = 1000;
  if (corelib_gateway_init(gateway_memory, sizeof(gateway_memory), &config,
                                  &gateway) != CORELIB_OK) return 0;
  memset(&link, 0, sizeof(link)); link.link_id = 1;
  link.role = CORELIB_LINK_UPSTREAM; link.available = true;
  if (corelib_gateway_add_link(gateway, &link) != CORELIB_OK) return 0;
  memset(&bootstrap, 0, sizeof(bootstrap));
  memcpy(bootstrap.node_uuid, config.device.node_uuid, 16);
  bootstrap.session_id = 1; bootstrap.transaction_id = 1;
  bootstrap.heartbeat_interval_ms = 2000; bootstrap.node_address = 2;
  bootstrap.parent_address = 1; bootstrap.link_id = 1;
  if (corelib_gateway_accept_bootstrap_assignment(gateway, &bootstrap, 0) !=
      CORELIB_OK) return 0;
  count = (size + 39) / 40;
  memset(&frame, 0, sizeof(frame)); frame.type = CORELIB_PFP_CONTROL;
  frame.destination = 2; frame.source = 1; frame.session_id = 1;
  frame.message_id = 2; frame.message_length = (uint16_t)size;
  frame.frame_count = (uint8_t)count; frame.hop_limit = 8; frame.priority = 3;
  for (part = 0; part < count; ++part) {
    uint8_t encoded[64];
    const size_t logical = (data[0] & 1u) != 0u ? count - part - 1u : part;
    const size_t offset = logical * 40;
    size_t chunk = size - offset;
    if (chunk > 40) chunk = 40;
    frame.frame_index = (uint8_t)(logical + 1);
    memset(frame.payload, 0, 40); memcpy(frame.payload, data + offset, chunk);
    if (corelib_pfp_encode(&frame, encoded) == CORELIB_OK)
      (void)corelib_gateway_receive_frame(gateway, 1, encoded, 1);
  }
  (void)corelib_gateway_tick(gateway, 1001);
  return 0;
}

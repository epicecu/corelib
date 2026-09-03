extern "C" {
#include "corelib/gateway.h"
#include "internal/corelib_internal.h"
}

#include <gtest/gtest.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

enum { ENTRY = CORELIB_GATEWAY_ENTRY_STORAGE_SIZE };

typedef struct {
  uint8_t frames[64][64];
  uint16_t links[64];
  size_t frame_count;
  size_t discover_count;
  size_t bootstrap_count;
  size_t topology_count;
  size_t diagnostic_count;
  size_t discover_busy;
  size_t bootstrap_busy;
  size_t send_busy;
  size_t send_failed;
  bool discover_failed;
  bool bootstrap_failed;
  uint16_t topology_addresses[32];
  bool topology_reachable[32];
  corelib_bootstrap_assignment_t assignment;
  uint8_t token[16];
} fixture_t;

typedef struct {
  alignas(max_align_t) uint8_t gateway_context[CORELIB_GATEWAY_CONTEXT_STORAGE_SIZE];
  alignas(max_align_t) uint8_t device_context[CORELIB_CONTEXT_STORAGE_SIZE];
  uint8_t messages[2 * 256];
  uint8_t received[2 * 255];
  uint8_t scratch[256];
  uint8_t outbound[16 * 64];
  alignas(max_align_t) uint8_t pending[4 * CORELIB_PENDING_REQUEST_STORAGE_SIZE];
  alignas(max_align_t) uint8_t links[3 * ENTRY];
  alignas(max_align_t) uint8_t routes[8 * ENTRY];
  alignas(max_align_t) uint8_t discoveries[2 * ENTRY];
  alignas(max_align_t) uint8_t candidates[8 * ENTRY];
  alignas(max_align_t) uint8_t assignments[4 * ENTRY];
  alignas(max_align_t) uint8_t forwarding[16 * ENTRY];
  uint8_t control_messages[2 * 128];
  uint8_t control_received[2 * 255];
} storage_t;

static corelib_send_result_t send_frame(void *user, uint16_t link,
                                        void *transport,
                                        const uint8_t frame[64]) {
  fixture_t *f = static_cast<fixture_t *>(user);
  EXPECT_TRUE(transport == (void *)(uintptr_t)link);
  if (f->send_busy != 0u) {
    --f->send_busy;
    return CORELIB_SEND_BUSY;
  }
  if (f->send_failed != 0u) {
    --f->send_failed;
    return CORELIB_SEND_FAILED;
  }
  EXPECT_TRUE(f->frame_count < 64);
  f->links[f->frame_count] = link;
  memcpy(f->frames[f->frame_count++], frame, 64);
  return CORELIB_SEND_ACCEPTED;
}

static corelib_send_result_t discover(void *user, uint16_t link,
                                      void *transport, uint32_t profile,
                                      const uint8_t token[16]) {
  fixture_t *f = static_cast<fixture_t *>(user);
  EXPECT_TRUE(link == 2 && transport == (void *)(uintptr_t)2 && profile == 0x80000001u);
  ++f->discover_count;
  memcpy(f->token, token, 16);
  if (f->discover_busy != 0) {
    --f->discover_busy;
    return CORELIB_SEND_BUSY;
  }
  if (f->discover_failed)
    return CORELIB_SEND_FAILED;
  return CORELIB_SEND_ACCEPTED;
}

static corelib_send_result_t bootstrap(
    void *user, void *transport,
    const corelib_bootstrap_assignment_t *assignment) {
  fixture_t *f = static_cast<fixture_t *>(user);
  EXPECT_TRUE(transport == (void *)(uintptr_t)2);
  ++f->bootstrap_count;
  f->assignment = *assignment;
  if (f->bootstrap_busy != 0) {
    --f->bootstrap_busy;
    return CORELIB_SEND_BUSY;
  }
  if (f->bootstrap_failed)
    return CORELIB_SEND_FAILED;
  return CORELIB_SEND_ACCEPTED;
}

static void diagnostic(void *user, corelib_diagnostic_t code,
                       corelib_status_t status) {
  fixture_t *f = static_cast<fixture_t *>(user);
  (void)code;
  (void)status;
  ++f->diagnostic_count;
}

static void topology(void *user, const corelib_topology_event_t *event) {
  fixture_t *f = static_cast<fixture_t *>(user);
  EXPECT_TRUE(f->topology_count < 32);
  f->topology_addresses[f->topology_count] = event->node_address;
  f->topology_reachable[f->topology_count] = event->reachable;
  ++f->topology_count;
}

static size_t captured_control(const fixture_t *fixture, uint8_t opcode,
                               uint8_t *message, size_t capacity) {
  size_t i;
  for (i = 0; i < fixture->frame_count; ++i) {
    corelib_pfp_frame_t first;
    if (corelib_pfp_decode(fixture->frames[i], &first) != CORELIB_OK ||
        first.type != CORELIB_PFP_CONTROL || first.frame_index != 1 ||
        first.payload[0] != 1 || first.payload[1] != opcode ||
        first.message_length > capacity)
      continue;
    {
      size_t j;
      for (j = i; j < fixture->frame_count; ++j) {
        corelib_pfp_frame_t part;
        size_t offset, chunk;
        if (corelib_pfp_decode(fixture->frames[j], &part) != CORELIB_OK ||
            part.message_id != first.message_id || part.source != first.source ||
            part.destination != first.destination)
          continue;
        offset = ((size_t)part.frame_index - 1u) * 40u;
        chunk = first.message_length - offset;
        if (chunk > 40u)
          chunk = 40u;
        memcpy(message + offset, part.payload, chunk);
      }
      return first.message_length;
    }
  }
  return 0;
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static size_t header(uint8_t *p, uint8_t opcode, uint32_t transaction) {
  memset(p, 0, 8);
  p[0] = 1;
  p[1] = opcode;
  put32(p + 4, transaction);
  return 8;
}
static size_t field(uint8_t *p, size_t n, uint8_t id, const void *value,
                    uint8_t size) {
  p[n++] = (uint8_t)(0x80u | id);
  p[n++] = size;
  memcpy(p + n, value, size);
  return n + size;
}
static size_t field16(uint8_t *p, size_t n, uint8_t id, uint16_t value) {
  uint8_t b[2];
  put16(b, value);
  return field(p, n, id, b, 2);
}
static size_t field32(uint8_t *p, size_t n, uint8_t id, uint32_t value) {
  uint8_t b[4];
  put32(b, value);
  return field(p, n, id, b, 4);
}

static void deliver(corelib_gateway_context_t *gateway, uint16_t ingress,
                    uint8_t type, uint16_t destination, uint16_t source,
                    uint32_t session, uint32_t message_id, const uint8_t *message,
                    size_t size, uint64_t now) {
  corelib_pfp_frame_t frame;
  size_t index;
  size_t count = size == 0 ? 1 : (size + 39) / 40;
  memset(&frame, 0, sizeof(frame));
  frame.type = type;
  frame.destination = destination;
  frame.source = source;
  frame.session_id = session;
  frame.message_id = message_id;
  frame.frame_count = (uint8_t)count;
  frame.message_length = (uint16_t)size;
  frame.hop_limit = type == CORELIB_PFP_PROBE_REQUEST ? 1 : 8;
  frame.priority = 3;
  for (index = 0; index < count; ++index) {
    uint8_t encoded[64];
    size_t offset = index * 40;
    size_t chunk = size > offset ? size - offset : 0;
    if (chunk > 40)
      chunk = 40;
    frame.frame_index = (uint8_t)(index + 1);
    memset(frame.payload, 0, 40);
    if (chunk != 0)
      memcpy(frame.payload, message + offset, chunk);
    EXPECT_TRUE(corelib_pfp_encode(&frame, encoded) == CORELIB_OK);
    EXPECT_TRUE(corelib_gateway_receive_frame(gateway, ingress, encoded, now) ==
                CORELIB_OK);
  }
}

static corelib_status_t deliver_reverse_result(
    corelib_gateway_context_t *gateway, uint16_t ingress, uint8_t type,
    uint16_t destination, uint16_t source, uint32_t session,
    uint32_t message_id, const uint8_t *message, size_t size, uint64_t now) {
  corelib_pfp_frame_t frame;
  size_t part;
  const size_t count = (size + 39) / 40;
  EXPECT_TRUE(count > 1);
  memset(&frame, 0, sizeof(frame));
  frame.type = type;
  frame.destination = destination;
  frame.source = source;
  frame.session_id = session;
  frame.message_id = message_id;
  frame.frame_count = (uint8_t)count;
  frame.message_length = (uint16_t)size;
  frame.hop_limit = 8;
  frame.priority = 3;
  for (part = count; part > 0; --part) {
    uint8_t encoded[64];
    const size_t offset = (part - 1) * 40;
    size_t chunk = size - offset;
    if (chunk > 40)
      chunk = 40;
    frame.frame_index = (uint8_t)part;
    memset(frame.payload, 0, 40);
    memcpy(frame.payload, message + offset, chunk);
    EXPECT_TRUE(corelib_pfp_encode(&frame, encoded) == CORELIB_OK);
    const corelib_status_t status = corelib_gateway_receive_frame(
        gateway, ingress, encoded, now);
    if (part != 1)
      EXPECT_TRUE(status == CORELIB_OK);
    else
      return status;
  }
  return CORELIB_INVALID_STATE;
}

static void deliver_reverse(corelib_gateway_context_t *gateway,
                            uint16_t ingress, uint8_t type,
                            uint16_t destination, uint16_t source,
                            uint32_t session, uint32_t message_id,
                            const uint8_t *message, size_t size, uint64_t now) {
  EXPECT_TRUE(deliver_reverse_result(gateway, ingress, type, destination, source,
                                     session, message_id, message, size, now) ==
              CORELIB_OK);
}

static corelib_status_t deliver_one(
    corelib_gateway_context_t *gateway, uint16_t ingress, uint8_t type,
    uint16_t destination, uint16_t source, uint32_t session,
    uint32_t message_id, const uint8_t *message, size_t size, uint64_t now) {
  corelib_pfp_frame_t frame;
  uint8_t encoded[64];
  EXPECT_TRUE(size <= 40);
  memset(&frame, 0, sizeof(frame));
  frame.type = type;
  frame.destination = destination;
  frame.source = source;
  frame.session_id = session;
  frame.message_id = message_id;
  frame.frame_index = 1;
  frame.frame_count = 1;
  frame.message_length = (uint16_t)size;
  frame.hop_limit = 8;
  frame.priority = 3;
  memcpy(frame.payload, message, size);
  EXPECT_TRUE(corelib_pfp_encode(&frame, encoded) == CORELIB_OK);
  return corelib_gateway_receive_frame(gateway, ingress, encoded, now);
}

static void configure(corelib_gateway_config_t *config, storage_t *s,
                      fixture_t *f) {
  memset(config, 0, sizeof(*config));
  config->device.node_uuid[6] = 0x40;
  config->device.node_uuid[8] = 0x80;
  config->device.heartbeat_interval_ms = 2000;
  config->device.application_response_timeout_ms = 1000;
  config->device.maximum_transaction_data_size = 128;
  config->device.callbacks.send_frame = send_frame;
  config->device.callbacks.diagnostic = diagnostic;
  config->device.callbacks.user = f;
  config->device.storage.reassembly.message = s->messages;
  config->device.storage.reassembly.received = s->received;
  config->device.storage.reassembly_slot_count = 2;
  config->device.storage.maximum_message_size = 256;
  config->device.storage.transaction_scratch = s->scratch;
  config->device.storage.outbound.frames = s->outbound;
  config->device.storage.outbound.capacity = 16;
  config->device.storage.pending_requests.entries = s->pending;
  config->device.storage.pending_requests.capacity = 4;
  config->device.storage.pending_requests.entry_size = CORELIB_PENDING_REQUEST_STORAGE_SIZE;
#define STORE(name, count)                   \
  do {                                       \
    config->storage.name.entries = s->name;  \
    config->storage.name.capacity = count;   \
    config->storage.name.entry_size = ENTRY; \
  } while (0)
  STORE(links, 3);
  STORE(routes, 8);
  STORE(discoveries, 2);
  STORE(candidates, 8);
  STORE(assignments, 4);
  STORE(forwarding, 16);
#undef STORE
  config->storage.control_reassembly.message = s->control_messages;
  config->storage.control_reassembly.received = s->control_received;
  config->storage.control_reassembly_slots = 2;
  config->storage.maximum_control_message_size = 128;
  config->storage.device_context_memory = s->device_context;
  config->storage.device_context_memory_size = sizeof(s->device_context);
  config->callbacks.discover = discover;
  config->callbacks.bootstrap_assign = bootstrap;
  config->callbacks.topology_changed = topology;
  config->discovery_timeout_ms = 1000;
  config->assignment_timeout_ms = 1000;
  config->candidate_retention_timeout_ms = 1000;
}

TEST(GatewayCApi, HandlesDiscoveryAssignmentRoutingAndLoss) {
  storage_t storage;
  fixture_t fixture;
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = NULL;
  corelib_link_config_t link;
  uint8_t control[80];
  uint8_t node_uuid[16] = {0};
  uint8_t descendant_uuid[16] = {0};
  uint8_t discovery_token[16];
  size_t n;
  corelib_candidate_t candidate;
  corelib_gateway_usage_t usage;
  corelib_pfp_frame_t data;
  uint8_t encoded[64];
  uint8_t captured[80];
  uint8_t expected[80];
  size_t expected_n;
  memset(&storage, 0, sizeof(storage));
  memset(&fixture, 0, sizeof(fixture));
  node_uuid[6] = 0x40;
  node_uuid[8] = 0x80;
  node_uuid[15] = 1;
  descendant_uuid[6] = 0x40;
  descendant_uuid[8] = 0x80;
  descendant_uuid[15] = 2;
  configure(&config, &storage, &fixture);
  EXPECT_TRUE(corelib_gateway_init(storage.gateway_context,
                                   sizeof(storage.gateway_context), &config, &gateway) == CORELIB_OK);
  memset(&link, 0, sizeof(link));
  link.link_id = 1;
  link.role = CORELIB_LINK_UPSTREAM;
  link.available = true;
  link.transport_context = (void *)(uintptr_t)1;
  EXPECT_TRUE(corelib_gateway_add_link(gateway, &link) == CORELIB_OK);
  link.link_id = 2;
  link.role = CORELIB_LINK_DOWNSTREAM;
  link.profile_id = 0x80000001u;
  link.transport_context = (void *)(uintptr_t)2;
  EXPECT_TRUE(corelib_gateway_add_link(gateway, &link) == CORELIB_OK);

  n = header(control, 1, 10);
  n = field32(control, n, 9, 2000);
  deliver(gateway, 1, CORELIB_PFP_CONTROL, 2, 1, 0x11223344u, 1,
          control, n, 1);
  EXPECT_TRUE(fixture.frame_count == 1 && fixture.links[0] == 1);
  EXPECT_TRUE(fixture.frames[0][50] == (uint8_t)(0x80u | 6u));

  memset(discovery_token, 0x5a, sizeof(discovery_token));
  fixture.discover_busy = 1;
  n = header(control, 3, 11);
  n = field(control, n, 11, discovery_token, 16);
  deliver(gateway, 1, CORELIB_PFP_CONTROL, 2, 1, 0x11223344u, 2,
          control, n, 2);
  EXPECT_TRUE(fixture.discover_count == 1);
  EXPECT_TRUE(corelib_gateway_tick(gateway, 2) == CORELIB_OK);
  EXPECT_TRUE(fixture.discover_count == 2);
  memset(&candidate, 0, sizeof(candidate));
  memcpy(candidate.node_uuid, node_uuid, 16);
  memcpy(candidate.discovery_token, fixture.token, 16);
  candidate.link_id = 2;
  candidate.capabilities = 0;
  EXPECT_TRUE(corelib_gateway_report_candidate(gateway, &candidate) == CORELIB_OK);
  expected_n = header(expected, 4, 0);
  expected_n = field(expected, expected_n, 1, node_uuid, 16);
  expected_n = field16(expected, expected_n, 3, 2);
  expected_n = field16(expected, expected_n, 4, 2);
  expected_n = field32(expected, expected_n, 5, 0x80000001u);
  expected_n = field32(expected, expected_n, 6, 0);
  expected_n = field(expected, expected_n, 11, discovery_token, 16);
  EXPECT_TRUE(captured_control(&fixture, 4, captured, sizeof(captured)) == expected_n);
  EXPECT_TRUE(memcmp(captured, expected, expected_n) == 0);

  n = header(control, 5, 12);
  n = field(control, n, 1, node_uuid, 16);
  n = field16(control, n, 2, 3);
  n = field16(control, n, 3, 2);
  n = field16(control, n, 4, 2);
  n = field32(control, n, 9, 2000);
  fixture.bootstrap_busy = 1;
  deliver_reverse(gateway, 1, CORELIB_PFP_CONTROL, 2, 1, 0x11223344u, 3,
                  control, n, 3);
  EXPECT_TRUE(fixture.bootstrap_count == 1 && fixture.assignment.node_address == 3);
  EXPECT_TRUE(corelib_gateway_tick(gateway, 3) == CORELIB_OK);
  EXPECT_TRUE(fixture.bootstrap_count == 2);
  EXPECT_TRUE(corelib_gateway_complete_assignment(
                  gateway, 12, node_uuid, CORELIB_CONTROL_SUCCESS) == CORELIB_OK);
  expected_n = header(expected, 6, 12);
  expected_n = field(expected, expected_n, 1, node_uuid, 16);
  expected_n = field16(expected, expected_n, 2, 3);
  expected_n = field16(expected, expected_n, 3, 2);
  expected_n = field16(expected, expected_n, 7, 0);
  EXPECT_TRUE(captured_control(&fixture, 6, captured, sizeof(captured)) == expected_n);
  EXPECT_TRUE(memcmp(captured, expected, expected_n) == 0);
  EXPECT_TRUE(fixture.topology_count == 1);
  EXPECT_TRUE(fixture.topology_addresses[0] == 3 && fixture.topology_reachable[0]);
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.links == 2 && usage.routes == 1 && usage.assignments == 0 &&
              usage.candidates == 0);

  memset(&data, 0, sizeof(data));
  data.type = CORELIB_PFP_DATA;
  data.destination = 3;
  data.source = 1;
  data.session_id = 0x11223344u;
  data.message_id = 20;
  data.frame_index = 1;
  data.frame_count = 1;
  data.message_length = 1;
  data.hop_limit = 8;
  data.priority = 3;
  data.payload[0] = 0xaa;
  EXPECT_TRUE(corelib_pfp_encode(&data, encoded) == CORELIB_OK);
  EXPECT_TRUE(corelib_gateway_receive_frame(gateway, 1, encoded, 4) == CORELIB_OK);
  EXPECT_TRUE(fixture.links[fixture.frame_count - 1] == 2);
  data.destination = 1;
  data.source = 3;
  data.message_id = 21;
  EXPECT_TRUE(corelib_pfp_encode(&data, encoded) == CORELIB_OK);
  EXPECT_TRUE(corelib_gateway_receive_frame(gateway, 2, encoded, 5) == CORELIB_OK);
  EXPECT_TRUE(fixture.links[fixture.frame_count - 1] == 1);

  /* A downstream gateway announces a nested node with fragmented control. */
  n = header(control, 7, 13);
  n = field(control, n, 1, descendant_uuid, 16);
  n = field16(control, n, 2, 4);
  n = field16(control, n, 3, 3);
  n = field32(control, n, 6, 0);
  n = field16(control, n, 7, 0);
  deliver_reverse(gateway, 2, CORELIB_PFP_CONTROL, 2, 3, 0x11223344u,
                  22, control, n, 6);
  EXPECT_TRUE(fixture.topology_count == 2);
  EXPECT_TRUE(fixture.topology_addresses[1] == 4 && fixture.topology_reachable[1]);
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 2);

  /* Extend the same branch to the normative eight-hop boundary. */
  {
    uint16_t address;
    for (address = 5; address <= 10; ++address) {
      memset(descendant_uuid, 0, sizeof(descendant_uuid));
      descendant_uuid[6] = 0x40;
      descendant_uuid[8] = 0x80;
      descendant_uuid[15] = (uint8_t)address;
      n = header(control, 7, (uint32_t)(20u + address));
      n = field(control, n, 1, descendant_uuid, 16);
      n = field16(control, n, 2, address);
      n = field16(control, n, 3, (uint16_t)(address - 1u));
      n = field32(control, n, 6, 0);
      n = field16(control, n, 7, 0);
      deliver_reverse(gateway, 2, CORELIB_PFP_CONTROL, 2, 3,
                      0x11223344u, (uint32_t)(30u + address), control, n,
                      (uint64_t)(7u + address));
    }
  }
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 8);

  memset(descendant_uuid, 0, sizeof(descendant_uuid));
  descendant_uuid[6] = 0x40;
  descendant_uuid[8] = 0x80;
  descendant_uuid[15] = 11;
  n = header(control, 7, 31);
  n = field(control, n, 1, descendant_uuid, 16);
  n = field16(control, n, 2, 11);
  n = field16(control, n, 3, 10);
  n = field32(control, n, 6, 0);
  n = field16(control, n, 7, 0);
  EXPECT_TRUE(deliver_reverse_result(gateway, 2, CORELIB_PFP_CONTROL, 2, 3,
                                     0x11223344u, 41, control, n, 18) ==
              CORELIB_INVALID_STATE);
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 8);

  data.destination = 4;
  data.source = 1;
  data.message_id = 23;
  EXPECT_TRUE(corelib_pfp_encode(&data, encoded) == CORELIB_OK);
  EXPECT_TRUE(corelib_gateway_receive_frame(gateway, 1, encoded, 20) == CORELIB_OK);
  EXPECT_TRUE(fixture.links[fixture.frame_count - 1] == 2);

  /* A trusted unknown destination produces a routeError toward the root. */
  data.destination = 99;
  data.message_id = 24;
  EXPECT_TRUE(corelib_pfp_encode(&data, encoded) == CORELIB_OK);
  EXPECT_TRUE(corelib_gateway_receive_frame(gateway, 1, encoded, 21) ==
              CORELIB_NOT_FOUND);
  EXPECT_TRUE(fixture.links[fixture.frame_count - 1] == 1);
  EXPECT_TRUE(fixture.frames[fixture.frame_count - 1][21] == 10);

  /* A malformed trusted control generates controlError without changing routes. */
  n = header(control, 3, 99);
  EXPECT_TRUE(deliver_one(gateway, 1, CORELIB_PFP_CONTROL, 2, 1,
                          0x11223344u, 25, control, n, 22) ==
              CORELIB_INVALID_FRAME);
  EXPECT_TRUE(fixture.links[fixture.frame_count - 1] == 1);
  EXPECT_TRUE(fixture.frames[fixture.frame_count - 1][21] == 12);
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 8);

  /* A downstream send failure removes the branch without recursively flushing
     the node-removed notifications queued for the upstream link. */
  data.destination = 4;
  ++data.message_id;
  EXPECT_TRUE(corelib_pfp_encode(&data, encoded) == CORELIB_OK);
  fixture.send_failed = 1;
  EXPECT_TRUE(corelib_gateway_receive_frame(gateway, 1, encoded, 23) ==
              CORELIB_INVALID_STATE);
  EXPECT_TRUE(fixture.topology_count == 16);
  EXPECT_TRUE(fixture.topology_addresses[8] == 10 && !fixture.topology_reachable[8]);
  EXPECT_TRUE(fixture.topology_addresses[15] == 3 && !fixture.topology_reachable[15]);
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 0);
}

TEST(GatewayCApi, KeepsAssignmentCapacityAtomic) {
  storage_t storage;
  fixture_t fixture;
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = NULL;
  corelib_link_config_t link;
  corelib_candidate_t candidate;
  corelib_gateway_usage_t usage;
  uint8_t control[80];
  uint8_t token[16];
  uint8_t uuid[16] = {0};
  size_t n;
  size_t sent_before;
  unsigned node;
  memset(&storage, 0, sizeof(storage));
  memset(&fixture, 0, sizeof(fixture));
  configure(&config, &storage, &fixture);
  config.storage.routes.capacity = 1;
  EXPECT_TRUE(corelib_gateway_init(storage.gateway_context,
                                   sizeof(storage.gateway_context), &config, &gateway) == CORELIB_OK);
  memset(&link, 0, sizeof(link));
  link.link_id = 1;
  link.role = CORELIB_LINK_UPSTREAM;
  link.available = true;
  link.transport_context = (void *)(uintptr_t)1;
  EXPECT_TRUE(corelib_gateway_add_link(gateway, &link) == CORELIB_OK);
  link.link_id = 2;
  link.role = CORELIB_LINK_DOWNSTREAM;
  link.profile_id = 0x80000001u;
  link.transport_context = (void *)(uintptr_t)2;
  EXPECT_TRUE(corelib_gateway_add_link(gateway, &link) == CORELIB_OK);
  n = header(control, 1, 1);
  n = field32(control, n, 9, 2000);
  deliver(gateway, 1, CORELIB_PFP_CONTROL, 2, 1, 0x55667788u, 1,
          control, n, 1);

  for (node = 0; node < 2; ++node) {
    memset(token, (int)(0x30u + node), sizeof(token));
    n = header(control, 3, (uint32_t)(2u + node));
    n = field(control, n, 11, token, 16);
    deliver(gateway, 1, CORELIB_PFP_CONTROL, 2, 1, 0x55667788u,
            (uint32_t)(2u + node), control, n, (uint64_t)(2u + node * 3u));
    memset(&candidate, 0, sizeof(candidate));
    memset(uuid, 0, sizeof(uuid));
    uuid[6] = 0x40;
    uuid[8] = 0x80;
    uuid[15] = (uint8_t)(10u + node);
    memcpy(candidate.node_uuid, uuid, 16);
    memcpy(candidate.discovery_token, token, 16);
    candidate.link_id = 2;
    EXPECT_TRUE(corelib_gateway_report_candidate(gateway, &candidate) == CORELIB_OK);
    n = header(control, 5, (uint32_t)(10u + node));
    n = field(control, n, 1, uuid, 16);
    n = field16(control, n, 2, (uint16_t)(3u + node));
    n = field16(control, n, 3, 2);
    n = field16(control, n, 4, 2);
    n = field32(control, n, 9, 2000);
    deliver_reverse(gateway, 1, CORELIB_PFP_CONTROL, 2, 1, 0x55667788u,
                    (uint32_t)(20u + node), control, n,
                    (uint64_t)(3u + node * 3u));
    if (node == 0) {
      EXPECT_TRUE(corelib_gateway_complete_assignment(
                      gateway, 10, uuid, CORELIB_CONTROL_SUCCESS) == CORELIB_OK);
    } else {
      sent_before = fixture.frame_count;
      EXPECT_TRUE(corelib_gateway_complete_assignment(
                      gateway, 11, uuid, CORELIB_CONTROL_SUCCESS) ==
                  CORELIB_CAPACITY_EXCEEDED);
      EXPECT_TRUE(fixture.frame_count == sent_before);
    }
  }
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 1 && usage.assignments == 1);
  EXPECT_TRUE(fixture.topology_count == 1);
  EXPECT_TRUE(corelib_gateway_tick(gateway, 1006) == CORELIB_OK);
  EXPECT_TRUE(corelib_gateway_usage(gateway, &usage) == CORELIB_OK);
  EXPECT_TRUE(usage.routes == 1 && usage.assignments == 0 && usage.candidates == 0);
  EXPECT_TRUE(fixture.diagnostic_count != 0);
}

TEST(GatewayCApi, RejectsNullContextsAndOutputs) {
  uint8_t frame[CORELIB_FRAME_SIZE] = {};
  uint8_t uuid[16] = {};
  corelib_link_config_t link = {};
  corelib_candidate_t candidate = {};
  corelib_bootstrap_assignment_t assignment = {};
  corelib_transaction_id_t request = {};
  corelib_gateway_usage_t usage = {};
  corelib_gateway_limits_t limits = {};

  EXPECT_EQ(corelib_gateway_reset(nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_tick(nullptr, 0u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_add_link(nullptr, &link), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_remove_link(nullptr, 1u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_set_link_available(nullptr, 1u, true), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_receive_frame(nullptr, 1u, frame, 0u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_accept_bootstrap_assignment(nullptr, &assignment, 0u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_report_candidate(nullptr, &candidate), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_complete_discovery(nullptr, 1u, uuid, CORELIB_OK), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_complete_assignment(nullptr, 1u, uuid, CORELIB_CONTROL_SUCCESS), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_report_node_lost(nullptr, uuid), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_respond(nullptr, &request, CORELIB_RESULT_SUCCESS, nullptr, 0u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_publish(nullptr, false, 1u, nullptr, 0u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_usage(nullptr, &usage), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_limits(nullptr, &limits), CORELIB_INVALID_ARGUMENT);
}

TEST(GatewayCApi, RejectsInvalidCoreConfiguration) {
  storage_t storage = {};
  fixture_t fixture = {};
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = nullptr;
  configure(&config, &storage, &fixture);

  corelib_gateway_config_t missing_send = config;
  missing_send.device.callbacks.send_frame = nullptr;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &missing_send, &gateway), CORELIB_INVALID_ARGUMENT);

  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, corelib_gateway_context_size() - 1u, &config, &gateway), CORELIB_INVALID_ARGUMENT);
}

TEST(GatewayCApi, RejectsInvalidGatewayStorageAndTimeouts) {
  storage_t storage = {};
  fixture_t fixture = {};
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = nullptr;
  configure(&config, &storage, &fixture);

  EXPECT_EQ(corelib_gateway_init(nullptr, sizeof(storage.gateway_context), &config, &gateway), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), nullptr, &gateway), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &config, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context + 1u, sizeof(storage.gateway_context) - 1u, &config, &gateway), CORELIB_INVALID_ARGUMENT);

  corelib_gateway_config_t invalid = config;
  invalid.storage.device_context_memory = nullptr;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.links.entries = nullptr;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.routes.capacity = 0u;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.discoveries.entry_size = corelib_gateway_entry_size() - 1u;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.candidates.entries = storage.candidates + 1u;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.assignments.entry_size++;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.forwarding.entries = nullptr;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.control_reassembly.message = nullptr;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  invalid = config;
  invalid.storage.control_reassembly.received = nullptr;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  for (size_t slots : {0u, 9u}) {
    invalid = config;
    invalid.storage.control_reassembly_slots = slots;
    EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  }
  invalid = config;
  invalid.storage.maximum_control_message_size = 63u;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  for (uint32_t timeout : {9u, 60001u}) {
    invalid = config;
    invalid.discovery_timeout_ms = timeout;
    EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
    invalid = config;
    invalid.assignment_timeout_ms = timeout;
    EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
    invalid = config;
    invalid.candidate_retention_timeout_ms = timeout;
    EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
  }
  invalid = config;
  invalid.storage.device_context_memory_size = corelib_context_size() - 1u;
  EXPECT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &invalid, &gateway), CORELIB_INVALID_ARGUMENT);
}

TEST(GatewayCApi, CoversLinkLifecycleCapacityAndLimits) {
  storage_t storage = {};
  fixture_t fixture = {};
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = nullptr;
  configure(&config, &storage, &fixture);
  ASSERT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &config, &gateway), CORELIB_OK);

  corelib_link_config_t link{};
  EXPECT_EQ(corelib_gateway_add_link(gateway, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_INVALID_ARGUMENT);
  link.link_id = 1u;
  link.role = static_cast<corelib_link_role_t>(9);
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_INVALID_ARGUMENT);
  link.role = CORELIB_LINK_UPSTREAM;
  link.profile_id = 1u;
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_INVALID_ARGUMENT);
  link.role = CORELIB_LINK_DOWNSTREAM;
  link.profile_id = 0u;
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_INVALID_ARGUMENT);

  link.role = CORELIB_LINK_UPSTREAM;
  link.profile_id = 0u;
  link.available = true;
  link.transport_context = reinterpret_cast<void *>(1u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_INVALID_STATE);
  link.link_id = 2u;
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_INVALID_STATE);

  link.role = CORELIB_LINK_DOWNSTREAM;
  link.profile_id = 0x80000001u;
  link.transport_context = reinterpret_cast<void *>(2u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_OK);
  link.link_id = 3u;
  link.transport_context = reinterpret_cast<void *>(3u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_OK);
  link.link_id = 4u;
  EXPECT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_CAPACITY_EXCEEDED);

  EXPECT_EQ(corelib_gateway_set_link_available(gateway, 99u, false), CORELIB_NOT_FOUND);
  EXPECT_EQ(corelib_gateway_set_link_available(gateway, 2u, true), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_set_link_available(gateway, 2u, false), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_remove_link(gateway, 99u), CORELIB_NOT_FOUND);
  EXPECT_EQ(corelib_gateway_remove_link(gateway, 2u), CORELIB_OK);

  corelib_gateway_limits_t limits{};
  corelib_gateway_usage_t usage{};
  EXPECT_EQ(corelib_gateway_limits(gateway, &limits), CORELIB_OK);
  EXPECT_EQ(limits.links, 3u);
  EXPECT_EQ(limits.maximum_control_message_size, 128u);
  EXPECT_EQ(corelib_gateway_usage(gateway, &usage), CORELIB_OK);
  EXPECT_EQ(usage.links, 2u);
  EXPECT_EQ(corelib_gateway_limits(gateway, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_usage(gateway, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_remove_link(gateway, 1u), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_reset(gateway), CORELIB_OK);
}

TEST(GatewayCApi, CoversBootstrapReceiveAndTransportFailures) {
  storage_t storage = {};
  fixture_t fixture = {};
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = nullptr;
  configure(&config, &storage, &fixture);
  ASSERT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &config, &gateway), CORELIB_OK);
  corelib_link_config_t upstream{};
  upstream.link_id = 1u;
  upstream.role = CORELIB_LINK_UPSTREAM;
  upstream.available = true;
  upstream.transport_context = reinterpret_cast<void *>(1u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &upstream), CORELIB_OK);
  corelib_link_config_t downstream{};
  downstream.link_id = 2u;
  downstream.profile_id = 0x80000001u;
  downstream.role = CORELIB_LINK_DOWNSTREAM;
  downstream.available = true;
  downstream.transport_context = reinterpret_cast<void *>(2u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &downstream), CORELIB_OK);

  corelib_bootstrap_assignment_t assignment{};
  memcpy(assignment.node_uuid, config.device.node_uuid, sizeof(assignment.node_uuid));
  assignment.transaction_id = 1u;
  assignment.session_id = 2u;
  assignment.node_address = 3u;
  assignment.parent_address = 1u;
  assignment.heartbeat_interval_ms = 1000u;
  assignment.link_id = 1u;
  EXPECT_EQ(corelib_gateway_accept_bootstrap_assignment(gateway, nullptr, 0u), CORELIB_INVALID_ARGUMENT);
  ASSERT_EQ(corelib_gateway_accept_bootstrap_assignment(gateway, &assignment, 1u), CORELIB_OK);

  fixture.send_busy = 1u;
  EXPECT_EQ(corelib_gateway_publish(gateway, false, 1u, nullptr, 0u), CORELIB_OK);
  corelib_gateway_usage_t usage{};
  EXPECT_EQ(corelib_gateway_usage(gateway, &usage), CORELIB_OK);
  EXPECT_EQ(usage.queued_frames, 1u);
  EXPECT_EQ(corelib_gateway_tick(gateway, 2u), CORELIB_OK);

  corelib_pfp_frame_t frame{};
  frame.type = CORELIB_PFP_DATA;
  frame.destination = 99u;
  frame.source = 1u;
  frame.session_id = assignment.session_id;
  frame.message_id = 10u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.message_length = 1u;
  frame.hop_limit = 8u;
  frame.priority = 3u;
  frame.payload[0] = 1u;
  uint8_t encoded[64]{};
  ASSERT_EQ(corelib_pfp_encode(&frame, encoded), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 99u, encoded, 3u), CORELIB_NOT_FOUND);
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 1u, nullptr, 3u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 1u, encoded, 1u), CORELIB_INVALID_ARGUMENT);
  encoded[60] ^= 1u;
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 1u, encoded, 3u), CORELIB_INVALID_FRAME);
  frame.session_id++;
  ASSERT_EQ(corelib_pfp_encode(&frame, encoded), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 1u, encoded, 3u), CORELIB_INVALID_STATE);
  frame.session_id = assignment.session_id;
  frame.destination = 0u;
  frame.message_id++;
  ASSERT_EQ(corelib_pfp_encode(&frame, encoded), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 1u, encoded, 3u), CORELIB_INVALID_STATE);

  corelib_pfp_frame_t probe{};
  probe.type = CORELIB_PFP_PROBE_REQUEST;
  probe.source = 1u;
  probe.message_id = 20u;
  probe.frame_index = 1u;
  probe.frame_count = 1u;
  probe.hop_limit = 1u;
  ASSERT_EQ(corelib_pfp_encode(&probe, encoded), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_receive_frame(gateway, 2u, encoded, 4u), CORELIB_INVALID_FRAME);

  uint8_t unknown[16]{};
  EXPECT_EQ(corelib_gateway_complete_discovery(gateway, 2u, nullptr, CORELIB_OK), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_complete_discovery(gateway, 2u, unknown, CORELIB_OK), CORELIB_NOT_FOUND);
  EXPECT_EQ(corelib_gateway_complete_assignment(gateway, 0u, unknown, CORELIB_CONTROL_SUCCESS), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_complete_assignment(gateway, 1u, nullptr, CORELIB_CONTROL_SUCCESS), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_complete_assignment(gateway, 1u, unknown, static_cast<corelib_control_status_t>(99)), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_report_node_lost(gateway, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_report_node_lost(gateway, unknown), CORELIB_NOT_FOUND);
  corelib_candidate_t candidate{};
  EXPECT_EQ(corelib_gateway_report_candidate(gateway, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_gateway_report_candidate(gateway, &candidate), CORELIB_NOT_FOUND);
  corelib_transaction_id_t request{};
  EXPECT_EQ(corelib_gateway_respond(gateway, &request, CORELIB_RESULT_SUCCESS, nullptr, 0u), CORELIB_NOT_FOUND);
  EXPECT_EQ(corelib_gateway_publish(gateway, false, 0u, nullptr, 0u), CORELIB_INVALID_ARGUMENT);
}

TEST(GatewayCApi, CoversDiscoveryAssignmentFailuresAndExpiry) {
  storage_t storage = {};
  fixture_t fixture = {};
  corelib_gateway_config_t config;
  corelib_gateway_context_t *gateway = nullptr;
  configure(&config, &storage, &fixture);
  ASSERT_EQ(corelib_gateway_init(storage.gateway_context, sizeof(storage.gateway_context), &config, &gateway), CORELIB_OK);
  corelib_link_config_t link{};
  link.link_id = 1u;
  link.role = CORELIB_LINK_UPSTREAM;
  link.available = true;
  link.transport_context = reinterpret_cast<void *>(1u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_OK);
  link.link_id = 2u;
  link.profile_id = 0x80000001u;
  link.role = CORELIB_LINK_DOWNSTREAM;
  link.transport_context = reinterpret_cast<void *>(2u);
  ASSERT_EQ(corelib_gateway_add_link(gateway, &link), CORELIB_OK);
  corelib_bootstrap_assignment_t local{};
  memcpy(local.node_uuid, config.device.node_uuid, sizeof(local.node_uuid));
  local.transaction_id = 1u;
  local.session_id = 2u;
  local.node_address = 3u;
  local.parent_address = 1u;
  local.heartbeat_interval_ms = 1000u;
  ASSERT_EQ(corelib_gateway_accept_bootstrap_assignment(gateway, &local, 1u), CORELIB_OK);

  uint8_t control[80]{};
  uint8_t token[16];
  memset(token, 0x4au, sizeof(token));
  size_t n = header(control, 3u, 10u);
  n = field(control, n, 11u, token, sizeof(token));
  fixture.discover_failed = true;
  EXPECT_EQ(deliver_one(gateway, 1u, CORELIB_PFP_CONTROL, 3u, 1u, 2u, 10u, control, n, 2u), CORELIB_OK);
  corelib_gateway_usage_t usage{};
  EXPECT_EQ(corelib_gateway_usage(gateway, &usage), CORELIB_OK);
  EXPECT_EQ(usage.discoveries, 0u);

  fixture.discover_failed = false;
  control[4] = 11u;
  EXPECT_EQ(deliver_one(gateway, 1u, CORELIB_PFP_CONTROL, 3u, 1u, 2u, 11u, control, n, 3u), CORELIB_OK);
  corelib_candidate_t candidate{};
  candidate.link_id = 2u;
  candidate.node_uuid[6] = 0x40u;
  candidate.node_uuid[8] = 0x80u;
  candidate.node_uuid[15] = 9u;
  memcpy(candidate.discovery_token, token, sizeof(token));
  EXPECT_EQ(corelib_gateway_report_candidate(gateway, &candidate), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_report_candidate(gateway, &candidate), CORELIB_INVALID_STATE);

  n = header(control, 5u, 20u);
  n = field(control, n, 1u, candidate.node_uuid, 16u);
  n = field16(control, n, 2u, 4u);
  n = field16(control, n, 3u, 3u);
  n = field16(control, n, 4u, 2u);
  n = field32(control, n, 9u, 1000u);
  fixture.bootstrap_failed = true;
  EXPECT_EQ(deliver_reverse_result(gateway, 1u, CORELIB_PFP_CONTROL, 3u, 1u, 2u, 12u, control, n, 4u), CORELIB_INVALID_STATE);
  fixture.bootstrap_failed = false;
  control[4] = 21u;
  EXPECT_EQ(deliver_reverse_result(gateway, 1u, CORELIB_PFP_CONTROL, 3u, 1u, 2u, 13u, control, n, 5u), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_complete_assignment(gateway, 21u, candidate.node_uuid, CORELIB_CONTROL_SESSION_REJECTED), CORELIB_OK);

  memset(token, 0x5bu, sizeof(token));
  n = header(control, 3u, 30u);
  n = field(control, n, 11u, token, sizeof(token));
  EXPECT_EQ(deliver_one(gateway, 1u, CORELIB_PFP_CONTROL, 3u, 1u, 2u, 14u, control, n, 6u), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_tick(gateway, 1006u), CORELIB_OK);
  EXPECT_EQ(corelib_gateway_usage(gateway, &usage), CORELIB_OK);
  EXPECT_EQ(usage.discoveries, 0u);
  EXPECT_GT(fixture.diagnostic_count, 0u);
}

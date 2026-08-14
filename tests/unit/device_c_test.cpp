extern "C" {
#include "internal/corelib_internal.h"
}

#include <gtest/gtest.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint8_t frames[32][64];
  size_t frame_count;
  size_t transaction_count;
  corelib_transaction_id_t last_request;
  size_t busy_sends;
} fixture_t;

static corelib_send_result_t send_frame(void *user, uint16_t link,
                                        void *transport_context,
                                        const uint8_t frame[64]) {
  fixture_t *fixture = static_cast<fixture_t *>(user);
  EXPECT_TRUE(link == 1u);
  EXPECT_TRUE(transport_context == NULL);
  if (fixture->busy_sends != 0u) {
    --fixture->busy_sends;
    return CORELIB_SEND_BUSY;
  }
  EXPECT_TRUE(fixture->frame_count < 32u);
  memcpy(fixture->frames[fixture->frame_count++], frame, 64u);
  return CORELIB_SEND_ACCEPTED;
}

static void transaction(void *user, const corelib_transaction_t *received) {
  fixture_t *fixture = static_cast<fixture_t *>(user);
  ++fixture->transaction_count;
  fixture->last_request = received->id;
}

static void write_u32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

TEST(DeviceProtocol, MatchesNormativeVectors) {
  static const uint8_t probe[64] = {
      0x02, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb1, 0xaa, 0x4a, 0xc7};
  static const uint8_t request[] = {
      0x08, 0x02, 0x15, 0x78, 0x56, 0x34, 0x12, 0x18, 0x01, 0x25, 0x01, 0x00, 0x00, 0x00};
  static const uint8_t response[] = {
      0x08, 0x02, 0x15, 0x78, 0x56, 0x34, 0x12, 0x18, 0x03, 0x25, 0x01, 0x00, 0x00, 0x00,
      0x2a, 0x03, 0x61, 0x62, 0x63, 0x30, 0x01};
  static const uint8_t data_vector[64] = {
      0x01, 0x01, 0x02, 0x00, 0x01, 0x00, 0xd4, 0xc3, 0xb2, 0xa1, 0x78, 0x56, 0x34, 0x12, 0x01, 0x01,
      0x03, 0x00, 0x01, 0x03, 0x61, 0x62, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x02, 0xf5, 0x3e};
  corelib_pfp_frame_t decoded;
  corelib_transaction_message_t message;
  uint8_t encoded[64];
  size_t encoded_size;
  EXPECT_TRUE(corelib_pfp_decode(probe, &decoded) == CORELIB_OK);
  EXPECT_TRUE(decoded.type == CORELIB_PFP_PROBE_REQUEST);
  EXPECT_TRUE(corelib_pfp_decode(data_vector, &decoded) == CORELIB_OK);
  EXPECT_TRUE(decoded.type == CORELIB_PFP_DATA && decoded.message_length == 3u);
  EXPECT_TRUE(corelib_pfp_encode(&decoded, encoded) == CORELIB_OK);
  EXPECT_TRUE(memcmp(encoded, data_vector, sizeof(data_vector)) == 0);
  encoded[20] ^= 1u;
  EXPECT_TRUE(corelib_pfp_decode(encoded, &decoded) == CORELIB_INVALID_FRAME);
  EXPECT_TRUE(corelib_transaction_decode(request, sizeof(request), 64u, &message) == CORELIB_OK);
  EXPECT_TRUE(message.token == 0x12345678u && message.share_id == 1u && message.action == 1u);
  message.action = 3u;
  message.result = 1u;
  message.data = (const uint8_t *)"abc";
  message.data_size = 3u;
  EXPECT_TRUE(corelib_transaction_encode(&message, encoded, sizeof(encoded), &encoded_size) == CORELIB_OK);
  EXPECT_TRUE(encoded_size == sizeof(response));
  EXPECT_TRUE(memcmp(encoded, response, sizeof(response)) == 0);
}

TEST(DeviceCApi, HandlesSessionTransactionsAndReassembly) {
  enum { MAX_MESSAGE = 256,
         SLOTS = 2,
         OUTBOUND = 32,
         PENDING = 4 };
  alignas(max_align_t) uint8_t context_memory[2048];
  uint8_t messages[MAX_MESSAGE * SLOTS];
  uint8_t received[255 * SLOTS];
  uint8_t scratch[MAX_MESSAGE];
  uint8_t outbound[64 * OUTBOUND];
  alignas(max_align_t) uint8_t pending[64 * PENDING];
  corelib_context_t *device = NULL;
  fixture_t fixture;
  corelib_config_t config;
  corelib_pfp_frame_t frame;
  uint8_t bytes[64];
  uint8_t control[16];
  uint8_t transaction_request[14] = {
      0x08, 0x02, 0x15, 0x78, 0x56, 0x34, 0x12, 0x18, 0x01, 0x25, 0x01, 0x00, 0x00, 0x00};
  memset(&fixture, 0, sizeof(fixture));
  memset(&config, 0, sizeof(config));
  config.node_uuid[6] = 0x40u;
  config.node_uuid[8] = 0x80u;
  config.heartbeat_interval_ms = 2000u;
  config.application_response_timeout_ms = 1000u;
  config.maximum_transaction_data_size = 128u;
  config.callbacks.send_frame = send_frame;
  config.callbacks.transaction = transaction;
  config.callbacks.user = &fixture;
  config.storage.reassembly.message = messages;
  config.storage.reassembly.received = received;
  config.storage.reassembly_slot_count = SLOTS;
  config.storage.maximum_message_size = MAX_MESSAGE;
  config.storage.transaction_scratch = scratch;
  config.storage.outbound.frames = outbound;
  config.storage.outbound.capacity = OUTBOUND;
  config.storage.pending_requests.entries = pending;
  config.storage.pending_requests.capacity = PENDING;
  config.storage.pending_requests.entry_size = 64u;
  EXPECT_TRUE(corelib_context_size() <= sizeof(context_memory));
  EXPECT_TRUE(corelib_init(context_memory, sizeof(context_memory), &config, &device) == CORELIB_OK);
  EXPECT_TRUE(corelib_add_link(device, 1u, NULL) == CORELIB_OK);

  memset(&frame, 0, sizeof(frame));
  frame.type = CORELIB_PFP_PROBE_REQUEST;
  frame.source = 1u;
  frame.message_id = 7u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.hop_limit = 1u;
  EXPECT_TRUE(corelib_pfp_encode(&frame, bytes) == CORELIB_OK);
  EXPECT_TRUE(corelib_receive_frame(device, 1u, bytes, 0u) == CORELIB_OK);
  EXPECT_TRUE(fixture.frame_count == 1u && fixture.frames[0][0] == CORELIB_PFP_PROBE_RESPONSE);

  memset(control, 0, sizeof(control));
  control[0] = 1u;
  control[1] = 1u;
  write_u32(control + 4, 9u);
  control[8] = 0x89u;
  control[9] = 4u;
  write_u32(control + 10, 2000u);
  memset(&frame, 0, sizeof(frame));
  frame.type = CORELIB_PFP_CONTROL;
  frame.source = 1u;
  /* The direct adapter starts the session at the node's reserved address. */
  frame.destination = 2u;
  frame.session_id = 0xa1b2c3d4u;
  frame.message_id = 8u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.message_length = 14u;
  frame.hop_limit = 1u;
  frame.priority = 3u;
  memcpy(frame.payload, control, 14u);
  EXPECT_TRUE(corelib_pfp_encode(&frame, bytes) == CORELIB_OK);
  EXPECT_TRUE(corelib_receive_frame(device, 1u, bytes, 1u) == CORELIB_OK);
  EXPECT_TRUE(fixture.frame_count == 2u && fixture.frames[1][0] == CORELIB_PFP_CONTROL);

  memset(&frame, 0, sizeof(frame));
  frame.type = CORELIB_PFP_DATA;
  frame.source = 1u;
  frame.destination = 2u;
  frame.session_id = 0xa1b2c3d4u;
  frame.message_id = 9u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.message_length = sizeof(transaction_request);
  frame.hop_limit = 1u;
  frame.priority = 3u;
  memcpy(frame.payload, transaction_request, sizeof(transaction_request));
  EXPECT_TRUE(corelib_pfp_encode(&frame, bytes) == CORELIB_OK);
  EXPECT_TRUE(corelib_receive_frame(device, 1u, bytes, 2u) == CORELIB_OK);
  EXPECT_TRUE(fixture.transaction_count == 1u);
  EXPECT_TRUE(corelib_respond(device, &fixture.last_request, CORELIB_RESULT_SUCCESS,
                              (const uint8_t *)"abc", 3u) == CORELIB_OK);
  EXPECT_TRUE(fixture.frame_count == 3u && fixture.frames[2][0] == CORELIB_PFP_DATA);

  fixture.busy_sends = 1u;
  EXPECT_TRUE(corelib_publish(device, false, 10u, (const uint8_t *)"xy", 2u) == CORELIB_OK);
  {
    corelib_usage_t usage;
    EXPECT_TRUE(corelib_usage(device, &usage) == CORELIB_OK);
    EXPECT_TRUE(usage.queued_frames == 1u);
  }
  EXPECT_TRUE(corelib_tick(device, 3u) == CORELIB_OK);
  EXPECT_TRUE(fixture.frame_count == 4u);

  transaction_request[3] = 0x79u;
  frame.message_id = 10u;
  memset(frame.payload, 0, sizeof(frame.payload));
  memcpy(frame.payload, transaction_request, sizeof(transaction_request));
  EXPECT_TRUE(corelib_pfp_encode(&frame, bytes) == CORELIB_OK);
  EXPECT_TRUE(corelib_receive_frame(device, 1u, bytes, 4u) == CORELIB_OK);
  EXPECT_TRUE(corelib_tick(device, 1004u) == CORELIB_OK);
  EXPECT_TRUE(corelib_respond(device, &fixture.last_request, CORELIB_RESULT_SUCCESS,
                              NULL, 0u) == CORELIB_NOT_FOUND);

  {
    uint8_t payload[60];
    uint8_t transaction_bytes[128];
    size_t transaction_size;
    corelib_transaction_message_t publication;
    size_t part;
    memset(payload, 0x5au, sizeof(payload));
    memset(&publication, 0, sizeof(publication));
    publication.token = 12u;
    publication.share_id = 5u;
    publication.action = 5u;
    publication.data = payload;
    publication.data_size = sizeof(payload);
    EXPECT_TRUE(corelib_transaction_encode(&publication, transaction_bytes,
                                           sizeof(transaction_bytes),
                                           &transaction_size) == CORELIB_OK);
    EXPECT_TRUE(transaction_size > 40u);
    frame.message_id = 11u;
    frame.message_length = (uint16_t)transaction_size;
    frame.frame_count = 2u;
    for (part = 2u; part > 0u; --part) {
      const size_t offset = (part - 1u) * 40u;
      size_t chunk = transaction_size - offset;
      if (chunk > 40u)
        chunk = 40u;
      frame.frame_index = (uint8_t)part;
      memset(frame.payload, 0, sizeof(frame.payload));
      memcpy(frame.payload, transaction_bytes + offset, chunk);
      EXPECT_TRUE(corelib_pfp_encode(&frame, bytes) == CORELIB_OK);
      EXPECT_TRUE(corelib_receive_frame(device, 1u, bytes, 1005u) == CORELIB_OK);
    }
    EXPECT_TRUE(fixture.transaction_count == 3u);
  }
  EXPECT_TRUE(corelib_tick(device, 2001u) == CORELIB_OK);
  EXPECT_TRUE(fixture.frame_count == 5u);
  EXPECT_TRUE(fixture.frames[4][0] == CORELIB_PFP_CONTROL);
}

TEST(DeviceCApi, ReportsDocumentedNullContextStatuses) {
  uint8_t frame[CORELIB_FRAME_SIZE] = {};
  corelib_transaction_id_t request = {};
  corelib_bootstrap_assignment_t assignment = {};
  corelib_usage_t usage = {};
  corelib_limits_t limits = {};
  corelib_context_t *context = nullptr;

  EXPECT_EQ(corelib_init(nullptr, 0u, nullptr, &context), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_reset(nullptr), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_tick(nullptr, 0u), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_add_link(nullptr, 1u, nullptr), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_remove_link(nullptr, 1u), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_receive_frame(nullptr, 1u, frame, 0u), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_accept_bootstrap_assignment(nullptr, &assignment, 0u), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_respond(nullptr, &request, CORELIB_RESULT_SUCCESS, nullptr, 0u), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_publish(nullptr, false, 1u, nullptr, 0u), CORELIB_REENTRANT);
  EXPECT_EQ(corelib_usage(nullptr, &usage), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_limits(nullptr, &limits), CORELIB_INVALID_ARGUMENT);
}

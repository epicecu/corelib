#include "corelib_self_test.h"

#include "corelib/device.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint8_t frames[8][CORELIB_FRAME_SIZE];
  size_t frame_count;
  size_t busy_count;
  size_t transaction_count;
  bool received_request;
  corelib_transaction_id_t request;
} self_test_fixture_t;

static uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = UINT32_MAX;
  size_t index;
  for (index = 0u; index < size; ++index) {
    unsigned bit;
    crc ^= data[index];
    for (bit = 0u; bit < 8u; ++bit) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ UINT32_MAX;
}

static void put_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

static void make_frame(uint8_t output[64], uint8_t type, uint16_t destination,
                       uint16_t source, uint32_t session_id,
                       uint32_t message_id, uint8_t frame_index,
                       uint8_t frame_count, uint16_t message_length,
                       const uint8_t *payload, size_t payload_size) {
  memset(output, 0, 64u);
  output[0] = type;
  output[1] = CORELIB_PFP_VERSION;
  put_u16(output + 2, destination);
  put_u16(output + 4, source);
  put_u32(output + 6, session_id);
  put_u32(output + 10, message_id);
  output[14] = frame_index;
  output[15] = frame_count;
  put_u16(output + 16, message_length);
  output[18] = 1u;
  output[19] = type == 2u ? 0u : 3u;
  if (payload_size != 0u) memcpy(output + 20, payload, payload_size);
  put_u32(output + 60, crc32(output, 60u));
}

static corelib_send_result_t capture_frame(
    void *user, corelib_link_id_t link_id, void *transport_context,
    const uint8_t frame[CORELIB_FRAME_SIZE]) {
  self_test_fixture_t *fixture = (self_test_fixture_t *)user;
  (void)transport_context;
  if (link_id != 1u) return CORELIB_SEND_FAILED;
  if (fixture->busy_count != 0u) {
    --fixture->busy_count;
    return CORELIB_SEND_BUSY;
  }
  if (fixture->frame_count >= 8u) return CORELIB_SEND_FAILED;
  memcpy(fixture->frames[fixture->frame_count++], frame, 64u);
  return CORELIB_SEND_ACCEPTED;
}

static void capture_transaction(void *user,
                                const corelib_transaction_t *value) {
  self_test_fixture_t *fixture = (self_test_fixture_t *)user;
  ++fixture->transaction_count;
  fixture->received_request = true;
  fixture->request = value->id;
}

uint8_t corelib_self_test(void) {
  enum { MAX_MESSAGE = 256, SLOT_COUNT = 2, OUTBOUND_COUNT = 16 };
  alignas(max_align_t) uint8_t context_memory[CORELIB_CONTEXT_STORAGE_SIZE];
  alignas(max_align_t)
      uint8_t pending[4u * CORELIB_PENDING_REQUEST_STORAGE_SIZE];
  uint8_t messages[MAX_MESSAGE * SLOT_COUNT];
  uint8_t received[255u * SLOT_COUNT];
  uint8_t scratch[MAX_MESSAGE];
  uint8_t outbound[CORELIB_FRAME_SIZE * OUTBOUND_COUNT];
  corelib_context_t *device = NULL;
  corelib_config_t config;
  self_test_fixture_t fixture;
  uint8_t frame[64];
  uint8_t control[14] = {1u, 1u, 0u, 0u, 9u, 0u, 0u,
                         0u, 0x89u, 4u, 0xd0u, 7u, 0u, 0u};
  uint8_t request[14] = {
      0x08, 0x02, 0x15, 0x78, 0x56, 0x34, 0x12,
      0x18, 0x01, 0x25, 0x01, 0x00, 0x00, 0x00};
  uint8_t publication[76];
  corelib_usage_t usage;
  memset(&config, 0, sizeof(config));
  memset(&fixture, 0, sizeof(fixture));
  config.node_uuid[0] = 0x40u;
  config.node_uuid[6] = 0x40u;
  config.node_uuid[8] = 0x80u;
  config.node_uuid[15] = 1u;
  config.heartbeat_interval_ms = 2000u;
  config.application_response_timeout_ms = 1000u;
  config.maximum_transaction_data_size = 128u;
  config.callbacks.send_frame = capture_frame;
  config.callbacks.transaction = capture_transaction;
  config.callbacks.user = &fixture;
  config.storage.reassembly.message = messages;
  config.storage.reassembly.received = received;
  config.storage.reassembly_slot_count = SLOT_COUNT;
  config.storage.maximum_message_size = MAX_MESSAGE;
  config.storage.transaction_scratch = scratch;
  config.storage.outbound.frames = outbound;
  config.storage.outbound.capacity = OUTBOUND_COUNT;
  config.storage.pending_requests.entries = pending;
  config.storage.pending_requests.capacity = 4u;
  config.storage.pending_requests.entry_size =
      CORELIB_PENDING_REQUEST_STORAGE_SIZE;
  if (corelib_init(context_memory, sizeof(context_memory), &config,
                          &device) != CORELIB_OK ||
      corelib_add_link(device, 1u, NULL) != CORELIB_OK) return 1u;

  make_frame(frame, 2u, 0u, 1u, 0u, 1u, 1u, 1u, 0u, NULL, 0u);
  if (corelib_receive_frame(device, 1u, frame, 0u) != CORELIB_OK ||
      fixture.frame_count != 1u || fixture.frames[0][0] != 3u) return 2u;

  make_frame(frame, 4u, 0u, 1u, 0xa1b2c3d4u, 2u, 1u, 1u,
             sizeof(control), control, sizeof(control));
  if (corelib_receive_frame(device, 1u, frame, 1u) != CORELIB_OK ||
      fixture.frame_count != 2u) return 3u;

  make_frame(frame, 1u, 2u, 1u, 0xa1b2c3d4u, 3u, 1u, 1u,
             sizeof(request), request, sizeof(request));
  if (corelib_receive_frame(device, 1u, frame, 2u) != CORELIB_OK ||
      !fixture.received_request ||
      corelib_respond(device, &fixture.request,
                             CORELIB_RESULT_UNSUPPORTED, NULL, 0u) !=
          CORELIB_OK) return 4u;

  memset(publication, 0x5au, sizeof(publication));
  publication[0] = 0x08u;
  publication[1] = 0x02u;
  publication[2] = 0x15u;
  publication[3] = 0x0cu;
  publication[4] = 0u;
  publication[5] = 0u;
  publication[6] = 0u;
  publication[7] = 0x18u;
  publication[8] = 0x05u;
  publication[9] = 0x25u;
  publication[10] = 0x05u;
  publication[11] = 0u;
  publication[12] = 0u;
  publication[13] = 0u;
  publication[14] = 0x2au;
  publication[15] = 60u;
  make_frame(frame, 1u, 2u, 1u, 0xa1b2c3d4u, 4u, 2u, 2u,
             sizeof(publication), publication + 40u,
             sizeof(publication) - 40u);
  if (corelib_receive_frame(device, 1u, frame, 3u) != CORELIB_OK)
    return 10u;
  make_frame(frame, 1u, 2u, 1u, 0xa1b2c3d4u, 4u, 1u, 2u,
             sizeof(publication), publication, 40u);
  if (corelib_receive_frame(device, 1u, frame, 3u) != CORELIB_OK)
    return 11u;
  if (fixture.transaction_count != 2u) return 12u;

  fixture.busy_count = 1u;
  if (corelib_publish(device, false, 1u, (const uint8_t *)"x", 1u) !=
          CORELIB_OK ||
      corelib_usage(device, &usage) != CORELIB_OK ||
      usage.queued_frames != 1u ||
      corelib_tick(device, 4u) != CORELIB_OK) return 6u;

  frame[20] ^= 1u;
  if (corelib_receive_frame(device, 1u, frame, 5u) !=
      CORELIB_INVALID_FRAME) return 7u;

  request[3] = 0x79u;
  make_frame(frame, 1u, 2u, 1u, 0xa1b2c3d4u, 5u, 1u, 1u,
             sizeof(request), request, sizeof(request));
  if (corelib_receive_frame(device, 1u, frame, 6u) != CORELIB_OK ||
      corelib_tick(device, 1006u) != CORELIB_OK ||
      corelib_respond(device, &fixture.request,
                             CORELIB_RESULT_SUCCESS, NULL, 0u) !=
          CORELIB_NOT_FOUND) return 8u;
  if (corelib_reset(device) != CORELIB_OK) return 9u;
  return 0u;
}

extern "C" {
#include "internal/corelib_internal.h"
}

#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

struct DeviceHarness {
  alignas(max_align_t) uint8_t contextMemory[CORELIB_CONTEXT_STORAGE_SIZE]{};
  uint8_t messages[2u * 256u]{};
  uint8_t received[2u * 255u]{};
  uint8_t scratch[256u]{};
  uint8_t outbound[4u * CORELIB_FRAME_SIZE]{};
  alignas(max_align_t) uint8_t pending[2u * CORELIB_PENDING_REQUEST_STORAGE_SIZE]{};
  corelib_config_t config{};
  corelib_context_t *context{nullptr};
  corelib_send_result_t sendResult{CORELIB_SEND_ACCEPTED};
  size_t sends{0u};
  size_t sessions{0u};
  size_t nodes{0u};
  size_t diagnostics{0u};

  DeviceHarness() {
    config.node_uuid[6] = 0x40u;
    config.node_uuid[8] = 0x80u;
    config.heartbeat_interval_ms = 1000u;
    config.application_response_timeout_ms = 100u;
    config.maximum_transaction_data_size = 128u;
    config.callbacks.send_frame = send;
    config.callbacks.session_changed = session;
    config.callbacks.node_changed = node;
    config.callbacks.diagnostic = diagnostic;
    config.callbacks.user = this;
    config.storage.reassembly.message = messages;
    config.storage.reassembly.received = received;
    config.storage.reassembly_slot_count = 2u;
    config.storage.maximum_message_size = 256u;
    config.storage.transaction_scratch = scratch;
    config.storage.outbound.frames = outbound;
    config.storage.outbound.capacity = 4u;
    config.storage.pending_requests.entries = pending;
    config.storage.pending_requests.capacity = 2u;
    config.storage.pending_requests.entry_size = CORELIB_PENDING_REQUEST_STORAGE_SIZE;
  }

  static corelib_send_result_t send(void *user, uint16_t, void *, const uint8_t[64]) {
    DeviceHarness &harness = *static_cast<DeviceHarness *>(user);
    ++harness.sends;
    return harness.sendResult;
  }
  static void session(void *user, corelib_session_state_t, uint32_t, uint16_t) {
    ++static_cast<DeviceHarness *>(user)->sessions;
  }
  static void node(void *user, const uint8_t[16], bool, uint16_t) {
    ++static_cast<DeviceHarness *>(user)->nodes;
  }
  static void diagnostic(void *user, corelib_diagnostic_t, corelib_status_t) {
    ++static_cast<DeviceHarness *>(user)->diagnostics;
  }
  corelib_status_t init(const corelib_config_t &value) {
    context = nullptr;
    return corelib_init(contextMemory, sizeof(contextMemory), &value, &context);
  }
  void initAndLink() {
    ASSERT_EQ(init(config), CORELIB_OK);
    ASSERT_EQ(corelib_add_link(context, 7u, this), CORELIB_OK);
  }
  corelib_bootstrap_assignment_t assignment() const {
    corelib_bootstrap_assignment_t value{};
    std::memcpy(value.node_uuid, config.node_uuid, sizeof(value.node_uuid));
    value.transaction_id = 1u;
    value.session_id = 2u;
    value.node_address = 3u;
    value.parent_address = 1u;
    value.heartbeat_interval_ms = 1000u;
    value.link_id = 7u;
    return value;
  }
};

} // namespace

TEST(DeviceValidation, RejectsInvalidConfigurationGroups) {
  DeviceHarness harness;
  corelib_config_t invalid = harness.config;
  corelib_context_t *context = nullptr;
  EXPECT_EQ(corelib_init(harness.contextMemory, sizeof(harness.contextMemory), nullptr, &context),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_init(harness.contextMemory, sizeof(harness.contextMemory), &invalid, nullptr),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_init(harness.contextMemory, corelib_context_size() - 1u, &invalid, &context),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_init(harness.contextMemory + 1u, sizeof(harness.contextMemory) - 1u, &invalid, &context),
            CORELIB_INVALID_ARGUMENT);

  invalid.callbacks.send_frame = nullptr;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.reassembly.message = nullptr;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.reassembly.received = nullptr;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.transaction_scratch = nullptr;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.outbound.frames = nullptr;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.pending_requests.entries = nullptr;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);

  for (size_t slots : {0u, 9u}) {
    invalid = harness.config;
    invalid.storage.reassembly_slot_count = slots;
    EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  }
  for (size_t size : {CORELIB_MIN_TRANSACTION_DATA_SIZE - 1u,
                      CORELIB_MAX_MESSAGE_SIZE + 1u}) {
    invalid = harness.config;
    invalid.storage.maximum_message_size = size;
    EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  }
  for (size_t capacity : {0u, 257u}) {
    invalid = harness.config;
    invalid.storage.outbound.capacity = capacity;
    EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  }
  invalid = harness.config;
  invalid.storage.pending_requests.capacity = 0u;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.pending_requests.entry_size = corelib_pending_request_entry_size() - 1u;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.pending_requests.entries = harness.pending + 1u;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.storage.pending_requests.entry_size++;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  for (size_t maximum : {CORELIB_MIN_TRANSACTION_DATA_SIZE - 1u, 238u}) {
    invalid = harness.config;
    invalid.maximum_transaction_data_size = maximum;
    EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  }
  for (uint32_t heartbeat : {99u, 60001u}) {
    invalid = harness.config;
    invalid.heartbeat_interval_ms = heartbeat;
    EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  }
  invalid = harness.config;
  invalid.application_response_timeout_ms = 0u;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.node_uuid[6] = 0u;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.node_uuid[8] = 0u;
  EXPECT_EQ(harness.init(invalid), CORELIB_INVALID_ARGUMENT);
  invalid = harness.config;
  invalid.capabilities = CORELIB_CAPABILITY_GATEWAY;
  EXPECT_EQ(harness.init(invalid), CORELIB_UNSUPPORTED);
}

TEST(DeviceLifecycle, CoversLinksBootstrapTimersAndLimits) {
  DeviceHarness harness;
  ASSERT_EQ(harness.init(harness.config), CORELIB_OK);
  EXPECT_EQ(corelib_add_link(harness.context, 0u, nullptr), CORELIB_INVALID_STATE);
  ASSERT_EQ(corelib_add_link(harness.context, 7u, &harness), CORELIB_OK);
  EXPECT_EQ(corelib_add_link(harness.context, 8u, nullptr), CORELIB_INVALID_STATE);
  EXPECT_EQ(corelib_remove_link(harness.context, 8u), CORELIB_NOT_FOUND);

  corelib_bootstrap_assignment_t assignment = harness.assignment();
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &assignment, 10u), CORELIB_OK);
  EXPECT_EQ(harness.sessions, 1u);
  EXPECT_EQ(harness.nodes, 1u);
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &assignment, 10u),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_tick(harness.context, 9u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(harness.diagnostics, 1u);

  corelib_limits_t limits{};
  EXPECT_EQ(corelib_limits(harness.context, &limits), CORELIB_OK);
  EXPECT_EQ(limits.maximum_message_size, 256u);
  EXPECT_EQ(corelib_limits(harness.context, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_usage(harness.context, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_remove_link(harness.context, 7u), CORELIB_OK);
  EXPECT_EQ(harness.sessions, 2u);
  EXPECT_EQ(harness.nodes, 2u);
  EXPECT_EQ(corelib_reset(harness.context), CORELIB_OK);
}

TEST(DeviceLifecycle, RejectsEveryInvalidBootstrapField) {
  DeviceHarness harness;
  harness.initAndLink();
  const corelib_bootstrap_assignment_t valid = harness.assignment();
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, nullptr, 0u),
            CORELIB_INVALID_ARGUMENT);
  corelib_bootstrap_assignment_t invalid = valid;
  invalid.session_id = 0u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
  invalid = valid;
  invalid.transaction_id = 0u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
  invalid = valid;
  invalid.node_address = 1u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
  invalid = valid;
  invalid.parent_address = 0u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
  invalid = valid;
  invalid.heartbeat_interval_ms = 99u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
  invalid = valid;
  invalid.heartbeat_interval_ms = 60001u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
  invalid = valid;
  invalid.node_uuid[15] ^= 1u;
  EXPECT_EQ(corelib_accept_bootstrap_assignment(harness.context, &invalid, 0u), CORELIB_INVALID_ARGUMENT);
}

TEST(DeviceTransport, RetainsBusyFramesAndReportsFailedSends) {
  DeviceHarness harness;
  harness.initAndLink();
  corelib_bootstrap_assignment_t assignment = harness.assignment();
  ASSERT_EQ(corelib_accept_bootstrap_assignment(harness.context, &assignment, 0u), CORELIB_OK);
  harness.sendResult = CORELIB_SEND_BUSY;
  EXPECT_EQ(corelib_publish(harness.context, false, 1u, nullptr, 0u), CORELIB_OK);
  corelib_usage_t usage{};
  EXPECT_EQ(corelib_usage(harness.context, &usage), CORELIB_OK);
  EXPECT_EQ(usage.queued_frames, 1u);
  harness.sendResult = CORELIB_SEND_FAILED;
  EXPECT_EQ(corelib_tick(harness.context, 1u), CORELIB_INVALID_STATE);
  EXPECT_EQ(harness.diagnostics, 1u);
  EXPECT_EQ(corelib_publish(harness.context, false, 0u, nullptr, 0u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_publish(harness.context, false, 1u, nullptr, 1u), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_publish(harness.context, false, 1u, harness.scratch, 129u), CORELIB_INVALID_ARGUMENT);
}

TEST(DeviceControl, ValidatesUtf8AcrossEveryEncodingClass) {
  const uint8_t ascii[] = {'h', 'e', 'l', 'l', 'o'};
  const uint8_t two[] = {0xc2u, 0xa2u};
  const uint8_t three[] = {0xe2u, 0x82u, 0xacu};
  const uint8_t four[] = {0xf0u, 0x9fu, 0x98u, 0x80u};
  EXPECT_TRUE(corelib_control_valid_utf8(ascii, sizeof(ascii)));
  EXPECT_TRUE(corelib_control_valid_utf8(two, sizeof(two)));
  EXPECT_TRUE(corelib_control_valid_utf8(three, sizeof(three)));
  EXPECT_TRUE(corelib_control_valid_utf8(four, sizeof(four)));
  EXPECT_TRUE(corelib_control_valid_utf8(nullptr, 0u));

  const std::initializer_list<std::vector<uint8_t>> invalid = {
      {0x80u}, {0xc0u, 0x80u}, {0xf5u, 0x80u, 0x80u, 0x80u}, {0xc2u}, {0xe2u, 0x82u}, {0xf0u, 0x9fu, 0x98u}, {0xc2u, 0x20u}, {0xe2u, 0x28u, 0xa1u}, {0xf0u, 0x90u, 0x28u, 0xbcu}, {0xedu, 0xa0u, 0x80u}, {0xf4u, 0x90u, 0x80u, 0x80u}, {0xe0u, 0x80u, 0x80u}, {0xf0u, 0x80u, 0x80u, 0x80u}};
  for (const std::vector<uint8_t> &bytes : invalid)
    EXPECT_FALSE(corelib_control_valid_utf8(bytes.data(), bytes.size()));
}

TEST(DeviceControl, RejectsMalformedTlvsAndSessionTransitions) {
  DeviceHarness harness;
  harness.initAndLink();
  uint8_t message[32]{};
  message[0] = 1u;
  message[1] = 1u;
  message[4] = 1u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 7u, 1u), CORELIB_INVALID_FRAME);
  message[0] = 2u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 8u, 1u), CORELIB_INVALID_FRAME);
  message[0] = 1u;
  message[2] = 1u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 8u, 1u), CORELIB_INVALID_FRAME);
  message[2] = 0u;
  message[8] = 0x89u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 9u, 1u), CORELIB_INVALID_FRAME);
  message[9] = 0u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 10u, 1u), CORELIB_INVALID_FRAME);
  message[9] = 5u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 11u, 1u), CORELIB_INVALID_FRAME);
  message[8] = 0x80u;
  message[9] = 1u;
  message[10] = 0u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 11u, 1u), CORELIB_INVALID_FRAME);
  message[8] = 0x8cu;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 11u, 1u), CORELIB_INVALID_FRAME);
  message[8] = 0x88u;
  message[10] = 0x80u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 11u, 1u), CORELIB_INVALID_FRAME);

  memset(message, 0, sizeof(message));
  message[0] = 1u;
  message[1] = 1u;
  message[4] = 1u;
  message[8] = 0x89u;
  message[9] = 4u;
  message[10] = 0xe8u;
  message[11] = 0x03u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 14u, 0u), CORELIB_INVALID_FRAME);
  message[4] = 0u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 14u, 1u), CORELIB_INVALID_FRAME);
  message[4] = 1u;
  message[8] = 0x09u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 14u, 1u), CORELIB_INVALID_FRAME);
  message[8] = 0x89u;
  message[10] = 99u;
  message[11] = 0u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 14u, 1u), CORELIB_INVALID_FRAME);
  message[10] = 0xe8u;
  message[11] = 0x03u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 14u, 1u), CORELIB_OK);
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 14u, 2u), CORELIB_INVALID_STATE);

  memset(message, 0, sizeof(message));
  message[0] = 1u;
  message[1] = 8u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 8u, 1u), CORELIB_OK);
  message[4] = 1u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 8u, 1u), CORELIB_INVALID_FRAME);
  message[4] = 0u;
  message[1] = 99u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 8u, 1u), CORELIB_UNSUPPORTED);
  message[1] = 11u;
  EXPECT_EQ(corelib_process_control_message(harness.context, message, 8u, 1u), CORELIB_OK);
}

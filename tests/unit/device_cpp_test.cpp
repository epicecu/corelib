#include <Corelib.h>
extern "C" {
#include "internal/corelib_internal.h"
}

#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <type_traits>

namespace {

class TestHandler final : public corelib::Handler {
public:
  corelib::SendResult sendFrame(corelib::LinkId link, void *context,
                                corelib::FrameView frame) override {
    EXPECT_TRUE(link == 7u);
    EXPECT_TRUE(context == this);
    EXPECT_TRUE(frame.size() == CORELIB_FRAME_SIZE);
    ++sendCount;
    lastFrameType = frame[0];
    return corelib::SendResult::Accepted;
  }

  void onTransaction(const corelib::TransactionView &transaction) override {
    ++transactionCount;
    lastTransaction = transaction.id;
  }
  void onSessionChanged(corelib::SessionState state, uint32_t,
                        uint16_t) override {
    ++sessionCount;
    lastSessionState = state;
  }
  void onNodeChanged(corelib::UuidView uuid, bool reachable,
                     uint16_t) override {
    EXPECT_TRUE(uuid.size() == 16u);
    ++nodeCount;
    lastReachable = reachable;
  }
  void onDiagnostic(corelib::Diagnostic code, corelib::Status status) override {
    ++diagnosticCount;
    lastDiagnostic = code;
    lastStatus = status;
  }

  size_t sendCount{0};
  size_t transactionCount{0};
  size_t sessionCount{0};
  size_t nodeCount{0};
  size_t diagnosticCount{0};
  uint8_t lastFrameType{0};
  bool lastReachable{false};
  corelib::TransactionId lastTransaction{};
  corelib::SessionState lastSessionState{corelib::SessionState::Inactive};
  corelib::Diagnostic lastDiagnostic{corelib::Diagnostic::InvalidFrame};
  corelib::Status lastStatus{corelib::Status::Ok};
};

class MockDeviceHandler final : public corelib::Handler {
public:
  MOCK_METHOD(corelib::SendResult, sendFrame, (corelib::LinkId, void *, corelib::FrameView), (override));
  MOCK_METHOD(void, onTransaction, (const corelib::TransactionView &), (override));
  MOCK_METHOD(void, onSessionChanged, (corelib::SessionState, uint32_t, uint16_t), (override));
  MOCK_METHOD(void, onNodeChanged, (corelib::UuidView, bool, uint16_t), (override));
  MOCK_METHOD(void, onDiagnostic, (corelib::Diagnostic, corelib::Status), (override));
};

corelib_send_result_t rawSend(void *, corelib_link_id_t, void *,
                              const uint8_t[64]) {
  return CORELIB_SEND_ACCEPTED;
}

void writeU32(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

TEST(DeviceCppFacade, ProvidesFixedStorageAndOperationParity) {
  static_assert(!std::is_copy_constructible<corelib::Device<256>>::value,
                "Corelib storage cannot be copied");
  static_assert(!std::is_move_constructible<corelib::Device<256>>::value,
                "Corelib storage cannot be moved");
  static_assert(sizeof(corelib::FrameView::element_type) == 1u,
                "frames contain bytes");

  static const uint8_t probe[CORELIB_FRAME_SIZE] = {
      0x02, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb1, 0xaa, 0x4a, 0xc7};

  corelib::Device<256, 2, 8, 4> device;
  TestHandler handler;
  corelib::Config config;
  config.nodeUuid[6] = 0x40u;
  config.nodeUuid[8] = 0x80u;
  config.maximumTransactionDataSize = 128u;

  EXPECT_TRUE(!device.isReady());
  EXPECT_TRUE(device.tick(0u) == corelib::Status::InvalidState);
  EXPECT_TRUE(device.addLink(7u) == corelib::Status::InvalidState);
  corelib::Usage unavailable{};
  EXPECT_TRUE(device.usage(unavailable) == corelib::Status::InvalidState);
  EXPECT_TRUE(device.init(config, handler) == corelib::Status::Ok);
  EXPECT_TRUE(device.init(config, handler) == corelib::Status::InvalidState);
  EXPECT_TRUE(device.isReady());
  EXPECT_TRUE(device.nativeHandle() != nullptr);
  EXPECT_TRUE(device.get() == device.nativeHandle());

  const corelib::Version version = device.version();
  EXPECT_TRUE(version.major == 1u && version.minor == 0u &&
              version.patch == 0u && version.pfp_version == 1u &&
              version.transaction_version == 2u);
  corelib::Limits limits{};
  EXPECT_TRUE(device.limits(limits) == corelib::Status::Ok);
  EXPECT_TRUE(limits.maximum_message_size == 256u);
  EXPECT_TRUE(limits.outbound_frames == 8u);
  corelib::Usage usage{};
  EXPECT_TRUE(device.usage(usage) == corelib::Status::Ok);
  EXPECT_TRUE(usage.queued_frames == 0u);

  EXPECT_TRUE(device.addLink(7u, &handler) == corelib::Status::Ok);
  EXPECT_TRUE(device.receive(7u, probe, 1u) == corelib::Status::Ok);
  EXPECT_TRUE(handler.sendCount == 1u);
  EXPECT_TRUE(handler.lastFrameType == 3u);

  uint8_t control[14] = {1u, 1u, 0u, 0u, 0u, 0u, 0u,
                         0u, 0x89u, 4u, 0u, 0u, 0u, 0u};
  writeU32(control + 4, 9u);
  writeU32(control + 10, 2000u);
  corelib_pfp_frame_t frame{};
  frame.type = CORELIB_PFP_CONTROL;
  frame.source = 1u;
  frame.destination = 2u;
  frame.session_id = 0xa1b2c3d4u;
  frame.message_id = 8u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.message_length = sizeof(control);
  frame.hop_limit = 1u;
  frame.priority = 3u;
  std::memcpy(frame.payload, control, sizeof(control));
  uint8_t encoded[CORELIB_FRAME_SIZE];
  EXPECT_TRUE(corelib_pfp_encode(&frame, encoded) == CORELIB_OK);
  EXPECT_TRUE(device.receive(7u, encoded, 2u) == corelib::Status::Ok);
  EXPECT_TRUE(handler.sessionCount == 1u);
  EXPECT_TRUE(handler.lastSessionState == corelib::SessionState::Active);
  EXPECT_TRUE(handler.nodeCount == 1u && handler.lastReachable);

  static const uint8_t request[] = {
      0x08, 0x02, 0x15, 0x78, 0x56, 0x34, 0x12, 0x18, 0x04, 0x25, 0x01, 0x00, 0x00, 0x00};
  frame.type = CORELIB_PFP_DATA;
  frame.message_id = 9u;
  frame.message_length = sizeof(request);
  std::memset(frame.payload, 0, sizeof(frame.payload));
  std::memcpy(frame.payload, request, sizeof(request));
  EXPECT_TRUE(corelib_pfp_encode(&frame, encoded) == CORELIB_OK);
  EXPECT_TRUE(device.receive(7u, encoded, 3u) == corelib::Status::Ok);
  EXPECT_TRUE(handler.transactionCount == 1u);
  EXPECT_TRUE(handler.lastTransaction.action == corelib::Action::ShareRequest);
  EXPECT_TRUE(device.respond(handler.lastTransaction, corelib::Result::Unsupported) ==
              corelib::Status::Ok);

  uint8_t invalid[CORELIB_FRAME_SIZE];
  std::memcpy(invalid, probe, sizeof(invalid));
  invalid[20] = 1u;
  EXPECT_TRUE(device.receive(7u, corelib::FrameView(invalid, sizeof(invalid)), 4u) ==
              corelib::Status::InvalidFrame);
  EXPECT_TRUE(handler.diagnosticCount == 1u);
  EXPECT_TRUE(handler.lastDiagnostic == corelib::Diagnostic::InvalidFrame);

  const uint8_t payloadBytes[] = {1u, 2u, 3u};
  const corelib::ByteView payload(payloadBytes, sizeof(payloadBytes));
  EXPECT_TRUE(device.publish(corelib::PayloadKind::Share, 1u, payload) ==
              corelib::Status::Ok);
  const corelib::TransactionId unknown = {
      1u, 1u, corelib::Action::ShareRequest};
  EXPECT_TRUE(device.respond(unknown, corelib::Result::Unsupported) ==
              corelib::Status::NotFound);
  EXPECT_TRUE(device.tick(5u) == corelib::Status::Ok);
  EXPECT_TRUE(device.removeLink(7u) == corelib::Status::Ok);
  EXPECT_TRUE(handler.sessionCount == 2u);
  EXPECT_TRUE(handler.nodeCount == 2u && !handler.lastReachable);
  EXPECT_TRUE(device.reset() == corelib::Status::Ok);

  corelib::Device<256> legacy;
  corelib_config_t raw{};
  raw.node_uuid[6] = 0x40u;
  raw.node_uuid[8] = 0x80u;
  raw.heartbeat_interval_ms = 2000u;
  raw.application_response_timeout_ms = 1000u;
  raw.maximum_transaction_data_size = 128u;
  raw.callbacks.send_frame = rawSend;
  EXPECT_TRUE(legacy.init(raw) == CORELIB_OK);
  EXPECT_TRUE(legacy.isReady());
}

TEST(DeviceCppFacade, DeliversProbeFramesThroughHandler) {
  using ::testing::_;
  using ::testing::Return;
  static const uint8_t probe[CORELIB_FRAME_SIZE] = {
      0x02, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb1, 0xaa, 0x4a, 0xc7};
  corelib::Device<256, 1, 4, 1> device;
  MockDeviceHandler handler;
  corelib::Config config;
  config.nodeUuid[6] = 0x40u;
  config.nodeUuid[8] = 0x80u;
  config.maximumTransactionDataSize = 128u;
  ASSERT_EQ(device.init(config, handler), corelib::Status::Ok);
  ASSERT_EQ(device.addLink(7u, &handler), corelib::Status::Ok);
  EXPECT_CALL(handler, sendFrame(7u, &handler, _)).WillOnce(Return(corelib::SendResult::Accepted));
  EXPECT_EQ(device.receive(7u, probe, 1u), corelib::Status::Ok);
}

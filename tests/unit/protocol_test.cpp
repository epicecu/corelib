extern "C" {
#include "internal/corelib_internal.h"
}

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

corelib_pfp_frame_t dataFrame() {
  corelib_pfp_frame_t frame{};
  frame.type = CORELIB_PFP_DATA;
  frame.destination = 2u;
  frame.source = 1u;
  frame.session_id = 1u;
  frame.message_id = 1u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.message_length = 1u;
  frame.hop_limit = 1u;
  frame.priority = 3u;
  frame.payload[0] = 0x5au;
  return frame;
}

corelib_pfp_frame_t probeFrame() {
  corelib_pfp_frame_t frame{};
  frame.type = CORELIB_PFP_PROBE_REQUEST;
  frame.source = 1u;
  frame.message_id = 1u;
  frame.frame_index = 1u;
  frame.frame_count = 1u;
  frame.hop_limit = 1u;
  return frame;
}

void expectInvalid(const corelib_pfp_frame_t &frame) {
  uint8_t bytes[CORELIB_FRAME_SIZE]{};
  EXPECT_EQ(corelib_pfp_encode(&frame, bytes), CORELIB_INVALID_FRAME);
}

} // namespace

TEST(PfpValidation, RejectsInvalidCommonFields) {
  corelib_pfp_frame_t frame = dataFrame();
  frame.type = 0u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.type = 5u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.message_id = 0u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.hop_limit = 0u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.hop_limit = 9u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.priority = 8u;
  expectInvalid(frame);
}

TEST(PfpValidation, RejectsInvalidProbeFields) {
  corelib_pfp_frame_t frame = probeFrame();
  frame.session_id = 1u;
  expectInvalid(frame);
  frame = probeFrame();
  frame.message_length = 1u;
  expectInvalid(frame);
  frame = probeFrame();
  frame.frame_count = 2u;
  expectInvalid(frame);
  frame = probeFrame();
  frame.frame_index = 2u;
  expectInvalid(frame);
  frame = probeFrame();
  frame.hop_limit = 2u;
  expectInvalid(frame);
  frame = probeFrame();
  frame.payload[39] = 1u;
  expectInvalid(frame);
}

TEST(PfpValidation, RejectsInvalidDataFragmentFieldsAndPadding) {
  corelib_pfp_frame_t frame = dataFrame();
  frame.session_id = 0u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.message_length = 0u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.message_length = CORELIB_MAX_MESSAGE_SIZE + 1u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.frame_count = 2u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.frame_index = 0u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.frame_index = 2u;
  expectInvalid(frame);
  frame = dataFrame();
  frame.payload[1] = 1u;
  expectInvalid(frame);

  frame = dataFrame();
  frame.message_length = 41u;
  frame.frame_count = 2u;
  frame.frame_index = 1u;
  std::memset(frame.payload, 0x5a, sizeof(frame.payload));
  uint8_t bytes[CORELIB_FRAME_SIZE]{};
  EXPECT_EQ(corelib_pfp_encode(&frame, bytes), CORELIB_OK);
  frame.frame_index = 2u;
  std::memset(frame.payload, 0, sizeof(frame.payload));
  frame.payload[0] = 0x5au;
  EXPECT_EQ(corelib_pfp_encode(&frame, bytes), CORELIB_OK);
}

TEST(PfpValidation, RejectsMalformedEncodedFramesAndNullArguments) {
  corelib_pfp_frame_t frame = dataFrame();
  uint8_t bytes[CORELIB_FRAME_SIZE]{};
  ASSERT_EQ(corelib_pfp_encode(&frame, bytes), CORELIB_OK);
  EXPECT_EQ(corelib_pfp_encode(nullptr, bytes), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_pfp_encode(&frame, nullptr), CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_pfp_decode(nullptr, &frame), CORELIB_INVALID_FRAME);
  EXPECT_EQ(corelib_pfp_decode(bytes, nullptr), CORELIB_INVALID_FRAME);

  std::array<uint8_t, CORELIB_FRAME_SIZE> invalid{};
  std::memcpy(invalid.data(), bytes, invalid.size());
  invalid[1] = 2u;
  EXPECT_EQ(corelib_pfp_decode(invalid.data(), &frame), CORELIB_INVALID_FRAME);
  std::memcpy(invalid.data(), bytes, invalid.size());
  invalid[60] ^= 1u;
  EXPECT_EQ(corelib_pfp_decode(invalid.data(), &frame), CORELIB_INVALID_FRAME);
  EXPECT_EQ(corelib_crc32(reinterpret_cast<const uint8_t *>("123456789"), 9u),
            0xcbf43926u);
}

TEST(TransactionValidation, AcceptsEveryValidActionShape) {
  const uint8_t payload[] = {1u, 2u, 3u};
  uint8_t bytes[128]{};
  for (uint8_t action = 1u; action <= 6u; ++action) {
    corelib_transaction_message_t input{};
    input.token = action;
    input.share_id = 1u;
    input.action = action;
    const bool response = action == 3u || action == 6u;
    const bool publish = action == 2u || action == 5u;
    input.result = response ? 1u : 0u;
    if (response || publish) {
      input.data = payload;
      input.data_size = sizeof(payload);
    }
    size_t size = 0u;
    ASSERT_EQ(corelib_transaction_encode(&input, bytes, sizeof(bytes), &size),
              CORELIB_OK);
    corelib_transaction_message_t output{};
    ASSERT_EQ(corelib_transaction_decode(bytes, size, sizeof(payload), &output),
              CORELIB_OK);
    EXPECT_EQ(output.action, action);
    EXPECT_EQ(output.data_size, input.data_size);
  }
}

TEST(TransactionValidation, RejectsInvalidMessageCombinations) {
  const uint8_t payload = 1u;
  uint8_t bytes[64]{};
  size_t size = 0u;
  corelib_transaction_message_t message{};
  message.token = 1u;
  message.share_id = 1u;
  message.action = 1u;
  auto invalid = [&]() {
    EXPECT_EQ(corelib_transaction_encode(&message, bytes, sizeof(bytes), &size),
              CORELIB_INVALID_ARGUMENT);
  };
  message.token = 0u;
  invalid();
  message.token = 1u;
  message.share_id = 0u;
  invalid();
  message.share_id = 1u;
  message.action = 0u;
  invalid();
  message.action = 7u;
  invalid();
  message.action = 1u;
  message.result = 1u;
  invalid();
  message.result = 0u;
  message.data = &payload;
  message.data_size = 1u;
  invalid();
  message.action = 2u;
  message.result = 1u;
  invalid();
  message.action = 3u;
  message.result = 0u;
  invalid();
  message.result = 2u;
  invalid();
  message.result = 6u;
  message.data_size = 0u;
  invalid();
  message.result = 1u;
  message.data_size = CORELIB_MAX_MESSAGE_SIZE + 1u;
  invalid();
  EXPECT_EQ(corelib_transaction_encode(nullptr, bytes, sizeof(bytes), &size),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_transaction_encode(&message, nullptr, sizeof(bytes), &size),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_transaction_encode(&message, bytes, sizeof(bytes), nullptr),
            CORELIB_INVALID_ARGUMENT);
}

TEST(TransactionValidation, RejectsMalformedAndOversizedEncodings) {
  uint8_t bytes[64]{};
  size_t size = 0u;
  corelib_transaction_message_t input{};
  input.token = 1u;
  input.share_id = 1u;
  input.action = 2u;
  input.data = reinterpret_cast<const uint8_t *>("abc");
  input.data_size = 3u;
  ASSERT_EQ(corelib_transaction_encode(&input, bytes, sizeof(bytes), &size),
            CORELIB_OK);
  corelib_transaction_message_t output{};
  EXPECT_EQ(corelib_transaction_decode(nullptr, size, 3u, &output),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_transaction_decode(bytes, size, 3u, nullptr),
            CORELIB_INVALID_ARGUMENT);
  EXPECT_EQ(corelib_transaction_decode(bytes, size, 2u, &output),
            CORELIB_INVALID_FRAME);
  EXPECT_EQ(corelib_transaction_decode(bytes, size - 1u, 3u, &output),
            CORELIB_INVALID_FRAME);
  bytes[1] = 3u;
  EXPECT_EQ(corelib_transaction_decode(bytes, size, 3u, &output),
            CORELIB_INVALID_FRAME);
  bytes[1] = CORELIB_TRANSACTION_VERSION;
  bytes[size++] = 0u;
  EXPECT_EQ(corelib_transaction_decode(bytes, size, 3u, &output),
            CORELIB_INVALID_FRAME);
  EXPECT_EQ(corelib_transaction_encode(&input, bytes, 1u, &size),
            CORELIB_CAPACITY_EXCEEDED);
}

#include <Corelib.h>
#include <corelib/device.hpp>

extern "C" {
#include "src/corelib_self_test.h"
#include "src/test_device.h"
}

namespace {
constexpr corelib_link_id_t kLinkId = 1;
constexpr size_t kPendingRequests = 4;
constexpr uint8_t kLedPin = LED_BUILTIN;

corelib::Device<512, 2, 16, kPendingRequests> core;
corelib_test_device_t device;
uint8_t incoming_frame[CORELIB_FRAME_SIZE];
corelib_transaction_id_t requests[kPendingRequests];
size_t request_head;
size_t request_count;

corelib_send_result_t send_frame(
    void *, corelib_link_id_t link_id, void *,
    const uint8_t frame[CORELIB_FRAME_SIZE]) {
  if (link_id != kLinkId) return CORELIB_SEND_FAILED;
  const int result = RawHID.send(frame, 0);
  if (result == CORELIB_FRAME_SIZE) return CORELIB_SEND_ACCEPTED;
  return result == 0 ? CORELIB_SEND_BUSY : CORELIB_SEND_FAILED;
}

void transaction_received(void *, const corelib_transaction_t *value) {
  const bool common = value->id.action == CORELIB_ACTION_COMMON_PUBLISH;
  if (value->id.action == CORELIB_ACTION_COMMON_PUBLISH ||
      value->id.action == CORELIB_ACTION_SHARE_PUBLISH) {
    (void)corelib_test_device_publish(&device, common, value->id.share_id,
                                         value->data, value->data_size);
    return;
  }
  if (request_count < kPendingRequests) {
    requests[(request_head + request_count) % kPendingRequests] = value->id;
    ++request_count;
  }
}

bool init_corelib() {
  corelib_config_t config{};
  config.node_uuid[0] = 0x41;
  config.node_uuid[6] = 0x40;
  config.node_uuid[8] = 0x80;
  config.node_uuid[15] = 0x01;
  config.heartbeat_interval_ms = 2000;
  config.application_response_timeout_ms = 1000;
  config.maximum_transaction_data_size = 256;
  config.callbacks.send_frame = send_frame;
  config.callbacks.transaction = transaction_received;
  return core.init(config) == CORELIB_OK &&
         corelib_add_link(core.get(), kLinkId, nullptr) ==
             CORELIB_OK;
}

void service_request() {
  if (request_count == 0) return;
  const corelib_transaction_id_t request = requests[request_head];
  request_head = (request_head + 1) % kPendingRequests;
  --request_count;
  const bool common = request.action == CORELIB_ACTION_COMMON_REQUEST;
  uint8_t payload[128];
  size_t payload_size = 0;
  const bool encoded = corelib_test_device_encode(
      &device, common, request.share_id, payload, sizeof(payload), &payload_size);
  (void)corelib_respond(
      core.get(), &request,
      encoded ? CORELIB_RESULT_SUCCESS
              : CORELIB_RESULT_UNSUPPORTED,
      encoded ? payload : nullptr, encoded ? payload_size : 0);
}

[[noreturn]] void signal_failure(uint8_t code) {
  for (;;) {
    for (uint8_t pulse = 0; pulse < code; ++pulse) {
      digitalWriteFast(kLedPin, HIGH);
      delay(150);
      digitalWriteFast(kLedPin, LOW);
      delay(150);
    }
    delay(1000);
  }
}
}  // namespace

void setup() {
  pinMode(kLedPin, OUTPUT);
  const uint8_t self_test = corelib_self_test();
  if (self_test != 0) signal_failure(self_test);
  corelib_test_device_init(&device, millis());
  if (!init_corelib()) signal_failure(8);
  for (uint8_t pulse = 0; pulse < 3; ++pulse) {
    digitalWriteFast(kLedPin, HIGH);
    delay(100);
    digitalWriteFast(kLedPin, LOW);
    delay(100);
  }
}

void loop() {
  const uint64_t now_ms = millis();
  if (RawHID.recv(incoming_frame, 0) == CORELIB_FRAME_SIZE) {
    (void)core.receive(kLinkId, incoming_frame, now_ms);
  }
  corelib_test_device_tick(&device, now_ms);
  service_request();
  (void)core.tick(now_ms);
}

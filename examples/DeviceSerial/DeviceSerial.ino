// Minimal Arduino serial transport integration for a device endpoint.
#include <Corelib.h>

constexpr corelib_link_id_t kSerialLink = 1;

corelib_serial_decoder_t serial_decoder{};
uint8_t incoming_frame[CORELIB_FRAME_SIZE];
bool response_pending = false;
corelib::TransactionId pending_request{};

class DeviceHandler final : public corelib::Handler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *, corelib::FrameView frame) override {
    uint8_t encoded[CORELIB_SERIAL_WIRE_SIZE];
    const size_t encoded_size = corelib_serial_encode(frame.data(), encoded);
    if (encoded_size == 0 || static_cast<size_t>(Serial.availableForWrite()) < encoded_size) {
      return corelib::SendResult::Busy;
    }
    return Serial.write(encoded, encoded_size) == encoded_size
               ? corelib::SendResult::Accepted
               : corelib::SendResult::Failed;
  }

  void onTransaction(const corelib::TransactionView &transaction) override {
    /* Decode or copy application-owned data here. The view is borrowed. */
    pending_request = transaction.id;
    response_pending = true;
  }
};

DeviceHandler handler;
corelib::Device<512, 2, 16, 4> device;

bool init_sdk() {
  corelib::Config config;
  /* Replace this example UUID with the device's provisioned persistent UUIDv4. */
  config.nodeUuid[0] = 0x12;
  config.nodeUuid[6] = 0x40;
  config.nodeUuid[8] = 0x80;
  config.maximumTransactionDataSize = 256;

  return device.init(config, handler) == corelib::Status::Ok &&
         device.addLink(kSerialLink) == corelib::Status::Ok;
}
void setup() {
  Serial.begin(115200);
  (void)init_sdk();
}

void loop() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value >= 0) {
      const corelib_serial_result_t result =
          corelib_serial_decoder_push(&serial_decoder, static_cast<uint8_t>(value), incoming_frame);
      if (result == CORELIB_SERIAL_FRAME) {
        (void)device.receive(kSerialLink, incoming_frame, millis());
      }
    }
  }

  if (response_pending) {
    response_pending = false;
    (void)device.respond(pending_request, corelib::Result::Unsupported);
  }

  (void)device.tick(millis());
}

// Teensyduino RawHID transport integration for a device endpoint.
#include <Corelib.h>

constexpr corelib_link_id_t kRawHidLink = 1;

uint8_t incoming_frame[CORELIB_FRAME_SIZE];

class DeviceHandler final : public corelib::Handler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *, corelib::FrameView frame) override {
    const int result = RawHID.send(const_cast<uint8_t *>(frame.data()), 0);
    if (result == static_cast<int>(frame.size())) {
      return corelib::SendResult::Accepted;
    }
    return result == 0 ? corelib::SendResult::Busy
                       : corelib::SendResult::Failed;
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
         device.addLink(kRawHidLink) == corelib::Status::Ok;
}
void setup() {
  (void)init_sdk();
}

void loop() {
  if (RawHID.recv(incoming_frame, 0) == CORELIB_FRAME_SIZE) {
    (void)device.receive(kRawHidLink, incoming_frame, millis());
  }
  (void)device.tick(millis());
}

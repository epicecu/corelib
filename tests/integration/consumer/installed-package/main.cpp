#include <Corelib.h>

class Handler final : public corelib::Handler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *,
                                corelib::FrameView) override {
    return corelib::SendResult::Accepted;
  }
};

int main() {
  corelib::Device<256> device;
  corelib::Config config;
  Handler handler;
  config.nodeUuid[6] = 0x40u;
  config.nodeUuid[8] = 0x80u;
  config.maximumTransactionDataSize = 128u;
  return device.init(config, handler) == corelib::Status::Ok ? 0 : 1;
}

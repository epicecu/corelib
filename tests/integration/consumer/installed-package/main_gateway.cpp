#include <Corelib.h>

class GatewayHandler final : public corelib::GatewayHandler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *,
                                corelib::FrameView) override {
    return corelib::SendResult::Accepted;
  }
  corelib::SendResult discover(corelib::LinkId, void *,
                               corelib::LinkProfileId,
                               corelib::UuidView) override {
    return corelib::SendResult::Accepted;
  }
  corelib::SendResult bootstrapAssign(
      void *, const corelib_bootstrap_assignment_t &) override {
    return corelib::SendResult::Accepted;
  }
};

int main() {
  corelib::Gateway<256> gateway;
  corelib::GatewayConfig config;
  GatewayHandler handler;
  config.nodeUuid[6] = 0x40u;
  config.nodeUuid[8] = 0x80u;
  config.maximumTransactionDataSize = 128u;
  return gateway.init(config, handler) == corelib::Status::Ok ? 0 : 1;
}

#define CORELIB_ENABLE_GATEWAY 1
#include <corelib/gateway.hpp>

#include <type_traits>

class GatewayHandler final : public corelib::GatewayHandler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *, corelib::FrameView) override {
    return corelib::SendResult::Accepted;
  }
  corelib::SendResult discover(corelib::LinkId, void *, corelib::LinkProfileId, corelib::UuidView) override {
    return corelib::SendResult::Accepted;
  }
  corelib::SendResult bootstrapAssign(void *, const corelib_bootstrap_assignment_t &) override {
    return corelib::SendResult::Accepted;
  }
};

int main() {
  using Gateway = corelib::Gateway<256, 1, 2, 1, 2, 2, 1, 1, 1, 2>;
  static_assert(!std::is_copy_constructible<Gateway>::value, "gateway storage must not be copied");
  Gateway gateway;
  GatewayHandler handler;
  corelib::GatewayConfig config;
  config.nodeUuid[6] = 0x40u;
  config.nodeUuid[8] = 0x80u;
  config.maximumTransactionDataSize = 128u;
  return gateway.init(config, handler) == corelib::Status::Ok && gateway.isReady() ? 0 : 1;
}

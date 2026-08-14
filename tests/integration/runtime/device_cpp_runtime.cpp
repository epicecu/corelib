#include <corelib/device.hpp>

#include <type_traits>

class DeviceHandler final : public corelib::Handler {
public:
  corelib::SendResult sendFrame(corelib::LinkId, void *, corelib::FrameView) override {
    return corelib::SendResult::Accepted;
  }
};

int main() {
  using Device = corelib::Device<256, 1, 2, 1>;
  static_assert(!std::is_copy_constructible<Device>::value, "device storage must not be copied");
  Device device;
  DeviceHandler handler;
  corelib::Config config;
  config.nodeUuid[6] = 0x40u;
  config.nodeUuid[8] = 0x80u;
  config.maximumTransactionDataSize = 128u;
  return device.init(config, handler) == corelib::Status::Ok && device.isReady() ? 0 : 1;
}

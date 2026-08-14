// Transport-neutral Arduino integration skeleton for a gateway endpoint.
#define CORELIB_ENABLE_GATEWAY 1
#include <Corelib.h>

constexpr corelib::LinkId kUpstreamLink = 1;
constexpr corelib::LinkId kDownstreamLink = 2;
constexpr corelib::LinkProfileId kDownstreamProfile = 1;

// Replace these hooks with non-blocking drivers for the dedicated PFP links
// and the chosen downstream discovery/bootstrap profile.
corelib::SendResult send_pfp_frame(corelib::LinkId, corelib::FrameView) {
  return corelib::SendResult::Failed;
}

corelib::SendResult start_profile_discovery(corelib::LinkId, corelib::LinkProfileId, corelib::UuidView) {
  return corelib::SendResult::Failed;
}

corelib::SendResult deliver_bootstrap_assignment(const corelib_bootstrap_assignment_t &) {
  return corelib::SendResult::Failed;
}

class GatewayHandler final : public corelib::GatewayHandler {
public:
  corelib::SendResult sendFrame(corelib::LinkId link, void *, corelib::FrameView frame) override {
    return send_pfp_frame(link, frame);
  }

  corelib::SendResult discover(corelib::LinkId link, void *, corelib::LinkProfileId profile, corelib::UuidView token) override {
    return start_profile_discovery(link, profile, token);
  }

  corelib::SendResult bootstrapAssign(void *, const corelib_bootstrap_assignment_t &assignment) override {
    return deliver_bootstrap_assignment(assignment);
  }

  void onTopologyChanged(const corelib_topology_event_t &event) override {
    // Copy or queue any event data needed after this synchronous callback.
    (void)event;
  }
};

GatewayHandler handler;
corelib::Gateway<512, 2, 16, 4, 2, 8, 2, 8, 4, 16, 128, 2> gateway;

bool init_gateway() {
  corelib::GatewayConfig config;
  // Replace this example UUID with the ECU's provisioned persistent UUIDv4.
  config.nodeUuid[0] = 0x12;
  config.nodeUuid[6] = 0x40;
  config.nodeUuid[8] = 0x80;
  config.maximumTransactionDataSize = 256;

  if (gateway.init(config, handler) != corelib::Status::Ok) {
    return false;
  }

  corelib::LinkConfig upstream;
  upstream.id = kUpstreamLink;
  upstream.role = corelib::LinkRole::Upstream;
  upstream.available = true;

  corelib::LinkConfig downstream;
  downstream.id = kDownstreamLink;
  downstream.profileId = kDownstreamProfile;
  downstream.role = corelib::LinkRole::Downstream;
  downstream.available = true;

  return gateway.addLink(upstream) == corelib::Status::Ok &&
         gateway.addLink(downstream) == corelib::Status::Ok;
}

// Call this from each transport after it has assembled one complete 64-byte
// PFP frame. Link IDs identify the ingress transport to the gateway.
void receive_pfp_frame(corelib::LinkId link, const uint8_t frame[CORELIB_FRAME_SIZE]) {
  (void)gateway.receive(link, corelib::FrameView(frame, CORELIB_FRAME_SIZE), millis());
}

void setup() {
  (void)init_gateway();
}

void loop() {
  // Poll the application-owned transports and profile completion queues here.
  // Report discovered candidates and assignment results through Gateway.
  (void)gateway.tick(millis());
}

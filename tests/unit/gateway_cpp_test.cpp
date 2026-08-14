#include <Corelib.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <type_traits>

class Handler final : public corelib::GatewayHandler {
public:
  MOCK_METHOD(corelib::SendResult, sendFrame, (corelib::LinkId, void *, corelib::FrameView), (override));
  MOCK_METHOD(corelib::SendResult, discover, (corelib::LinkId, void *, corelib::LinkProfileId, corelib::UuidView), (override));
  MOCK_METHOD(corelib::SendResult, bootstrapAssign, (void *, const corelib_bootstrap_assignment_t &), (override));
};

TEST(GatewayCppFacade, ProvidesFixedStorageAndOperationParity) {
  using Gateway = corelib::Gateway<256, 2, 8, 4, 3, 8, 2, 8, 4, 8>;
  static_assert(!std::is_copy_constructible<Gateway>::value,
                "gateway storage cannot be copied");
  Gateway gateway;
  Handler handler;
  EXPECT_CALL(handler, sendFrame).Times(0);
  EXPECT_CALL(handler, discover).Times(0);
  EXPECT_CALL(handler, bootstrapAssign).Times(0);
  corelib::GatewayConfig config;
  config.nodeUuid[6] = 0x40;
  config.nodeUuid[8] = 0x80;
  config.maximumTransactionDataSize = 128;
  EXPECT_TRUE(gateway.init(config, handler) == corelib::Status::Ok);
  corelib::LinkConfig upstream;
  upstream.id = 1;
  upstream.role = corelib::LinkRole::Upstream;
  upstream.available = true;
  EXPECT_TRUE(gateway.addLink(upstream) == corelib::Status::Ok);
  corelib::LinkConfig downstream;
  downstream.id = 2;
  downstream.role = corelib::LinkRole::Downstream;
  downstream.profileId = 0x80000001u;
  downstream.available = true;
  EXPECT_TRUE(gateway.addLink(downstream) == corelib::Status::Ok);
  EXPECT_TRUE(gateway.tick(0u) == corelib::Status::Ok);
  corelib::GatewayUsage usage{};
  EXPECT_TRUE(gateway.usage(usage) == corelib::Status::Ok);
  EXPECT_TRUE(gateway.setLinkAvailable(2u, false) == corelib::Status::Ok);
  EXPECT_TRUE(gateway.setLinkAvailable(2u, true) == corelib::Status::Ok);
  corelib_candidate_t candidate{};
  EXPECT_TRUE(gateway.reportCandidate(candidate) == corelib::Status::NotFound);
  corelib_bootstrap_assignment_t bootstrap{};
  EXPECT_TRUE(gateway.acceptBootstrap(bootstrap, 0u) == corelib::Status::InvalidArgument);
  corelib::TransactionId missing{};
  EXPECT_TRUE(gateway.respond(missing, corelib::Result::Unsupported) == corelib::Status::NotFound);
  EXPECT_TRUE(gateway.publish(corelib::PayloadKind::Share, 0u, corelib::ByteView()) == corelib::Status::InvalidArgument);
  corelib::Uuid unknown{};
  EXPECT_TRUE(gateway.completeAssignment(1, corelib::UuidView(unknown.data(), unknown.size()),
                                         corelib::ControlStatus::Success) ==
              corelib::Status::NotFound);
  corelib::GatewayLimits limits{};
  EXPECT_TRUE(gateway.limits(limits) == corelib::Status::Ok);
  EXPECT_TRUE(limits.links == 3 && limits.routes == 8);
  EXPECT_TRUE(gateway.completeDiscovery(2u, corelib::UuidView(unknown.data(), unknown.size()), corelib::Status::Ok) == corelib::Status::NotFound);
  EXPECT_TRUE(gateway.reportNodeLost(corelib::UuidView(unknown.data(), unknown.size())) == corelib::Status::NotFound);
  EXPECT_TRUE(gateway.removeLink(2u) == corelib::Status::Ok);
  EXPECT_TRUE(gateway.reset() == corelib::Status::Ok);
}

TEST(GatewayCppFacade, RejectsOperationsBeforeInitialisation) {
  using Gateway = corelib::Gateway<256, 2, 8, 4, 3, 8, 2, 8, 4, 8>;
  Gateway gateway;
  EXPECT_FALSE(gateway.isReady());
  EXPECT_EQ(gateway.nativeHandle(), nullptr);
  const Gateway &constant = gateway;
  EXPECT_EQ(constant.nativeHandle(), nullptr);
  corelib::LinkConfig link;
  corelib::GatewayUsage usage{};
  corelib::GatewayLimits limits{};
  corelib::Uuid uuid{};
  corelib_candidate_t candidate{};
  corelib_bootstrap_assignment_t assignment{};
  corelib::TransactionId transaction{};
  const uint8_t frameBytes[CORELIB_FRAME_SIZE]{};
  EXPECT_EQ(gateway.reset(), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.tick(0u), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.addLink(link), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.removeLink(1u), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.setLinkAvailable(1u, true), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.receive(1u, corelib::FrameView(frameBytes, sizeof(frameBytes)), 0u), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.reportCandidate(candidate), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.completeDiscovery(1u, corelib::UuidView(uuid.data(), uuid.size()), corelib::Status::Ok), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.completeAssignment(1u, corelib::UuidView(uuid.data(), uuid.size()), corelib::ControlStatus::Success), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.reportNodeLost(corelib::UuidView(uuid.data(), uuid.size())), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.acceptBootstrap(assignment, 0u), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.respond(transaction, corelib::Result::Unsupported), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.publish(corelib::PayloadKind::Share, 1u, corelib::ByteView()), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.usage(usage), corelib::Status::InvalidState);
  EXPECT_EQ(gateway.limits(limits), corelib::Status::InvalidState);
}

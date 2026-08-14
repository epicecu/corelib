/**
 * @file gateway.hpp
 * @brief Fixed-storage C++14 facade for the Programmor gateway API.
 */
#ifndef CORELIB_GATEWAY_HPP
#define CORELIB_GATEWAY_HPP

#include "corelib/device.hpp"
#include "corelib/gateway.h"

namespace corelib {

/** @brief Downstream transport profile identifier. */
using LinkProfileId = corelib_link_profile_id_t;

/** @brief Gateway resource-usage snapshot. */
using GatewayUsage = corelib_gateway_usage_t;

/** @brief Gateway fixed-capacity limits. */
using GatewayLimits = corelib_gateway_limits_t;

/** @brief Type-safe PFP control completion status. */
enum class ControlStatus : uint8_t {
  Success = CORELIB_CONTROL_SUCCESS,
  Malformed = CORELIB_CONTROL_MALFORMED,
  Unsupported = CORELIB_CONTROL_UNSUPPORTED,
  Conflict = CORELIB_CONTROL_CONFLICT,
  DuplicateIdentity = CORELIB_CONTROL_DUPLICATE_IDENTITY,
  NoRoute = CORELIB_CONTROL_NO_ROUTE,
  HopLimit = CORELIB_CONTROL_HOP_LIMIT,
  SessionRejected = CORELIB_CONTROL_SESSION_REJECTED,
  ResourceLimit = CORELIB_CONTROL_RESOURCE_LIMIT
};

/** @brief Direction of a dedicated gateway link. */
enum class LinkRole : uint8_t {
  Upstream = CORELIB_LINK_UPSTREAM,    /** Link toward the root adapter. */
  Downstream = CORELIB_LINK_DOWNSTREAM /** Link toward candidate nodes. */
};

/** @brief High-level configuration for one gateway link. */
struct LinkConfig {
  LinkId id{0};                      /**< Non-zero link identifier. */
  LinkProfileId profileId{0};        /**< Downstream profile identifier. */
  LinkRole role{LinkRole::Upstream}; /**< Link direction. */
  void *transportContext{nullptr};   /**< Opaque transport context. */
  bool available{false};             /**< Initial availability. */
};

/** @brief Gateway configuration extending local endpoint settings. */
struct GatewayConfig : Config {
  uint32_t discoveryTimeoutMs{1000};          /**< Profile discovery deadline. */
  uint32_t assignmentTimeoutMs{1000};         /**< Profile assignment deadline. */
  uint32_t candidateRetentionTimeoutMs{1000}; /**< Candidate retention deadline. */
};

/** @brief Synchronous application and transport integration for a gateway. */
class GatewayHandler : public Handler {
public:
  /** @brief Starts profile discovery. @param link Downstream link. @param transportContext Profile context. @param profile Profile identifier. @param token Discovery token. @return Send result. */
  virtual SendResult discover(LinkId link, void *transportContext, LinkProfileId profile, UuidView token) = 0;
  /** @brief Delivers a bootstrap assignment through a profile. @param transportContext Profile context. @param assignment Assignment value. @return Send result. */
  virtual SendResult bootstrapAssign(void *transportContext, const corelib_bootstrap_assignment_t &assignment) = 0;
  /** @brief Observes committed topology changes. @param event Borrowed topology event. */
  virtual void onTopologyChanged(const corelib_topology_event_t &event) { (void)event; }

protected:
  ~GatewayHandler() = default;
};

/**
 * @brief Fixed-storage owner and type-safe facade for one PFP gateway.
 * @tparam MaximumMessageBytes Maximum endpoint message bytes.
 * @tparam ReassemblySlots Endpoint reassembly slots.
 * @tparam OutboundFrames Endpoint outbound frames.
 * @tparam PendingRequests Endpoint pending requests.
 * @tparam Links Registered link capacity.
 * @tparam Routes Installed route capacity.
 * @tparam Discoveries Active discovery capacity.
 * @tparam Candidates Retained candidate capacity.
 * @tparam Assignments Pending assignment capacity.
 * @tparam ForwardingFrames Routed frame capacity.
 * @tparam ControlBytes Maximum local control message bytes.
 * @tparam ControlSlots Local control reassembly slots.
 */
template <size_t MaximumMessageBytes = 1024, size_t ReassemblySlots = 2, size_t OutboundFrames = 32, size_t PendingRequests = 8, size_t Links = 4, size_t Routes = 16, size_t Discoveries = 4, size_t Candidates = 16, size_t Assignments = 8, size_t ForwardingFrames = 32, size_t ControlBytes = 128, size_t ControlSlots = 2>
class Gateway final {
  static_assert(MaximumMessageBytes >= CORELIB_MIN_TRANSACTION_DATA_SIZE + 19u,
                "message storage is too small");
  static_assert(ReassemblySlots > 0 && ReassemblySlots <= 8,
                "invalid reassembly slot count");
  static_assert(Links >= 2 && Routes > 0 && Discoveries > 0 && Candidates > 0 &&
                    Assignments > 0 && ForwardingFrames > 0,
                "gateway capacities must be non-zero");
  static_assert(ControlBytes >= 64 && ControlSlots > 0 && ControlSlots <= 8,
                "invalid gateway control storage");

public:
  /** @brief Constructs an uninitialised gateway with zeroed fixed storage. */
  Gateway() = default;
  Gateway(const Gateway &) = delete;
  Gateway &operator=(const Gateway &) = delete;
  Gateway(Gateway &&) = delete;
  Gateway &operator=(Gateway &&) = delete;

  /** @brief Initialises the gateway and callback handler. @param config Gateway settings. @param handler Long-lived handler. @return Initialisation status. */
  Status init(const GatewayConfig &config, GatewayHandler &handler) noexcept {
    if (isReady())
      return Status::InvalidState;
    corelib_gateway_config_t native{};
    for (size_t i = 0; i < config.nodeUuid.size(); ++i)
      native.device.node_uuid[i] = config.nodeUuid[i];
    native.device.capabilities = config.capabilities | CORELIB_CAPABILITY_GATEWAY;
    native.device.heartbeat_interval_ms = config.heartbeatIntervalMs;
    native.device.application_response_timeout_ms = config.applicationResponseTimeoutMs;
    native.device.maximum_transaction_data_size = config.maximumTransactionDataSize;
    native.device.callbacks.send_frame = &sendThunk;
    native.device.callbacks.transaction = &transactionThunk;
    native.device.callbacks.session_changed = &sessionThunk;
    native.device.callbacks.node_changed = &nodeThunk;
    native.device.callbacks.diagnostic = &diagnosticThunk;
    native.device.callbacks.user = this;
    native.callbacks.discover = &discoverThunk;
    native.callbacks.bootstrap_assign = &bootstrapThunk;
    native.callbacks.topology_changed = &topologyThunk;
    native.discovery_timeout_ms = config.discoveryTimeoutMs;
    native.assignment_timeout_ms = config.assignmentTimeoutMs;
    native.candidate_retention_timeout_ms = config.candidateRetentionTimeoutMs;
    populateStorage(native);
    handler_ = &handler;
    const Status status = detail::status(corelib_gateway_init(
        contextMemory_, sizeof(contextMemory_), &native, &context_));
    if (status != Status::Ok)
      handler_ = nullptr;
    return status;
  }

  /** @brief Tests whether initialisation succeeded. @return True when ready. */
  bool isReady() const noexcept { return context_ != nullptr; }
  /** @brief Accesses the mutable native gateway. @return Native context or null. */
  corelib_gateway_context_t *nativeHandle() noexcept { return context_; }
  /** @brief Accesses the read-only native gateway. @return Native context or null. */
  const corelib_gateway_context_t *nativeHandle() const noexcept { return context_; }
  /** @brief Clears protocol and topology state. @return Operation status. */
  Status reset() noexcept { return call(corelib_gateway_reset); }
  /** @brief Advances timers and queued work. @param now Non-decreasing time. @return Operation status. */
  Status tick(uint64_t now) noexcept {
    return isReady() ? detail::status(corelib_gateway_tick(context_, now))
                     : Status::InvalidState;
  }
  /** @brief Registers a dedicated PFP link. @param value Link configuration. @return Operation status. */
  Status addLink(const LinkConfig &value) noexcept {
    if (!isReady())
      return Status::InvalidState;
    const corelib_link_config_t native = {
        value.id, value.profileId,
        static_cast<corelib_link_role_t>(value.role),
        value.transportContext, value.available};
    return detail::status(corelib_gateway_add_link(context_, &native));
  }
  /** @brief Removes a link and dependent routes. @param id Link identifier. @return Operation status. */
  Status removeLink(LinkId id) noexcept {
    return isReady() ? detail::status(corelib_gateway_remove_link(context_, id))
                     : Status::InvalidState;
  }
  /** @brief Changes link availability. @param id Link identifier. @param available New state. @return Operation status. */
  Status setLinkAvailable(LinkId id, bool available) noexcept {
    return isReady() ? detail::status(corelib_gateway_set_link_available(
                           context_, id, available))
                     : Status::InvalidState;
  }
  /** @brief Processes one gateway frame. @param id Receiving link. @param frame Borrowed frame. @param now Non-decreasing time. @return Processing status. */
  Status receive(LinkId id, FrameView frame, uint64_t now) noexcept {
    return isReady() ? detail::status(corelib_gateway_receive_frame(
                           context_, id, frame.data(), now))
                     : Status::InvalidState;
  }
  /** @brief Reports a discovered candidate. @param candidate Candidate value. @return Operation status. */
  Status reportCandidate(const corelib_candidate_t &candidate) noexcept {
    return isReady() ? detail::status(corelib_gateway_report_candidate(
                           context_, &candidate))
                     : Status::InvalidState;
  }
  /** @brief Completes profile discovery. @param id Link identifier. @param token Round token. @param result Profile result. @return Correlation status. */
  Status completeDiscovery(LinkId id, UuidView token, Status result) noexcept {
    return isReady() ? detail::status(corelib_gateway_complete_discovery(
                           context_, id, token.data(),
                           static_cast<corelib_status_t>(result)))
                     : Status::InvalidState;
  }
  /** @brief Completes profile assignment. @param transaction Transaction identifier. @param uuid Candidate UUID. @param result Control status. @return Commit status. */
  Status completeAssignment(uint32_t transaction, UuidView uuid, ControlStatus result) noexcept {
    return isReady() ? detail::status(corelib_gateway_complete_assignment(
                           context_, transaction, uuid.data(),
                           static_cast<corelib_control_status_t>(result)))
                     : Status::InvalidState;
  }
  /** @brief Reports downstream node loss. @param uuid Lost node UUID. @return Operation status. */
  Status reportNodeLost(UuidView uuid) noexcept {
    return isReady() ? detail::status(corelib_gateway_report_node_lost(
                           context_, uuid.data()))
                     : Status::InvalidState;
  }
  /** @brief Accepts a local bootstrap assignment. @param assignment Assignment. @param now Non-decreasing time. @return Validation status. */
  Status acceptBootstrap(const corelib_bootstrap_assignment_t &assignment, uint64_t now) noexcept {
    return isReady() ? detail::status(
                           corelib_gateway_accept_bootstrap_assignment(context_, &assignment, now))
                     : Status::InvalidState;
  }
  /** @brief Responds to a local request. @param request Request identity. @param result Application result. @param data Optional payload. @return Operation status. */
  Status respond(const TransactionId &request, Result result, ByteView data = ByteView()) noexcept {
    if (!isReady())
      return Status::InvalidState;
    const corelib_transaction_id_t native = detail::transactionId(request);
    return detail::status(corelib_gateway_respond(
        context_, &native, static_cast<corelib_transaction_result_t>(result),
        data.data(), data.size()));
  }
  /** @brief Publishes a local application payload. @param kind Payload kind. @param shareId Schema identifier. @param data Encoded bytes. @return Operation status. */
  Status publish(PayloadKind kind, uint32_t shareId, ByteView data) noexcept {
    return isReady() ? detail::status(corelib_gateway_publish(
                           context_, kind == PayloadKind::Common, shareId,
                           data.data(), data.size()))
                     : Status::InvalidState;
  }
  /** @brief Reads gateway resource usage. @param value Receives usage. @return Operation status. */
  Status usage(GatewayUsage &value) const noexcept {
    return isReady() ? detail::status(corelib_gateway_usage(context_, &value))
                     : Status::InvalidState;
  }
  /** @brief Reads gateway capacities and deadlines. @param value Receives limits. @return Operation status. */
  Status limits(GatewayLimits &value) const noexcept {
    return isReady() ? detail::status(corelib_gateway_limits(context_, &value))
                     : Status::InvalidState;
  }

private:
  /** @brief Native gateway operation taking only a context. */
  typedef corelib_status_t (*SimpleCall)(corelib_gateway_context_t *);
  /** @brief Invokes a simple native operation when ready. */
  Status call(SimpleCall operation) noexcept {
    return isReady() ? detail::status(operation(context_)) : Status::InvalidState;
  }
  /** @brief Binds every native storage descriptor to owned fixed arrays. */
  void populateStorage(corelib_gateway_config_t &native) noexcept {
    native.device.storage.reassembly.message = messages_;
    native.device.storage.reassembly.received = received_;
    native.device.storage.reassembly_slot_count = ReassemblySlots;
    native.device.storage.maximum_message_size = MaximumMessageBytes;
    native.device.storage.transaction_scratch = scratch_;
    native.device.storage.outbound.frames = outbound_;
    native.device.storage.outbound.capacity = OutboundFrames;
    native.device.storage.pending_requests.entries = pending_;
    native.device.storage.pending_requests.capacity = PendingRequests;
    native.device.storage.pending_requests.entry_size = CORELIB_PENDING_REQUEST_STORAGE_SIZE;
    native.storage.device_context_memory = deviceContext_;
    native.storage.device_context_memory_size = sizeof(deviceContext_);
    /** @brief Populates one native fixed-entry storage descriptor. */
#define CORELIB_GATEWAY_STORE(name, member, count)                       \
  do {                                                                   \
    native.storage.name.entries = member;                                \
    native.storage.name.capacity = count;                                \
    native.storage.name.entry_size = CORELIB_GATEWAY_ENTRY_STORAGE_SIZE; \
  } while (false)
    CORELIB_GATEWAY_STORE(links, links_, Links);
    CORELIB_GATEWAY_STORE(routes, routes_, Routes);
    CORELIB_GATEWAY_STORE(discoveries, discoveries_, Discoveries);
    CORELIB_GATEWAY_STORE(candidates, candidates_, Candidates);
    CORELIB_GATEWAY_STORE(assignments, assignments_, Assignments);
    CORELIB_GATEWAY_STORE(forwarding, forwarding_, ForwardingFrames);
#undef CORELIB_GATEWAY_STORE
    native.storage.control_reassembly.message = controlMessages_;
    native.storage.control_reassembly.received = controlReceived_;
    native.storage.control_reassembly_slots = ControlSlots;
    native.storage.maximum_control_message_size = ControlBytes;
  }
  /** @brief Adapts native frame transmission to GatewayHandler. */
  static corelib_send_result_t sendThunk(void *user, LinkId link, void *ctx, const uint8_t frame[64]) {
    Gateway &device = *static_cast<Gateway *>(user);
    return static_cast<corelib_send_result_t>(device.handler_->sendFrame(
        link, ctx, FrameView(frame, 64)));
  }
  /** @brief Adapts native transactions to GatewayHandler. */
  static void transactionThunk(void *user, const corelib_transaction_t *v) {
    Gateway &device = *static_cast<Gateway *>(user);
    const TransactionView view = {{v->id.token, v->id.share_id,
                                   static_cast<Action>(v->id.action)},
                                  ByteView(v->data, v->data_size)};
    device.handler_->onTransaction(view);
  }
  /** @brief Adapts native session changes to GatewayHandler. */
  static void sessionThunk(void *user, corelib_session_state_t state, uint32_t session, uint16_t address) {
    static_cast<Gateway *>(user)->handler_->onSessionChanged(
        static_cast<SessionState>(state), session, address);
  }
  /** @brief Adapts native node changes to GatewayHandler. */
  static void nodeThunk(void *user, const uint8_t uuid[16], bool reachable, uint16_t address) {
    static_cast<Gateway *>(user)->handler_->onNodeChanged(
        UuidView(uuid, 16), reachable, address);
  }
  /** @brief Adapts native diagnostics to GatewayHandler. */
  static void diagnosticThunk(void *user, corelib_diagnostic_t code, corelib_status_t status) {
    static_cast<Gateway *>(user)->handler_->onDiagnostic(
        static_cast<Diagnostic>(code), detail::status(status));
  }
  /** @brief Adapts profile discovery to GatewayHandler. */
  static corelib_send_result_t discoverThunk(void *user, LinkId link, void *ctx, LinkProfileId profile, const uint8_t token[16]) {
    return static_cast<corelib_send_result_t>(
        static_cast<Gateway *>(user)->handler_->discover(
            link, ctx, profile, UuidView(token, 16)));
  }
  /** @brief Adapts profile assignment to GatewayHandler. */
  static corelib_send_result_t bootstrapThunk(void *user, void *ctx, const corelib_bootstrap_assignment_t *value) {
    return static_cast<corelib_send_result_t>(
        static_cast<Gateway *>(user)->handler_->bootstrapAssign(ctx, *value));
  }
  /** @brief Adapts native topology events to GatewayHandler. */
  static void topologyThunk(void *user, const corelib_topology_event_t *value) {
    static_cast<Gateway *>(user)->handler_->onTopologyChanged(*value);
  }

  alignas(max_align_t) uint8_t contextMemory_[CORELIB_GATEWAY_CONTEXT_STORAGE_SIZE]{};               /** Gateway context. */
  alignas(max_align_t) uint8_t deviceContext_[CORELIB_CONTEXT_STORAGE_SIZE]{};                       /** Local endpoint context. */
  uint8_t messages_[MaximumMessageBytes * ReassemblySlots]{};                                        /** Endpoint messages. */
  uint8_t received_[255 * ReassemblySlots]{};                                                        /** Endpoint fragment flags. */
  uint8_t scratch_[MaximumMessageBytes]{};                                                           /** Transaction scratch bytes. */
  uint8_t outbound_[64 * OutboundFrames]{};                                                          /** Local outbound frames. */
  alignas(max_align_t) uint8_t pending_[CORELIB_PENDING_REQUEST_STORAGE_SIZE * PendingRequests]{};   /** Pending requests. */
  alignas(max_align_t) uint8_t links_[CORELIB_GATEWAY_ENTRY_STORAGE_SIZE * Links]{};                 /** Link entries. */
  alignas(max_align_t) uint8_t routes_[CORELIB_GATEWAY_ENTRY_STORAGE_SIZE * Routes]{};               /** Route entries. */
  alignas(max_align_t) uint8_t discoveries_[CORELIB_GATEWAY_ENTRY_STORAGE_SIZE * Discoveries]{};     /** Discovery entries. */
  alignas(max_align_t) uint8_t candidates_[CORELIB_GATEWAY_ENTRY_STORAGE_SIZE * Candidates]{};       /** Candidate entries. */
  alignas(max_align_t) uint8_t assignments_[CORELIB_GATEWAY_ENTRY_STORAGE_SIZE * Assignments]{};     /** Assignment entries. */
  alignas(max_align_t) uint8_t forwarding_[CORELIB_GATEWAY_ENTRY_STORAGE_SIZE * ForwardingFrames]{}; /**< Forwarded frames. */
  uint8_t controlMessages_[ControlBytes * ControlSlots]{};                                           /** Local control messages. */
  uint8_t controlReceived_[255 * ControlSlots]{};                                                    /** Control fragment flags. */
  corelib_gateway_context_t *context_{nullptr};                                                      /** Native gateway context. */
  GatewayHandler *handler_{nullptr};                                                                 /** Long-lived application handler. */
};

} // namespace corelib

#endif

/**
 * @file device.hpp
 * @brief Fixed-storage C++14 facade for the Programmor device endpoint API.
 */
#ifndef CORELIB_DEVICE_HPP
#define CORELIB_DEVICE_HPP

#include "corelib/device.h"

#include <etl/array.h>
#include <etl/span.h>
#include <etl/version.h>

#include <stddef.h>
#include <stdint.h>

#if __cplusplus < 201402L
#error "Corelib C++ facade requires C++14 or newer"
#endif

#if ETL_VERSION_MAJOR != 20 || ETL_VERSION_VALUE < 203201
#error "Corelib requires ETL >=20.32.1 and <21.0.0"
#endif

/** @brief Type-safe C++ facade for Corelib device API. */
namespace corelib {

/** @brief Application link identifier. */
using LinkId = corelib_link_id_t;
/** @brief Corelib and protocol version information. */
using Version = corelib_version_t;
/** @brief Endpoint resource-usage snapshot. */
using Usage = corelib_usage_t;
/** @brief Endpoint fixed-capacity limits. */
using Limits = corelib_limits_t;
/** @brief Borrowed dynamically sized byte view. */
using ByteView = etl::span<const uint8_t>;
/** @brief Borrowed view of one complete PFP frame. */
using FrameView = etl::span<const uint8_t, CORELIB_FRAME_SIZE>;
/** @brief Owning 16-byte UUID value. */
using Uuid = etl::array<uint8_t, 16>;
/** @brief Borrowed 16-byte UUID view. */
using UuidView = etl::span<const uint8_t, 16>;

/** @brief Type-safe Corelib operation status. */
enum class Status : uint8_t {
  Ok = CORELIB_OK,                              /** Operation succeeded. */
  InvalidArgument = CORELIB_INVALID_ARGUMENT,   /** Invalid argument. */
  InvalidState = CORELIB_INVALID_STATE,         /** Invalid lifecycle state. */
  InvalidFrame = CORELIB_INVALID_FRAME,         /** Invalid PFP frame. */
  Unsupported = CORELIB_UNSUPPORTED,            /** Unsupported behaviour. */
  CapacityExceeded = CORELIB_CAPACITY_EXCEEDED, /** Fixed storage is full. */
  Busy = CORELIB_BUSY,                          /** Operation may be retried. */
  NotFound = CORELIB_NOT_FOUND,                 /** Requested state was not found. */
  Expired = CORELIB_EXPIRED,                    /** Deadline expired. */
  Reentrant = CORELIB_REENTRANT                 /** Corelib callback attempted re-entry. */
};

/** @brief Type-safe transport send result. */
enum class SendResult : uint8_t {
  Accepted = CORELIB_SEND_ACCEPTED, /** Complete frame accepted. */
  Busy = CORELIB_SEND_BUSY,         /** No bytes consumed; retry later. */
  Failed = CORELIB_SEND_FAILED      /** Permanent transport failure. */
};

/** @brief Generic transaction action. */
enum class Action : uint8_t {
  CommonRequest = CORELIB_ACTION_COMMON_REQUEST,   /** Common request. */
  CommonPublish = CORELIB_ACTION_COMMON_PUBLISH,   /** Common publication. */
  CommonResponse = CORELIB_ACTION_COMMON_RESPONSE, /** Common response. */
  ShareRequest = CORELIB_ACTION_SHARE_REQUEST,     /** Share request. */
  SharePublish = CORELIB_ACTION_SHARE_PUBLISH,     /** Share publication. */
  ShareResponse = CORELIB_ACTION_SHARE_RESPONSE    /** Share response. */
};

/** @brief Application transaction result. */
enum class Result : uint8_t {
  Success = CORELIB_RESULT_SUCCESS,                /** Operation succeeded. */
  Unsupported = CORELIB_RESULT_UNSUPPORTED,        /** Schema or operation unsupported. */
  Busy = CORELIB_RESULT_BUSY,                      /** Application is busy. */
  InvalidRequest = CORELIB_RESULT_INVALID_REQUEST, /** Payload or request invalid. */
  InternalError = CORELIB_RESULT_INTERNAL_ERROR    /** Application internal error. */
};

/** @brief Endpoint session state. */
enum class SessionState : uint8_t {
  Inactive = CORELIB_SESSION_INACTIVE, /** No assigned session. */
  Active = CORELIB_SESSION_ACTIVE,     /** Assigned active session. */
  Stale = CORELIB_SESSION_STALE        /** Heartbeat deadline missed. */
};

/** @brief Recoverable Corelib diagnostic category. */
enum class Diagnostic : uint8_t {
  InvalidFrame = CORELIB_DIAGNOSTIC_INVALID_FRAME,     /** Frame rejected. */
  InvalidMessage = CORELIB_DIAGNOSTIC_INVALID_MESSAGE, /** Message rejected. */
  ResourceLimit = CORELIB_DIAGNOSTIC_RESOURCE_LIMIT,   /** Fixed storage exhausted. */
  SendFailed = CORELIB_DIAGNOSTIC_SEND_FAILED,         /** Permanent send failure. */
  RequestExpired = CORELIB_DIAGNOSTIC_REQUEST_EXPIRED, /** App response expired. */
  TimeReversed = CORELIB_DIAGNOSTIC_TIME_REVERSED      /** Monotonic time reversed. */
};

/** @brief Selects the application payload namespace. */
enum class PayloadKind : uint8_t { Common,
                                   Share };

/** @brief Retainable transaction correlation identity. */
struct TransactionId {
  uint32_t token;   /**< Transaction correlation token. */
  uint32_t shareId; /**< Common or Share schema identifier. */
  Action action;    /**< Transaction action. */
};

/** @brief Transaction callback value with borrowed application bytes. */
struct TransactionView {
  TransactionId id; /**< Retainable transaction identity. */
  ByteView data;    /**< Borrowed payload valid only during the callback. */
};

/** @brief High-level endpoint configuration with safe defaults. */
struct Config {
  Uuid nodeUuid{};                                                      /**< Persistent UUIDv4. */
  uint32_t capabilities{0};                                             /**< Advertised PFP capabilities. */
  uint32_t heartbeatIntervalMs{2000};                                   /**< Preferred heartbeat interval. */
  uint32_t applicationResponseTimeoutMs{1000};                          /**< Application response deadline. */
  size_t maximumTransactionDataSize{CORELIB_MIN_TRANSACTION_DATA_SIZE}; /**< Maximum app payload. */
};

/** @brief Synchronous application integration interface for a device endpoint. */
class Handler {
public:
  /** @brief Attempts to enqueue one frame. @param link Link identifier. @param transportContext Link context. @param frame Borrowed frame. @return Send result. */
  virtual SendResult sendFrame(LinkId link, void *transportContext, FrameView frame) = 0;
  /** @brief Receives a decoded transaction. @param transaction Borrowed callback value. */
  virtual void onTransaction(const TransactionView &transaction) { (void)transaction; }
  /** @brief Observes session lifecycle changes. @param state New state. @param sessionId Session identifier. @param localAddress Assigned address. */
  virtual void onSessionChanged(SessionState state, uint32_t sessionId, uint16_t localAddress) {
    (void)state;
    (void)sessionId;
    (void)localAddress;
  }
  /** @brief Observes local-node reachability. @param uuid Node UUID. @param reachable Reachability. @param localAddress Assigned address. */
  virtual void onNodeChanged(UuidView uuid, bool reachable, uint16_t localAddress) {
    (void)uuid;
    (void)reachable;
    (void)localAddress;
  }
  /** @brief Receives a recoverable diagnostic. @param diagnostic Category. @param status Associated status. */
  virtual void onDiagnostic(Diagnostic diagnostic, Status status) {
    (void)diagnostic;
    (void)status;
  }

protected:
  ~Handler() = default;
};

namespace detail {
/** @brief Converts a native operation status. @param value Native status. @return Type-safe status. */
inline Status status(corelib_status_t value) noexcept {
  return static_cast<Status>(value);
}
/** @brief Converts a retainable transaction identity. @param value C++ identity. @return Native identity. */
inline corelib_transaction_id_t transactionId(const TransactionId &value) noexcept {
  const corelib_transaction_id_t converted = {
      value.token, value.shareId,
      static_cast<corelib_action_t>(value.action)};
  return converted;
}
} // namespace detail

/**
 * @brief Fixed-storage owner and type-safe facade for one device endpoint.
 * @tparam MaximumMessageBytes Maximum complete PFP message bytes.
 * @tparam ReassemblySlots Concurrent fragmented inbound messages.
 * @tparam OutboundFrames Frames that may await transport acceptance.
 * @tparam PendingRequests Requests that may await application responses.
 */
template <size_t MaximumMessageBytes = 1024, size_t ReassemblySlots = 2, size_t OutboundFrames = 32, size_t PendingRequests = 8>
class Device final {
  static_assert(MaximumMessageBytes >=
                    CORELIB_MIN_TRANSACTION_DATA_SIZE + 19u,
                "message storage is too small for the minimum transaction payload");
  static_assert(MaximumMessageBytes <= CORELIB_MAX_MESSAGE_SIZE,
                "message storage exceeds the PFP limit");
  static_assert(ReassemblySlots > 0 && ReassemblySlots <= 8,
                "reassembly slot count must be between 1 and 8");
  static_assert(OutboundFrames > 0 && OutboundFrames <= 255,
                "outbound frame count must be between 1 and 255");
  static_assert(PendingRequests > 0, "at least one pending request is required");

public:
  /** @brief Constructs an uninitialised endpoint with zeroed fixed storage. */
  Device() = default;
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&) = delete;
  Device &operator=(Device &&) = delete;

  /** @brief Initialises the type-safe endpoint. @param config Endpoint settings. @param handler Long-lived callback handler. @return Initialisation status. */
  Status init(const Config &config, Handler &handler) noexcept {
    if (isReady())
      return Status::InvalidState;
    corelib_config_t native{};
    for (size_t index = 0; index < config.nodeUuid.size(); ++index) {
      native.node_uuid[index] = config.nodeUuid[index];
    }
    native.capabilities = config.capabilities;
    native.heartbeat_interval_ms = config.heartbeatIntervalMs;
    native.application_response_timeout_ms = config.applicationResponseTimeoutMs;
    native.maximum_transaction_data_size = config.maximumTransactionDataSize;
    native.callbacks.send_frame = &sendFrameThunk;
    native.callbacks.transaction = &transactionThunk;
    native.callbacks.session_changed = &sessionThunk;
    native.callbacks.node_changed = &nodeThunk;
    native.callbacks.diagnostic = &diagnosticThunk;
    native.callbacks.user = this;
    handler_ = &handler;
    const Status result = initNative(native);
    if (result != Status::Ok)
      handler_ = nullptr;
    return result;
  }

  /** @brief Initialises through the C configuration compatibility path. @param config Native configuration with callbacks. @return Native initialisation status. */
  corelib_status_t init(corelib_config_t config) noexcept {
    if (isReady())
      return CORELIB_INVALID_STATE;
    handler_ = nullptr;
    return static_cast<corelib_status_t>(initNative(config));
  }

  /** @brief Tests whether initialisation succeeded. @return True when a native context exists. */
  bool isReady() const noexcept { return context_ != nullptr; }
  /** @brief Accesses the native C context. @return Mutable native context or null. */
  corelib_context_t *nativeHandle() noexcept { return context_; }
  /** @brief Accesses the native C context. @return Read-only native context or null. */
  const corelib_context_t *nativeHandle() const noexcept { return context_; }
  /** @brief Compatibility alias for nativeHandle(). @return Mutable native context or null. */
  corelib_context_t *get() noexcept { return nativeHandle(); }
  /** @brief Compatibility alias for nativeHandle(). @return Read-only native context or null. */
  const corelib_context_t *get() const noexcept { return nativeHandle(); }

  /** @brief Clears endpoint protocol state. @return Operation status. */
  Status reset() noexcept {
    return isReady() ? detail::status(corelib_reset(context_))
                     : Status::InvalidState;
  }
  /** @brief Advances timers and queued work. @param nowMs Non-decreasing monotonic time. @return Operation status. */
  Status tick(uint64_t nowMs) noexcept {
    return isReady() ? detail::status(corelib_tick(context_, nowMs))
                     : Status::InvalidState;
  }
  /** @brief Registers the single endpoint link. @param link Non-zero link identifier. @param transportContext Opaque link context. @return Operation status. */
  Status addLink(LinkId link, void *transportContext = nullptr) noexcept {
    return isReady()
               ? detail::status(
                     corelib_add_link(context_, link, transportContext))
               : Status::InvalidState;
  }
  /** @brief Removes the registered endpoint link. @param link Link identifier. @return Operation status. */
  Status removeLink(LinkId link) noexcept {
    return isReady()
               ? detail::status(corelib_remove_link(context_, link))
               : Status::InvalidState;
  }
  /** @brief Processes one complete frame view. @param link Receiving link. @param frame Borrowed frame. @param nowMs Non-decreasing time. @return Processing status. */
  Status receive(LinkId link, FrameView frame, uint64_t nowMs) noexcept {
    return isReady()
               ? detail::status(corelib_receive_frame(
                     context_, link, frame.data(), nowMs))
               : Status::InvalidState;
  }
  /** @brief Processes one complete C array frame. @param link Receiving link. @param frame Borrowed frame. @param nowMs Non-decreasing time. @return Processing status. */
  Status receive(LinkId link, const uint8_t (&frame)[CORELIB_FRAME_SIZE], uint64_t nowMs) noexcept {
    return receive(link, FrameView(frame, CORELIB_FRAME_SIZE), nowMs);
  }
  /** @brief Accepts a link-profile bootstrap assignment. @param assignment Assignment value. @param nowMs Non-decreasing time. @return Validation status. */
  Status acceptBootstrap(const corelib_bootstrap_assignment_t &assignment, uint64_t nowMs) noexcept {
    return isReady()
               ? detail::status(corelib_accept_bootstrap_assignment(
                     context_, &assignment, nowMs))
               : Status::InvalidState;
  }
  /** @brief Responds to a pending application request. @param request Retained request identity. @param result Application result. @param data Optional encoded payload. @return Operation status. */
  Status respond(const TransactionId &request, Result result, ByteView data = ByteView()) noexcept {
    if (!isReady())
      return Status::InvalidState;
    const corelib_transaction_id_t native = detail::transactionId(request);
    return detail::status(corelib_respond(
        context_, &native, static_cast<corelib_transaction_result_t>(result),
        data.data(), data.size()));
  }
  /** @brief Publishes an encoded application payload. @param kind Common or Share. @param shareId Schema identifier. @param data Encoded bytes. @return Operation status. */
  Status publish(PayloadKind kind, uint32_t shareId, ByteView data) noexcept {
    return isReady()
               ? detail::status(corelib_publish(
                     context_, kind == PayloadKind::Common, shareId, data.data(),
                     data.size()))
               : Status::InvalidState;
  }
  /** @brief Returns Corelib and protocol versions. @return Version information. */
  static Version version() noexcept { return corelib_version(); }
  /** @brief Reads current resource use. @param value Receives usage. @return Operation status. */
  Status usage(Usage &value) const noexcept {
    return isReady() ? detail::status(corelib_usage(context_, &value))
                     : Status::InvalidState;
  }
  /** @brief Reads fixed endpoint capacities. @param value Receives limits. @return Operation status. */
  Status limits(Limits &value) const noexcept {
    return isReady()
               ? detail::status(corelib_limits(context_, &value))
               : Status::InvalidState;
  }

private:
  /** @brief Binds fixed storage and initialises the native endpoint. @param config Native configuration. @return Initialisation status. */
  Status initNative(corelib_config_t config) noexcept {
    context_ = nullptr;
    config.storage.reassembly.message = messages_;
    config.storage.reassembly.received = received_;
    config.storage.reassembly.headers = nullptr;
    config.storage.reassembly_slot_count = ReassemblySlots;
    config.storage.maximum_message_size = MaximumMessageBytes;
    config.storage.transaction_scratch = scratch_;
    config.storage.outbound.frames = frames_;
    config.storage.outbound.capacity = OutboundFrames;
    config.storage.pending_requests.entries = pending_;
    config.storage.pending_requests.capacity = PendingRequests;
    config.storage.pending_requests.entry_size = PendingEntrySize;
    return detail::status(corelib_init(
        contextMemory_, sizeof(contextMemory_), &config, &context_));
  }

  /** @brief Adapts the native send callback to Handler::sendFrame. */
  static corelib_send_result_t sendFrameThunk(void *user, LinkId link, void *transportContext, const uint8_t frame[CORELIB_FRAME_SIZE]) {
    Device &device = *static_cast<Device *>(user);
    return static_cast<corelib_send_result_t>(device.handler_->sendFrame(
        link, transportContext, FrameView(frame, CORELIB_FRAME_SIZE)));
  }
  /** @brief Adapts the native transaction callback to Handler::onTransaction. */
  static void transactionThunk(void *user, const corelib_transaction_t *value) {
    Device &device = *static_cast<Device *>(user);
    const TransactionView view = {
        {value->id.token, value->id.share_id,
         static_cast<Action>(value->id.action)},
        ByteView(value->data, value->data_size)};
    device.handler_->onTransaction(view);
  }
  /** @brief Adapts the native session callback to Handler::onSessionChanged. */
  static void sessionThunk(void *user, corelib_session_state_t state, uint32_t sessionId, uint16_t localAddress) {
    static_cast<Device *>(user)->handler_->onSessionChanged(
        static_cast<SessionState>(state), sessionId, localAddress);
  }
  /** @brief Adapts the native node callback to Handler::onNodeChanged. */
  static void nodeThunk(void *user, const uint8_t uuid[16], bool reachable, uint16_t localAddress) {
    static_cast<Device *>(user)->handler_->onNodeChanged(
        UuidView(uuid, 16), reachable, localAddress);
  }
  /** @brief Adapts the native diagnostic callback to Handler::onDiagnostic. */
  static void diagnosticThunk(void *user, corelib_diagnostic_t code, corelib_status_t status) {
    static_cast<Device *>(user)->handler_->onDiagnostic(
        static_cast<Diagnostic>(code), detail::status(status));
  }

  static constexpr size_t ContextBytes = CORELIB_CONTEXT_STORAGE_SIZE;             /** Native context bytes. */
  static constexpr size_t PendingEntrySize = CORELIB_PENDING_REQUEST_STORAGE_SIZE; /**< Pending entry bytes. */
  alignas(max_align_t) uint8_t contextMemory_[ContextBytes]{};                     /** Native context storage. */
  uint8_t messages_[MaximumMessageBytes * ReassemblySlots]{};                      /** Reassembled messages. */
  uint8_t scratch_[MaximumMessageBytes]{};                                         /** Transaction scratch storage. */
  uint8_t received_[255 * ReassemblySlots]{};                                      /** Fragment receipt flags. */
  uint8_t frames_[CORELIB_FRAME_SIZE * OutboundFrames]{};                          /** Outbound frame queue. */
  alignas(max_align_t) uint8_t pending_[PendingEntrySize * PendingRequests]{};     /** Pending requests. */
  corelib_context_t *context_{nullptr};                                            /** Initialised native context. */
  Handler *handler_{nullptr};                                                      /** Long-lived application handler. */
};

} // namespace corelib

#endif

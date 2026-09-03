/**
 * @file device.h
 * @brief Heap-free C11 device endpoint API for Portable Frame Protocol.
 */
#ifndef CORELIB_DEVICE_H
#define CORELIB_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Corelib semantic major version. */
#define CORELIB_VERSION_MAJOR 1u
/** @brief Corelib semantic minor version. */
#define CORELIB_VERSION_MINOR 0u
/** @brief Corelib semantic patch version. */
#define CORELIB_VERSION_PATCH 0u
/** @brief Supported PFP version. */
#define CORELIB_PFP_VERSION 1u
/** @brief Supported Transaction Protocol version. */
#define CORELIB_TRANSACTION_VERSION 2u
/** @brief Complete encoded PFP frame bytes. */
#define CORELIB_FRAME_SIZE 64u
/** @brief PFP payload bytes per frame. */
#define CORELIB_FRAME_PAYLOAD_SIZE 40u
/** @brief Protocol maximum complete message bytes. */
#define CORELIB_MAX_MESSAGE_SIZE 10200u
/** @brief Minimum supported opaque transaction payload bytes. */
#define CORELIB_MIN_TRANSACTION_DATA_SIZE 64u
/** @brief Capability bit identifying a gateway node. */
#define CORELIB_CAPABILITY_GATEWAY 0x00000001u
/** @brief Conservative portable storage bytes for an endpoint context. */
#define CORELIB_CONTEXT_STORAGE_SIZE 2048u
/** @brief Conservative storage bytes for one pending request. */
#define CORELIB_PENDING_REQUEST_STORAGE_SIZE 64u

/** @brief Opaque state for one independently serialized Corelib endpoint. */
typedef struct corelib_context corelib_context_t;
/** @brief Application-defined identifier for a registered physical link. */
typedef uint16_t corelib_link_id_t;

/** @brief Result of a Corelib operation. */
typedef enum {
  CORELIB_OK = 0,            /** Operation completed successfully. */
  CORELIB_INVALID_ARGUMENT,  /** An argument or configuration value is invalid. */
  CORELIB_INVALID_STATE,     /** The operation is not valid in the current state. */
  CORELIB_INVALID_FRAME,     /** A PFP frame failed structural or protocol validation. */
  CORELIB_UNSUPPORTED,       /** The requested behaviour or protocol value is unsupported. */
  CORELIB_CAPACITY_EXCEEDED, /** Caller-provided bounded storage has no free capacity. */
  CORELIB_BUSY,              /** Work could not complete now and may be retried. */
  CORELIB_NOT_FOUND,         /** The requested link, transaction, or node does not exist. */
  CORELIB_EXPIRED,           /** The requested operation exceeded its deadline. */
  CORELIB_REENTRANT          /** An Corelib operation was called from a Corelib callback. */
} corelib_status_t;

/** @brief Result returned by the application's non-blocking frame sender. */
typedef enum {
  CORELIB_SEND_ACCEPTED = 0, /** The complete frame was accepted. */
  CORELIB_SEND_BUSY,         /** No bytes were consumed and retry is safe. */
  CORELIB_SEND_FAILED        /** A permanent transport failure occurred. */
} corelib_send_result_t;

/** @brief Generic Transaction Protocol operation delivered to the application. */
typedef enum {
  CORELIB_ACTION_COMMON_REQUEST = 1,  /** Request a Common payload. */
  CORELIB_ACTION_COMMON_PUBLISH = 2,  /** Publish a Common payload. */
  CORELIB_ACTION_COMMON_RESPONSE = 3, /** Respond with a Common payload. */
  CORELIB_ACTION_SHARE_REQUEST = 4,   /** Request a Share payload. */
  CORELIB_ACTION_SHARE_PUBLISH = 5,   /** Publish a Share payload. */
  CORELIB_ACTION_SHARE_RESPONSE = 6   /** Respond with a Share payload. */
} corelib_action_t;

/** @brief Application-level result encoded in a transaction response. */
typedef enum {
  CORELIB_RESULT_SUCCESS = 1,         /** The operation completed successfully. */
  CORELIB_RESULT_UNSUPPORTED = 2,     /** The Common or Share operation is unsupported. */
  CORELIB_RESULT_BUSY = 3,            /** The application cannot process the request now. */
  CORELIB_RESULT_INVALID_REQUEST = 4, /** The application payload or request is invalid. */
  CORELIB_RESULT_INTERNAL_ERROR = 5   /** The application encountered an internal failure. */
} corelib_transaction_result_t;

/** @brief Current PFP session lifecycle state. */
typedef enum {
  CORELIB_SESSION_INACTIVE = 0, /** The endpoint has no assigned session. */
  CORELIB_SESSION_ACTIVE,       /** The endpoint has an active assigned session. */
  CORELIB_SESSION_STALE         /** The session heartbeat deadline was missed. */
} corelib_session_state_t;

/** @brief Diagnostic category reported to the application. */
typedef enum {
  CORELIB_DIAGNOSTIC_INVALID_FRAME = 1, /** A received PFP frame was rejected. */
  CORELIB_DIAGNOSTIC_INVALID_MESSAGE,   /** A reassembled control or transaction message was rejected. */
  CORELIB_DIAGNOSTIC_RESOURCE_LIMIT,    /** Bounded storage prevented an operation. */
  CORELIB_DIAGNOSTIC_SEND_FAILED,       /** The transport reported a permanent send failure. */
  CORELIB_DIAGNOSTIC_REQUEST_EXPIRED,   /** An application response deadline expired. */
  CORELIB_DIAGNOSTIC_TIME_REVERSED      /** Monotonic time moved backwards. */
} corelib_diagnostic_t;

/** @brief Stable correlation identity for one application transaction. */
typedef struct {
  uint32_t token;          /**< Protocol correlation token. */
  uint32_t share_id;       /**< Common or Share schema identifier. */
  corelib_action_t action; /**< Transaction operation. */
} corelib_transaction_id_t;

/** @brief Borrowed application transaction delivered during a callback. */
typedef struct {
  corelib_transaction_id_t id; /**< Identity retained when responding later. */
  const uint8_t *data;         /**< Borrowed opaque Common or Share payload. */
  size_t data_size;            /**< Number of payload bytes. */
} corelib_transaction_t;

/** @brief Corelib and supported wire-protocol versions. */
typedef struct {
  uint16_t major;              /**< Corelib semantic major version. */
  uint16_t minor;              /**< Corelib semantic minor version. */
  uint16_t patch;              /**< Corelib semantic patch version. */
  uint8_t pfp_version;         /**< Supported PFP version. */
  uint8_t transaction_version; /**< Supported Transaction Protocol version. */
} corelib_version_t;

/** @brief Profile-delivered assignment for a node that is not yet PFP-addressable. */
typedef struct {
  uint8_t node_uuid[16];          /**< Persistent UUIDv4 of the assigned node. */
  uint32_t session_id;            /**< Non-zero session identifier. */
  uint32_t transaction_id;        /**< Assignment transaction identifier. */
  uint32_t heartbeat_interval_ms; /**< Assigned heartbeat interval. */
  uint16_t node_address;          /**< Assigned PFP address. */
  uint16_t parent_address;        /**< Address of the direct parent gateway. */
  corelib_link_id_t link_id;      /**< Link carrying the assigned session. */
} corelib_bootstrap_assignment_t;

/** @brief Snapshot of currently occupied endpoint resources. */
typedef struct {
  uint16_t active_reassemblies; /**< Reassembly slots currently occupied. */
  uint16_t queued_frames;       /**< Frames awaiting transport acceptance. */
  uint16_t pending_requests;    /**< Requests awaiting application responses. */
  size_t reassembly_bytes;      /**< Bytes retained by active reassemblies. */
} corelib_usage_t;

/** @brief Fixed capacities configured for an endpoint context. */
typedef struct {
  size_t maximum_message_size;          /**< Maximum complete PFP message size. */
  size_t maximum_transaction_data_size; /**< Maximum opaque application payload size. */
  size_t reassembly_slots;              /**< Concurrent inbound messages. */
  size_t outbound_frames;               /**< Frames that may await transmission. */
  size_t pending_requests;              /**< Concurrent application requests. */
} corelib_limits_t;

/** @brief Caller-owned storage used for fragmented-message reassembly. */
typedef struct {
  uint8_t *message;  /**< Message byte storage for every slot. */
  uint8_t *received; /**< Per-fragment receipt flags for every slot. */
  uint8_t *headers;  /**< Private per-slot Corelib metadata storage. */
} corelib_reassembly_storage_t;

/** @brief Caller-owned queue storage for complete PFP frames. */
typedef struct {
  uint8_t *frames; /**< Contiguous storage for 64-byte frames. */
  size_t capacity; /**< Number of frames that fit in the storage. */
} corelib_frame_storage_t;

/** @brief Generic caller-owned fixed-entry storage description. */
typedef struct {
  void *entries;     /**< Suitably aligned entry storage. */
  size_t capacity;   /**< Number of available entries. */
  size_t entry_size; /**< Size of each entry supplied by the caller. */
} corelib_entry_storage_t;

/** @brief Complete caller-owned memory configuration for an endpoint. */
typedef struct {
  corelib_reassembly_storage_t reassembly;  /**< Fragment reassembly storage. */
  size_t reassembly_slot_count;             /**< Number of reassembly slots. */
  size_t maximum_message_size;              /**< Bytes available to each reassembly slot. */
  uint8_t *transaction_scratch;             /**< Scratch buffer of maximum message size. */
  corelib_frame_storage_t outbound;         /**< Outbound frame queue storage. */
  corelib_entry_storage_t pending_requests; /**< Pending application request storage. */
} corelib_storage_t;

/** @brief Attempts to enqueue one complete PFP frame without blocking. */
typedef corelib_send_result_t (*corelib_send_callback_t)(
    void *user, corelib_link_id_t link_id, void *transport_context,
    const uint8_t frame[CORELIB_FRAME_SIZE]);
/** @brief Delivers one decoded generic transaction with borrowed payload bytes. */
typedef void (*corelib_transaction_callback_t)(
    void *user, const corelib_transaction_t *transaction);
/** @brief Reports a local session lifecycle change. */
typedef void (*corelib_session_callback_t)(
    void *user, corelib_session_state_t state, uint32_t session_id,
    uint16_t local_address);
/** @brief Reports local node reachability and address changes. */
typedef void (*corelib_node_callback_t)(
    void *user, const uint8_t node_uuid[16], bool reachable,
    uint16_t local_address);
/** @brief Reports a recoverable Corelib diagnostic. */
typedef void (*corelib_diagnostic_callback_t)(
    void *user, corelib_diagnostic_t diagnostic,
    corelib_status_t status);

/** @brief Application callback table invoked synchronously by Corelib operations. */
typedef struct {
  corelib_send_callback_t send_frame;         /**< Required non-blocking frame sender. */
  corelib_transaction_callback_t transaction; /**< Optional transaction handler. */
  corelib_session_callback_t session_changed; /**< Optional session observer. */
  corelib_node_callback_t node_changed;       /**< Optional local-node observer. */
  corelib_diagnostic_callback_t diagnostic;   /**< Optional diagnostic observer. */
  void *user;                                 /**< Opaque value passed to every callback. */
} corelib_callbacks_t;

/** @brief Immutable endpoint configuration copied during initialisation. */
typedef struct {
  uint8_t node_uuid[16];                    /**< Persistent UUIDv4 for this node. */
  uint32_t capabilities;                    /**< PFP capability bits advertised by this node. */
  uint32_t heartbeat_interval_ms;           /**< Preferred heartbeat interval. */
  uint32_t application_response_timeout_ms; /**< Deadline for application responses. */
  size_t maximum_transaction_data_size;     /**< Maximum opaque Common or Share payload. */
  corelib_callbacks_t callbacks;            /**< Application integration callbacks. */
  corelib_storage_t storage;                /**< Caller-owned fixed-capacity storage. */
} corelib_config_t;

/** @brief Returns the bytes required for opaque endpoint context storage. @return Required context bytes. */
size_t corelib_context_size(void);
/** @brief Returns the alignment required for endpoint context storage. @return Required byte alignment. */
size_t corelib_context_alignment(void);
/** @brief Returns the bytes required for one pending-request entry. @return Required entry bytes. */
size_t corelib_pending_request_entry_size(void);

/**
 * @brief Initialises an endpoint in caller-owned memory.
 * @param[in,out] context_memory Suitably aligned writable context storage.
 * @param[in] context_memory_size Available context storage bytes.
 * @param[in] config Endpoint configuration copied by Corelib.
 * @param[out] context Receives the initialised opaque context on success.
 * @return Status describing validation or initialisation success.
 */
corelib_status_t corelib_init(void *context_memory, size_t context_memory_size, const corelib_config_t *config, corelib_context_t **context);
/** @brief Clears protocol state while retaining configuration and registered link. @param[in,out] context Initialised context. @return Operation status. */
corelib_status_t corelib_reset(corelib_context_t *context);
/** @brief Advances deadlines, heartbeats, and queued-frame transmission. @param[in,out] context Initialised context. @param[in] monotonic_ms Non-decreasing monotonic time. @return Operation status. */
corelib_status_t corelib_tick(corelib_context_t *context, uint64_t monotonic_ms);
/** @brief Registers the endpoint's single dedicated PFP link. @param[in,out] context Initialised context. @param[in] link_id Non-zero application link identifier. @param[in] transport_context Opaque value returned to the send callback. @return Operation status. */
corelib_status_t corelib_add_link(corelib_context_t *context, corelib_link_id_t link_id, void *transport_context);
/** @brief Removes the registered link and clears its protocol state. @param[in,out] context Initialised context. @param[in] link_id Registered link identifier. @return Operation status. */
corelib_status_t corelib_remove_link(corelib_context_t *context, corelib_link_id_t link_id);
/** @brief Processes one complete 64-byte PFP frame. @param[in,out] context Initialised context. @param[in] link_id Receiving link. @param[in] frame Complete borrowed frame. @param[in] monotonic_ms Non-decreasing monotonic time. @return Frame-processing status. */
corelib_status_t corelib_receive_frame(corelib_context_t *context, corelib_link_id_t link_id, const uint8_t frame[CORELIB_FRAME_SIZE], uint64_t monotonic_ms);
/** @brief Activates a profile-delivered downstream assignment. @param[in,out] context Initialised inactive endpoint. @param[in] assignment Assignment matching this node and link. @param[in] monotonic_ms Non-decreasing monotonic time. @return Assignment validation status. */
corelib_status_t corelib_accept_bootstrap_assignment(corelib_context_t *context, const corelib_bootstrap_assignment_t *assignment, uint64_t monotonic_ms);
/** @brief Sends an application response for a pending request. @param[in,out] context Initialised context. @param[in] request Retained request identity. @param[in] result Application result. @param[in] data Optional borrowed encoded payload. @param[in] data_size Payload bytes. @return Queueing or validation status. */
corelib_status_t corelib_respond(corelib_context_t *context, const corelib_transaction_id_t *request, corelib_transaction_result_t result, const uint8_t *data, size_t data_size);
/** @brief Publishes an encoded Common or Share payload. @param[in,out] context Initialised context. @param[in] common True for Common and false for Share. @param[in] share_id Schema identifier. @param[in] data Borrowed encoded payload. @param[in] data_size Payload bytes. @return Queueing or validation status. */
corelib_status_t corelib_publish(corelib_context_t *context, bool common, uint32_t share_id, const uint8_t *data, size_t data_size);
/** @brief Returns compile-time Corelib and protocol versions. @return Version values. */
corelib_version_t corelib_version(void);
/** @brief Reads current resource usage. @param[in] context Initialised context. @param[out] usage Receives a consistent usage snapshot. @return Operation status. */
corelib_status_t corelib_usage(const corelib_context_t *context, corelib_usage_t *usage);
/** @brief Reads fixed context capacities. @param[in] context Initialised context. @param[out] limits Receives configured limits. @return Operation status. */
corelib_status_t corelib_limits(const corelib_context_t *context, corelib_limits_t *limits);

#ifdef __cplusplus
}
#endif

#endif

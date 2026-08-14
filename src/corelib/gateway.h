/**
 * @file gateway.h
 * @brief Optional heap-free C11 gateway API for routed PFP topologies.
 */
#ifndef CORELIB_GATEWAY_H
#define CORELIB_GATEWAY_H

#include "corelib/device.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Conservative portable storage bytes for a gateway context. */
#define CORELIB_GATEWAY_CONTEXT_STORAGE_SIZE 3072u
/** @brief Conservative storage bytes for one gateway entry. */
#define CORELIB_GATEWAY_ENTRY_STORAGE_SIZE 96u

/** @brief Opaque state for one independently serialized gateway. */
typedef struct corelib_gateway_context corelib_gateway_context_t;
/** @brief Application-defined identifier for a downstream link profile. */
typedef uint32_t corelib_link_profile_id_t;

/** @brief Direction of a dedicated PFP link relative to the gateway. */
typedef enum {
  CORELIB_LINK_UPSTREAM = 1,  /** Link toward the root adapter. */
  CORELIB_LINK_DOWNSTREAM = 2 /** Link toward candidate nodes. */
} corelib_link_role_t;

/** @brief Lifecycle state of a gateway route. */
typedef enum {
  CORELIB_ROUTE_PROVISIONAL = 1, /** Reserved during atomic assignment. */
  CORELIB_ROUTE_READY = 2        /** Published and available for routing. */
} corelib_route_state_t;

/** @brief PFP control status returned for downstream assignment. */
typedef enum {
  CORELIB_CONTROL_SUCCESS = 0,            /** Control operation succeeded. */
  CORELIB_CONTROL_MALFORMED = 1,          /** Control payload is malformed. */
  CORELIB_CONTROL_UNSUPPORTED = 2,        /** Control operation is unsupported. */
  CORELIB_CONTROL_CONFLICT = 3,           /** Requested state conflicts with existing state. */
  CORELIB_CONTROL_DUPLICATE_IDENTITY = 4, /** UUID already exists in the topology. */
  CORELIB_CONTROL_NO_ROUTE = 5,           /** No route exists for the target. */
  CORELIB_CONTROL_HOP_LIMIT = 6,          /** Route would exceed the PFP hop limit. */
  CORELIB_CONTROL_SESSION_REJECTED = 7,   /** Downstream node rejected the session. */
  CORELIB_CONTROL_RESOURCE_LIMIT = 8      /** Bounded storage cannot accept the operation. */
} corelib_control_status_t;

/** @brief Configuration for one dedicated gateway link. */
typedef struct {
  corelib_link_id_t link_id;            /**< Non-zero application link identifier. */
  corelib_link_profile_id_t profile_id; /**< Profile identifier for downstream discovery. */
  corelib_link_role_t role;             /**< Upstream or downstream direction. */
  void *transport_context;              /**< Opaque value supplied to transport callbacks. */
  bool available;                       /**< Initial link availability. */
} corelib_link_config_t;

/** @brief Profile-discovered candidate reported to the gateway. */
typedef struct {
  uint8_t node_uuid[16];       /**< Persistent candidate UUIDv4. */
  uint32_t capabilities;       /**< Candidate PFP capabilities. */
  uint8_t discovery_token[16]; /**< Token of the active discovery round. */
  corelib_link_id_t link_id;   /**< Downstream link that found the candidate. */
} corelib_candidate_t;

/** @brief Topology reachability event emitted after committed state changes. */
typedef struct {
  uint8_t node_uuid[16];          /**< Persistent node UUIDv4. */
  uint32_t capabilities;          /**< Node PFP capabilities. */
  uint16_t node_address;          /**< Assigned session address. */
  uint16_t parent_address;        /**< Parent gateway address. */
  corelib_link_id_t ingress_link; /**< Direct gateway link for the route. */
  corelib_route_state_t state;    /**< Provisional or ready route state. */
  bool reachable;                 /**< True when added and false when removed. */
} corelib_topology_event_t;

/** @brief Snapshot of currently occupied gateway resources. */
typedef struct {
  uint16_t links;                       /**< Registered links. */
  uint16_t routes;                      /**< Installed routes. */
  uint16_t discoveries;                 /**< Active discovery rounds. */
  uint16_t candidates;                  /**< Retained discovered candidates. */
  uint16_t assignments;                 /**< Assignments awaiting completion. */
  uint16_t queued_frames;               /**< Forwarding frames awaiting transport. */
  uint16_t active_control_reassemblies; /**< Locally addressed control reassemblies. */
} corelib_gateway_usage_t;

/** @brief Fixed capacities and deadlines configured for a gateway. */
typedef struct {
  size_t links;                            /**< Link capacity. */
  size_t routes;                           /**< Route capacity. */
  size_t discoveries;                      /**< Discovery-round capacity. */
  size_t candidates;                       /**< Candidate capacity. */
  size_t assignments;                      /**< Pending-assignment capacity. */
  size_t queued_frames;                    /**< Forwarding-frame capacity. */
  size_t control_reassembly_slots;         /**< Local control reassembly capacity. */
  size_t maximum_control_message_size;     /**< Maximum local control message bytes. */
  uint32_t discovery_timeout_ms;           /**< Discovery completion deadline. */
  uint32_t assignment_timeout_ms;          /**< Assignment completion deadline. */
  uint32_t candidate_retention_timeout_ms; /**< Unassigned candidate retention deadline. */
} corelib_gateway_limits_t;

/** @brief Starts profile-specific discovery on one downstream link. */
typedef corelib_send_result_t (*corelib_discover_callback_t)(
    void *user, corelib_link_id_t link_id, void *transport_context,
    corelib_link_profile_id_t profile_id,
    const uint8_t discovery_token[16]);
/** @brief Delivers an address assignment through a downstream link profile. */
typedef corelib_send_result_t (*corelib_bootstrap_callback_t)(
    void *user, void *transport_context,
    const corelib_bootstrap_assignment_t *assignment);
/** @brief Reports a committed gateway topology change. */
typedef void (*corelib_topology_callback_t)(
    void *user, const corelib_topology_event_t *event);

/** @brief Gateway-specific synchronous callback table. */
typedef struct {
  corelib_discover_callback_t discover;          /**< Required discovery callback. */
  corelib_bootstrap_callback_t bootstrap_assign; /**< Required assignment callback. */
  corelib_topology_callback_t topology_changed;  /**< Optional topology observer. */
} corelib_gateway_callbacks_t;

/** @brief Caller-owned fixed-capacity gateway storage. */
typedef struct {
  corelib_entry_storage_t links;                   /**< Registered link entries. */
  corelib_entry_storage_t routes;                  /**< Topology route entries. */
  corelib_entry_storage_t discoveries;             /**< Active discovery entries. */
  corelib_entry_storage_t candidates;              /**< Candidate entries. */
  corelib_entry_storage_t assignments;             /**< Pending assignment entries. */
  corelib_entry_storage_t forwarding;              /**< Forwarded frame entries. */
  corelib_reassembly_storage_t control_reassembly; /**< Local control reassembly storage. */
  size_t control_reassembly_slots;                 /**< Number of control reassembly slots. */
  size_t maximum_control_message_size;             /**< Bytes available to each control slot. */
  void *device_context_memory;                     /**< Embedded endpoint context storage. */
  size_t device_context_memory_size;               /**< Embedded endpoint storage bytes. */
} corelib_gateway_storage_t;

/** @brief Immutable gateway configuration copied during initialisation. */
typedef struct {
  corelib_config_t device;                 /**< Local endpoint configuration. */
  corelib_gateway_callbacks_t callbacks;   /**< Gateway integration callbacks. */
  corelib_gateway_storage_t storage;       /**< Caller-owned storage. */
  uint32_t discovery_timeout_ms;           /**< Discovery deadline. */
  uint32_t assignment_timeout_ms;          /**< Assignment deadline. */
  uint32_t candidate_retention_timeout_ms; /**< Candidate retention deadline. */
} corelib_gateway_config_t;

/** @brief Returns required gateway context bytes. @return Required context bytes. */
size_t corelib_gateway_context_size(void);
/** @brief Returns required gateway context alignment. @return Required byte alignment. */
size_t corelib_gateway_context_alignment(void);
/** @brief Returns required bytes for one gateway entry. @return Required entry bytes. */
size_t corelib_gateway_entry_size(void);
/** @brief Initialises a gateway in caller-owned memory. @param[in,out] context_memory Aligned writable storage. @param[in] context_memory_size Storage bytes. @param[in] config Configuration copied by Corelib. @param[out] context Receives the context. @return Operation status. */
corelib_status_t corelib_gateway_init(void *context_memory, size_t context_memory_size, const corelib_gateway_config_t *config, corelib_gateway_context_t **context);
/** @brief Clears gateway protocol and topology state. @param[in,out] context Initialised gateway. @return Operation status. */
corelib_status_t corelib_gateway_reset(corelib_gateway_context_t *context);
/** @brief Advances gateway deadlines and queued work. @param[in,out] context Initialised gateway. @param[in] monotonic_ms Non-decreasing time. @return Operation status. */
corelib_status_t corelib_gateway_tick(corelib_gateway_context_t *context, uint64_t monotonic_ms);
/** @brief Registers one dedicated PFP link. @param[in,out] context Initialised gateway. @param[in] link Link configuration. @return Operation status. */
corelib_status_t corelib_gateway_add_link(corelib_gateway_context_t *context, const corelib_link_config_t *link);
/** @brief Removes a link and all dependent topology. @param[in,out] context Initialised gateway. @param[in] link_id Link identifier. @return Operation status. */
corelib_status_t corelib_gateway_remove_link(corelib_gateway_context_t *context, corelib_link_id_t link_id);
/** @brief Changes link availability and performs loss cleanup. @param[in,out] context Initialised gateway. @param[in] link_id Link identifier. @param[in] available New availability. @return Operation status. */
corelib_status_t corelib_gateway_set_link_available(corelib_gateway_context_t *context, corelib_link_id_t link_id, bool available);
/** @brief Processes one frame received on a gateway link. @param[in,out] context Initialised gateway. @param[in] link_id Receiving link. @param[in] frame Borrowed frame. @param[in] monotonic_ms Non-decreasing time. @return Processing status. */
corelib_status_t corelib_gateway_receive_frame(corelib_gateway_context_t *context, corelib_link_id_t link_id, const uint8_t frame[CORELIB_FRAME_SIZE], uint64_t monotonic_ms);
/** @brief Accepts a profile-delivered assignment for the local gateway. @param[in,out] context Initialised gateway. @param[in] assignment Assignment. @param[in] monotonic_ms Non-decreasing time. @return Validation status. */
corelib_status_t corelib_gateway_accept_bootstrap_assignment(corelib_gateway_context_t *context, const corelib_bootstrap_assignment_t *assignment, uint64_t monotonic_ms);
/** @brief Reports one profile-discovered candidate. @param[in,out] context Initialised gateway. @param[in] candidate Candidate description. @return Operation status. */
corelib_status_t corelib_gateway_report_candidate(corelib_gateway_context_t *context, const corelib_candidate_t *candidate);
/** @brief Completes profile discovery. @param[in,out] context Initialised gateway. @param[in] link_id Discovery link. @param[in] discovery_token Round token. @param[in] result Profile result. @return Correlation status. */
corelib_status_t corelib_gateway_complete_discovery(corelib_gateway_context_t *context, corelib_link_id_t link_id, const uint8_t discovery_token[16], corelib_status_t result);
/** @brief Completes one downstream assignment. @param[in,out] context Initialised gateway. @param[in] transaction_id Assignment transaction. @param[in] node_uuid Candidate UUID. @param[in] result PFP control result. @return Commit or rollback status. */
corelib_status_t corelib_gateway_complete_assignment(corelib_gateway_context_t *context, uint32_t transaction_id, const uint8_t node_uuid[16], corelib_control_status_t result);
/** @brief Reports loss of a downstream node and its descendants. @param[in,out] context Initialised gateway. @param[in] node_uuid Lost UUID. @return Operation status. */
corelib_status_t corelib_gateway_report_node_lost(corelib_gateway_context_t *context, const uint8_t node_uuid[16]);
/** @brief Responds to a local application request. @param[in,out] context Initialised gateway. @param[in] request Request identity. @param[in] result Application result. @param[in] data Optional payload. @param[in] data_size Payload bytes. @return Operation status. */
corelib_status_t corelib_gateway_respond(corelib_gateway_context_t *context, const corelib_transaction_id_t *request, corelib_transaction_result_t result, const uint8_t *data, size_t data_size);
/** @brief Publishes a local Common or Share payload. @param[in,out] context Initialised gateway. @param[in] common Payload kind. @param[in] share_id Schema identifier. @param[in] data Encoded payload. @param[in] data_size Payload bytes. @return Operation status. */
corelib_status_t corelib_gateway_publish(corelib_gateway_context_t *context, bool common, uint32_t share_id, const uint8_t *data, size_t data_size);
/** @brief Reads current gateway resource usage. @param[in] context Initialised gateway. @param[out] usage Usage snapshot. @return Operation status. */
corelib_status_t corelib_gateway_usage(const corelib_gateway_context_t *context, corelib_gateway_usage_t *usage);
/** @brief Reads fixed gateway limits. @param[in] context Initialised gateway. @param[out] limits Configured limits. @return Operation status. */
corelib_status_t corelib_gateway_limits(const corelib_gateway_context_t *context, corelib_gateway_limits_t *limits);

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file corelib_internal.h
 * @brief Private state and codec contracts shared by owned Corelib implementation units.
 */
#ifndef CORELIB_INTERNAL_H
#define CORELIB_INTERNAL_H

#include "corelib/device.h"

#define CORELIB_PFP_DATA 1u
#define CORELIB_PFP_PROBE_REQUEST 2u
#define CORELIB_PFP_PROBE_RESPONSE 3u
#define CORELIB_PFP_CONTROL 4u
#define CORELIB_ROOT_ADDRESS 1u
#define CORELIB_DIRECT_NODE_ADDRESS 2u
#define CORELIB_DEFAULT_PRIORITY 3u
#define CORELIB_REASSEMBLY_TIMEOUT_MS 1000u
#define CORELIB_MAX_REASSEMBLY_SLOTS 8u
#define CORELIB_MAX_OUTBOUND_FRAMES 255u
#define CORELIB_PENDING_MAGIC 0x50524d51u

/** @brief Decoded in-memory representation of one PFP frame. */
typedef struct {
  uint8_t type;
  uint16_t destination;
  uint16_t source;
  uint32_t session_id;
  uint32_t message_id;
  uint8_t frame_index;
  uint8_t frame_count;
  uint16_t message_length;
  uint8_t hop_limit;
  uint8_t priority;
  uint8_t payload[40];
} corelib_pfp_frame_t;

/** @brief Metadata retained for one fragmented inbound message. */
typedef struct {
  bool used;
  uint32_t session_id;
  uint32_t message_id;
  uint16_t source;
  uint16_t destination;
  uint16_t message_length;
  uint8_t type;
  uint8_t frame_count;
  uint8_t hop_limit;
  uint8_t priority;
  uint64_t started_ms;
} corelib_reassembly_entry_t;

/** @brief Correlation state for one request awaiting an application response. */
typedef struct {
  uint32_t magic;
  uint32_t token;
  uint32_t share_id;
  uint64_t deadline_ms;
  uint8_t request_action;
  bool used;
} corelib_pending_request_t;

/** @brief Generic Transaction Protocol envelope used by the codec. */
typedef struct {
  uint32_t token;
  uint32_t share_id;
  uint8_t action;
  uint8_t result;
  const uint8_t *data;
  size_t data_size;
} corelib_transaction_message_t;

/** @brief Private endpoint context stored in caller-owned memory. */
struct corelib_context {
  uint32_t signature;
  corelib_config_t config;
  corelib_link_id_t link_id;
  void *transport_context;
  bool link_active;
  bool in_call;
  uint64_t now_ms;
  uint32_t session_id;
  uint32_t next_message_id;
  uint32_t next_publish_token;
  uint64_t next_heartbeat_ms;
  uint16_t local_address;
  corelib_session_state_t session_state;
  corelib_reassembly_entry_t reassembly[CORELIB_MAX_REASSEMBLY_SLOTS];
  size_t outbound_head;
  size_t outbound_count;
};

/** @brief Computes the protocol CRC-32. @param[in] data Input bytes. @param[in] size Input size. @return CRC-32 value. */
uint32_t corelib_crc32(const uint8_t *data, size_t size);
/** @brief Decodes and validates one PFP frame. @param[in] bytes Encoded frame. @param[out] frame Decoded value. @return Validation status. */
corelib_status_t corelib_pfp_decode(const uint8_t bytes[64], corelib_pfp_frame_t *frame);
/** @brief Encodes one validated PFP frame. @param[in] frame Frame value. @param[out] bytes Encoded frame. @return Validation status. */
corelib_status_t corelib_pfp_encode(const corelib_pfp_frame_t *frame, uint8_t bytes[64]);
/** @brief Decodes a Transaction Protocol envelope. @param[in] bytes Encoded bytes. @param[in] size Encoded size. @param[in] max_data Payload limit. @param[out] message Decoded envelope. @return Validation status. */
corelib_status_t corelib_transaction_decode(const uint8_t *bytes, size_t size, size_t max_data, corelib_transaction_message_t *message);
/** @brief Encodes a Transaction Protocol envelope. @param[in] message Envelope. @param[out] bytes Output storage. @param[in] capacity Output capacity. @param[out] size Encoded size. @return Encoding status. */
corelib_status_t corelib_transaction_encode(const corelib_transaction_message_t *message, uint8_t *bytes, size_t capacity, size_t *size);
/** @brief Applies a complete standard endpoint control message. @param[in,out] context Endpoint context. @param[in] bytes Control bytes. @param[in] size Control size. @param[in] frame_session Frame session. @return Processing status. */
corelib_status_t corelib_process_control_message(corelib_context_t *context, const uint8_t *bytes, size_t size, uint32_t frame_session);
/** @brief Validates protocol UTF-8 bytes. @param[in] bytes Text bytes. @param[in] size Text size. @return True when valid UTF-8. */
bool corelib_control_valid_utf8(const uint8_t *bytes, size_t size);

#endif

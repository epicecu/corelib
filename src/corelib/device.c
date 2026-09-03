/**
 * @file device.c
 * @brief Device endpoint state machine, session lifecycle, and transaction service.
 */
#include "internal/corelib_internal.h"

#include <stdalign.h>
#include <string.h>

#define SDK_SIGNATURE 0x5044534bu
#define CONTROL_SESSION_START 1u
#define CONTROL_SESSION_STATUS 2u
#define CONTROL_HEARTBEAT 8u
#define CONTROL_SESSION_END 11u
#define TLV_NODE_UUID 1u
#define TLV_NODE_ADDRESS 2u
#define TLV_CAPABILITIES 6u
#define TLV_STATUS 7u
#define TLV_HEARTBEAT_INTERVAL 9u

_Static_assert(sizeof(corelib_context_t) <= CORELIB_CONTEXT_STORAGE_SIZE,
               "increase CORELIB_CONTEXT_STORAGE_SIZE");
_Static_assert(sizeof(corelib_pending_request_t) <=
                   CORELIB_PENDING_REQUEST_STORAGE_SIZE,
               "increase CORELIB_PENDING_REQUEST_STORAGE_SIZE");

/**
 * @brief Begin call.
 * @param[in,out] context Corelib context used by the operation.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool begin_call(corelib_context_t *context) {
  if (context == NULL || context->signature != SDK_SIGNATURE || context->in_call) {
    return false;
  }
  context->in_call = true;
  return true;
}

/**
 * @brief End call.
 * @param[in,out] context Corelib context used by the operation.
 */
static void end_call(corelib_context_t *context) {
  context->in_call = false;
}

/**
 * @brief Diagnostic.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] code Value supplied through `code`.
 * @param[in] status Value supplied through `status`.
 */
static void diagnostic(corelib_context_t *context, corelib_diagnostic_t code, corelib_status_t status) {
  if (context->config.callbacks.diagnostic != NULL) {
    context->config.callbacks.diagnostic(context->config.callbacks.user, code, status);
  }
}

/**
 * @brief Address a byte within caller-owned storage.
 * @param[in,out] base Start of the validated storage region.
 * @param[in] offset Bounded byte offset within the region.
 * @return Address of the selected byte.
 */
static uint8_t *device_byte_at(uint8_t *base, size_t offset) {
  return &base[offset];
}

/**
 * @brief Test whether caller-owned storage meets an alignment requirement.
 * @param[in] memory Storage address to test.
 * @param[in] alignment Required power-of-two alignment.
 * @return True when the address is suitably aligned; otherwise false.
 */
static bool device_pointer_is_aligned(const void *memory, size_t alignment) {
  return ((uintptr_t)memory % (uintptr_t)alignment) == (uintptr_t)0u;
}

/**
 * @brief Return the alignment required by a pending-request entry.
 * @return Required alignment in bytes.
 */
static size_t pending_request_alignment(void) {
  return alignof(corelib_pending_request_t);
}

/**
 * @brief Pending at.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] index Bounded storage index.
 * @return Matching internal entry, or null when none is available.
 */
static corelib_pending_request_t *pending_at(corelib_context_t *context, size_t index) {
  uint8_t *base = (uint8_t *)context->config.storage.pending_requests.entries;
  return (corelib_pending_request_t *)(void *)&base[index * context->config.storage.pending_requests.entry_size];
}

/**
 * @brief Read-only pending request at a bounded storage index.
 * @param[in] context Corelib context used by the operation.
 * @param[in] index Bounded storage index.
 * @return Matching internal entry.
 */
static const corelib_pending_request_t *pending_at_const(const corelib_context_t *context, size_t index) {
  const uint8_t *base = (const uint8_t *)context->config.storage.pending_requests.entries;
  return (const corelib_pending_request_t *)(const void *)&base[index * context->config.storage.pending_requests.entry_size];
}

/**
 * @brief Clear protocol state.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] notify Value supplied through `notify`.
 */
static void clear_protocol_state(corelib_context_t *context, bool notify) {
  size_t index;
  context->session_id = 0u;
  context->local_address = 0u;
  context->session_state = CORELIB_SESSION_INACTIVE;
  context->outbound_head = 0u;
  context->outbound_count = 0u;
  (void)memset(context->reassembly, 0, sizeof(context->reassembly));
  for (index = 0u; index < context->config.storage.pending_requests.capacity; ++index) {
    (void)memset(pending_at(context, index), 0, sizeof(corelib_pending_request_t));
  }
  if (notify && context->config.callbacks.session_changed != NULL) {
    context->config.callbacks.session_changed(context->config.callbacks.user, CORELIB_SESSION_INACTIVE, 0u, 0u);
  }
  if (notify && context->config.callbacks.node_changed != NULL) {
    context->config.callbacks.node_changed(context->config.callbacks.user, context->config.node_uuid, false, 0u);
  }
}

/**
 * @brief Flush frames.
 * @param[in,out] context Corelib context used by the operation.
 * @return Operation status.
 */
static corelib_status_t flush_frames(corelib_context_t *context) {
  while (context->outbound_count != 0u) {
    uint8_t *frame = &context->config.storage.outbound.frames[context->outbound_head * CORELIB_FRAME_SIZE];
    const corelib_send_result_t result = context->config.callbacks.send_frame(
        context->config.callbacks.user, context->link_id,
        context->transport_context, frame);
    if (result == CORELIB_SEND_BUSY) {
      return CORELIB_BUSY;
    }
    context->outbound_head = (context->outbound_head + 1u) %
                             context->config.storage.outbound.capacity;
    --context->outbound_count;
    if (result == CORELIB_SEND_FAILED) {
      diagnostic(context, CORELIB_DIAGNOSTIC_SEND_FAILED, CORELIB_INVALID_STATE);
      return CORELIB_INVALID_STATE;
    }
  }
  return CORELIB_OK;
}

/**
 * @brief Queue frame.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] frame PFP frame used by the operation.
 * @return Operation status.
 */
static corelib_status_t queue_frame(corelib_context_t *context, const corelib_pfp_frame_t *frame) {
  size_t slot;
  corelib_status_t status;
  if (context->outbound_count >= context->config.storage.outbound.capacity) {
    diagnostic(context, CORELIB_DIAGNOSTIC_RESOURCE_LIMIT, CORELIB_CAPACITY_EXCEEDED);
    return CORELIB_CAPACITY_EXCEEDED;
  }
  slot = (context->outbound_head + context->outbound_count) %
         context->config.storage.outbound.capacity;
  status = corelib_pfp_encode(frame, &context->config.storage.outbound.frames[slot * CORELIB_FRAME_SIZE]);
  if (status != CORELIB_OK) {
    return status;
  }
  ++context->outbound_count;
  status = flush_frames(context);
  return status == CORELIB_BUSY ? CORELIB_OK : status;
}

/**
 * @brief Next message id.
 * @param[in,out] context Corelib context used by the operation.
 * @return Computed internal value.
 */
static uint32_t next_message_id(corelib_context_t *context) {
  ++context->next_message_id;
  if (context->next_message_id == 0u) {
    context->next_message_id = 1u;
  }
  return context->next_message_id;
}

/**
 * @brief Send message.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] type Value supplied through `type`.
 * @param[in] message Message used by the operation.
 * @param[in] message_size Complete message size in bytes.
 * @return Operation status.
 */
static corelib_status_t send_message(corelib_context_t *context, uint8_t type, const uint8_t *message, size_t message_size) {
  corelib_pfp_frame_t frame;
  const size_t frame_count = (message_size + 39u) / 40u;
  size_t frame_index;
  uint32_t message_id;
  if (!context->link_active || context->session_state == CORELIB_SESSION_INACTIVE ||
      message == NULL || message_size == 0u ||
      message_size > context->config.storage.maximum_message_size || frame_count > 255u) {
    return CORELIB_INVALID_STATE;
  }
  if (context->config.storage.outbound.capacity - context->outbound_count < frame_count) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  message_id = next_message_id(context);
  (void)memset(&frame, 0, sizeof(frame));
  frame.type = type;
  frame.destination = CORELIB_ROOT_ADDRESS;
  frame.source = context->local_address;
  frame.session_id = context->session_id;
  frame.message_id = message_id;
  frame.frame_count = (uint8_t)frame_count;
  frame.message_length = (uint16_t)message_size;
  frame.hop_limit = 1u;
  frame.priority = CORELIB_DEFAULT_PRIORITY;
  for (frame_index = 0u; frame_index < frame_count; ++frame_index) {
    const size_t offset = frame_index * 40u;
    const size_t remaining = message_size - offset;
    const size_t chunk = remaining < 40u ? remaining : 40u;
    corelib_status_t status;
    frame.frame_index = (uint8_t)(frame_index + 1u);
    (void)memset(frame.payload, 0, sizeof(frame.payload));
    (void)memcpy(frame.payload, &message[offset], chunk);
    status = queue_frame(context, &frame);
    if (status != CORELIB_OK) {
      return status;
    }
  }
  return CORELIB_OK;
}

/**
 * @brief Send probe response.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] message_id Value supplied through `message_id`.
 * @return Operation status.
 */
static corelib_status_t send_probe_response(corelib_context_t *context, uint32_t message_id) {
  corelib_pfp_frame_t response;
  (void)memset(&response, 0, sizeof(response));
  response.type = CORELIB_PFP_PROBE_RESPONSE;
  response.destination = CORELIB_ROOT_ADDRESS;
  response.source = CORELIB_DIRECT_NODE_ADDRESS;
  response.message_id = message_id;
  response.frame_index = 1u;
  response.frame_count = 1u;
  response.hop_limit = 1u;
  return queue_frame(context, &response);
}

/**
 * @brief Control tlv.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] size Number of valid bytes.
 * @param[in] wanted Value supplied through `wanted`.
 * @param[in] value Value used by the operation.
 * @param[in,out] value_size Value supplied through `value_size`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool control_tlv(const uint8_t *bytes, size_t size, uint8_t wanted, const uint8_t **value, size_t *value_size) {
  size_t offset = 8u;
  uint8_t previous = 0u;
  while (offset < size) {
    uint8_t type;
    size_t length;
    if (size - offset < 2u) {
      return false;
    }
    type = (uint8_t)(bytes[offset] & 0x7fu);
    length = bytes[offset + 1u];
    offset += 2u;
    if (type == 0u || type <= previous || length == 0u || length > size - offset) {
      return false;
    }
    previous = type;
    if (type == wanted) {
      *value = &bytes[offset];
      *value_size = length;
      return true;
    }
    offset += length;
  }
  return false;
}

bool corelib_control_valid_utf8(const uint8_t *bytes, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    uint32_t codepoint;
    size_t continuation;
    uint8_t first = bytes[offset];
    ++offset;
    if (first < 0x80u) {
      continue;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
      codepoint = (uint32_t)(uint8_t)(first & 0x1fu);
      continuation = 1u;
    } else if (first >= 0xe0u && first <= 0xefu) {
      codepoint = (uint32_t)(uint8_t)(first & 0x0fu);
      continuation = 2u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
      codepoint = (uint32_t)(uint8_t)(first & 0x07u);
      continuation = 3u;
    } else {
      return false;
    }
    if (continuation > size - offset) {
      return false;
    }
    while (continuation != 0u) {
      const uint8_t next = bytes[offset];
      --continuation;
      ++offset;
      if ((next & 0xc0u) != 0x80u) {
        return false;
      }
      codepoint = (codepoint << 6u) | (uint32_t)(uint8_t)(next & 0x3fu);
    }
    if ((codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
        codepoint > 0x10ffffu ||
        (codepoint < 0x80u) ||
        (codepoint < 0x800u && first >= 0xe0u) ||
        (codepoint < 0x10000u && first >= 0xf0u)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Valid control tlvs.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] size Number of valid bytes.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool valid_control_tlvs(const uint8_t *bytes, size_t size) {
  size_t offset = 8u;
  uint8_t previous = 0u;
  while (offset < size) {
    const uint8_t encoded_type = bytes[offset];
    const uint8_t type = (uint8_t)(encoded_type & 0x7fu);
    size_t length;
    bool valid_length;
    if (size - offset < 2u) {
      return false;
    }
    length = bytes[offset + 1u];
    offset += 2u;
    if (type == 0u || type <= previous || length == 0u || length > size - offset) {
      return false;
    }
    valid_length = ((type == 1u || type == 11u) && length == 16u) ||
                   ((type >= 2u && type <= 4u) && length == 2u) ||
                   ((type == 5u || type == 6u || type == 9u || type == 10u) && length == 4u) ||
                   (type == 7u && length == 2u) ||
                   (type == 8u && length <= 16u);
    if ((!valid_length && (encoded_type & 0x80u) != 0u) ||
        (type == 8u && !corelib_control_valid_utf8(&bytes[offset], length))) {
      return false;
    }
    previous = type;
    offset += length;
  }
  return offset == size;
}

/**
 * @brief Put u16.
 * @param[in,out] bytes Encoded byte buffer used by the operation.
 * @param[in] value Value used by the operation.
 */
static void put_u16(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

/**
 * @brief Put u32.
 * @param[in,out] bytes Encoded byte buffer used by the operation.
 * @param[in] value Value used by the operation.
 */
static void put_u32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

/**
 * @brief Get u32.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @return Computed internal value.
 */
static uint32_t get_u32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/**
 * @brief Exact control fields.
 * @param[in] bytes Encoded byte buffer used by the operation.
 * @param[in] size Number of valid bytes.
 * @param[in] allowed Value supplied through `allowed`.
 * @param[in] required_critical Value supplied through `required_critical`.
 * @param[in] optional_noncritical Value supplied through `optional_noncritical`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool exact_control_fields(const uint8_t *bytes, size_t size, uint16_t allowed, uint16_t required_critical, uint16_t optional_noncritical) {
  size_t offset = 8u;
  uint16_t seen = 0u;
  while (offset < size) {
    const uint8_t encoded = bytes[offset];
    const uint8_t type = (uint8_t)(encoded & 0x7fu);
    const bool critical = (encoded & 0x80u) != 0u;
    const size_t length = bytes[offset + 1u];
    const uint16_t bit = type < 16u ? (uint16_t)((uint16_t)1u << type) : 0u;
    if (bit == 0u || (allowed & bit) == 0u ||
        ((required_critical & bit) != 0u && !critical) ||
        ((optional_noncritical & bit) != 0u && critical)) {
      return false;
    }
    seen |= bit;
    offset += 2u + length;
  }
  return (seen & required_critical) == required_critical;
}

corelib_status_t corelib_process_control_message(corelib_context_t *context, const uint8_t *bytes, size_t size, uint32_t frame_session) {
  uint8_t opcode;
  uint32_t transaction_id;
  const uint8_t *value;
  size_t value_size;
  if (size < 8u || bytes[0] != 1u || bytes[2] != 0u || bytes[3] != 0u) {
    return CORELIB_INVALID_FRAME;
  }
  if (!valid_control_tlvs(bytes, size)) {
    return CORELIB_INVALID_FRAME;
  }
  opcode = bytes[1];
  transaction_id = get_u32(&bytes[4]);
  if (opcode == CONTROL_SESSION_START) {
    uint8_t response[64];
    size_t response_size = 8u;
    uint32_t heartbeat;
    if (frame_session == 0u || transaction_id == 0u ||
        !exact_control_fields(bytes, size, (uint16_t)((uint16_t)1u << TLV_HEARTBEAT_INTERVAL),
                              (uint16_t)((uint16_t)1u << TLV_HEARTBEAT_INTERVAL), 0u)) {
      return CORELIB_INVALID_FRAME;
    }
    if (context->session_state != CORELIB_SESSION_INACTIVE &&
        context->session_id != frame_session) {
      return CORELIB_INVALID_STATE;
    }
    if (!control_tlv(bytes, size, TLV_HEARTBEAT_INTERVAL, &value, &value_size) ||
        value_size != 4u) {
      return CORELIB_INVALID_FRAME;
    }
    heartbeat = get_u32(value);
    if (heartbeat < 100u || heartbeat > 60000u) {
      return CORELIB_INVALID_FRAME;
    }
    context->session_id = frame_session;
    context->local_address = CORELIB_DIRECT_NODE_ADDRESS;
    context->session_state = CORELIB_SESSION_ACTIVE;
    context->config.heartbeat_interval_ms = heartbeat;
    context->next_heartbeat_ms = context->now_ms + heartbeat;
    (void)memset(response, 0, sizeof(response));
    response[0] = 1u;
    response[1] = CONTROL_SESSION_STATUS;
    put_u32(&response[4], transaction_id);
    response[response_size] = (uint8_t)(0x80u | TLV_NODE_UUID);
    ++response_size;
    response[response_size] = 16u;
    ++response_size;
    (void)memcpy(&response[response_size], context->config.node_uuid, 16u);
    response_size += 16u;
    response[response_size] = (uint8_t)(0x80u | TLV_NODE_ADDRESS);
    ++response_size;
    response[response_size] = 2u;
    ++response_size;
    put_u16(&response[response_size], context->local_address);
    response_size += 2u;
    response[response_size] = (uint8_t)(0x80u | TLV_CAPABILITIES);
    ++response_size;
    response[response_size] = 4u;
    ++response_size;
    put_u32(&response[response_size], context->config.capabilities);
    response_size += 4u;
    response[response_size] = (uint8_t)(0x80u | TLV_STATUS);
    ++response_size;
    response[response_size] = 2u;
    ++response_size;
    put_u16(&response[response_size], 0u);
    response_size += 2u;
    if (context->config.callbacks.session_changed != NULL) {
      context->config.callbacks.session_changed(context->config.callbacks.user, context->session_state, context->session_id, context->local_address);
    }
    if (context->config.callbacks.node_changed != NULL) {
      context->config.callbacks.node_changed(context->config.callbacks.user, context->config.node_uuid, true, context->local_address);
    }
    return send_message(context, CORELIB_PFP_CONTROL, response, response_size);
  }
  if (frame_session != context->session_id ||
      context->session_state == CORELIB_SESSION_INACTIVE) {
    return CORELIB_INVALID_STATE;
  }
  if (opcode == CONTROL_SESSION_END) {
    if (transaction_id != 0u ||
        !exact_control_fields(bytes, size, (uint16_t)((uint16_t)1u << 8u), 0u,
                              (uint16_t)((uint16_t)1u << 8u))) {
      return CORELIB_INVALID_FRAME;
    }
    clear_protocol_state(context, true);
    return CORELIB_OK;
  }
  if (opcode == CONTROL_HEARTBEAT) {
    if (transaction_id != 0u || size != 8u) {
      return CORELIB_INVALID_FRAME;
    }
    return CORELIB_OK;
  }
  return CORELIB_UNSUPPORTED;
}

/**
 * @brief Find assembly.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] frame PFP frame used by the operation.
 * @param[in,out] slot_index Value supplied through `slot_index`.
 * @return Matching internal entry, or null when none is available.
 */
static corelib_reassembly_entry_t *find_assembly(corelib_context_t *context, const corelib_pfp_frame_t *frame, size_t *slot_index) {
  size_t index;
  corelib_reassembly_entry_t *free_entry = NULL;
  size_t free_index = 0u;
  const size_t count = context->config.storage.reassembly_slot_count;
  for (index = 0u; index < count; ++index) {
    corelib_reassembly_entry_t *entry = &context->reassembly[index];
    if (!entry->used && free_entry == NULL) {
      free_entry = entry;
      free_index = index;
    } else if (entry->used && entry->session_id == frame->session_id &&
               entry->message_id == frame->message_id && entry->source == frame->source &&
               entry->destination == frame->destination && entry->type == frame->type) {
      *slot_index = index;
      return entry;
    } else {
      /* Retain the first free slot while searching for an exact match. */
    }
  }
  if (free_entry != NULL) {
    *slot_index = free_index;
  }
  return free_entry;
}

/**
 * @brief Process complete.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] type Value supplied through `type`.
 * @param[in] message Message used by the operation.
 * @param[in] size Number of valid bytes.
 * @param[in] session_id Value supplied through `session_id`.
 * @return Operation status.
 */
static corelib_status_t process_complete(corelib_context_t *context, uint8_t type, const uint8_t *message, size_t size, uint32_t session_id) {
  if (type == CORELIB_PFP_CONTROL) {
    return corelib_process_control_message(context, message, size,
                                           session_id);
  }
  if (type == CORELIB_PFP_DATA) {
    corelib_transaction_message_t decoded;
    corelib_status_t status = corelib_transaction_decode(message, size,
                                                         context->config.maximum_transaction_data_size, &decoded);
    if (status != CORELIB_OK ||
        (decoded.action != 1u && decoded.action != 2u &&
         decoded.action != 4u && decoded.action != 5u)) {
      diagnostic(context, CORELIB_DIAGNOSTIC_INVALID_MESSAGE, CORELIB_INVALID_FRAME);
      return CORELIB_INVALID_FRAME;
    }
    if (decoded.action == 1u || decoded.action == 4u) {
      size_t index;
      corelib_pending_request_t *pending = NULL;
      for (index = 0u; index < context->config.storage.pending_requests.capacity; ++index) {
        corelib_pending_request_t *candidate = pending_at(context, index);
        if (candidate->magic != CORELIB_PENDING_MAGIC || !candidate->used) {
          pending = candidate;
          break;
        }
      }
      if (pending == NULL) {
        return CORELIB_CAPACITY_EXCEEDED;
      }
      (void)memset(pending, 0, sizeof(*pending));
      pending->magic = CORELIB_PENDING_MAGIC;
      pending->used = true;
      pending->token = decoded.token;
      pending->share_id = decoded.share_id;
      pending->request_action = decoded.action;
      pending->deadline_ms = context->now_ms + context->config.application_response_timeout_ms;
    }
    if (context->config.callbacks.transaction != NULL) {
      const corelib_transaction_t transaction = {
          {decoded.token, decoded.share_id, (corelib_action_t)decoded.action},
          decoded.data,
          decoded.data_size};
      context->config.callbacks.transaction(context->config.callbacks.user, &transaction);
    }
    return CORELIB_OK;
  }
  return CORELIB_UNSUPPORTED;
}

/**
 * @brief Accept fragment.
 * @param[in,out] context Corelib context used by the operation.
 * @param[in] frame PFP frame used by the operation.
 * @return Operation status.
 */
static corelib_status_t accept_fragment(corelib_context_t *context, const corelib_pfp_frame_t *frame) {
  size_t slot_index = 0u;
  corelib_reassembly_entry_t *entry = find_assembly(context, frame, &slot_index);
  uint8_t *message;
  uint8_t *received;
  size_t offset;
  size_t chunk;
  size_t index;
  if (entry == NULL) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  message = device_byte_at(context->config.storage.reassembly.message,
                           slot_index * context->config.storage.maximum_message_size);
  received = device_byte_at(context->config.storage.reassembly.received, slot_index * 255u);
  if (!entry->used) {
    (void)memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->session_id = frame->session_id;
    entry->message_id = frame->message_id;
    entry->source = frame->source;
    entry->destination = frame->destination;
    entry->message_length = frame->message_length;
    entry->type = frame->type;
    entry->frame_count = frame->frame_count;
    entry->hop_limit = frame->hop_limit;
    entry->priority = frame->priority;
    entry->started_ms = context->now_ms;
    (void)memset(received, 0, 255u);
  } else if (entry->message_length != frame->message_length ||
             entry->frame_count != frame->frame_count ||
             entry->hop_limit != frame->hop_limit || entry->priority != frame->priority) {
    entry->used = false;
    return CORELIB_INVALID_FRAME;
  } else {
    /* Continue receiving a fragment for the matching assembly. */
  }
  offset = ((size_t)frame->frame_index - 1u) * 40u;
  chunk = (size_t)entry->message_length - offset;
  if (chunk > 40u) {
    chunk = 40u;
  }
  if (received[frame->frame_index - 1u] != 0u) {
    if (memcmp(&message[offset], frame->payload, chunk) != 0) {
      entry->used = false;
      return CORELIB_INVALID_FRAME;
    }
    return CORELIB_OK;
  }
  (void)memcpy(&message[offset], frame->payload, chunk);
  received[frame->frame_index - 1u] = 1u;
  for (index = 0u; index < entry->frame_count; ++index) {
    if (received[index] == 0u) {
      return CORELIB_OK;
    }
  }
  {
    const uint8_t type = entry->type;
    const size_t message_size = entry->message_length;
    const uint32_t session_id = entry->session_id;
    entry->used = false;
    return process_complete(context, type, message, message_size, session_id);
  }
}

size_t corelib_context_size(void) {
  return sizeof(corelib_context_t);
}
size_t corelib_context_alignment(void) {
  return alignof(corelib_context_t);
}
size_t corelib_pending_request_entry_size(void) {
  return sizeof(corelib_pending_request_t);
}

corelib_status_t corelib_init(void *context_memory, size_t context_memory_size, const corelib_config_t *config, corelib_context_t **context) {
  const size_t pending_alignment = pending_request_alignment();
  corelib_context_t *created;
  if (context_memory == NULL || config == NULL || context == NULL ||
      context_memory_size < sizeof(corelib_context_t) ||
      !device_pointer_is_aligned(context_memory, corelib_context_alignment()) ||
      config->callbacks.send_frame == NULL || config->storage.reassembly.message == NULL ||
      config->storage.reassembly.received == NULL || config->storage.transaction_scratch == NULL ||
      config->storage.outbound.frames == NULL ||
      config->storage.pending_requests.entries == NULL ||
      config->storage.reassembly_slot_count == 0u ||
      config->storage.reassembly_slot_count > CORELIB_MAX_REASSEMBLY_SLOTS ||
      config->storage.maximum_message_size < CORELIB_MIN_TRANSACTION_DATA_SIZE ||
      config->storage.maximum_message_size > CORELIB_MAX_MESSAGE_SIZE ||
      config->storage.outbound.capacity == 0u ||
      config->storage.outbound.capacity > CORELIB_MAX_OUTBOUND_FRAMES ||
      config->storage.pending_requests.capacity == 0u ||
      config->storage.pending_requests.entry_size < sizeof(corelib_pending_request_t) ||
      !device_pointer_is_aligned(config->storage.pending_requests.entries, pending_alignment) ||
      (config->storage.pending_requests.entry_size % pending_alignment) != 0u ||
      config->maximum_transaction_data_size < CORELIB_MIN_TRANSACTION_DATA_SIZE ||
      config->maximum_transaction_data_size > config->storage.maximum_message_size - 19u ||
      config->heartbeat_interval_ms < 100u || config->heartbeat_interval_ms > 60000u ||
      config->application_response_timeout_ms == 0u) {
    return CORELIB_INVALID_ARGUMENT;
  }
  if ((config->node_uuid[6] & 0xf0u) != 0x40u ||
      (config->node_uuid[8] & 0xc0u) != 0x80u) {
    return CORELIB_INVALID_ARGUMENT;
  }
  if ((config->capabilities & CORELIB_CAPABILITY_GATEWAY) != 0u) {
    return CORELIB_UNSUPPORTED;
  }
  created = (corelib_context_t *)context_memory;
  (void)memset(created, 0, sizeof(*created));
  created->config = *config;
  created->signature = SDK_SIGNATURE;
  created->next_message_id = 0u;
  created->next_publish_token = 0u;
  clear_protocol_state(created, false);
  *context = created;
  return CORELIB_OK;
}

corelib_status_t corelib_reset(corelib_context_t *context) {
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  clear_protocol_state(context, true);
  end_call(context);
  return CORELIB_OK;
}

corelib_status_t corelib_tick(corelib_context_t *context, uint64_t monotonic_ms) {
  size_t index;
  corelib_status_t result = CORELIB_OK;
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (monotonic_ms < context->now_ms) {
    diagnostic(context, CORELIB_DIAGNOSTIC_TIME_REVERSED, CORELIB_INVALID_ARGUMENT);
    end_call(context);
    return CORELIB_INVALID_ARGUMENT;
  }
  context->now_ms = monotonic_ms;
  for (index = 0u; index < context->config.storage.reassembly_slot_count; ++index) {
    if (context->reassembly[index].used &&
        monotonic_ms - context->reassembly[index].started_ms >= CORELIB_REASSEMBLY_TIMEOUT_MS) {
      context->reassembly[index].used = false;
    }
  }
  for (index = 0u; index < context->config.storage.pending_requests.capacity; ++index) {
    corelib_pending_request_t *pending = pending_at(context, index);
    if (pending->magic == CORELIB_PENDING_MAGIC && pending->used &&
        monotonic_ms >= pending->deadline_ms) {
      pending->used = false;
      diagnostic(context, CORELIB_DIAGNOSTIC_REQUEST_EXPIRED, CORELIB_EXPIRED);
    }
  }
  if (context->session_state == CORELIB_SESSION_ACTIVE &&
      monotonic_ms >= context->next_heartbeat_ms) {
    uint8_t heartbeat[8] = {1u, CONTROL_HEARTBEAT, 0u, 0u, 0u, 0u, 0u, 0u};
    result = send_message(context, CORELIB_PFP_CONTROL, heartbeat,
                          sizeof(heartbeat));
    context->next_heartbeat_ms = monotonic_ms +
                                 context->config.heartbeat_interval_ms;
    end_call(context);
    return result;
  }
  result = flush_frames(context);
  end_call(context);
  return result;
}

corelib_status_t corelib_add_link(corelib_context_t *context, corelib_link_id_t link_id, void *transport_context) {
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (link_id == 0u || context->link_active) {
    end_call(context);
    return CORELIB_INVALID_STATE;
  }
  context->link_id = link_id;
  context->transport_context = transport_context;
  context->link_active = true;
  end_call(context);
  return CORELIB_OK;
}

corelib_status_t corelib_remove_link(corelib_context_t *context, corelib_link_id_t link_id) {
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (!context->link_active || context->link_id != link_id) {
    end_call(context);
    return CORELIB_NOT_FOUND;
  }
  context->link_active = false;
  context->transport_context = NULL;
  clear_protocol_state(context, true);
  end_call(context);
  return CORELIB_OK;
}

corelib_status_t corelib_receive_frame(corelib_context_t *context, corelib_link_id_t link_id, const uint8_t frame_bytes[CORELIB_FRAME_SIZE], uint64_t monotonic_ms) {
  corelib_pfp_frame_t frame;
  corelib_status_t status;
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (frame_bytes == NULL || !context->link_active || context->link_id != link_id ||
      monotonic_ms < context->now_ms) {
    end_call(context);
    return CORELIB_INVALID_ARGUMENT;
  }
  context->now_ms = monotonic_ms;
  status = corelib_pfp_decode(frame_bytes, &frame);
  if (status != CORELIB_OK) {
    diagnostic(context, CORELIB_DIAGNOSTIC_INVALID_FRAME, status);
    end_call(context);
    return status;
  }
  if (frame.type == CORELIB_PFP_PROBE_REQUEST && frame.destination == 0u &&
      frame.source == CORELIB_ROOT_ADDRESS) {
    status = send_probe_response(context, frame.message_id);
  } else if (frame.destination != context->local_address &&
             !(context->session_state == CORELIB_SESSION_INACTIVE &&
               (frame.destination == 0u ||
                frame.destination == CORELIB_DIRECT_NODE_ADDRESS) &&
               frame.type == CORELIB_PFP_CONTROL)) {
    status = CORELIB_INVALID_FRAME;
  } else if (context->session_state == CORELIB_SESSION_INACTIVE &&
             frame.type == CORELIB_PFP_CONTROL &&
             frame.source != CORELIB_ROOT_ADDRESS) {
    status = CORELIB_INVALID_FRAME;
  } else if (frame.type != CORELIB_PFP_CONTROL &&
             frame.session_id != context->session_id) {
    status = CORELIB_INVALID_STATE;
  } else {
    status = accept_fragment(context, &frame);
  }
  if (status != CORELIB_OK) {
    diagnostic(context, CORELIB_DIAGNOSTIC_INVALID_FRAME, status);
  }
  end_call(context);
  return status;
}

corelib_status_t corelib_respond(corelib_context_t *context, const corelib_transaction_id_t *request, corelib_transaction_result_t result, const uint8_t *data, size_t data_size) {
  size_t index;
  corelib_pending_request_t *pending = NULL;
  corelib_transaction_message_t message;
  uint8_t *encoded;
  size_t encoded_size = 0u;
  corelib_status_t status;
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (request == NULL || result < CORELIB_RESULT_SUCCESS ||
      result > CORELIB_RESULT_INTERNAL_ERROR ||
      (result == CORELIB_RESULT_SUCCESS && data_size != 0u && data == NULL) ||
      (result != CORELIB_RESULT_SUCCESS && data_size != 0u) ||
      data_size > context->config.maximum_transaction_data_size) {
    end_call(context);
    return CORELIB_INVALID_ARGUMENT;
  }
  for (index = 0u; index < context->config.storage.pending_requests.capacity; ++index) {
    corelib_pending_request_t *candidate = pending_at(context, index);
    if (candidate->magic == CORELIB_PENDING_MAGIC && candidate->used &&
        candidate->token == request->token && candidate->share_id == request->share_id &&
        candidate->request_action == (uint8_t)request->action) {
      pending = candidate;
      break;
    }
  }
  if (pending == NULL) {
    end_call(context);
    return CORELIB_NOT_FOUND;
  }
  encoded = context->config.storage.transaction_scratch;
  (void)memset(&message, 0, sizeof(message));
  message.token = request->token;
  message.share_id = request->share_id;
  message.action = request->action == CORELIB_ACTION_COMMON_REQUEST ? 3u : 6u;
  message.result = (uint8_t)result;
  message.data = data;
  message.data_size = data_size;
  status = corelib_transaction_encode(&message, encoded,
                                      context->config.storage.maximum_message_size, &encoded_size);
  if (status == CORELIB_OK) {
    status = send_message(context, CORELIB_PFP_DATA, encoded, encoded_size);
  }
  if (status == CORELIB_OK) {
    pending->used = false;
  }
  end_call(context);
  return status;
}

corelib_status_t corelib_publish(corelib_context_t *context, bool common, uint32_t share_id, const uint8_t *data, size_t data_size) {
  corelib_transaction_message_t message;
  uint8_t *encoded;
  size_t encoded_size = 0u;
  corelib_status_t status;
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (share_id == 0u || (data_size != 0u && data == NULL) ||
      data_size > context->config.maximum_transaction_data_size) {
    end_call(context);
    return CORELIB_INVALID_ARGUMENT;
  }
  ++context->next_publish_token;
  if (context->next_publish_token == 0u) {
    context->next_publish_token = 1u;
  }
  (void)memset(&message, 0, sizeof(message));
  message.token = context->next_publish_token;
  message.share_id = share_id;
  message.action = common ? 2u : 5u;
  message.data = data;
  message.data_size = data_size;
  encoded = context->config.storage.transaction_scratch;
  status = corelib_transaction_encode(&message, encoded,
                                      context->config.storage.maximum_message_size, &encoded_size);
  if (status == CORELIB_OK) {
    status = send_message(context, CORELIB_PFP_DATA, encoded, encoded_size);
  }
  end_call(context);
  return status;
}

corelib_version_t corelib_version(void) {
  const corelib_version_t version = {
      CORELIB_VERSION_MAJOR, CORELIB_VERSION_MINOR,
      CORELIB_VERSION_PATCH, CORELIB_PFP_VERSION,
      CORELIB_TRANSACTION_VERSION};
  return version;
}

corelib_status_t corelib_usage(const corelib_context_t *context, corelib_usage_t *usage) {
  size_t index;
  if (context == NULL || context->signature != SDK_SIGNATURE || usage == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  (void)memset(usage, 0, sizeof(*usage));
  usage->queued_frames = (uint16_t)context->outbound_count;
  for (index = 0u; index < context->config.storage.reassembly_slot_count; ++index) {
    if (context->reassembly[index].used) {
      ++usage->active_reassemblies;
      usage->reassembly_bytes += context->reassembly[index].message_length;
    }
  }
  for (index = 0u; index < context->config.storage.pending_requests.capacity; ++index) {
    const corelib_pending_request_t *pending = pending_at_const(context, index);
    if (pending->magic == CORELIB_PENDING_MAGIC && pending->used) {
      ++usage->pending_requests;
    }
  }
  return CORELIB_OK;
}

corelib_status_t corelib_limits(const corelib_context_t *context, corelib_limits_t *limits) {
  if (context == NULL || context->signature != SDK_SIGNATURE || limits == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  limits->maximum_message_size = context->config.storage.maximum_message_size;
  limits->maximum_transaction_data_size = context->config.maximum_transaction_data_size;
  limits->reassembly_slots = context->config.storage.reassembly_slot_count;
  limits->outbound_frames = context->config.storage.outbound.capacity;
  limits->pending_requests = context->config.storage.pending_requests.capacity;
  return CORELIB_OK;
}

corelib_status_t corelib_accept_bootstrap_assignment(corelib_context_t *context, const corelib_bootstrap_assignment_t *assignment, uint64_t monotonic_ms) {
  if (!begin_call(context)) {
    return CORELIB_REENTRANT;
  }
  if (assignment == NULL || monotonic_ms < context->now_ms ||
      assignment->session_id == 0u || assignment->transaction_id == 0u ||
      assignment->node_address < CORELIB_DIRECT_NODE_ADDRESS ||
      assignment->parent_address == 0u ||
      assignment->heartbeat_interval_ms < 100u ||
      assignment->heartbeat_interval_ms > 60000u ||
      memcmp(assignment->node_uuid, context->config.node_uuid, 16u) != 0 ||
      !context->link_active || context->session_state != CORELIB_SESSION_INACTIVE) {
    end_call(context);
    return CORELIB_INVALID_ARGUMENT;
  }
  context->now_ms = monotonic_ms;
  context->session_id = assignment->session_id;
  context->local_address = assignment->node_address;
  context->session_state = CORELIB_SESSION_ACTIVE;
  context->config.heartbeat_interval_ms = assignment->heartbeat_interval_ms;
  context->next_heartbeat_ms = monotonic_ms + assignment->heartbeat_interval_ms;
  if (context->config.callbacks.session_changed != NULL) {
    context->config.callbacks.session_changed(context->config.callbacks.user, context->session_state, context->session_id, context->local_address);
  }
  if (context->config.callbacks.node_changed != NULL) {
    context->config.callbacks.node_changed(context->config.callbacks.user, context->config.node_uuid, true, context->local_address);
  }
  end_call(context);
  return CORELIB_OK;
}

/**
 * @file transaction.c
 * @brief Transaction Protocol v2 envelope encoding and decoding.
 */
#include "internal/corelib_internal.h"

#include "protocol/v2/generated/transaction.pb.h"
#include "vendor/nanopb/pb_decode.h"
#include "vendor/nanopb/pb_encode.h"

#include <string.h>

typedef struct {
  const uint8_t *data;
  size_t size;
} bytes_view_t;

/**
 * @brief Decode bytes.
 * @param[in,out] stream Value supplied through `stream`.
 * @param[in] field Value supplied through `field`.
 * @param[in,out] argument Value supplied through `argument`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool decode_bytes(pb_istream_t *stream, const pb_field_t *field, void **argument) {
  bytes_view_t *view = (bytes_view_t *)*argument;
  (void)field;
  view->data = (const uint8_t *)stream->state;
  view->size = stream->bytes_left;
  return pb_read(stream, NULL, stream->bytes_left);
}

/**
 * @brief Encode bytes.
 * @param[in,out] stream Value supplied through `stream`.
 * @param[in] field Value supplied through `field`.
 * @param[in] argument Value supplied through `argument`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool encode_bytes(pb_ostream_t *stream, const pb_field_t *field, void *const *argument) {
  const bytes_view_t *view = (const bytes_view_t *)*argument;
  return pb_encode_tag_for_field(stream, field) &&
         pb_encode_string(stream, view->data, view->size);
}

/**
 * @brief Valid message.
 * @param[in] message Message used by the operation.
 * @param[in] max_data Value supplied through `max_data`.
 * @return True when the condition or operation succeeds; otherwise false.
 */
static bool valid_message(const corelib_transaction_message_t *message, size_t max_data) {
  const bool request = message->action == 1u || message->action == 4u;
  const bool publish = message->action == 2u || message->action == 5u;
  const bool response = message->action == 3u || message->action == 6u;
  if (message->token == 0u || message->share_id == 0u ||
      (!request && !publish && !response) || message->result > 5u ||
      message->data_size > max_data)
    return false;
  if ((request || publish) && message->result != 0u)
    return false;
  if (request && message->data_size != 0u)
    return false;
  if (response && message->result == 0u)
    return false;
  if (response && message->result != 1u && message->data_size != 0u)
    return false;
  return true;
}

corelib_status_t corelib_transaction_decode(const uint8_t *bytes, size_t size, size_t max_data, corelib_transaction_message_t *message) {
  epicecu_programmor_transaction_v2_TransactionMessage decoded =
      epicecu_programmor_transaction_v2_TransactionMessage_init_zero;
  bytes_view_t view = {NULL, 0u};
  pb_istream_t stream;
  if (bytes == NULL || message == NULL)
    return CORELIB_INVALID_ARGUMENT;
  decoded.data.funcs.decode = decode_bytes;
  decoded.data.arg = &view;
  stream = pb_istream_from_buffer(bytes, size);
  if (!pb_decode(&stream,
                 epicecu_programmor_transaction_v2_TransactionMessage_fields,
                 &decoded) ||
      stream.bytes_left != 0u ||
      decoded.protocol_version != CORELIB_TRANSACTION_VERSION) {
    return CORELIB_INVALID_FRAME;
  }
  memset(message, 0, sizeof(*message));
  message->token = decoded.token;
  message->share_id = decoded.share_id;
  message->action = (uint8_t)decoded.action;
  message->result = (uint8_t)decoded.result;
  message->data = view.data;
  message->data_size = view.size;
  return valid_message(message, max_data) ? CORELIB_OK
                                          : CORELIB_INVALID_FRAME;
}

corelib_status_t corelib_transaction_encode(const corelib_transaction_message_t *message, uint8_t *bytes, size_t capacity, size_t *size) {
  epicecu_programmor_transaction_v2_TransactionMessage encoded =
      epicecu_programmor_transaction_v2_TransactionMessage_init_zero;
  bytes_view_t view;
  pb_ostream_t stream;
  if (message == NULL || bytes == NULL || size == NULL ||
      !valid_message(message, CORELIB_MAX_MESSAGE_SIZE)) {
    return CORELIB_INVALID_ARGUMENT;
  }
  encoded.protocol_version = CORELIB_TRANSACTION_VERSION;
  encoded.token = message->token;
  encoded.action =
      (epicecu_programmor_transaction_v2_TransactionMessage_Action)message->action;
  encoded.share_id = message->share_id;
  encoded.result =
      (epicecu_programmor_transaction_v2_TransactionMessage_Result)message->result;
  view.data = message->data;
  view.size = message->data_size;
  if (view.size != 0u) {
    encoded.data.funcs.encode = encode_bytes;
    encoded.data.arg = &view;
  }
  stream = pb_ostream_from_buffer(bytes, capacity);
  if (!pb_encode(&stream,
                 epicecu_programmor_transaction_v2_TransactionMessage_fields,
                 &encoded)) {
    return CORELIB_CAPACITY_EXCEEDED;
  }
  *size = stream.bytes_written;
  return CORELIB_OK;
}

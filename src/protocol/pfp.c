/**
 * @file pfp.c
 * @brief Encoding, decoding, and validation for 64-byte PFP frames.
 */
#include "internal/corelib_internal.h"

#include <string.h>

/**
 * @brief Read u16.
 * @param[in] data Byte buffer used by the operation.
 * @return Computed internal value.
 */
static uint16_t read_u16(const uint8_t *data) {
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/**
 * @brief Read u32.
 * @param[in] data Byte buffer used by the operation.
 * @return Computed internal value.
 */
static uint32_t read_u32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/**
 * @brief Write u16.
 * @param[in,out] data Byte buffer used by the operation.
 * @param[in] value Value used by the operation.
 */
static void write_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

/**
 * @brief Write u32.
 * @param[in,out] data Byte buffer used by the operation.
 * @param[in] value Value used by the operation.
 */
static void write_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

uint32_t corelib_crc32(const uint8_t *data, size_t size) {
  uint32_t crc = UINT32_MAX;
  size_t index;
  for (index = 0; index < size; ++index) {
    unsigned bit;
    crc ^= data[index];
    for (bit = 0; bit < 8u; ++bit) {
      const uint32_t mask = (crc & 1u) != 0u ? UINT32_MAX : 0u;
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ UINT32_MAX;
}

/**
 * @brief Validate.
 * @param[in] frame PFP frame used by the operation.
 * @return Operation status.
 */
static corelib_status_t validate(const corelib_pfp_frame_t *frame) {
  const bool probe = frame->type == CORELIB_PFP_PROBE_REQUEST ||
                     frame->type == CORELIB_PFP_PROBE_RESPONSE;
  size_t expected_count;
  size_t used;
  if (frame->type < CORELIB_PFP_DATA || frame->type > CORELIB_PFP_CONTROL ||
      frame->message_id == 0u || frame->hop_limit == 0u ||
      frame->hop_limit > 8u || frame->priority > 7u) {
    return CORELIB_INVALID_FRAME;
  }
  if (probe) {
    if (frame->session_id != 0u || frame->message_length != 0u ||
        frame->frame_count != 1u || frame->frame_index != 1u ||
        frame->hop_limit != 1u) {
      return CORELIB_INVALID_FRAME;
    }
    for (used = 0; used < sizeof(frame->payload); ++used) {
      if (frame->payload[used] != 0u) {
        return CORELIB_INVALID_FRAME;
      }
    }
    return CORELIB_OK;
  }
  if (frame->session_id == 0u || frame->message_length == 0u ||
      frame->message_length > CORELIB_MAX_MESSAGE_SIZE) {
    return CORELIB_INVALID_FRAME;
  }
  expected_count = ((size_t)frame->message_length + 39u) / 40u;
  if ((size_t)frame->frame_count != expected_count || frame->frame_index == 0u ||
      frame->frame_index > frame->frame_count) {
    return CORELIB_INVALID_FRAME;
  }
  if (frame->frame_index == frame->frame_count) {
    used = (size_t)frame->message_length - ((size_t)frame->frame_count - 1u) * 40u;
    while (used < sizeof(frame->payload)) {
      if (frame->payload[used++] != 0u) {
        return CORELIB_INVALID_FRAME;
      }
    }
  }
  return CORELIB_OK;
}

corelib_status_t corelib_pfp_decode(const uint8_t bytes[64], corelib_pfp_frame_t *frame) {
  if (bytes == NULL || frame == NULL || bytes[1] != CORELIB_PFP_VERSION ||
      read_u32(&bytes[60]) != corelib_crc32(bytes, 60u)) {
    return CORELIB_INVALID_FRAME;
  }
  frame->type = bytes[0];
  frame->destination = read_u16(&bytes[2]);
  frame->source = read_u16(&bytes[4]);
  frame->session_id = read_u32(&bytes[6]);
  frame->message_id = read_u32(&bytes[10]);
  frame->frame_index = bytes[14];
  frame->frame_count = bytes[15];
  frame->message_length = read_u16(&bytes[16]);
  frame->hop_limit = bytes[18];
  frame->priority = bytes[19];
  (void)memcpy(frame->payload, &bytes[20], sizeof(frame->payload));
  return validate(frame);
}

corelib_status_t corelib_pfp_encode(const corelib_pfp_frame_t *frame, uint8_t bytes[64]) {
  corelib_status_t status;
  if (frame == NULL || bytes == NULL) {
    return CORELIB_INVALID_ARGUMENT;
  }
  status = validate(frame);
  if (status != CORELIB_OK) {
    return status;
  }
  (void)memset(bytes, 0, 64u);
  bytes[0] = frame->type;
  bytes[1] = CORELIB_PFP_VERSION;
  write_u16(&bytes[2], frame->destination);
  write_u16(&bytes[4], frame->source);
  write_u32(&bytes[6], frame->session_id);
  write_u32(&bytes[10], frame->message_id);
  bytes[14] = frame->frame_index;
  bytes[15] = frame->frame_count;
  write_u16(&bytes[16], frame->message_length);
  bytes[18] = frame->hop_limit;
  bytes[19] = frame->priority;
  (void)memcpy(&bytes[20], frame->payload, sizeof(frame->payload));
  write_u32(&bytes[60], corelib_crc32(bytes, 60u));
  return CORELIB_OK;
}

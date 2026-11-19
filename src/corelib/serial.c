#include "corelib/serial.h"

#include <stdbool.h>

size_t corelib_serial_encode(const uint8_t frame[CORELIB_FRAME_SIZE], uint8_t output[CORELIB_SERIAL_WIRE_SIZE]) {
  size_t code_index = 0u;
  size_t write_index = 1u;
  uint8_t code = 1u;
  size_t index;
  if (frame == NULL || output == NULL) {
    return 0u;
  }
  for (index = 0u; index < CORELIB_FRAME_SIZE; ++index) {
    if (frame[index] == 0u) {
      output[code_index] = code;
      code_index = write_index;
      ++write_index;
      code = 1u;
    } else {
      output[write_index] = frame[index];
      ++write_index;
      ++code;
    }
  }
  output[code_index] = code;
  output[write_index] = 0u;
  return write_index + 1u;
}

void corelib_serial_decoder_reset(corelib_serial_decoder_t *decoder) {
  if (decoder != NULL) {
    decoder->packet_size = 0u;
    decoder->discarding = 0u;
  }
}

static bool decode_packet(const uint8_t *packet, size_t packet_size, uint8_t frame[CORELIB_FRAME_SIZE]) {
  size_t read_index = 0u;
  size_t write_index = 0u;
  while (read_index < packet_size) {
    const uint8_t code = packet[read_index];
    const size_t code_size = (size_t)code;
    size_t count;
    ++read_index;
    if (code == 0u || read_index + (code_size - 1u) > packet_size) {
      return false;
    }
    for (count = 1u; count < code_size; ++count) {
      if (write_index >= CORELIB_FRAME_SIZE) {
        return false;
      }
      frame[write_index] = packet[read_index];
      ++write_index;
      ++read_index;
    }
    if (code != 0xffu && read_index < packet_size) {
      if (write_index >= CORELIB_FRAME_SIZE) {
        return false;
      }
      frame[write_index] = 0u;
      ++write_index;
    }
  }
  return write_index == CORELIB_FRAME_SIZE;
}

corelib_serial_result_t corelib_serial_decoder_push(corelib_serial_decoder_t *decoder, uint8_t byte, uint8_t frame[CORELIB_FRAME_SIZE]) {
  bool valid;
  if (decoder == NULL || frame == NULL) {
    return CORELIB_SERIAL_INVALID;
  }
  if (byte != 0u) {
    if (decoder->discarding != 0u) {
      return CORELIB_SERIAL_INCOMPLETE;
    }
    if (decoder->packet_size == CORELIB_SERIAL_PACKET_SIZE) {
      decoder->discarding = 1u;
      return CORELIB_SERIAL_INCOMPLETE;
    }
    decoder->packet[decoder->packet_size] = byte;
    ++decoder->packet_size;
    return CORELIB_SERIAL_INCOMPLETE;
  }
  if (decoder->discarding != 0u) {
    corelib_serial_decoder_reset(decoder);
    return CORELIB_SERIAL_INVALID;
  }
  if (decoder->packet_size == 0u) {
    return CORELIB_SERIAL_INCOMPLETE;
  }
  valid = decode_packet(decoder->packet, decoder->packet_size, frame);
  corelib_serial_decoder_reset(decoder);
  return valid ? CORELIB_SERIAL_FRAME : CORELIB_SERIAL_INVALID;
}

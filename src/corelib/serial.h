/**
 * @file serial.h
 * @brief Allocation-free COBS carriage for complete PFP frames on serial byte streams.
 */
#ifndef CORELIB_SERIAL_H
#define CORELIB_SERIAL_H

#include "corelib/device.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum COBS packet size excluding its zero-byte delimiter. */
#define CORELIB_SERIAL_PACKET_SIZE 65u
/** Maximum encoded frame size including its zero-byte delimiter. */
#define CORELIB_SERIAL_WIRE_SIZE 66u

/** Result produced while consuming a serial byte stream. */
typedef enum {
  CORELIB_SERIAL_INCOMPLETE = 0,
  CORELIB_SERIAL_FRAME = 1,
  CORELIB_SERIAL_INVALID = 2
} corelib_serial_result_t;

/** Fixed storage used to recover complete PFP frames from a serial stream. */
typedef struct {
  /** Encoded packet bytes collected before the delimiter. */
  uint8_t packet[CORELIB_SERIAL_PACKET_SIZE];
  /** Number of bytes currently stored in packet. */
  size_t packet_size;
  /** Nonzero while discarding an oversized packet through its delimiter. */
  uint8_t discarding;
} corelib_serial_decoder_t;

/**
 * @brief Encodes one complete PFP frame as COBS followed by a zero delimiter.
 * @param[in] frame Complete borrowed 64-byte PFP frame.
 * @param[out] output Caller-owned buffer of at least CORELIB_SERIAL_WIRE_SIZE bytes.
 * @return Encoded byte count, including the delimiter, or zero for invalid pointers.
 */
size_t corelib_serial_encode(const uint8_t frame[CORELIB_FRAME_SIZE], uint8_t output[CORELIB_SERIAL_WIRE_SIZE]);

/** @brief Resets a decoder and discards any incomplete packet. @param[out] decoder Decoder to reset. */
void corelib_serial_decoder_reset(corelib_serial_decoder_t *decoder);

/**
 * @brief Consumes one serial byte and emits at most one complete PFP frame.
 * @param[in,out] decoder Fixed decoder state.
 * @param[in] byte Next byte from the serial stream.
 * @param[out] frame Complete decoded frame when CORELIB_SERIAL_FRAME is returned.
 * @return Incomplete, complete-frame, or invalid-packet status.
 */
corelib_serial_result_t corelib_serial_decoder_push(corelib_serial_decoder_t *decoder, uint8_t byte, uint8_t frame[CORELIB_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif

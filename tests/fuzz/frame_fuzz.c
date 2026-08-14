#include "internal/corelib_internal.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == CORELIB_FRAME_SIZE) {
    corelib_pfp_frame_t frame;
    (void)corelib_pfp_decode(data, &frame);
  } else if (size != 0u) {
    corelib_transaction_message_t transaction;
    (void)corelib_transaction_decode(data, size,
                                        CORELIB_MAX_MESSAGE_SIZE,
                                        &transaction);
  }
  return 0;
}

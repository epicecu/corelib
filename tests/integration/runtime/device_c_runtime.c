#include <corelib/device.h>

#include <stddef.h>

int main(void) {
  const corelib_version_t version = corelib_version();
  if (corelib_context_size() == 0u ||
      corelib_context_alignment() == 0u ||
      corelib_pending_request_entry_size() == 0u) {
    return 1;
  }
  return version.major == 1u && version.minor == 0u && version.patch == 0u &&
                 version.pfp_version == 1u && version.transaction_version == 2u
             ? 0
             : 2;
}

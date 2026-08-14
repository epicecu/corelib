#include <corelib/gateway.h>

#include <stddef.h>

int main(void) {
  if (corelib_gateway_context_size() == 0u ||
      corelib_gateway_context_alignment() == 0u ||
      corelib_gateway_entry_size() == 0u) {
    return 1;
  }
  return 0;
}

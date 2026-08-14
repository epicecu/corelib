#include <corelib/gateway.h>

int main(void) {
  return corelib_gateway_context_size() <=
                 CORELIB_GATEWAY_CONTEXT_STORAGE_SIZE
             ? 0
             : 1;
}

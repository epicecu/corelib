#include <Corelib.h>

int main(void) {
  corelib_version_t version = corelib_version();
  return version.pfp_version == 1u && version.transaction_version == 2u ? 0 : 1;
}

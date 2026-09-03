#include <Corelib.h>

int main(void) {
  corelib_version_t version = corelib_version();
  return version.major == 1u && version.minor == 0u && version.patch == 0u &&
                 version.pfp_version == 1u && version.transaction_version == 2u
             ? 0
             : 1;
}

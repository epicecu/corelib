# Arduino

Corelib follows the Arduino 1.5 library layout. Install a stable release ZIP or
clone the repository into the sketchbook libraries directory. The declared
dependency installs Embedded Template Library automatically.

Include the C and C++ Device APIs through the umbrella header:

```cpp
#include <Corelib.h>
```

For a gateway sketch, define the feature before the include:

```cpp
#define CORELIB_ENABLE_GATEWAY 1
#include <Corelib.h>
```

This preprocessor switch exposes the optional gateway facade in Arduino builds.
It is distinct from the `CORELIB_BUILD_GATEWAY` CMake option.

Corelib does not depend on Arduino APIs. A sketch supplies `millis()` as its
monotonic clock and adapts `Serial`, USB CDC, RawHID, CAN, or another driver to
complete 64-byte Portable Frame Protocol frames.

The included sketches demonstrate:

- `DeviceSerial`: one PFP frame assembled over Arduino `Serial`;
- `DeviceTeensyRawHID`: one frame per Teensyduino RawHID report; and
- `GatewayIntegration`: gateway links and transport-profile callback boundaries.

Replace the example identity, payload handling, and placeholder gateway
transport hooks before using a sketch in production.

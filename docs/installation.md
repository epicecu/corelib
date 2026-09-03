# Installation

## CMake source build

Add Corelib directly and link the portable C Device target:

```cmake
add_subdirectory(path/to/corelib)
target_link_libraries(firmware PRIVATE Corelib::Device)
```

Enable the optional C gateway before adding the repository:

```cmake
set(CORELIB_BUILD_GATEWAY ON)
add_subdirectory(path/to/corelib)
target_link_libraries(firmware PRIVATE Corelib::Gateway)
```

Include the role-specific C header:

```c
#include <corelib/device.h>
```

## Installed CMake package

Install Corelib with the normal CMake install flow, then consume the Device
target:

```cmake
find_package(Corelib 1 CONFIG REQUIRED)
target_link_libraries(firmware PRIVATE Corelib::Device)
```

Request the gateway component when it is needed:

```cmake
find_package(Corelib 1 CONFIG REQUIRED COMPONENTS Gateway)
target_link_libraries(firmware PRIVATE Corelib::Gateway)
```

## C++14 facade

The C++ facade requires Embedded Template Library 20.32.1 or newer within
major version 20. For a source build, set `CORELIB_ETL_ROOT`, enable
`CORELIB_BUILD_CPP`, and link `Corelib::DeviceCpp`. Enable
`CORELIB_BUILD_GATEWAY` and link `Corelib::GatewayCpp` for a gateway.

Installed-package consumers request `Cpp` or `GatewayCpp`. Set `ETL_ROOT` when
ETL is not on the normal include path.

## Pin a revision

Firmware should consume a stable release tag or exact commit rather than an
unbounded development branch. The documentation version selector distinguishes
the current `main` branch, labelled **Latest**, from retained stable releases.

Arduino installation is described separately in the [Arduino guide](./arduino).

# Changelog

## 1.0.0

- Renamed the library, C and C++ APIs, CMake package and targets, Arduino
  metadata, examples, and validation tooling to Corelib. Programmor naming is
  retained only for the supported wire protocols and external integration.
- Reorganised native validation into GoogleTest unit suites and framework-free
  C11/C++14 runtime tests with focused CTest labels, plus GCC/gcovr line and
  branch coverage reporting.
- Shortened the C++ initialisation API to `init()` and exposed `isReady()` for
  state inspection; the C API retains its existing `_init()` functions.
- Grouped the device and gateway implementations with their public headers
  under `corelib`, while retaining separate protocol and internal layers.
- Added pinned ClangFormat 18 formatting, one-line signature checks, Doxygen
  API documentation, owned-source documentation linting, and CI style gates.
- Added the optional heap-free gateway component with dedicated PFP links,
  bounded discovery and bootstrap integration, atomic route publication,
  nested routing, topology loss handling, and C++14 facade parity.
- Added the heap-free C11 standard-node runtime.
- Added Portable Frame Protocol (PFP) v1 framing, validation, sessions,
  fragmentation, and reassembly.
- Added Transaction Protocol v2 request, response, result, and publication APIs.
- Added the C++14 ETL-based facade, typed callback interface, full C API
  operation parity, CMake package component, Arduino metadata, examples,
  conformance tests, sanitizer builds, and fuzz entry points.
- Organized the repository around CMake and Task with one canonical `src/`
  tree, Arduino CLI compile checks, and native Pico SDK and STM32Cube fixtures.

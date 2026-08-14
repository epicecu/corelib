# Dependencies

## Nanopb 0.4.9.1

The release package includes the minimal Nanopb C runtime and generated
TransactionMessage sources. Nanopb is distributed under the zlib licence in
`src/vendor/nanopb/LICENSE.txt`. Corelib consumers do not need Python, `protoc`, or
Nanopb generation tools.

The pinned generator lives under `tools/nanopb/` and is used only by maintainer
tasks. `task protocol:check` regenerates into a temporary directory and compares
the result byte-for-byte. `task protocol:regen` updates the checked-in output.
These maintainer tasks use the pinned Python packages in
`tools/nanopb/requirements.txt`. CI runs them with Python 3.12; pass a specific
interpreter as `PYTHON=/path/to/python` when it is not the default `python3`.

## Embedded Template Library 20.x

The C++14 facade requires ETL 20.32.1 or newer within major version 20. It uses
fixed-size arrays and borrowed spans without dynamic allocation. Arduino Library
Manager installs the declared `Embedded Template Library ETL` dependency. CMake
consumers request the `Cpp` package component and provide an ETL installation;
the C API and `Corelib::Device` target do not require ETL.

## GoogleTest 1.17.0

Native unit tests use GoogleTest and GoogleMock pinned to commit
`52eb8108c5bdec04579160ae17225d66034bd723`. CMake fetches this dependency only
when `CORELIB_BUILD_TESTS=ON`; installed packages and firmware consumers
do not depend on it. GoogleTest targets use C++17 while Corelib's public C++
facade remains C++14.

For an offline build, populate a local checkout and configure CMake with
`FETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest`.

## gcovr

Maintainers use GCC coverage instrumentation and `gcovr` to measure the owned
device, gateway, and protocol sources. Install `gcovr`, then run:

```sh
task quality:coverage
```

The task uses a clean `build/coverage/` directory, reuses the GoogleTest source
prepared by the native configuration, and writes terminal, HTML, Cobertura XML,
and text reports below `build/coverage/report/`. Vendored Nanopb and generated
protocol sources are excluded so the results describe code owned by this
repository. Set `GCOVR=/path/to/gcovr` when the executable is not on `PATH`.
The task fails when total line coverage falls below 85%; branch coverage is
reported for review without being used as an automated gate.

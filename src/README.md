# Source layout

This directory contains the public Corelib interfaces, their implementations, the
wire-protocol codecs, and third-party code needed to build the library.

## `corelib/`

The main device and gateway APIs live here. The directory name also provides
the public include namespace, preventing collisions with headers from other
SDKs.

Applications using the C API include:

```c
#include <corelib/device.h>
#include <corelib/gateway.h>
```

C++ applications use the corresponding `.hpp` headers. The `.c` files are the
compiled implementations and are not installed as public headers.

## `internal/`

Private types, constants, and function contracts shared by Corelib implementation
units live here. Application code must not include these headers. Their content
and layout may change without preserving source compatibility.

Some white-box and fuzz tests include internal headers so they can directly
exercise codecs and private state validation.

## `protocol/`

The owned PFP and Transaction Protocol encoding, decoding, validation, and
reassembly support lives here. These files implement wire behaviour used by the
public device and gateway APIs; they are not a separate application-facing API.

### `protocol/v2/generated/`

Generated Protocol Buffers sources for version 2 protocol messages live here.
Update these files through the repository's schema-generation workflow instead
of editing them manually.

## `vendor/`

Third-party source code compiled as part of Corelib lives here. In particular,
`vendor/nanopb/` contains the nanopb runtime used by the generated Protocol
Buffers code. Avoid applying Corelib-wide style changes to vendored files.

## `Corelib.h`

This is the Arduino-friendly umbrella header. It selects the public C or C++
interface appropriate for the sketch while keeping Arduino library usage
simple.

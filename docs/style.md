# Corelib source style

The repository uses a compact embedded C and C++ style. C headers use `.h`, C++
headers use `.hpp`, C implementations use `.c`, and C++ implementations use
`.cpp`. Public device and gateway APIs live below `src/corelib`; owned
implementations sit beside their matching headers under `src/corelib`.
Protocol and internal support remain separate. Generated and vendored sources
retain their upstream formatting.

Complete function declarations and definition signatures stay on one physical
line, even when long. Opening braces attach to the declaration. Code uses two
spaces, no tabs, right-bound pointer and reference symbols, and deterministic
include ordering.

Every owned symbol in `src` has concise Doxygen documentation.
Document inputs, outputs, return behaviour, borrowed lifetimes, capacity, timing,
and callback or serialisation requirements where relevant. Do not restate an
implementation in prose, and do not duplicate a public contract at its source
definition.

Use UK/Australian English in comments, documentation, diagnostics, and other
human-readable prose. Use US English for source-code identifiers so the API
follows common programming conventions. Prefer language-neutral abbreviations
such as `init()` where they remain clear, and write "initialise the endpoint"
in prose. Do not rename protocol-defined,
generated, or third-party identifiers to enforce this convention.

Examples use ordinary teaching comments for integration decisions, ownership,
timing, and transport constraints. They do not require API-style Doxygen
coverage for every sketch function or class.

Use the pinned formatter and checks with:

```sh
task quality:format CLANG_FORMAT=clang-format-18
task quality:format-check CLANG_FORMAT=clang-format-18
task quality:docs
```

Formatting generated protobuf or `src/vendor` files is prohibited because it
would create regeneration drift and obscure upstream changes.

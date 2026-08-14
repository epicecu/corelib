# Repository Guidelines

## Project Structure

The portable C11 core and optional C++14 facade live under `src/`. Public device and gateway APIs belong in `src/corelib`, protocol codecs in `src/protocol`, private contracts in `src/internal`, and third-party code in `src/vendor`. Generated protocol sources remain under their `generated` directories and must not be edited manually.

Examples live under `examples/`. Tests are divided into `tests/unit`, `tests/integration`, `tests/fuzz`, and `tests/hardware`. CMake, Taskfile, Arduino, packaging, and quality support remain in their existing focused directories.

## Development Commands

Use Taskfile as the repository entry point:

- `task quick` or `task test` builds and runs the portable native test suite.
- `task check` builds with strict GCC and Clang warnings.
- `task quality:format` formats owned sources with pinned ClangFormat 18.
- `task quality:format-check` verifies formatting and one-line function signatures.
- `task quality:docs` builds API documentation and lints owned symbol comments.
- `task quality:coverage` reports line and branch coverage and enforces the 85 percent owned-source line floor.
- `task all` runs the complete software release gate.

Run focused namespaced tasks when diagnosing native, protocol, package, Arduino, or hardware failures.

## Source Style and Documentation

Follow `docs/style.md` as the authoritative source style. Use two spaces and no tabs, attached opening braces, right-bound pointer and reference symbols, deterministic include ordering, and complete function signatures on one physical line. Use C11 for the core and C++14 for the facade. Apply repository formatting only to owned source; generated and vendored files retain upstream formatting.

Every owned symbol in `src` has concise Doxygen documentation. Document inputs, outputs, return behaviour, borrowed lifetimes, capacity, timing, and callback or serialisation constraints where relevant. Comments explain intent, invariants, ownership, and non-obvious integration decisions without restating code. Examples use teaching comments for transport, timing, and ownership decisions.

Use UK/Australian English in comments, documentation, diagnostics, and other human-readable prose. Use conventional US English for source-code identifiers. Do not rename protocol-defined, schema-defined, generated, vendored, third-party, or compatibility-sensitive identifiers to enforce this convention. Existing content may adopt the convention when it is otherwise edited; do not rewrite it solely to change spelling.

## Testing and Changes

Add unit tests for normal behaviour, boundaries, malformed input, resource exhaustion, callbacks, lifecycle transitions, and failure paths. Put language, linkage, runtime, and normative byte checks in integration tests; parser robustness entry points in fuzz tests; and board-specific verification in hardware fixtures. Protocol changes require matching conformance coverage against authoritative schemas or golden vectors.

Run `task all` and `git diff --check` before submitting changes. Keep public API and protocol compatibility explicit, preserve the no-heap and bounded-resource guarantees, and keep hardware tests separable from checks that run without connected devices.

## Security and Generated Content

Do not commit credentials, private keys, authentication tokens, raw device payloads, private deployment paths, or device identifiers. Do not manually edit generated Protobuf sources or reformat vendored Nanopb files; use the repository regeneration and comparison workflows.

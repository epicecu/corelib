# Teensy 4.1 RawHID test device

This fixture runs the same deterministic Common/Share model and public Corelib
self-test as the Pico fixture. It uses Teensyduino RawHID (`16c0:0486`) with
64-byte reports and is built with Arduino CLI and the pinned Teensy core.

For a fresh clone, follow the root
[Teensy testing walkthrough](../../../README.md#test-with-a-teensy-41). The
recommended deterministic hardware run is:

```sh
task teensy:test \
  PROGRAMMOR_ADAPTERS_DIR=../programmor-adapters
```

This builds and uploads fresh firmware, waits for RawHID re-enumeration, and
executes the complete adapter manifest. Uploading first resets the mutable
Common/Share state, so a run does not inherit publications from an earlier
manifest. Press the Teensy PROGRAM button if the loader requests it.

The component tasks are available for development and troubleshooting:

```sh
task teensy:setup
task teensy:build
task teensy:upload
task teensy:wait
task teensy:e2e PROGRAMMOR_ADAPTERS_DIR=../programmor-adapters
```

The setup task installs Arduino CLI 1.5.1 and Teensy core 1.62.0 under the
ignored `build/tooling` directory; no global Arduino installation is required.

On Linux, install PJRC's `00-teensy.rules` before uploading or testing. If the
loader is outside `PATH`, set `TEENSY_LOADER_CLI=/path/to/teensy_loader_cli`.
A repeating LED blink count reports an on-device self-test failure; three short
flashes indicate success.

The fixture keeps local copies of the Pico model and generated Nanopb sources
because Arduino compiles only files inside the sketch. `task teensy:build`
compares every copy with its canonical Pico source before compiling, preventing
the two hardware fixtures from drifting.

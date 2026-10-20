# SignalHandler

SignalHandler is a C++20 component for centralized POSIX signal handling in
multi-threaded applications. It blocks a defined signal set and uses a
dedicated synchronous waiter thread to invoke a normal C++ callback and support
orderly shutdown.

The reusable boundary is `src/signal_handler.hpp` and
`src/signal_handler.cpp`. The public API includes `block_signals()`, callback
registration, `start()`, `stop()`, optional `setPriority()`, and signal-name
lookup.

## Building and safe testing

From the WsprryPi checkout:

```sh
cd src/Signal-Handler/src
make release
make test
```

The fixed outputs are `build/bin/signal-handler` and
`build/bin/signal-handler_test`. Ordinary `make test` is bounded and
unprivileged. It blocks the component's handled signals, sends one controlled
`SIGTERM` only to its own test process, verifies the callback and repeated-stop
contract, and exits within a fixed deadline. It does not change scheduler
policy, use `sudo`, signal another process, or access hardware or services.

The retained demonstration waits for an operator signal and attempts the
optional real-time scheduling call. It is excluded from ordinary testing and
guarded behind explicit opt-in:

```sh
make live-test SIGNAL_HANDLER_LIVE_TEST=YES
```

## Integration notes

Call `block_signals()` before creating application worker threads so they
inherit the mask. Register a short, non-blocking callback, start the handler,
and call `stop()` before destroying dependent state. Real-time scheduling is
optional and may require additional host privileges.

## Component and extraction boundary

This directory is ordinary tracked content in the WsprryPi repository. Keep the
source hierarchy, README, standalone Makefile, demonstration, and bounded test
together.

For standalone extraction, copy `src/Signal-Handler`, initialize a new
repository, and add an appropriate license file.

## License

While part of WsprryPi, this component is covered by the repository-root
`LICENSE.md` (MIT).

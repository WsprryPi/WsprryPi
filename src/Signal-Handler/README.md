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

## Native Windows adapter

Windows uses `SetConsoleCtrlHandler`, native events, and the same waiter-thread callback interface. `CTRL_C_EVENT` and `CTRL_BREAK_EVENT` map to `SIGINT`; console close, logoff, and shutdown events map to `SIGTERM` when delivered by Windows. A closing console callback gives the application up to five seconds to finish, subject to the operating system's own deadline. This adapter does not implement Windows Service Control Manager callbacks or CRT fault handling. Windows `block_signals()` needs no POSIX signal mask, and `setPriority()` returns `false`.

Only one active Windows console registration is admitted. Registration failure leaves the handler failed; `running()` reports whether the waiter is active. `stop()` wakes and joins the waiter, releases native event handles, and unregisters the callback. Repeated stop and subsequent restart retain the existing lifecycle contract.

CMake supports the standalone component on Linux, macOS, and Windows:

```sh
cmake -S src/Signal-Handler -B /tmp/wsprrypi-signal-build
cmake --build /tmp/wsprrypi-signal-build
ctest --test-dir /tmp/wsprrypi-signal-build --output-on-failure
```

Use native private path syntax on Windows. Its bounded test starts its own child process group and sends `CTRL_BREAK_EVENT` only to that group, checks callback delivery and stop/restart, and exercises waiter failure. Native Windows execution remains required; cross-compilation does not qualify console shutdown or service lifecycle.

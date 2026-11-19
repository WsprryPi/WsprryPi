# Pi management and native schedules

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

The local Pi retains its own native schedule and local Operations controls. Fleet manages selected targets without changing the Pi backend. Use native schedule read, disabled save, authoritative readback, and explicit enable under the target’s current authority. A managing browser is not the recurrence owner. Unsupported target semantics remain disabled until an explicit supported choice is made.

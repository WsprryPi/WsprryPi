# Native execution plans

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

Native producers retain the existing compiler, bounded admission, immutable events, frequency policy, and cancellation contracts described in [native execution safety](native-execution-safety.md). Internal complete requests are implementation data, not a new operator finite-job feature. The local Pi scheduler and Pico standalone scheduler independently produce and execute their plans. No conversion API is exposed for remote execution.

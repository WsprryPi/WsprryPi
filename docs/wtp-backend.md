# Pico management

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

The Pi manages a Pico independently of its own transmitter backend. Saving a Fleet connection does not select an output or enable a schedule. Use the full target identity, current boot and revision, and existing authenticated management carrier. Duplicate display names remain distinguishable by identity. Native Pi GPIO/Si5351/RP1 behavior and the explicit simulated backend retain their own policies.

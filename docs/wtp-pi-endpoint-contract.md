# Pi native authority and management

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

A Pi exposes purpose-separated authenticated management and retirement channels, not a WTP execution endpoint. Keep grant/revocation checks, device/boot/profile bindings, rotation, protected journals, accepted-write reconciliation, and durable retirement receipts. New legacy controller-resume or retirement duties that would recreate dispatch are refused; exact existing receipts remain readable. A disconnected or replaced coordinator cannot clear unresolved authority.

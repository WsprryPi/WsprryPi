# Native execution and retained management

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

Startup validates the selected SQLite generation before admitting work. The closed supported settings model preserves current custom station, hardware, timing, and policy values. Existing INI input is one-time migration only; normal operation never reloads or projects it. Backend and simulator selection remain explicit, with no fallback after initialization failure. The [storage/process contract](development/sqlite-storage-contract.md) defines ownership, advanced commands, and recovery.

A managing Pi provides target discovery, configuration, native recurrence, status, pairing, recovery, retirement receipt inspection, USB time, logging, and independent Pico maintenance. Read-only legacy Fleet rows remain visible with their saved state and cleanup duties. Remote recurrence/job actions refuse before connecting or writing.

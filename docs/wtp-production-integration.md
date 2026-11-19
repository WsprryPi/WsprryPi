# Native execution and retained management

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

Startup inspects stored configuration before a transient backend override. Active legacy selection or listener configuration produces a migration-required refusal; simulation or local hardware is never chosen automatically. Native configurations containing inactive dormant connection settings remain valid. Public settings omit retired backend/listener sections, while private storage preserves meaningful explicit values and sealed recovery identities.

A managing Pi provides target discovery, configuration, native recurrence, status, pairing, recovery, retirement receipt inspection, USB time, logging, and independent Pico maintenance. Read-only legacy Fleet rows remain visible with their saved state and cleanup duties. Remote recurrence/job actions refuse before connecting or writing.

# Fleet target management

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

Fleet retains saved connection profiles, USB and DNS-SD discovery, duplicate-name identity labels, target management, native scheduling, status, pairing, recovery, and durable retirement observations. Saving or viewing a profile neither selects an output nor enables recurrence. Per-target drafts and accepted-write recovery remain bound to full device, boot, profile generation, and revision.

Legacy controller recurrence rows and cleanup journals remain read-only. Display migration-required state without normalizing saved enabled/in-flight flags, reconnecting, inspecting a remote job, dispatching, pausing, aborting, or erasing a duty. POST actions refuse before any carrier factory or target write. Existing protected receipts remain available; unsupported new dispatch/cleanup duties are not fabricated.

Use explicit status and native stop controls for the selected Pico. Unknown replies retain the hold and draft, and require authoritative readback. A different target remains independently manageable. Local Pi Operations continues to control the Pi’s own native execution.

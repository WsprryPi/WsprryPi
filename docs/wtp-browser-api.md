# Browser management contract

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

The single current prerelease `Wsprry-Control/1` contract remains major 1. Its current schema, generated validators, producer descriptors, and all consumers evolve together. There are no historical control views or version ranges. Independent transport, storage, and retained internal execution formats keep their own identities.

Capabilities advertise native `execution` and `message_limits`; they do not advertise jobs or WTP carriers. `/api/control/v1/jobs`, descendants, and aliases return the ordinary unsupported-route response before body parsing or mutation for supported HTTP methods. Unsupported methods remain parser-level refusals. Authenticated `/status`, configuration/resources, native scheduling, and `/schedules/stop` remain available through the current management registry.

Validate closed document shape before field access, exact descriptor/returned-schema agreement, current limits, full identity/boot/profile generation, authorization, and revision before writes. Missing/extra fields, wrong types, unsupported carriers/versions, malformed documents, and changed bindings refuse without mutation. Preserve redaction, partial saved progress, complete apply semantics, per-target drafts, one explicit write, and accepted-write readback. Do not infer delivery or inactive output from disconnect.

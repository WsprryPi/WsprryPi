# UDP syslog coordinated work

Status: implementation and finite runtime acceptance passed, 2026-10-28; coordinated publication is tracked in the canonical Pico backlog. The [canonical Pico proposal](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/development/udp-syslog-proposal.md) records the reviewed design, closed resource shapes, resource budgets, review findings and acceptance matrix. The [implementation contract](udp-logging.md) and linked qualification record describe the resulting behavior and actual acceptance evidence. This is a linked Pi deliverable, not a second feature authority.

## Pi and first-party UI deliverables

- [x] Add the separate unprivileged journal sender/forwarder and optional IPv4 UDP collector, preserving Pico/native origins and separately claimed relay identity. Do not receive hostile UDP inside the root transmitter service or replace the host logging stack.
- [x] Add independent strict `host-udp-logging/1` policy, authenticated versioned capability/settings resources, atomic persistence and bounded service/socket/installer/remover lifecycle. Preserve existing native/USB settings and default UDP off.
- [x] Extend actual selected-member network and USB negotiation/proxy paths for `transmitter-udp-logging/1`, matching the intended Pico sources, exact target/profile/revision bindings and limits. Keep historical closed descriptors unchanged.
- [x] Add Pi sender, attached-Pico forwarding and collector settings in the existing UI; keep host and selected-member settings visibly separate. Add explicit Logs stream version 2 with fixed trusted-unit scope, remote-Pi/peer filters, provenance and filter-specific cursors. Preserve the existing Logs API and USB controls.
- [x] Package the worker/unit/socket assets in source and precompiled installation paths; document journal-read privilege, optional port 514 reception, retention pressure, upgrades, removal and rollback without changing unrelated logging services.
- [x] Extend codec, USB logging, native event redaction, service/installer, journal/SSE and browser tests. Exercise the complete current `.github/workflows/debian-non-hardware.yml` network and actual USB pair recipes with explicit candidate Pico sources and pinned TLS dependencies. Neither mocks nor old green CI close this gate.
- [x] Coordinate the linked WsprryPi-Docs TODO and Pico operator documentation; run finite RF-inhibited native Pi → collector and physical USB Pico → member → collector campaigns, record exact identities/images/configuration and restore the setup. Record both source revisions, tested dirty changes and CI links before feature acceptance.

The UI is ordinary tracked parent content at `WsprryPi-UI`, not a submodule. All deliverables above passed their software and finite runtime gates. The [qualification record](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/development/udp-logging-qualification.md) records exact sources, tested images, actual packet/journal counts and restored host/device state; it does not qualify RF behavior.

## Proposal validation

The original design inspection used Pi `d89e7062e891c80ba933aa63b6bbb0b5295cf831` and Pico `6e8e1afd4ba5532602669947dd540781b03aeae8`. The canonical proposal records two in-thread adversarial assessments and corrections; no independent review is claimed. Proposal validation covered links/anchors, source paths, wire-size arithmetic, schema/identity consistency and whitespace. The later implementation and acceptance sources are recorded separately in the qualification record.

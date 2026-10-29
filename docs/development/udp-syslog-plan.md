# UDP syslog coordinated work

The [Pico UDP logging contract](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/development/udp-logging.md) defines the implemented shared wire profile, independent resources, resource bounds and validation applicability. The [Pi implementation contract](udp-logging.md) defines the host worker, service and UI. Selected work is maintained in the canonical Pico backlog.

## Pi and first-party UI deliverables

- [x] Add the separate unprivileged journal sender/forwarder and optional IPv4 UDP collector, preserving Pico/native origins and separately claimed relay identity. Do not receive hostile UDP inside the root transmitter service or replace the host logging stack.
- [x] Add independent strict `host-udp-logging/1` policy, authenticated versioned capability/settings resources, atomic persistence and bounded service/socket/installer/remover lifecycle. Preserve existing native/USB settings and default UDP off.
- [x] Extend actual selected-member network and USB negotiation/proxy paths for `transmitter-udp-logging/1`, matching the intended Pico sources, exact target/profile/revision bindings and limits. Keep historical closed descriptors unchanged.
- [x] Add Pi sender, attached-Pico forwarding and collector settings in the existing UI; keep host and selected-member settings visibly separate. Add explicit Logs stream version 2 with fixed trusted-unit scope, remote-Pi/peer filters, provenance and filter-specific cursors. Preserve the existing Logs API and USB controls.
- [x] Package the worker/unit/socket assets in source and precompiled installation paths; document journal-read privilege, optional port 514 reception, retention pressure, upgrades, removal and rollback without changing unrelated logging services.
- [x] Extend codec, USB logging, native event redaction, service/installer, journal/SSE and browser tests. Exercise the complete current `.github/workflows/debian-non-hardware.yml` network and actual USB pair recipes with explicit candidate Pico sources and pinned TLS dependencies. Neither mocks nor old green CI close this gate.
- [x] Coordinate the linked WsprryPi-Docs TODO and Pico operator documentation; run finite RF-inhibited native Pi → collector and physical USB Pico → member → collector campaigns, record exact identities/images/configuration and restore the setup. Record both source revisions, tested dirty changes and CI links before feature acceptance.

The UI is ordinary tracked parent content at `WsprryPi-UI`. The [source-bound validation applicability](https://github.com/WsprryPi/WsprryPico/blob/devel/engineering/development/udp-logging.md#validation-and-applicability) identifies the retained software and physical scope; it does not qualify RF behavior.

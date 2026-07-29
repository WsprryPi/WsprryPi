# Hellschreiber Research Decision Log

## Purpose

This log records durable spike decisions and hypotheses without confusing research progress with implementation approval.

## Status vocabulary

- **Accepted decision:** Governs the spike until explicitly superseded.
- **Provisional working hypothesis:** Directs research but is not an established conclusion.
- **Deferred decision:** Intentionally postponed until named evidence is available.
- **Rejected alternative:** Considered and rejected with evidence or scope rationale.
- **Superseded decision:** Replaced by a later identified decision.

Use stable identifiers in the form `HELL-DEC-0001`.

## Decisions

| Decision ID | Date | Step | Status | Decision or hypothesis | Evidence | Consequences | Revisit condition |
| --- | --- | --- | --- | --- | --- | --- | --- |
| HELL-DEC-0001 | 2026-07-29 | Setup | Accepted decision | The spike is research and documentation only. | User-authorized spike boundary | No application, UI, service, hardware, or RF changes are permitted. | Explicit later authorization |
| HELL-DEC-0002 | 2026-07-29 | 1.1 | Provisional working hypothesis | Feld-Hell will be investigated first as the proposed reference baseline; it is not an accepted Wsprry Pi implementation choice. | Research-path ordering; technical evidence not yet registered | Step 1.1 must test the reference definition before later comparisons rely on it. | Step 1.1 evidence and conclusion |
| HELL-DEC-0003 | 2026-07-29 | Setup | Accepted decision | Application implementation requires separate, explicit authorization after the research gates. | User-authorized spike boundary | Research findings cannot silently expand into production work. | Explicit later authorization |
| HELL-DEC-0004 | 2026-07-29 | Setup | Accepted decision | Hardware and over-the-air validation are separate and currently unauthorized. | Repository hardware-safety policy and user scope | Offline or documentary evidence cannot qualify GPIO, RF output, spectral behavior, or deployment. | Explicit hardware-validation authorization |
| HELL-DEC-0005 | 2026-07-29 | Setup | Accepted decision | Important outcomes must be committed in this workspace rather than existing only in chat history. | User request for durable spike outcomes | Step reports, evidence, decisions, and unresolved questions must remain reviewable in Git. | Spike closeout |

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
| HELL-DEC-0002 | 2026-07-29 | 1.1 | Accepted decision | Standard Feld-Hell is the reference comparison baseline for this spike; this does not select it for Wsprry Pi implementation. | HELL-EVID-0001, HELL-EVID-0003 through HELL-EVID-0009 | Later modes, fonts, adoption evidence, and interoperability results must be compared with the Step 1.1 contract. | Evidence that the historical baseline is materially misidentified or an explicit spike-scope change |
| HELL-DEC-0003 | 2026-07-29 | Setup | Accepted decision | Application implementation requires separate, explicit authorization after the research gates. | User-authorized spike boundary | Research findings cannot silently expand into production work. | Explicit later authorization |
| HELL-DEC-0004 | 2026-07-29 | Setup | Accepted decision | Hardware and over-the-air validation are separate and currently unauthorized. | Repository hardware-safety policy and user scope | Offline or documentary evidence cannot qualify GPIO, RF output, spectral behavior, or deployment. | Explicit hardware-validation authorization |
| HELL-DEC-0005 | 2026-07-29 | Setup | Accepted decision | Important outcomes must be committed in this workspace rather than existing only in chat history. | User request for durable spike outcomes | Step reports, evidence, decisions, and unresolved questions must remain reviewable in Git. | Spike closeout |
| HELL-DEC-0006 | 2026-07-29 | 1.1 | Accepted decision | Use 122.5 baud and 8.163 ms logical cells for the Feld-Hell on-air reference; describe 7x14 at 245 samples/s only as a compatible paired-sample representation. | HELL-EVID-0001, HELL-EVID-0005 through HELL-EVID-0009 | Later research must distinguish independent signal intervals from internal raster samples and preserve the 400 ms character duration. | Contrary primary evidence or failed offline equivalence testing in Step 5 |
| HELL-DEC-0007 | 2026-07-29 | 1.1 | Deferred decision | Do not freeze an exact font, pulse shape, audio pitch, or occupied-bandwidth target in Step 1.1. | HELL-EVID-0001, HELL-EVID-0004 through HELL-EVID-0010 | Historical timing remains stable while font and spectral choices proceed through their named evidence and test gates. | Font evidence in Step 3 and offline spectral/interoperability evidence in Step 5 |
| HELL-DEC-0008 | 2026-07-29 | 1.2 | Accepted decision | Use the Step 1.2 layered comparison schema and explicit `OBS`, `CALC`, `INFER`, `CONFLICT`, `UNKNOWN`, `NOT-ASSESSED`, and `N/A` evidence states. | HELL-EVID-0011 through HELL-EVID-0016 | Later mode records must separate encoding, sampled representation, waveform, transport, display, support, adoption, and feasibility rather than leave ambiguous blanks. | Step 2 finds an evidenced family the schema cannot represent additively |
| HELL-DEC-0009 | 2026-07-29 | 1.2 | Accepted decision | Normalize independent logical baud separately from constrained sample rate, and compare bandwidth only under an identified metric and conditions. | HELL-EVID-0005, HELL-EVID-0008, HELL-EVID-0010 through HELL-EVID-0012 | Later tables may preserve source terminology but must not compare sample rate, keying fundamental, filter width, and occupied bandwidth as one quantity. | Contrary primary definition or a later measurement standard adopted explicitly |
| HELL-DEC-0010 | 2026-07-29 | 1.2 | Accepted decision | Adoption labels require time-bounded observations, denominators, mode-identification methods, bias notes, and preferably independent datasets; software availability is not on-air prevalence. | HELL-EVID-0011, HELL-EVID-0013 through HELL-EVID-0015 | Step 4 must report supported, scheduled, and observed use separately when representative quantitative data are unavailable. | Step 4 adopts a stronger documented population method |
| HELL-DEC-0011 | 2026-07-29 | 1.2 | Accepted decision | Evaluate historical fidelity, interoperability, font reproducibility, adoption, timing, spectra, safety, and maintainability with independent hard gates rather than a composite score. | Step 1.2 methodology; HELL-EVID-0016 | A favorable total cannot hide a safety failure, missing adoption evidence, incompatible waveform, or unlicensed font. | An explicit, auditable decision model proves necessary without weakening any hard gate |
| HELL-DEC-0012 | 2026-07-29 | 1.2 | Deferred decision | Do not finalize taxonomy, font, adoption rank, interoperability, spectral target, or Wsprry Pi feasibility in Step 1.2. | HELL-EVID-0011 through HELL-EVID-0016 | Each conclusion remains assigned to Steps 2 through 6 and the gated Step 7 recommendation. | Completion of the corresponding numbered evidence gate |

# Step 7 — Make a gated recommendation

## Status

**Complete.** Recommend Standard Feld-Hell as the sole subject of a separately authorized **Standard Feld implementation-design and qualification-planning spike**, subject to seven mandatory gates. This is a moderate-strength recommendation for research suitability and implementation-design suitability only. It does not select a production font, authorize implementation, qualify hardware or RF output, or establish release readiness. [HELL-DEC-0036] [HELL-DEC-0037]

fldigi 4.2.12 `FSKH105` and xfhell 3.5.2 `FMHell105` remain separate deferred contracts. A blended generic “Hell 105” mode is rejected. The documentary six-row/105-baud/55-Hz profile remains `INSUFFICIENT EVIDENCE`. [HELL-DEC-0038] through [HELL-DEC-0040]

## Executive recommendation

Wsprry Pi should proceed, if separately authorized, with a documentation-first implementation-design and qualification-planning spike for **Standard Feld-Hell only**. The evidence is sufficient to design how the mode would fit the product because:

- the historical timing and raster traversal are sufficiently stable for a bounded protocol contract;
- fldigi 4.2.12 and xfhell 3.5.2 rendered each other's Standard Feld transmission readably in both directions in the clean contained Step 5 trial;
- Wsprry Pi already separates frozen requests, compiled RF events, scheduling, cancellation, and backend execution; and
- sequential OOK avoids the continuous-phase requirement that blocks the two 105-labelled frequency-shift profiles. [HELL-EVID-0001] [HELL-EVID-0023] [HELL-EVID-0042] [HELL-EVID-0044] through [HELL-EVID-0046]

The recommendation is conditional because the exact production font and spacing policy are unresolved, the Step 5 interoperability result does not transfer automatically to a new font, backend timing is unmeasured, and no spectral criterion has been selected. Those are hard gates, not ordinary follow-up tasks.

## Research question

> Based on Steps 1.1 through 6, should Wsprry Pi proceed with a separately authorized implementation-design spike for Hellschreiber, and if so, for which mode, under what gates, with what exclusions, and with what evidence still required before implementation or release?

**Answer:** yes, for Standard Feld-Hell alone and only as a separately authorized implementation-design and qualification-planning spike. No candidate is ready for implementation, hardware qualification, RF qualification, release, or deployment.

## Scope

Step 7 synthesizes the frozen documentary, taxonomy, font, adoption, interoperability, and Wsprry Pi feasibility evidence. It makes no new waveform measurement and introduces no new external source. It does not choose a font, design product interfaces, implement a mode, run software tests, operate hardware, or transmit RF.

## Inputs reviewed

- Step 1.1: reference timing, raster traversal, historical limits, and unresolved waveform details.
- Step 1.2: layered comparison schema, evidence states, and independent hard gates.
- Step 2: family/profile taxonomy and rejection of ambiguous aliases.
- Step 3: immutable font references, raster comparisons, provenance, and licensing limits.
- Step 4: bounded adoption evidence and the absence of representative worldwide mode-share data.
- Step 5: source-contract comparisons and contained application interoperability evidence.
- Step 6: Wsprry Pi architecture fit, backend limitations, resource bounds, and qualification ladder.
- The evidence register, decision log, and retained artifacts for those steps.

No new evidence-register entry is required: this report is a synthesis of already registered evidence, not a new experiment or source.

## Method

The Step 1.2 hard-gate method is applied without a composite score. Historical fidelity, reproducible assets, interoperability, architecture fit, backend timing, safety, spectra, and maintainability remain independently necessary. Adoption informs priority but cannot compensate for a failed technical, licensing, or safety gate. [HELL-DEC-0010] [HELL-DEC-0011]

Each candidate receives one final disposition:

- `RECOMMEND FOR IMPLEMENTATION-DESIGN SPIKE`: enough evidence exists to design a possible implementation, but coding still requires a later authorization.
- `DEFER`: a concrete contract exists, but named blockers make design selection premature.
- `INSUFFICIENT EVIDENCE`: the available record does not define a reproducible implementation target.
- `REJECT`: the proposed alternative is materially ambiguous, unsafe, or unnecessary under the current evidence.

## Candidate disposition matrix

| Candidate | Final disposition | Decisive evidence | Blocking evidence or uncertainty | Evidence that could change the disposition |
| --- | --- | --- | --- | --- |
| Standard Feld-Hell | **`RECOMMEND FOR IMPLEMENTATION-DESIGN SPIKE`** | Stable reference timing; clean contained F3 in both directions; sequential OOK; compatible request/event-plan architecture | Production font/spacing unresolved; exact-asset retest absent; backend timing and spectra unmeasured | Passing Gates A–G below would advance it from design candidacy toward implementation and release decisions |
| fldigi 4.2.12 `FSKH105` | **`DEFER`** | Versioned, inspectable 14-position, 245-decision/s, 55-Hz CPFSK contract; exact-profile schedule evidence | No independent exact F3 endpoint; GPL asset decision; no phase-preserving backend proof | Licensed exact asset, independent endpoint, exact-profile interoperability, and qualified phase-preserving backend |
| xfhell 3.5.2 `FMHell105` | **`DEFER`** | Versioned, inspectable 12-position, 210-decision/s, 210-Hz CPFSK contract | No independent exact F3 endpoint; GPL-derived asset; no phase-preserving backend proof | Licensed exact asset, independent endpoint, exact-profile interoperability, and qualified phase-preserving backend |
| Documentary six-row/105-baud/55-Hz profile | **`INSUFFICIENT EVIDENCE`** | Documentary timing and waveform description only | Authoritative licensed bitmap, spacing/idle policy, and verified current endpoint absent | Recovery of a licensed immutable font plus a versioned exact transmitter/receiver contract |

The 105-labelled rows are not negative interoperability results. Step 5 found them `NOT CONFIGURABLE` as exact reciprocal application contracts; a nearest-label substitution was intentionally not scored. [HELL-EVID-0035] through [HELL-EVID-0037] [HELL-EVID-0043]

## Standard Feld is suitable for design, not implementation

The recommendation has six distinct boundaries:

| Readiness class | Step 7 result | Meaning |
| --- | --- | --- |
| Research suitability | **Pass** | The mode is sufficiently defined to compare and plan. |
| Implementation-design suitability | **Conditional pass** | A bounded design spike can resolve the exact contract and qualification plan. |
| Implementation authorization | **Not authorized** | No production code, UI, configuration, test, or dependency work may begin from this report alone. |
| Hardware qualification | **Not assessed** | No Raspberry Pi, GPIO, clock generator, Si5351, or attached transmitter was exercised. |
| RF and spectral qualification | **Not assessed** | No occupied bandwidth, spur, harmonic, filtering, or on-air measurement was made. |
| Release readiness | **Not ready** | Every gate and later implementation validation must pass first. |

Standard Feld is preferred because it is the simplest candidate that has a clean, application-readable baseline. That is a technical-risk conclusion, not a measured popularity ranking. Public evidence does not support naming a worldwide most-used Hellschreiber waveform. [HELL-EVID-0029] through [HELL-EVID-0033]

## Seven mandatory gates

The gates are conjunctive: failure or incompleteness of any gate blocks release readiness. A later spike may refine the acceptance criteria, but it may not silently remove a gate. [HELL-DEC-0037]

### Gate A — Freeze the protocol contract

Before coding, record one normative Standard Feld profile containing:

1. seven columns and 14 physical positions per column at 245 positions/s, retaining the conventional 122.5-baud designation;
2. bottom-to-top position traversal and left-to-right column traversal;
3. whether the implementation preserves arbitrary half-position placement or intentionally uses a documented compatible paired representation;
4. sequential OOK polarity and the exact RF-on/RF-off meaning;
5. character leading/trailing blanks, word spacing, stream leaders/trailers, and idle behavior;
6. message normalization, supported repertoire, and unsupported-character behavior;
7. exact duration calculation, repeat validity, scheduling, cancellation boundaries, and safe terminal state.

**Pass evidence:** a versioned protocol specification plus deterministic fixtures whose raster positions, event offsets, RF states, and total duration can be reviewed without executing hardware.

### Gate B — Freeze a font and licensing decision

Select exactly one immutable production font and spacing policy. Record its provenance, redistribution license, checksum, glyph repertoire, widths, spacing, and substitution behavior. If GPL-derived data are proposed for the MIT project, make an explicit reviewed distribution decision; this report makes no legal conclusion. If an independently authored font is proposed, preserve evidence of independent authorship and do not infer compatibility from visual similarity. [HELL-EVID-0023] through [HELL-EVID-0028] [HELL-EVID-0048]

**Pass evidence:** a named immutable asset with checksum, provenance, explicit redistribution basis, complete contract fixtures, and an approved repository-placement decision.

### Gate C — Requalify application interoperability

Retest the exact Gate B asset and Gate A protocol against named, immutable receiver versions. At minimum:

- use fldigi 4.2.12 and xfhell 3.5.2, or explicitly document why a newer immutable version replaces either target;
- test both application directions where the target supports transmission and reception;
- retain the Step 5 corpus `HELL TEST 0123456789 DE WSPRY WSPRY 73` and add characters needed to cover the selected repertoire and substitution rules;
- retain exact settings, generated fixtures, application renders, manifests, and checksums;
- require readable distinctive text for F3 and record whether F4 character identity is feasible;
- repeat runs and use blind or independently repeated scoring to reduce the current manual, single-run limitation;
- test defined frequency/clock offset, inversion, noise, truncation, and dropout conditions;
- classify approximate or substituted receiver settings separately and never score them as exact-contract interoperability.

**Pass evidence:** reproducible, version-qualified results meeting declared F3/F4 criteria under clean and bounded impaired conditions.

### Gate D — Qualify backend timing and waveform behavior

For every supported Raspberry Pi and RF-backend combination proposed for release, measure:

- minimum sustainable event duration;
- event timing error and jitter under declared load;
- frequency accuracy and transition latency;
- RF gating behavior and safe idle state;
- cancellation and configuration-change latency;
- recovery after interruption and fault;
- phase behavior where the selected waveform contract requires it.

The present source establishes event-plan representability, not physical performance. Qualification must decide whether an existing backend can receive a bounded extension or whether a new backend is necessary. Standard Feld does not require the continuous-phase contract that blocks the 105 profiles, but it still requires accurate 4.0816 ms position timing and safe RF gating. [HELL-EVID-0044] through [HELL-EVID-0047]

**Pass evidence:** repeatable no-transmit timing traces followed, under separate authorization, by instrumented attached-hardware results on every supported combination.

### Gate E — Define and pass spectral and RF criteria

Before attached-hardware or on-air claims, name:

- the occupied-bandwidth or emission-mask criterion and measurement bandwidth;
- worst-case raster patterns, including rapid alternating RF-on/RF-off positions;
- center frequencies, drive conditions, load, filtering, and measurement equipment;
- frequency accuracy, keying sideband, harmonic, and spur limits;
- the dummy-load or shielded test environment and stopping procedure;
- operator licensing, band-plan, station-identification, and other applicable regulatory responsibilities.

**Pass evidence:** separately authorized, retained measurements showing the chosen backend and output path meet the named criterion. Offline application decoding does not satisfy this gate.

### Gate F — Define the product and operator contract

A later design must specify the mode name, configuration fields, defaults, validation, persistence, CLI representation, UI workflow, scheduling, message limits, duration display, progress, cancellation, errors, safety warnings, logging, operator documentation, and backward compatibility. It must trace each field through the complete lifecycle and must not overload QRSS, FSKCW, or DFCW timing semantics. [HELL-EVID-0044] [HELL-EVID-0046]

**Pass evidence:** a reviewed implementation specification and test plan covering backend, parent application, UI submodule, persistence, scheduling, and operator documentation boundaries.

### Gate G — Establish release readiness

Release readiness requires Gates A–F to pass, the separately authorized implementation to be complete, non-hardware regression tests to pass, hardware and spectral results to cover the supported matrix, operator documentation to be reviewed, and unresolved safety or licensing issues to be closed.

**Pass evidence:** a release checklist linking the exact implementation revision, asset checksum, receiver versions, supported hardware matrix, test results, spectral evidence, documentation, and known limitations.

## Deferred and excluded alternatives

### fldigi `FSKH105` and xfhell `FMHell105`

Both remain deferred because each combines a continuous-phase backend gap, an asset/licensing decision, and no independent exact-profile F3 endpoint. They must remain separately named in configurations, tests, logs, and documentation. A later reconsideration requires the evidence named in the candidate matrix; schedule evidence alone is insufficient.

### Generic or blended “Hell 105”

**Rejected.** The label would erase material differences in raster height, physical decision rate, tone separation, font, and implementation lineage. It would make interoperability claims and operator configuration ambiguous. [HELL-DEC-0025] [HELL-DEC-0026] [HELL-DEC-0039]

### Documentary six-row/105-baud/55-Hz profile

It remains insufficient evidence rather than rejected. A documented nominal rate and shift do not supply the licensed raster, spacing, idle behavior, or maintained endpoint needed for a reproducible product contract.

### Hell-80

Historical Siemens Hell-80 and fldigi Hell-80 remain separate profiles whose compatibility class was not established in this spike. Neither has the complete asset, adoption, interoperability, and Wsprry Pi feasibility record required to displace Standard Feld. Reconsideration requires an immutable licensed contract and bidirectional application evidence. [HELL-DEC-0016]

### A private Wsprry Pi variant

**Rejected for this path.** Inventing a variant would abandon the demonstrated Standard Feld receiver baseline and create a new interoperability burden without evidence of operator benefit. A future proposal would require an independently justified use case and the complete gate set as a new candidate.

### Audio output as an assumed shortcut

Wsprry Pi has no general audio waveform backend. Audio would be a new backend and would still require timing, device routing, cancellation, spectral, operator, and interoperability qualification. It is not an existing low-risk configuration option. [HELL-EVID-0045]

### Immediate hardware or RF testing

Excluded from this research closeout. Such work requires a separately authorized plan naming the device, mode, frequency, duration, output path, load, measurement setup, and stop procedure.

## Recommended next authorized work unit

### Standard Feld implementation-design and qualification-planning spike

**Purpose:** convert the conditional recommendation into a reviewable normative profile, asset decision, architecture design, and staged verification plan without yet implementing or transmitting it.

**Required inputs:** this Step 7 report; all prior reports and artifacts; current parent, transmitter, and UI contracts; candidate font provenance and licensing evidence; supported-platform policy; and the private interoperability rig at its recorded immutable revision.

**Expected outputs:**

1. Gate A normative profile and deterministic raster/event fixtures.
2. Gate B font/license decision and immutable asset manifest.
3. Gate C exact-asset interoperability protocol and acceptance criteria.
4. A component-by-component architecture design covering payload, compiler, controller, backends, parent configuration, scheduling, persistence, CLI, UI, tests, and documentation.
5. Gate D no-transmit and attached-hardware qualification plans with a proposed supported matrix.
6. Gate E spectral test specification and worst-case fixtures.
7. Gate F product/operator contract and Gate G release checklist.
8. A final go/no-go decision for an implementation slice.

**Acceptance criteria:** every output is versioned and internally consistent; the font is reproducible and distributable under an explicit decision; the exact-asset application protocol is executable; backend performance thresholds and spectral criteria are numeric or otherwise objectively testable; risks and unsupported hardware remain explicit; and a bounded implementation plan can be reviewed without guessing protocol semantics.

**Permitted decisions:** choose the proposed Standard Feld profile and asset; determine whether a bounded backend extension or new backend is the appropriate design; define objective validation criteria; propose implementation slices and supported targets.

**Required deferrals:** code changes, UI changes, dependency changes, service changes, device access, GPIO activity, audio output, attached-hardware measurements, RF emission, on-air testing, release claims, and inclusion of any 105-labelled or Hell-80 candidate.

**Condition before coding:** an explicit later authorization must approve the frozen Gate A/B contract, reviewed architecture, acceptance tests, repository boundaries, and first implementation slice.

## Evidence-confidence assessment

**Overall recommendation strength: moderate.**

### High confidence

- Standard Feld's historical timing and scan-order baseline is suitable for a bounded design contract.
- The two 105-labelled application profiles are materially different and cannot be treated as one exact contract.
- Wsprry Pi has a reusable request/compiler/event-plan/scheduler boundary but no existing Hell mode or general audio backend.
- No current backend declares continuous-phase support.
- The candidate dispositions cannot establish hardware, RF, or release readiness.

### Moderate confidence

- Standard Feld is readable between fldigi 4.2.12 and xfhell 3.5.2 in the tested clean contained path.
- Standard Feld is the lowest-risk candidate for an implementation-design spike.
- Its sequential OOK plan can fit the architecture with a bounded extension or new backend determined by qualification.

These conclusions are moderate because the application result is one manual, non-blind run and backend behavior is source-derived rather than measured.

### Low confidence or unresolved

- The correct production font and spacing policy.
- Interoperability of that future asset.
- Timing, jitter, cancellation latency, spectral behavior, and supported-hardware breadth.
- Representative worldwide mode share.
- Whether either 105 profile would be useful through deliberately substituted settings.
- The exact compatibility class of historical and modern Hell-80 implementations.

The recommendation is not strong because the production asset and physical-output evidence remain open. It is not weak because the reference contract, application-readable baseline, and architectural seam independently support a bounded next design step.

## Decisions affected

- HELL-DEC-0036 recommends Standard Feld only for a separately authorized implementation-design and qualification-planning spike.
- HELL-DEC-0037 makes Gates A–G mandatory and conjunctive.
- HELL-DEC-0038 preserves both 105-labelled profiles as separate deferred candidates.
- HELL-DEC-0039 rejects a blended generic “Hell 105” product contract.
- HELL-DEC-0040 retains the documentary profile as insufficient evidence.
- HELL-DEC-0041 closes this research spike for its scoped questions without authorizing implementation or qualification.
- HELL-DEC-0042 defines the next bounded work unit and its authorization boundary.

## Remaining questions

The proposed next spike must resolve:

- Which immutable, redistributable Standard Feld font and spacing policy should Wsprry Pi propose?
- Does the exact proposed asset retain F3, or achieve F4, with named receiver versions under repeatable scoring?
- What timing-error, jitter, cancellation-latency, and supported-platform thresholds are acceptable?
- Can an existing backend meet those thresholds, or is a new backend required?
- What occupied-bandwidth or emission criterion and worst-case raster establish acceptable RF behavior?
- What initial hardware/backend matrix is supportable without overstating qualification?

Longer-term research may still ask:

- Can privacy-preserving, exact-profile observations establish representative Hellschreiber mode share?
- Can the 105-profile, six-row, and Hell-80 evidence gaps be closed with licensed assets and independent endpoints?

## Research-spike closure assessment

Steps 1.1 through 7 are complete for their scoped research questions. The spike established the reference baseline, comparison method, taxonomy, font evidence, bounded adoption evidence, offline application interoperability, source-level feasibility, and final gated recommendation.

Completion means the research question has a durable answer. It does **not** mean that Hellschreiber is implemented, selected for release, hardware-qualified, RF-qualified, regulatory-qualified, or deployment-ready. The remaining work transfers to a separately authorized design and qualification-planning spike. [HELL-DEC-0041]

## Explicit non-authorizations

This report does not authorize:

- production or test code changes;
- a production font or license choice;
- configuration, CLI, UI, scheduling, persistence, service, or dependency changes;
- creation or modification of an RF or audio backend;
- GPIO, clock-generator, Si5351, audio-device, transmitter, or antenna operation;
- test-tone generation or RF transmission;
- attached-hardware, spectral, on-air, regulatory, installation, or deployment claims;
- operator-documentation changes;
- staging, committing, pushing, or opening a pull request.

## Recommended next action

Review this closeout. If the conditional recommendation and gate set are accepted, authorize a fresh **Standard Feld implementation-design and qualification-planning spike**. Begin with Gate A and Gate B; do not authorize coding until the normative protocol and immutable redistributable font are approved and the exact-asset interoperability protocol is ready.

# Phase 3 — exact-asset offline qualification

## Result

`PHASE 3 PASS — EXACT-CORPUS OFFLINE RECEIVE F3 QUALIFIED THROUGH THE PINNED APPLICATION ADAPTER; GATE C PARTIAL; INDEPENDENT REVIEW PENDING`

The committed rig evidence supports application-level receive F3 readability for the pinned Wsprry exact asset and corpus in fldigi 4.2.12 and xfhell 3.5.2. It does not fully close the parent Gate C because exact-contract application-direction coverage, repertoire/substitution coverage, an F4 feasibility disposition, and blind or independently repeated scoring remain open.

Phase 4 architecture design may begin: no Phase 3 evidence invalidates the frozen raster, timing, font, spacing, event, cancellation, or terminal-state contract. This decision does not authorize implementation.

## Immutable identities

| Item | Identity |
| --- | --- |
| WsprryPi producer | `07efffbf3a449600d67ed6f80e3f15203db50375` |
| Rig evidence and capability | `088bbaecb1afe0f55be13cfb90f1496c031a24fb` on `research/standard-feld-contract` |
| Rig baseline | `9e37756758e1d59eeb7b3b8a01dd477a66dd9309` |
| Profile | `standard-feld-wsprry-v1` |
| Fixture schema and set | `standard-feld-fixture-v1`; `standard-feld-exact-asset-v1` |
| Font asset | `wsprry-standard-feld-radiolib-5x5-v1` |
| Font SHA-256 | `025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227` |
| Messages SHA-256 | `6ce3ec82b97af539c3188df1fd8d6f53b96e25c9c3fb97223964ac89f5c8f003` |
| Fixture manifest SHA-256 | `e38202653fb025e5ee41285804d8840360c6fc8ff6e7ae1ad4095338d07d59ac` |
| Applications | fldigi 4.2.12; xfhell 3.5.2 |
| Corrected container image | `sha256:f099bf9bd4bc5bd20997f57eb42d261826b9a68c34bd68f150ce4c93bb18b1e3` |

The rig's committed `phase-3-results.md` still labels its evidence revision `UNCOMMITTED`. That internal label is stale after publication. This product record does not rely on it for identity; it cites the verified rig commit above.

## Fixture and application adapter

The scored fixture corpus is:

`HELL TEST 0123456789 DE WSPRY WSPRY 73`

It contains 3,920 binary physical positions at exactly 245 positions/s and lasts exactly 16 seconds. The deterministic unwrapped 48 kHz PCM file has SHA-256 `2e2368ca3782ecb0b3086984adf104dbdf6db2ecbfbba40210c487a36cdf07b5`.

The rig's `standard-feld-application-audio-adapter-v2` maps logical on positions to a 1,500 Hz audio sine, explicitly configures each receiver for that adapter tone, and prepends a separately identified 0.4-second 1,500 Hz acquisition mark for application trials. The resulting 16.4-second application input has SHA-256 `7cc5f7954e620b6ba4613afa05ff9128d43553b0816384cd605afa950bd4cf64`.

The adapter tone and acquisition wrapper are not normative RF parameters. The wrapper is not part of the Wsprry fixture, font, spacing policy, position stream, or reported 16-second transmission duration.

## Gate G — deterministic generation

`PASS`. At retained evidence directory `out/20260730T210504Z-gate-g/`, two fresh in-container generations were byte-identical. `reproducibility.txt` is empty, the complete evidence checksum index verifies, and the pinned input hashes match the WsprryPi commit above.

## Historical Gate H and Gate I failure

The original scored clean run at `out/20260730T202509Z-gate-h/` failed: fldigi clipped the initial `H`, and xfhell showed input energy in its waterfall but no receive raster in all three repetitions. The original fixed impairment matrix at `out/20260730T202804Z-gate-i/` retained the same receiver split. Both checksum indexes still verify. The historical failure is preserved rather than overwritten by the correction.

## Corrective diagnosis

The preregistered matrix at `out/20260730T205603Z-corrective-diagnostics/` showed readable xfhell rasters with matched 500 Hz audio/receiver settings and matched 1,500 Hz audio/receiver settings. Phase reset and an additional two-second receive-settle interval did not repair the mismatched 1,500 Hz audio/500 Hz detector case.

The predeclared decision rule therefore yields:

`ADAPTER DEFECT CONFIRMED`

The original blank raster was a rig application-adapter defect, not evidence of a Wsprry position-stream or asset defect. Source inspection explains why an active waterfall was insufficient: xfhell feeds its waterfall independently from the configured Feld Goertzel detector. The controlled matrix, not source inspection alone, supplies the causal evidence.

The separately preregistered framing matrix at `out/20260730T210048Z-fldigi-framing-diagnostics/` found that two extra seconds of silence did not preserve fldigi's initial `H`, while a 0.4-second mark carrier did. Its disposition is:

`ACQUISITION WRAPPER REQUIRED`

This is an application-capture requirement only and does not revise `standard-feld-fixed-cell-spacing-v1`.

## Corrected Gate H — clean repetitions

`PASS` for the bounded exact-corpus receive F3 criterion. At `out/20260730T210530Z-gate-h/`, both applications rendered every required token readably and in order in three fresh-container receive repetitions each. The six screenshots were inspected again during product reconciliation; this was an additional analysis pass, not an independent human review.

Process completion was not acceptance. The visible acquisition mark was excluded from the corpus score.

## Corrected Gate I — characterization

At `out/20260730T210952Z-gate-i/`, the fixed 16-point matrix covered carrier offsets, position-clock offsets, polarity inversion, additive noise, forced-off dropouts, and leader/trailer truncation. All 32 receiver rasters were scored readable for the required corpus. The complete checksum index and opaque trial mapping verify.

This is one run per point and characterization only. It establishes no product tolerance, acceptance threshold, backend requirement, or RF guarantee.

## Gate C disposition

The Phase 3 exact-corpus objective passes, but the parent Gate C remains partially satisfied:

| Gate C obligation | Result |
| --- | --- |
| Named immutable receiver versions | Satisfied |
| Exact selected asset and protocol identity | Satisfied |
| Deterministic fixtures, manifests, settings, renders, and checksums | Satisfied |
| Repeated clean F3 corpus readability | Satisfied: three repetitions per receiver |
| Fixed impaired-condition characterization | Satisfied for the declared points; no threshold inferred |
| Both exact-contract application directions, where supported | Open; this phase injected Wsprry-derived audio into both receivers, while prior Gate F used application-native assets |
| Selected-repertoire and substitution-rule coverage | Open |
| F4 feasibility disposition | `NOT-ASSESSED` |
| Blind or independently repeated scoring | Open; one documented reviewer |

Independent review is evidence-strengthening and remains required for Gate C closure. It does not block Phase 4 design because no current result contradicts the frozen protocol or asset.

## Supported claim

The pinned `standard-feld-exact-asset-v1` interoperability corpus is dependably readable at application-level receive F3 in fldigi 4.2.12 and xfhell 3.5.2 through `standard-feld-application-audio-adapter-v2` in the contained no-RF rig.

## Explicit non-claims

This evidence does not establish:

- F4 exact transcription;
- complete repertoire or substitution behavior in either application;
- backend timing, jitter, cancellation latency, safe-idle behavior, or fault recovery;
- Raspberry Pi, GPIO, clock-generator, transmitter, or attached-hardware behavior;
- occupied bandwidth, sidebands, harmonics, spurs, frequency accuracy, RF compliance, or on-air performance;
- implementation authorization, product-contract completeness, release readiness, or deployment readiness.

## Phase 4 readiness

Phase 4 may inspect and design the full WsprryPi path from payload normalization through raster compilation, event planning, duration, scheduling, cancellation, backend execution, configuration, persistence, CLI, UI, status, tests, and documentation. It must determine from current source whether an existing backend can accept a bounded Standard Feld extension or whether a new backend is required.

Phase 4 must keep architecture feasibility separate from Gate D backend qualification, Gate E spectral/RF qualification, Gate F product/operator contract completion, implementation authorization, and Gate G release readiness.

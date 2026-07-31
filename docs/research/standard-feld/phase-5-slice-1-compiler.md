# Standard Feld Phase 5 Slice 1 production compiler

## Result

`PHASE 5 SLICE 1 PASS — IMMUTABLE PRODUCTION ASSET AND BACKEND-NEUTRAL STANDARD FELD COMPILER IMPLEMENTED AND VALIDATED`

This is a bounded compiler result. It does not enable Standard Feld in the parent application or either hardware backend, and it establishes no hardware, RF, operator, or release readiness.

## Scope and identities

- **EVIDENCE:** The implementation is committed in `WSPR-Transmitter` as `879aa9ff234641c5af0e650acf9aba10377a63b1` on `research/standard-feld-compiler` with message `Implement the Standard Feld production compiler`.
- **EVIDENCE:** WsprryPi records that exact submodule revision in `b07514247572f3456e66b4f1e061e8de7c20a010` on `research/standard-feld-design` with message `Advance WSPR-Transmitter for the Standard Feld compiler`.
- **EVIDENCE:** `WsprryPi-UI` remains unchanged at `bbb90ac77775f957e0de1175325b21e1a393f082`.
- **EVIDENCE:** The interoperability rig remains unchanged at `088bbaecb1afe0f55be13cfb90f1496c031a24fb`.
- **DESIGN DECISION:** This slice contains only immutable production asset data, a first-class transmitter payload/mode, backend-neutral compilation, progress metadata, focused tests, asset verification, license retention, and transmitter developer documentation.

## Implemented contract

### Immutable asset

The transmitter now retains the production table for:

- profile `standard-feld-wsprry-v1`;
- asset `wsprry-standard-feld-radiolib-5x5-v1`;
- canonical asset SHA-256 `025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227`;
- RadioLib source commit `0795caa41c6350a2f862137cfc22528c2aaad2bc`;
- RadioLib source-file SHA-256 `44e1e4fd22d130d018e8e02745845fe2cf059eb6730a813190f8a2b30486c3cb`.

The checked-in table covers all 64 stored glyphs. A deterministic checker compares it with the canonical research JSON. The byte-identical MIT license is retained with SHA-256 `025378110a5679f82e8a59c19cf91b8ed760dc8752e5504596ef0c0592b8a3e8`.

### Payload and validation

`TransmissionMode::STANDARD_FELD` and `StandardFeldPayload` make Standard Feld a first-class raster mode rather than a Morse variant. The payload carries message, carrier frequency, and the immutable profile identity; it exposes no dot length, shift, gap, fade, row rate, font, or scan-order tuning.

Validation normalizes ASCII lowercase to uppercase, accepts stored ASCII `U+0020` through `U+005F`, rejects an empty message and all other bytes atomically, and performs no substitution. Complete validation occurs before plan construction.

### Raster and timebase

The compiler preserves every accepted space, adds one blank leader and trailer cell, traverses seven columns left to right, and traverses 14 physical positions per column bottom to top. Logical `1` becomes carrier RF-on intent and `0` RF-off intent.

Every boundary is calculated from its absolute integer position index at exactly 245 positions/s using integer round-half-up of `n × 1,000,000,000 / 245`. Per-position rounded durations are not accumulated. For `C` normalized characters the plan contains `98 × (C + 2)` events and lasts exactly `2 × (C + 2) / 5` seconds.

### Events and progress

- **DESIGN DECISION:** Retain one event per physical position. Coalescing was rejected for this slice because explicit events preserve every frozen cancellation boundary and make character, cell column, physical position, and absolute position directly recoverable.
- **EVIDENCE:** Optional raster progress metadata leaves existing modes unchanged and avoids fabricating raster coordinates for them.
- **EVIDENCE:** The blank trailer makes the final event RF-off.
- **INFERENCE:** The shared event vocabulary represents the frozen raster without a new hardware primitive.

## Backend boundary

No backend was enabled for Standard Feld. The Si5351 mode mapper explicitly returns unsupported, while the existing Raspberry Pi backend mode guard does not admit Standard Feld. No parent request path can construct or schedule the new payload.

- **DESIGN DECISION:** Compiler implementation and backend realization remain separate slices.
- **UNRESOLVED QUALIFICATION:** GPIO and Si5351 event realization, timing error, cancellation latency, physical safe idle, fault behavior, and spectral behavior remain untested.

## Raspberry Pi validation

Validation ran on `pi@wspr5` with WsprryPi `b075142` content, transmitter `879aa9f`, UI `bbb90ac`, and no transmitter binary, service, GPIO, I2C, audio device, or RF operation.

### Focused compiler test

`make standard-feld-test` reported:

```text
PASS: Standard Feld compiler contract
```

The test covers all 64 glyphs, lowercase normalization, the full accepted repertoire, all 13 frozen rejection cases, atomic rejection, no substitution, leader/trailer cells, preserved spaces, exact raster traversal, frozen message durations, progress metadata, terminal RF-off, and all 3,921 boundaries of the 3,920-position interoperability corpus.

### Asset and fixture validation

The production-table checker reported:

```text
PASS: immutable Standard Feld production table matches canonical asset
```

The retained license checksum matched. Fresh research validation reported:

```text
PASS: 8 deterministic fixture files match regeneration
PASS: independent fixture validation completed
64 glyphs; 9 message cases; input rejection; event expansion; timing; repeat; cancellation; safe terminal state
```

### Parent regression validation

The parent `semantics-test` built the changed transmitter production sources, including the execution-plan compiler, Standard Feld source, both backend translation units, and controller sources. It then passed dial-frequency semantics, UI/source regression, log timestamp, and update-check comparison tests. `non-wspr-repeat-policy-test` also passed.

Expected argument-parser rejection cases printed usage text during the semantics suite; the suite completed successfully. No transmission path ran.

### Standalone debug-build limitation

The transmitter submodule's standalone `make debug` stopped because it could not locate parent-owned `band_gpio.hpp` through its standalone include path. That dependency predates this slice and is already present in `wspr_transmit_types.hpp` at the Phase 4 baseline.

- **EVIDENCE:** The subsequent parent build located the parent header and compiled the affected transmitter sources successfully.
- **DESIGN DECISION:** Record the standalone include-boundary problem as build hygiene rather than expand this compiler slice to repair it.
- **UNRESOLVED QUALIFICATION:** The standalone transmitter debug target remains unverified until its parent-header dependency is resolved or its supported checkout/build contract is clarified.

## Result rationale

The bounded objective passes because the immutable asset, normalization, raster, event, timing, duration, progress, cancellation-boundary, and terminal-state contracts were directly exercised on the target Linux/Raspberry Pi host, and the parent build compiled all changed production translation units. The standalone target failure exposes no material uncertainty in the compiler behavior or changed production source; it is a separately documented pre-existing integration limitation.

This result does not convert source-level terminal RF-off intent into a physical safe-idle claim.

## Readiness boundary

| Readiness class | Result after Slice 1 |
| --- | --- |
| Gate A protocol contract | Satisfied |
| Gate B asset and licensing | Satisfied |
| Gate C interoperability | Partial; unchanged |
| Phase 4 architecture | Remains valid |
| Immutable production asset | Implemented and validated |
| Backend-neutral compiler | Implemented and validated |
| Parent mode/request integration | Not implemented |
| GPIO or Si5351 execution | Not enabled or qualified |
| Hardware/RF/spectral/operator/release | Not assessed or ready |

## Explicit non-goals

This slice did not implement parent `ModeType`, configuration, validation, scheduling, committed-request construction, cancellation orchestration, CLI, JSON/INI persistence, web API, UI, runtime status, service behavior, deployment, backend realization, hardware tests, audio generation, RF tests, spectral tests, or release packaging.

## Next bounded slice

Phase 5 Slice 2 should integrate the compiler into parent source only:

1. add parent `ModeType::STANDARD_FELD` and a dedicated in-memory config model;
2. validate message, carrier, profile, compiled duration, and repeat policy;
3. construct one immutable committed controller request;
4. reuse scheduling and cancellation orchestration without enabling backend execution;
5. expose a source-level runtime status model;
6. add focused parent tests and regressions.

CLI, persistence, web API, UI, services, hardware, RF, and backend enablement remain excluded unless source inspection proves a configuration-lifecycle dependency inseparable from safe parent mode introduction. Such a dependency requires a revised proposal, not silent scope expansion.


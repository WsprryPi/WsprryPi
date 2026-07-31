# Standard Feld Phase 4 product architecture

## Result

`PHASE 4 PASS — USE A FIRST-CLASS STANDARD FELD PAYLOAD AND COMPILER OVER THE SHARED EXECUTION-PLAN ABSTRACTION; NO NEW HARDWARE BACKEND; IMPLEMENTATION REMAINS UNAUTHORIZED`

Phase 4 is a design result, not an implementation or qualification result. It traces the frozen `standard-feld-wsprry-v1` contract through the current product and chooses the smallest architecture that preserves that contract without making Standard Feld a disguised Morse mode.

## Scope and baseline

- **EVIDENCE:** WsprryPi was inspected at `e224b5642c0306bbd5485f9d407677062472244f` on `research/standard-feld-design`.
- **EVIDENCE:** `src/WSPR-Transmitter` was inspected read-only at recorded submodule commit `868201f1004f77889052a64ca2d55b3f6bc1136c`.
- **EVIDENCE:** `WsprryPi-UI` was inspected read-only at recorded submodule commit `bbb90ac77775f957e0de1175325b21e1a393f082`.
- **EVIDENCE:** The sibling interoperability rig remained read-only at `088bbaecb1afe0f55be13cfb90f1496c031a24fb`.
- **EVIDENCE:** All submodules were initialized, clean, and at recorded commits before this documentation change.
- **DESIGN DECISION:** This phase changes research documentation only. It does not authorize application code, tests, configuration, CLI, UI, submodule, dependency, service, hardware, audio-device, or RF changes.

## Frozen inputs

The production design must consume, without reinterpretation:

1. profile ID `standard-feld-wsprry-v1`;
2. asset ID `wsprry-standard-feld-radiolib-5x5-v1` and its canonical checksum;
3. ASCII `U+0020`–`U+005F` repertoire;
4. lowercase-to-uppercase normalization, atomic rejection otherwise, and no substitution;
5. one blank-cell leader and trailer, fixed seven-column cells, and preservation of every space cell;
6. seven columns by 14 physical positions, columns left to right and positions bottom to top;
7. integer physical-position timebase at exactly 245 positions/s;
8. logical `1` as RF enabled and `0` as RF disabled at one carrier;
9. cancellation at every physical-position boundary and RF disabled after cancel, completion, fault, or idle;
10. exact duration `98 × (C + 2)` positions, equivalently `2 × (C + 2) / 5` seconds.

**DESIGN DECISION:** The canonical asset belongs in production source as immutable generated data with provenance, ID, checksum, and retained license—not as a runtime file that can drift independently of the executable.

## Current product seams

| Concern | Current seam | Standard Feld disposition |
| --- | --- | --- |
| Product mode | `ModeType` in `src/config_handler.hpp` | Add `STANDARD_FELD`; never alias QRSS, CW, or TONE |
| Persisted model | `ArgParserConfig`, JSON/INI conversion and candidate validation | Add a dedicated mode config containing message and carrier frequency; timing and font are profile constants |
| CLI | `src/arg_parser.cpp` mode and transient-message parsing | Add explicit Standard Feld mode/message/frequency options with atomic input validation |
| Scheduling | committed request boundary in `src/scheduling.cpp` | Build one immutable controller request and reuse current start-minute/start-second/repeat policy |
| Duration | `ExecutionPlanCompiler` summary used by repeat validation | Compile position counts exactly, then expose the plan summary; equality with repeat interval remains valid |
| Payload | `TransmissionPayload` in the transmitter submodule | Add `StandardFeldPayload`; do not flatten the raster into Morse timing fields |
| Compilation | `ExecutionPlanCompiler` | Add a raster compiler that emits frequency plus RF-on/RF-off events at exact position boundaries |
| Execution | shared `ExecutionPlan` and `RfEvent` | Reuse without adding a hardware backend |
| GPIO clock | `WsprRpiBackend` plan execution | Reuse generic plan path after source-level support and later physical qualification |
| Si5351 | `WsprSi5351Backend` plan execution | Reuse generic plan path after source-level support and later physical qualification |
| Stop/safe idle | interruptible backend waits, controller stop, backend cleanup | Preserve; qualify boundary latency and terminal RF-off later |
| Status | `WsprRuntimeStatusSnapshot` and WebSocket messages | Report profile, normalized message, character/column/position progress, duration, and terminal reason |
| UI | configuration and operation views in `WsprryPi-UI` | Add a clearly named mode and bounded fields; reuse existing Stop action and state hierarchy |

## Selected architecture

### Payload and immutable asset

Add `TransmissionMode::STANDARD_FELD` and a `StandardFeldPayload` to the transmitter library. The payload contains:

- raw operator message for validation and diagnostics;
- normalized message produced atomically;
- carrier frequency in Hz;
- profile ID and asset ID, either fixed by the compiler or verified against the only supported values;
- no editable dot length, shift, gap, fade, row rate, or font fields.

**DESIGN DECISION:** A v1 payload must not allow callers to override protocol constants. Extensibility comes from a later, separately versioned profile—not a bag of runtime tuning parameters.

The immutable table should be generated from the retained canonical JSON by a deterministic maintainer tool. The checked-in C++ representation must include the asset ID, source checksum, license notice linkage, and compile-time dimensions. A test must prove that regenerating it from the canonical research asset is byte-for-byte stable.

### Compiler ownership

The transmitter library already owns payload-to-`ExecutionPlan` compilation for WSPR, QRSS, FSKCW, and DFCW. It also owns the backend-neutral event vocabulary. Standard Feld compilation therefore belongs there, beside those compilers.

**DESIGN DECISION:** Extend the existing shared execution-plan abstraction. Do not add a parent-only compiler and do not introduce a third hardware backend.

The Standard Feld compiler must:

1. validate and normalize the whole message before emitting any event;
2. look up only the pinned immutable asset;
3. prepend and append the frozen blank cells;
4. traverse exact bits in frozen scan order;
5. coalesce adjacent identical RF states only when the resulting event retains exact start and duration position counts;
6. derive every boundary from integer position indexes and convert `n / 245` seconds to nanoseconds using the frozen round-half-up rule;
7. set event metadata sufficient to recover normalized character, cell column, and physical position;
8. end with RF disabled and a summary matching fixture counts and duration.

**DESIGN DECISION:** Extend `RfEvent` metadata with a mode-neutral progress locator rather than overloading `message_char_index` with undocumented column/row encodings. Existing modes may leave the new fields unavailable.

### Request and scheduling flow

The parent application should mirror the current committed non-WSPR flow:

```text
operator/config/CLI
  -> candidate configuration validation
  -> StandardFeldPayload request snapshot
  -> exact ExecutionPlan compilation and duration check
  -> selector GPIO preparation
  -> committed controller request
  -> selected existing backend
  -> interruptible event execution
  -> RF-off cleanup and terminal status
```

The committed request must capture message, carrier, backend/output selection, calibration, policy, schedule slot, and origin. Reloads after commitment apply only to a later request. No backend may read mutable global message or timing state during execution.

**DESIGN DECISION:** Reuse `Start Minute`, `Start Second`, and `Repeat Minutes`. A compiled duration equal to the repeat interval remains valid; a greater duration is rejected before persistence or execution. There is no implicit truncation, overlap, queueing, or skipped trailing raster.

**DESIGN DECISION:** Direct CLI repeat and persisted scheduling are separate operator intents, as they are today. Both compile the same immutable request; neither receives a distinct raster implementation.

### Cancellation and failure

- Stop before launch cancels the committed request and never enables RF.
- Stop during execution is observed through the existing interruptible backend path.
- The backend must disable RF before reporting cancelled, completed, or faulted.
- Selector GPIO, LED, and amplifier cleanup follows current orchestration ownership.
- Status must distinguish rejected input, cancelled-before-start, cancelled-in-progress, backend fault, and successful completion.

**UNRESOLVED QUALIFICATION:** Source structure supports prompt interruption, but neither exact physical-position boundary latency nor physical RF-off latency is established. Those are Gate D measurements.

## Backend decision

### Candidate 1: extend the Raspberry Pi clock path only

This is feasible because the GPIO backend already consumes plan events and explicitly handles RF-on/RF-off for timed modes. It would, however, make the product contract backend-specific and postpone discovering whether the shared abstraction is genuinely portable.

Disposition: **not selected as the architecture**, but acceptable as the first bounded backend implementation/qualification slice.

### Candidate 2: add a new Standard Feld hardware backend

This would duplicate scheduling, stop, safe-idle, frequency, calibration, and hardware ownership already expressed by the current two backends. Standard Feld is an event compiler, not a new oscillator device.

Disposition: **rejected**.

### Candidate 3: extend the shared execution-plan abstraction

Both existing backends already accept ordered events with offsets, durations, frequency, RF state, envelope data, stop checks, and terminal cleanup. Standard Feld requires no new physical output primitive beyond exact carrier on/off intervals.

Disposition: **selected**. Add a first-class payload/compiler and only the minimal mode-neutral progress metadata needed by status. Backend changes should be limited to removing mode whitelists or compatibility assumptions that prevent generic on/off plans.

**UNRESOLVED QUALIFICATION:** Source compatibility is not Gate D. Each enabled backend still needs dry-run/logic-level timing, cancellation, safe-idle, and fault-path evidence. Si5351 I2C update behavior and GPIO DMA/event-boundary behavior must be measured independently.

## Configuration and persistence contract

Proposed public configuration shape:

```json
{
  "Operation": { "Mode": "STANDARD_FELD" },
  "Standard Feld": {
    "Message": "CQ DE WSPRY",
    "Frequency": 14096900.0,
    "Profile": "standard-feld-wsprry-v1"
  },
  "CW": {
    "Start Minute": 0,
    "Start Second": 5,
    "Repeat Minutes": 10
  }
}
```

The persisted profile is explicit for auditability but only the one compiled profile is accepted. The font asset is implied by that profile and reported read-only; it is not an operator selection.

Validation order is atomic: schema and finite frequency, complete-string normalization/repertoire validation, plan compilation, duration/repeat validation, then semantic hardware validation. A failed candidate remains an editable UI draft but does not replace active/persisted configuration.

**DESIGN DECISION:** Standard Feld gets its own JSON/INI section. Reusing `CW.Message`, `CW.Dot Seconds`, gaps, shift, or fades would expose invalid controls and create silent coupling to Morse behavior.

## CLI contract

The implementation slice should define one persisted selection and one explicit transient form, following current conventions:

- `--mode STANDARD_FELD` with persisted configuration;
- `--standard-feld-message TEXT`;
- `--standard-feld-frequency HZ`;
- existing `--repeat` for a direct transient request.

The message and frequency transient options are a complete pair. Partial combinations fail before transmitter initialization. Unsupported input reports the first code point and allowed repertoire without substitution. Help text states the fixed profile, computed duration, and RF-output warning consistent with other transmit modes.

## Operator UI contract

The UI remains a restrained bench-instrument interface for technical radio operators. Phase 4 changes no UI files, but the implementation contract is:

- Add **Standard Feld** as a peer mode, not under a label that implies Morse/CW.
- Show only Message, Carrier Frequency, schedule, repeat interval, backend, and existing safety controls.
- Display profile and font asset as read-only technical identity in a secondary details area.
- Show normalized preview, character count, exact duration, and repeat-policy result adjacent to Message/Repeat controls before activation.
- Keep invalid drafts locally visible with a concise corrective error; do not silently persist or activate them.
- In Operate, show runtime mode, normalized message, current character/column/position when available, elapsed/total positions, next scheduled start, backend, carrier, and terminal reason.
- Keep the existing red Stop action in the current-state area. Stopping must not alter the persisted mode or message unless the operator separately edits configuration.
- Preserve identical information architecture in light and dark themes, keyboard operation, visible focus, and live-region state announcements.

**DESIGN DECISION:** Do not render a decorative bitmap preview in the first implementation slice. A truthful preview requires exact asset rendering, accessible text equivalence, responsive layout, and its own source-regression coverage; it is optional after the core operator contract works.

## Diagnostics and observability

At minimum, logs and runtime status should expose:

- request ID, origin, mode, profile ID, asset ID/checksum prefix;
- raw/normalized character counts without logging message content at debug-disabled defaults;
- total positions, total duration, event count, carrier, backend/output, and calibration snapshot;
- scheduled and actual start timestamps;
- current normalized character index, cell column, physical position, and event index;
- stop requested/observed timestamps and terminal RF-off acknowledgement where the backend can report it;
- completion, cancellation, validation failure, or backend fault reason.

**DESIGN DECISION:** Status fields are additive and unavailable values are explicit, not fabricated as zero-valued progress.

## Test architecture

### Compiler and asset tests

- canonical asset checksum/provenance and license presence;
- byte-stable production-table regeneration;
- all 64 glyphs against frozen raster fixtures;
- lowercase normalization, accepted boundary characters, all negative input fixtures, no substitution, and atomic rejection;
- leader/trailer, repeated spaces, empty-message policy, scan order, paired production geometry, and synthetic single-position mechanics;
- exact expanded and coalesced events against fixtures;
- integer boundary conversion, event offsets, summary counts, and exact duration;
- cancellation fixture boundaries and terminal RF-off event/state.

### Parent integration tests

- JSON and INI round trips, defaults, migrations, and rejection without persistence;
- CLI complete/partial/conflicting options and help text;
- committed-request snapshot isolation from reloads;
- repeat equality accepted and overflow rejected in CLI, config load, and web patch paths;
- selector GPIO, LED, amplifier, stop, completion, and fault cleanup;
- runtime status/WebSocket progress and terminal states;
- no regressions to WSPR, tone, QRSS, FSKCW, or DFCW routes.

### Backend qualification tests

- backend compile result preserves all required transitions;
- dry-run trace matches the frozen event fixture;
- observed event-boundary error distribution and cumulative duration error;
- stop at representative early/middle/final position boundaries;
- RF disabled on normal completion, stop, injected failure, and process shutdown;
- separate GPIO-clock and Si5351 results with declared equipment and tolerances.

**UNRESOLVED QUALIFICATION:** Acceptance tolerances for event jitter, cumulative timing error, cancellation latency, and spectral behavior must be approved before Gate D/E execution. Phase 4 does not invent passing numbers.

## Documentation contract

Implementation must update operator and developer documentation to state:

- exact supported profile/asset and repertoire;
- lowercase normalization and atomic rejection behavior;
- duration formula and scheduling rule;
- carrier/on-off semantics, backend prerequisites, and safe-stop behavior;
- configuration, CLI, UI, status, and recovery examples;
- explicit separation among implemented behavior, Gate C remainder, backend qualification, RF/spectral qualification, and release readiness.

## Bounded implementation slices

No slice is authorized by this document. If implementation is approved, use these review gates:

1. **Production asset and compiler:** transmitter-submodule table, payload, compiler, exact fixture parity, no backend execution changes.
2. **Generic plan/backend enablement:** remove mode-specific blockers, add dry-run traces and safe-idle tests; target GPIO first while retaining the shared contract.
3. **Parent model and committed request:** mode/config/validation/duration/scheduling/cancellation/status in WsprryPi; no UI.
4. **CLI and persistence:** JSON/INI round trips, transient CLI, migration and negative cases.
5. **Operator UI:** configuration and Operate surfaces following the contract above; source and interaction regression coverage.
6. **Gate D qualification:** GPIO and, if offered, Si5351 timing/cancellation/safe-idle measurements with no RF antenna path.
7. **Gate C completion and Gate E/F planning:** finish outstanding interoperability obligations, then separately authorize spectral/RF and operator qualification.

Each submodule change must be reviewed and committed in its own repository before the parent pointer is advanced. Each slice stops for diff and evidence review before commit or push.

### Phase 5 parent source seam

The first parent-integration slice deliberately stops before operator or backend
enablement. `ModeType::STANDARD_FELD` and its dedicated configuration exist only
in memory; CLI, INI, JSON, web, WebSocket, and UI selection or persistence do
not recognize the mode. Parent validation delegates normalization, repertoire,
raster compilation, and exact duration to the committed transmitter compiler.

A Standard Feld controller request may be committed only through the
test-execution-suppressed source seam. That snapshot owns the message, carrier,
fixed profile, output selection, calibration, policy, and Standard Feld-specific
metadata. Source-level scheduling generation, cancellation, and additive status
totals can therefore be tested without selector GPIO preparation, transmitter
dispatch, or backend acceptance. Progress coordinates remain explicitly
unavailable until a later authorized runtime seam can report `RasterProgress`;
neither existing hardware backend is enabled by this parent slice.

## Readiness assessment

| Question | Result |
| --- | --- |
| Frozen contract maps to current product seams | PASS |
| Payload/compiler ownership selected | PASS |
| Hardware backend strategy selected | PASS — shared plan, no new backend |
| Configuration/CLI/UI/status/test contracts defined | PASS |
| Production implementation authorized | NO |
| Existing backends physically qualified for Standard Feld | NO |
| Gate C complete | NO — remains partial |
| RF/spectral, operator, or release ready | NO |

## Phase 4 conclusion

`PHASE 4 PASS — ARCHITECTURE SELECTED; FIRST IMPLEMENTATION SLICE MAY BE PROPOSED FOR SEPARATE AUTHORIZATION; GATES C–G REMAIN OPEN AS RECORDED`

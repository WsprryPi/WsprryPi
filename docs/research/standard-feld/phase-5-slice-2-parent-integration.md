# Phase 5 Slice 2: parent source integration

## Result

`PHASE 5 SLICE 2 PASS — PARENT STANDARD FELD MODE, VALIDATION, REQUEST, SCHEDULING, CANCELLATION, AND SOURCE STATUS INTEGRATED WITHOUT BACKEND ENABLEMENT`

## Scope and identity

This record reconciles the bounded parent implementation committed as WsprryPi
`51af62b72c1018e3e02d7a491e0339283fcde899` (`Integrate the Standard Feld
parent source seam`). The reviewed parent records transmitter
`879aa9ff234641c5af0e650acf9aba10377a63b1` and UI
`bbb90ac77775f957e0de1175325b21e1a393f082`; neither submodule changed in this
slice.

The slice adds an internal parent model and a test-execution-suppressed path for
validating, compiling, committing, scheduling, cancelling, and inspecting a
Standard Feld request. It does not make Standard Feld operator-selectable and
does not enable either hardware backend.

## Implemented behavior

**EVIDENCE:** The parent now has `ModeType::STANDARD_FELD` and a distinct
`StandardFeldModeConfig` containing message, carrier, and the fixed v1 profile
identity. Copy and assignment preserve that model independently of the Morse
modes.

**EVIDENCE:** Parent candidate validation delegates normalization, repertoire
checking, raster compilation, and exact duration to the transmitter
`ExecutionPlanCompiler`. Empty or unsupported input, invalid or non-finite
carrier values, and an unsupported profile are rejected atomically. A compiled
duration equal to the repeat interval is accepted; overflow is rejected with
Standard Feld-specific corrective guidance.

**EVIDENCE:** A valid suppressed commitment snapshots its schedule, payload,
backend and output choice, GPIO, PPM correction, execution policy, and metadata
in an immutable controller request. A successful commit advances the scheduler
generation once. A rejected candidate neither advances the generation nor
replaces the previously committed request or status.

**EVIDENCE:** Source-level cancellation invalidates pending scheduling, clears
the committed Standard Feld request and compiled plan, and reports the terminal
reason `cancelled`. It does not claim a measured physical stop latency.

**EVIDENCE:** Internal runtime status exposes the frozen profile and asset
identity, raw and normalized character counts, total physical positions, and
exact compiled duration. Current raster progress is explicitly unavailable
because no executing backend reports it in this slice.

## Enforced exclusions

**EVIDENCE:** JSON, INI, web patch, and CLI mode parsing do not recognize
`STANDARD_FELD`; serialization does not persist it as a selectable mode.
WebSocket source contains no Standard Feld status fields, and the UI submodule
is unchanged. Standard Feld therefore remains unavailable through CLI, INI,
JSON, web, WebSocket, and UI surfaces.

**EVIDENCE:** The commitment helper rejects use unless scheduler execution is
suppressed for testing. Under suppression it does not prepare selector GPIO,
assert LED or amplifier paths, or invoke transmitter execution. The Raspberry
Pi backend retains its existing supported-mode guard, and the Si5351 planner
explicitly rejects `STANDARD_FELD`.

**DESIGN DECISION:** Treat the suppression requirement as the hard boundary of
this slice. Internal integration evidence must not be interpreted as backend,
hardware, RF, operator, or release enablement.

## Validation evidence

The following targets passed independently on `pi@wspr5`:

- `make standard-feld-parent-integration-test`
- `make semantics-test`
- `make non-wspr-repeat-policy-test`
- `make selector-shutdown-cleanup-test`
- `make qrss-execution-regression-test`
- `make wspr-tone-regression-test`
- `make debug` (compiled and linked only; the binary was not run)
- `python3 docs/research/standard-feld/tools/generate_fixtures.py --check`
- `python3 docs/research/standard-feld/tools/validate_fixtures.py`
- `git diff --check`

The fixture checks matched all eight deterministic files. Independent
validation covered 64 glyphs, nine messages, rejection, timing, repeat,
cancellation, and terminal-state cases. Existing tests emitted expected host
environment warnings for unavailable system bus, Chrony, and
`/dev/gpiochip`; the named targets nevertheless passed without operating
hardware.

### Pre-existing guarded-mode regression

`make guarded-mode-change-persistence-test` fails with exit code 2 and the
exact exception `Configuration update rejected: QRSS payload message is
empty.` The same target compiled, linked, and failed with the same exception in
the retained isolated baseline worktree
`/tmp/wsprrypi-b07ff55-attribution` at parent
`b07ff55efad0107b09c8c4ff9234869c8c4052e6`, with the same recorded recursive
submodule revisions used by Slice 2.

**EVIDENCE:** Exact reproduction at the clean predecessor attributes this
failure to code predating Slice 2. The temporary baseline worktree was retained
and remained clean except for ignored build products.

**DESIGN DECISION:** Do not repair the unrelated QRSS behavior in this slice.
An attempted web-path workaround was removed because it would have changed
QRSS, FSKCW, and DFCW behavior outside the approved Standard Feld boundary.

**UNRESOLVED QUALIFICATION:** The guarded-mode regression remains repository
debt. This record does not claim that every regression target passes.

## Safety and readiness boundary

No transmission, tone, GPIO, I2C, audio, RF, service, installation, or
privileged runtime operation was performed. Physical timing, cancellation
latency, RF-off behavior, backend suitability, spectral behavior, operator
workflow, and release readiness remain unqualified.

**INFERENCE:** The source seam is sufficient to proceed to a separately
authorized backend-plan slice because request and plan identity can now cross
the parent boundary without exposing an unusable operator mode. It is not
sufficient to expose persistence or operator controls because no backend can
yet accept or dry-run the plan through an enabled runtime route.

## Recommended next bounded slice

Phase 5 Slice 3 should enable Standard Feld plan acceptance in the GPIO backend
only behind a non-transmitting dry-run/test seam. It should prove an exact event
trace, normal completion, stop, injected-fault, and safe-idle intent without
touching physical GPIO or launching a transmitter. Si5351 enablement, physical
timing and cancellation measurement, CLI/INI/JSON/web persistence, WebSocket,
UI, operator documentation, and RF work remain separate authorizations.

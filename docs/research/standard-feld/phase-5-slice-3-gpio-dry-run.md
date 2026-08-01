# Phase 5 Slice 3: GPIO non-transmitting dry-run

## Result

`PHASE 5 SLICE 3 PASS — GPIO BACKEND STANDARD FELD PLAN ACCEPTANCE AND NON-TRANSMITTING DRY-RUN TRACE IMPLEMENTED WITHOUT HARDWARE ENABLEMENT`

## Scope and identity

This record reconciles transmitter
`31ccc45e8e85b3ff1686fb2541298e9153200324` (`Add a non-transmitting
Standard Feld GPIO dry-run`) and parent
`c55a2851d07012ff2631b41332756a5854055316` (`Integrate the Standard Feld
GPIO dry-run seam`). The parent records UI
`bbb90ac77775f957e0de1175325b21e1a393f082` and the new transmitter revision.
No other submodule changed.

The slice adds a pure, backend-adjacent interpreter that proves how a compiled
Standard Feld plan would be understood by the Raspberry Pi GPIO backend. It is
test-only: it does not enable the production backend, construct hardware, or
expose the mode to an operator.

## Architecture and ownership

**EVIDENCE:** The interpreter header and implementation live under the
transmitter `tests/` directory and in the `wsprrypi::testing` namespace. The
focused target compiles them explicitly with the authoritative compiler and
Standard Feld helper. Ordinary parent and transmitter source discovery does
not include them.

**EVIDENCE:** The interpreter accepts only a nonempty `STANDARD_FELD` plan
targeting `RPI_CLOCK_GPIO`. It consumes the compiler's `ExecutionPlan` directly
and neither normalizes input nor performs glyph lookup, spacing, raster
generation, or duration compilation.

**DESIGN DECISION:** Retain the transmitter compiler as the sole owner of the
profile, normalization, repertoire, raster, spacing, and duration contract.
The dry-run seam validates and copies compiled intent; it is not a second
compiler or execution engine.

## Atomic plan validation

**EVIDENCE:** Validation completes before any plan-event record is emitted. It
requires:

- a positive finite reference carrier and a matching carrier on every event;
- summary event count equal to the plan event count;
- complete 98-position cells containing one leader, at least one message cell,
  and one trailer;
- progress metadata on every event;
- contiguous absolute position equal to event index;
- column and physical-position values in range and exactly derived from the
  absolute position;
- leader, message, and trailer kinds in their required cells;
- normalized character index `-1` for leader and trailer;
- message character indexes beginning at zero and advancing once per cell;
- agreement between event and raster character indexes;
- exact offset and duration at the 245-position/s timebase;
- agreement between RF event type and RF intent;
- terminal RF-off intent; and
- summary duration equal to the exact final boundary.

Malformed plans return one terminal safe-idle record and no partial plan-event
trace.

## Exact trace evidence

The focused test independently constructs the frozen single-`A` expectation
from a blank leader cell, the retained glyph raster, and a blank trailer cell.
It does not derive the expected bits from the plan being tested.

**EVIDENCE:** The passing trace contains:

- 294 physical-position events: 98 leader, 98 glyph, and 98 trailer;
- one following terminal safe-idle record;
- exact original sequence and `RasterProgress` identity;
- exact integer round-half-up offsets and durations derived from `n / 245`;
- exact 14,096,900 Hz carrier intent;
- exact RF-on and RF-off intent from the independent raster; and
- exact total duration of 1.2 seconds.

No event is dropped, reordered, merged, quantized, approximated, or
regenerated.

## Cancellation, failure, rejection, and safe idle

**EVIDENCE:** Cancellation and injected failure accept logical boundaries from
zero through the event count, inclusive. Tests cover boundaries 0, 147, and
294. Initial and middle termination stop before the requested position and
report the preceding completed and requested pending positions. Final-boundary
termination reports the final position completed and no pending position.

Final-boundary failure remains `INJECTED_FAILURE`, not completion. Out-of-range
positions and simultaneous cancellation/failure requests reject atomically.
Completion, cancellation, injected failure, and rejection all end with an
explicit RF-off safe-idle record whose event index is absent.

**UNRESOLVED QUALIFICATION:** These are deterministic logical boundaries. They
do not measure physical cancellation or RF-off latency.

## Exact timing and representability

**EVIDENCE:** Boundary conversion separates quotient and remainder by 245,
checks whole-second multiplication, rounds the bounded remainder half up, and
checks final nanosecond addition. Tests accept the largest representable
position and reject its successor and `UINT64_MAX` without unchecked
position-times-one-billion arithmetic.

## Review history

The first read-only review found no Critical or High issue but required six
corrections:

- complete `RasterProgress` structural validation;
- malformed-progress and valid-transition test coverage;
- exclusion of the test-only interpreter from production build graphs;
- final-boundary and invalid/conflicting cancellation/failure semantics;
- complete focused-target header dependencies; and
- overflow-safe exact boundary conversion.

The correction pass moved the seam under `tests/`, expanded atomic validation
and behavioral coverage, defined every terminal option boundary, completed
dependencies, and replaced the overflow-prone arithmetic. A second read-only
review found no Critical, High, or Medium issue and concluded:

`REVIEW PASS — READY FOR TRANSMITTER COMMIT`

Two optional Low observations remain:

- `std::move` in the test-only interpreter relies on a transitive `<utility>`
  include; and
- one maximum-representation assertion compares a value with its own type's
  maximum and is harmless but redundant.

**DESIGN DECISION:** These Low observations do not invalidate the tested Slice
3 contract. Correct both at the start of the next transmitter code slice and
rerun the focused test rather than losing them from the follow-up record.

**FOLLOW-UP:** Slice 4 closed both observations in transmitter `5350dc0`: the
dry-run implementation now includes `<utility>` directly, and the redundant
maximum-representation comparison was removed. The focused dry-run regression
passed in the Slice 4 validation matrix.

## Pi validation evidence

The following targets and checks passed independently on `pi@wspr5`:

- `make standard-feld-gpio-dry-run-test`
- `make standard-feld-test`
- `make standard-feld-parent-integration-test`
- `make semantics-test`
- `make non-wspr-repeat-policy-test`
- `make selector-shutdown-cleanup-test`
- `make qrss-execution-regression-test`
- `make wspr-tone-regression-test`
- fixture generator `--check`
- independent fixture validator
- parent debug compilation and link without running the binary
- parent and transmitter `git diff --check`
- focused executable hardware-symbol audit
- production debug and release dry-run-symbol audits
- parent debug/release dependency-graph inspection
- transmitter debug/release dependency-graph inspection

The fixture checks retained all eight generated files and independently
validated 64 glyphs, nine messages, rejection, expansion, timing, repeat,
cancellation, and safe terminal state.

Expected environment diagnostics included unavailable system bus, Chrony
fallback, unavailable `/dev/gpiochip*` in existing guarded setup cases, and one
Make jobserver FIFO warning during a follow-up link. The named targets passed;
the diagnostics establish no hardware behavior.

The known `guarded-mode-change-persistence-test` failure was not run or changed.
Its clean-baseline attribution to the pre-Slice-2 QRSS empty-message path
remains repository debt. This record does not claim that every regression
target passes.

## Enforced exclusions and readiness boundary

**EVIDENCE:** The production Raspberry Pi backend still accepts only WSPR,
QRSS, FSKCW, and DFCW. Si5351 still explicitly rejects `STANDARD_FELD`. The
parent commitment seam still requires test execution suppression. No runtime
flag or production dispatch references the dry-run seam.

CLI, INI, JSON, web, WebSocket, UI, services, and operator documentation remain
unchanged. No physical backend, GPIO, DMA, PWM, clock, mailbox, I2C, Si5351,
selector, LED, amplifier, audio, tone, or RF operation occurred. The production
binary was compiled and linked but not run.

**UNRESOLVED QUALIFICATION:** Production GPIO execution, event timing and
jitter, cumulative duration error, cancellation and RF-off latency, spectral
and RF behavior, hardware suitability, operator workflow, and release readiness
remain unqualified.

## Recommended next bounded slice

**INFERENCE:** CLI, persistence, web, and UI exposure would present a mode that
the production backend still rejects. Physical qualification cannot measure the
final execution route until that route exists. The smallest useful next step is
therefore an internal production GPIO execution slice with fake or non-device
adapter coverage first.

**DESIGN DECISION:** Recommend Phase 5 Slice 4: integrate Standard Feld plan
execution into the production Raspberry Pi backend behind the still-internal
parent seam, proving event application, stop, failure, and safe-idle behavior
through fake/non-device dependencies. Begin by correcting the two retained Low
test observations. Keep physical device operation, Si5351, CLI/persistence,
web/WebSocket/UI, RF, and operator exposure separately unauthorized.

# Phase 5 Slice 4: internal Raspberry Pi execution

## Result

`PHASE 5 SLICE 4 PASS — INTERNAL RASPBERRY PI STANDARD FELD EXECUTION INTEGRATED AND VALIDATED WITHOUT PRODUCTION RUNTIME OR HARDWARE OPERATION`

## Scope and identity

This record reconciles transmitter
`5350dc034f3d2ce9181475a5baf681ca31325b1f` (`Integrate Standard Feld
Raspberry Pi execution`) and parent
`0835baf9f6bba75cff2e1888c3deabab490c981f` (`Integrate the Standard Feld
Raspberry Pi execution seam`). The slice began from parent `f00653a` and
transmitter `31ccc45`; the parent continues to record UI `bbb90ac`. No other
submodule changed.

Slice 4 adds an internal production Raspberry Pi route for validated compiled
Standard Feld plans. Qualification used the production execution, gate, and
progress cores with deterministic fake or non-device dependencies. It did not
run the production binary or operate attached hardware.

## Implemented execution contract

**EVIDENCE:** The Raspberry Pi backend delegates a `STANDARD_FELD` plan to a
production-owned event core. Complete production validation occurs before the
first adapter operation. The core retains the compiler's event order, carrier,
RF intent, absolute deadlines, and typed `RasterProgress` identity.

**EVIDENCE:** Once startup begins, completion, cancellation, watchdog fault,
adapter failure, and ordinary adapter exceptions converge on one outer
finalization and classification path. Terminal shutdown is attempted exactly
once. Cleanup failure remains distinct from the primary result, prevents a
successful classification when safe idle is unconfirmed, and does not erase a
latched watchdog safety fault.

**EVIDENCE:** Deadlines are absolute offsets from one monotonic origin. A late
boundary returns from its absolute wait without adding a relative delay or
moving later deadlines. Deterministic fake-clock tests establish logical
catch-up behavior only; they make no physical timing or jitter claim.

## RF transition and stop ownership

**EVIDENCE:** A generation-bound production gate serializes the last RF-on
authorization edge with user-stop and watchdog publication. Stop publication
that completes first denies later RF-on authorization; an RF-on callback that
already owns authorization completes before stop publication returns. Stale
generation operations and live-generation replacement are rejected.

The gate is deliberately short. It is not held across joins, cleanup,
allocation, progress reporting, configuration, callbacks other than the
authorized RF-enable edge, watchdog joins, or device waits. Cancellation and
watchdog state are checked at position boundaries, after carrier application,
and again at the final RF-on authorization point.

**UNRESOLVED QUALIFICATION:** This invariant prevents an RF-on edge from
starting after stop publication returns. It does not claim zero physical
latency for RF that was already enabled.

## Internal progress lifecycle

**EVIDENCE:** The production progress bridge installs an immutable snapshot of
generation, compiler-owned plan ID, declared total, and expected raster and
message identity. Reports supply generation, event index, and compiler-produced
progress, accept only the next matching active position, and occur only after
the corresponding position operation succeeds.

Lifecycle states are `EMPTY`, `ACTIVE`, `COMPLETED`, `CANCELLED`, `FAILED`, and
`WATCHDOG_FAULT`. Terminal transitions are generation-matched; stale reports
and transitions reject. Replacement is prepare-then-swap, so malformed input or
allocation failure preserves the prior complete snapshot. Completed history is
reserved during preparation, remains at stable capacity through all accepted
reports, and clears at the controlled lifecycle boundary after shutdown joins.

Focused-only allocation and mutex-ownership hooks proved failure recovery and
representative report, snapshot, terminal, clear, and replacement orderings.
Production debug and release binaries contained the production execution,
gate, and progress symbols but excluded hook controls, fake adapters, and the
dry-run interpreter.

## Review corrections retained in the final design

Review and correction passes materially strengthened the slice by:

- closing the cancellation-versus-RF-on race with the shared stop/watchdog
  gate;
- centralizing exactly-once finalization and post-cleanup watchdog
  classification;
- retaining primary and cleanup diagnostics separately;
- replacing mutable progress identity with an immutable generation-bound
  bridge and explicit terminal lifecycle;
- preserving prior progress state across malformed or allocation-failed
  replacement;
- proving pre-reserved report history and production/test symbol isolation;
  and
- removing hook-only typed arguments from non-hook compilation after a final
  review exposed that build-boundary defect.

The final comprehensive review found no Critical, High, Medium, or actionable
Low issue and returned `REVIEW PASS — READY FOR TRANSMITTER COMMIT`.

## Pi validation evidence

The following non-hardware targets and checks passed on `pi@wspr5`:

- Standard Feld compiler contract;
- GPIO non-transmitting dry run;
- Raspberry Pi execution core;
- RF execution gate;
- production progress bridge;
- parent integration;
- non-WSPR repeat policy;
- selector shutdown and cleanup;
- QRSS execution regression;
- WSPR tone regression;
- parent debug and release compilation and link without running either binary;
- parent and transmitter whitespace checks; and
- production-versus-focused symbol isolation audits.

The focused evidence included deterministic fake clocks and adapters,
allocation failure injection, exactly-once cleanup, exception and watchdog
classification, and controlled concurrency without ordering sleeps. The
selector target emitted expected unavailable system-bus and Chrony diagnostics
and passed without modifying a service or operating hardware.

## Enforced exclusions and qualification limits

The parent commitment boundary remains execution-suppressed. Standard Feld is
not selectable or persisted through CLI, INI, JSON, web, WebSocket, or UI.
Raster progress remains internal. Si5351 explicitly rejects Standard Feld.

No production binary was run. No GPIO, DMA, PWM, clock, mailbox, I2C, Si5351,
selector, LED, amplifier, audio, service, tone, transmission, or RF operation
occurred. This slice does not establish:

- physical GPIO timing, jitter, or long-duration stability;
- physical DMA, PWM, or GPCLK behavior;
- physical cancellation or RF-off latency;
- spectral purity or transmitted Feld-Hell readability;
- Si5351 compatibility;
- remaining Gate C interoperability evidence;
- operator readiness; or
- release readiness.

## Slice 3 maintenance closure

Slice 4 closed both optional Slice 3 Low observations. The test-only dry-run
interpreter now includes `<utility>` directly for `std::move`, and its focused
test no longer contains the redundant maximum-representation comparison. The
passing dry-run regression retained the accepted Slice 3 behavior.

## Recommended next bounded work

1. Separately authorize Raspberry Pi device qualification for the GPIO/DMA/
   clock route, beginning with no-RF or instrumented-safe verification.
2. Separately design, implement, and validate Si5351 Standard Feld plan
   mapping, timing, cancellation, shutdown, and device behavior. Keep it
   explicitly unsupported until that work passes its own gates.
3. Design persistence and operator surfaces only after a backend has passed its
   applicable device qualification.
4. Measure physical timing, cancellation latency, RF-off behavior, spectral
   behavior, and interoperability before any release-readiness claim.

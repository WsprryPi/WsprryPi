# Execute backend-derived Pi WTP capabilities on devel

## Objective

Correct the Pi WTP integration so the selected Si5351 backend exposes and executes
its existing WSPR, TONE, QRSS, FSKCW and DFCW capabilities, including qualified
amateur bands from 2200m through 2m. Do not introduce a second qualification table
or requalify the physical backend. CAPS describes backend capabilities; the
existing frequency policy and numerical planner still decide job admission.

## Implementation requirements

- Inspect and preserve the current devel tree. Reuse backend capabilities,
  frequency policy, planner and execution infrastructure.
- Remove the artificial 20m, ten-second, two-event WTP restrictions. Derive wire
  modes and numerical frequency limits from backend metadata. Retain the bounded
  WTP protocol limits (512 events, 24 hours) and explicit timing/cleanup limits.
- Translate contiguous finite WTP RF events into the selected backend execution
  plan, preserving mode, offsets, durations, RF states and requested frequencies.
  Reject unsupported modes, invalid timelines, bounds and policy violations.
- Obtain per-event realized frequencies from the backend's actual configured
  multi-tone planner; never infer multi-tone realization from independent TONE
  plans. Require caller consent for frequency adjustment.
- Preserve scheduled first-enable admission and observed launch, subsequent RF
  gating and frequency transitions, cancellation, ownership, local precedence,
  output-off confirmation, replay and recovery. Preparation must not transmit.
- Keep simulation explicit and hardware-free; do not add fallback backends.
  GPIO/RP1 server adapters are not part of this correction and must not advertise
  capabilities on an unavailable listener.
- Component changes in WSPR-Transmitter are limited to reusable capability,
  realization observation and scheduled execution hooks needed by the adapter.
  Preserve local execution behavior and independent component tests.
- Update misleading application UI guidance and current development docs. Keep
  historical acceptance records identifiable as historical. Update the authorized
  sibling operator docs behind the existing Pico documentation flag if needed,
  with separate commit and push boundaries.

## Validation and adversarial review

Exercise all five modes and the qualified band range, including a job longer
than ten seconds, more than two events, terminal off, malformed timelines,
unsupported modes, stale prepared state, adjustment consent, configuration
failure, late/unsynchronized start, cancellation between gated events, and
unconfirmed cleanup. Compare CAPS to the selected backend metadata and configured
planner results. Run focused parent and standalone component tests, portable
semantics, and relevant UI/doc checks. On native Linux use a fresh build; reuse
the authorized Pi/20m/SDR workflow for integration RF observation when available.
A corresponding SDR signal is the requested RF pass criterion; no full RF-chain
inventory is required. Report exact tested paths and any remaining limitations.

Perform an adversarial source/behavior assessment after implementation, repair
all material findings, rerun affected checks, and reassess. Record evidence and
remaining limitations. Review staged diffs, commit and push devel, verify remote
commit parity, then report.

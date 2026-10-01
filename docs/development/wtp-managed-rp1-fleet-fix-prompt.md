# Managed RP1 and live Fleet workflow execution prompt

## Objective

Work on `devel`. Resolve the two implementation blockers recorded in
[the concurrent Fleet report](wtp-concurrent-fleet-report.md): managed local RP1
WSPR preparation cannot bind its launch confirmation while the route is
inhibited, and Fleet Pause/Remove uses a browser-native confirmation that
blocks the live browser workflow. Preserve local priority and independent local,
inbound WTP and outbound Fleet control.

## Required implementation

1. Inspect the branch, complete working tree, affected component boundaries and
   existing RP1, scheduling, service and Fleet contracts. Preserve existing
   preparation and result artifacts.
2. Permit managed RP1 scheduling to prepare a finite request when its explicit
   transient launch confirmation is supplied. Apply the behavior consistently
   to WSPR, QRSS, FSKCW and DFCW. Keep local ownership, startup quiescence,
   managed reload, provider readiness, clock, route and output cleanup gates.
   Validate and bind the confirmation before committing a request; require the
   ordinary transmit gate after reconciliation. Missing or invalid confirmation
   must never authorize physical output. Do not add persistent INI authorization,
   automatic fallback, or a provider-inhibition bypass.
   Repeated Enable commits during an already enabled local schedule must retain
   effective local admission. Enable while a Test Tone or disabled schedule
   drains must continue to wait for cleanup.
3. Extend managed-service command validation narrowly to admit the canonical
   installed command with one validated RP1 confirmation argument. Retain
   rejection of alternate binaries, INI files, unsupported options, malformed or
   ambiguous confirmation and command chaining.
4. Use Impeccable and the incumbent Bootstrap patterns to replace Fleet Pause
   and Remove native confirmations with an accessible in-page dialog. Show the
   selected assignment and consequence; cancellation, Escape, backdrop, feature
   hiding and page departure must send no mutation. Preserve focus, prevent
   duplicate requests, and bind the confirmation to the revision reviewed by
   the operator. A conflicting revision must require refresh and review.
   Keep development visibility and independent persistence intact.
5. Add meaningful negative and positive regression tests at the parent and UI
   component boundaries. Review developer and operator documentation impact.

## Validation and live acceptance

- Run Python service-validator tests, focused scheduling/RP1 tests, portable
  semantics on macOS, and full hardware-disabled semantics on a Linux host with
  the required development headers. Run UI integration/source coverage and
  inspect desktop and mobile dialog rendering.
- Deploy validated source to wspr5 with rollback backups while local output is
  disabled and all remote assignments are paused. Preserve GPS/PPS, provider
  services, existing route reconciliation and the explicitly approved Fleet
  frequency override. Use the ordinary managed service for the concurrency
  retest. Bound the test and restore its configuration afterward.
  Verify the actual running process arguments, installed source/binary hashes,
  provider companion binding and Avahi advertisement/discovery support.
- Use the five existing physical targets (wspr1 GPIO4, wspr2 GPIO4, wspr4 GPIO4,
  Pico A GP2 and Pico B GP2), alongside wspr5's local RP1 GPIO20 schedule.
  Use the prepared 20m frequency separation and finite remote jobs. Record
  distinct job identities, simultaneous actual output-active observations,
  authoritative completion and output-off status. Demonstrate a normal local
  110.592-second WSPR frame, rather than treating Enable as transmission proof.
- Exercise actual browser Resume, canceled and confirmed Pause, and canceled
  and confirmed Remove against the installed controller. Preserve the existing
  assignments by using a disposable assignment for destructive workflow tests
  where appropriate. Capture desktop/mobile evidence.
- The currently connected receiver group is wspr1, wspr4 and Pico B. A stopped
  manual combiner rotation is necessary to observe wspr2, Pico A and wspr5.
  Ask for that change only after the software and managed-service preparation
  are ready. Record corresponding SDR signals as the RF acceptance criterion;
  do not require or claim characterization of the entire RF chain.
- Keep earlier failed attempts and successful evidence separate. Do not claim
  eight physical devices, frequency calibration, WSPR decoding, sustained soak
  or discovery topology acceptance from this bounded batch. List those wider
  campaigns explicitly if they remain outstanding.

## Review, delivery and report

Perform an adversarial review of authorization ordering, stale revisions,
duplicate actions, malformed service commands, restoration, actual source
deployment and evidence claims. Repair findings, rerun the relevant checks and
perform another adversarial assessment. Continue until actionable issues within
this scope are closed. Record any external/manual acceptance boundary honestly.

Review the complete and staged diff, include only approved source, tests,
documentation and compact evidence, and exclude credentials, full private
configuration archives and raw IQ. Commit and push `origin/devel`; independently
verify remote parity. Report implementation, exact checks and their outcomes,
live/browser/RF evidence, Documentation Impact, outstanding acceptance, branch,
working-tree state, commit and push result.

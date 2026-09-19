# WTP complete-plan backend

Phase 10 Slice 5 implements `WtpTransmitBackend` in
[`src/wtp_integration`](../src/wtp_integration/backend.hpp), using the existing
`ITransmissionBackend` interface and `TransmissionController`. It composes
WTP-Client Session with the execution-plan converter. Its explicit stream input
accepts the [USB CDC adapter](wtp-usb-cdc.md), [TLS network adapter](wtp-network.md), or an injected test transport.
The [production application](wtp-production-integration.md) now owns this backend
through a dedicated complete-job scheduler and worker. The reusable transmitter
component's generic factory still reports `BackendKind::WTP` unavailable: its
symbol-oriented construction cannot supply this parent-owned session lifecycle.

## Explicit lifecycle

A caller provides a `WtpHostClock` (production `WtpSteadyClock` or a virtual test
clock), SessionOptions with fresh session/owner IDs and expected WTP device ID,
and an explicitly selected, already opened ByteStream. USB selection must use
the USB adapter's path, serial, VID/PID and WTP-interface checks. No endpoint is
enumerated, opened or replaced automatically by the backend.

All calls and observations belong to one owner thread, except `stop()`, which
only sets an atomic cancellation flag. The clock and connected stream must
outlive the backend. Connect the backend only after USB opening reaches Ready;
disconnect it before closing/reopening/destroying the stream. The destructor
closes the local session without attempting remote cleanup. Explicit cleanup
and inspection of its result are required before destruction.

1. `connect(stream)` performs HELLO, expected device/boot identity validation,
   STATUS and CAPS. It never claims ownership, changes device time or aborts a
   job. Explicit same-session reconnect preserves uncertain transaction history.
2. `quiesceForStartup()` obtains fresh STATUS and accepts only unowned, inactive,
   empty or safely terminal state. It reports an observation; it does not force
   a foreign or standalone transmitter to stop.
3. `schedule(WtpPlanOptions)` supplies a fresh 32-hex job ID, absolute UTC start
   and maximum start uncertainty. The plan itself has no absolute UTC field.
   Scheduling resets the local stop signal only when previous work is cleaned
   up. A backend retains up to 4,096 used job IDs and then refuses further work.
   IDs must also be fresh across process lifetimes; this is not a durable ledger.
4. `configure(plan, inputs)`, or `TransmissionController::prepare`, validates
   the complete finite plan and frequency policy before CLAIM and LOAD. It
   requires `BackendKind::WTP`, zero host GPIO and electrical power controls,
   and no enabled RP1 development operation. WSPR encoded power metadata is
   separate from electrical output power. Conversion rejects unsupported
   calibration, envelopes, modes, limits and implicit continuous tone as
   described in the [conversion contract](wtp-execution-plan.md).
5. Call `execute` / `execute_prepared` **early**, while sufficient ARM lead time
   remains. It verifies the immutable RF job and plan/request identity, renews
   the acknowledged lease if needed, observes GET_CLOCK and submits ARM once.
   RP2350 owns all RF boundaries. The backend observes STATUS and renews the
   lease while waiting; it issues no per-symbol commands or inferred symbol
   progress. ARM acknowledgment alone is never reported as execution success.
6. Inspect the execution and cleanup results. The shared controller always calls
   cleanup. A direct backend caller must call it on configuration failure,
   execution failure, success or abandonment of a loaded plan. Cleanup uses fresh
   STATUS, only this client's tracked ABORT, and owned inactive RELEASE. When
   `execute` has just established the exact authoritative terminal/inactive state,
   cleanup reuses that proof and proceeds to RELEASE; it still requires the fresh
   post-RELEASE STATUS. Every other cleanup entry obtains fresh STATUS first.
   Repeated cleanup rechecks current state and cannot clear a latched fault.

After an acknowledged RELEASE, the follow-up STATUS may already show an
inactive job owned by another authenticated principal. Cleanup accepts that
handoff only when retained authoritative evidence still proves this backend's
own terminal job inactive and current device output is inactive. It never
ABORTs or RELEASEs the successor. Active output, fault evidence, uncertainty or
missing retained evidence remains a cleanup failure.

Generic `BackendCompileResult.adjustments` remains empty: the existing controller
interprets its first entry as a uniform frequency shift. Exact per-event WTP
nanohertz adjustments are instead retained by `adjustments()`, including after
cleanup until the next prepared job. They are checked against CAPS ranges and
host frequency policy before execution is enabled. The requested plan stays
unchanged. Parent runtime status consumes this explicit remote evidence.

## Clock and bounded observation

The device must already have an independently provisioned valid UTC clock.
GET_CLOCK observes it; this backend sends no Console clock-provisioning commands.
Host UTC scheduling, device UTC validity and RF calibration remain separate.

Before ARM, admission checks synchronization, requested/device uncertainty limits,
holdover age, minimum lead, maximum horizon, arithmetic overflow and the normative
one-second exclusion around a pending leap transition. Elapsed host time from
GET_CLOCK submission is conservatively included in lead and holdover checks.
Session validates the returned ARM clock and exact UTC-to-monotonic mapping.
A rejection or uncertainty never causes late execution, reload or rearm.

The backend advances I/O in bounded turns separated by waits of at most 10 ms.
Each transaction plus follow-up observation has the configured Session transaction
budget plus 2 seconds (10 seconds with defaults). Cleanup shares one such budget
across observation, ABORT and RELEASE. Completion monitoring ends at the estimated
job end plus that same observation allowance. This accommodates fragmented
responses and advisory-event reconciliation; it does not extend the RF job.
Stalled or regressing injected host clocks fail closed. OS wait/syscall scheduling
is not a hard real-time guarantee, and RF event timing never depends on these waits.

## Truthful outcomes and recovery

Completion requires matching device/boot/job evidence for Complete with both
job and current device output inactive. Cancellation requires the analogous
Aborted evidence. A completion/ABORT race can return successful completion;
Missed is an execution failure even if a stop request is concurrent. Active
terminal output and device faults block successful cleanup and future work.

Lost CLAIM, LOAD, ARM, ABORT or RELEASE replies remain unknown. No automatic
same-ID retry is used by this backend. Keep the backend/Session alive, explicitly
reopen the selected stream and call `connect` for read-only reconciliation, then
`cleanup`. That cleanup may abort the same owned tracked job; it never reloads,
rearms or aborts a foreign job. Missing job records or changed device/boot identity
cannot authorize resubmission. Closing USB cannot establish inactive RF output.
A prior expired lease does not imply that an armed/running job stopped.

There is no automatic reconnect or durable process-restart recovery. Parent
production integration supplies status and repeat/reload orchestration, blocks
new work while earlier output or cleanup is unresolved, and rejects host GPIO/
amp control for WTP. Advertised capabilities and the
external RF output class are not RF qualification. WTP modes/frequencies remain
untested in host qualification policy and require explicit experimental opt-in;
non-amateur frequencies require the separate existing opt-in too.

## Validation

Run from `src`, without physical hardware:

```sh
make wtp-backend-test wtp-protocol-test wtp-plan-test wtp-usb-test SUDO=
make wtp-backend-test SUDO= WTP_BACKEND_BUILD_DIR=build/wtp-backend-sanitized \
  WTP_BACKEND_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_BACKEND_LDFLAGS='-fsanitize=address,undefined'
make wtp-backend-interop-test SUDO= PICO_SOURCE=WTP-Client/build/pico-reference
make backend-profile-factory-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
make semantics-test-portable SUDO=
```

The optional interoperability target requires an existing source checkout at the
WTP-Client provenance pin; it verifies that pin and does not clone or install
anything. It compiles the actual Pico endpoint/job service in separate translation
units, with a software clock/engine and no SDK, USB or RF. The ordinary backend
tests use a scripted peer, actual framing/Session/converter/controller and virtual
time. CI runs ordinary WTP tests on macOS and Linux; local macOS results do not
establish a Linux runtime pass. The shared component controller and simulator tests
remain independently runnable from `src/WSPR-Transmitter/src`.

## Documentation impact and next slice

This guide, the saved [execution prompt](development/wtp-slice5-prompt.md), review
record and root/component progress links describe developer behavior. No operator
configuration or UI changed. The separate Wsprry_Pi_Docs
`Advanced_Operations/ini_configuration/transmitter_backends.md` and
`Advanced_Operations/timing_calibration.md` were reviewed and left unchanged.
Future authorized operator documentation must cover explicit Pico endpoint
selection, USB permissions, independent device UTC prerequisites, per-event
adjustments and unresolved-output recovery.

Slice 6 adds the [early scheduler and absolute-UTC handoff](wtp-scheduling.md).
[Production integration](wtp-production-integration.md) supplies status,
configuration and the Impeccable-reviewed gated UI. [Network integration](wtp-network.md)
extends the existing backend through TLS. Actual Linux USB/DTR/unplug and
firmware functional validation, process/service lifecycle, RF qualification and
release readiness require separate evidence and authorization.

## Extended finite jobs

The execution monitor derives its deadline from the admitted full job duration
and launch time with bounded 64-bit arithmetic. Transaction, progress and abort
budgets retain their existing short bounds. STATUS and acknowledged-lease renewal
continue throughout an hour-long job; there is no 110.592-second host cutoff.
The scripted-peer regression now completes a full virtual hour with continuing
renewals, aborts while Armed and aborts after 120 seconds Running. It verifies
one LOAD/ARM per attempt and inactive cleanup, and uses retained completion
records after RELEASE clears current state. This is hardware-free regression
evidence, not a physical hour-long acceptance result.

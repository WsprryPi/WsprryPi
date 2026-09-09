# Early WTP scheduling and UTC handoff

Phase 10 Slice 6 adds the parent
[`WtpScheduler`](../src/wtp_integration/scheduler.hpp). It connects the actual
shared `TransmissionRequest::slot`, compiler and transmission controller to the
[WTP backend](wtp-backend.md). This dedicated path dispatches preparation early;
it does not use the legacy transmitter's wait-until-start execution path.

The scheduler remains a typed parent API. The subsequent
[production integration](wtp-production-integration.md) owns it in a worker and
connects persisted configuration, early scheduling, completion dispatch and UI
status/recovery. Its timing and fault-injection seams remain typed test APIs;
the production settings do not expose them.

## Caller lifecycle

Construct `WtpScheduler` with a `WtpScheduleClock` and SessionOptions containing
fresh session/owner IDs and the expected WTP device ID. Connect it to an explicitly
selected, already opened ByteStream. For USB, the existing CDC adapter must first
verify the selected path, serial, VID/PID and WTP interface. No endpoint discovery,
USB open, Console access or implicit fallback occurs in this scheduler.

One owner thread calls `connect`, `submit`, `run`, `recover` and `disconnect`, and
reads reports/backend evidence. `run` waits and executes synchronously on that
owner; it creates no detached thread. Only `request_stop`, `invalidate_pending`,
the atomic `phase` observation and the copy-returning `status()` may run
concurrently with `run`. Submission and connection management must be serialized with these notifications. The
clock and stream must outlive the scheduler. Do not destroy it with `run` active.
Destruction closes the local session; explicit cleanup/recovery is still required
for remote output. A close is not an RF-off acknowledgment.

```cpp
// clock supplies monotonic time and a currently trusted host UTC observation.
wsprrypi::WtpScheduler scheduler(clock, session_options);
if (!scheduler.connect(selected_stream)) { /* report negotiation failure */ }
// request is a complete wsprrypi::TransmissionRequest with BackendKind::WTP.
// Its slot.start_time and signed slot.start_offset specify one exact UTC start.
if (scheduler.submit(request, fresh_job_id, max_uncertainty_ns, timing_policy)) {
    const auto report = scheduler.run(); // run on the sole session owner
    // Blocked requires explicit same-session reconnection and recover().
}
```

`submit` freezes the request, job ID, uncertainty and scheduling policy by value.
It preflights compilation, capabilities, finite-job limits and host frequency
policy without CLAIM or LOAD. Host GPIO/output selection and hardware-profile
inputs are rejected. WSPR requires **exactly one explicitly extracted frame per
slot**, because the shared compiler selects one frame from a larger payload.
Paired messages need separately submitted slot requests; no frame is implicitly
selected, split or dropped by this scheduler. Single-frame metadata may still
identify its place in a larger paired plan.

After successful cleanup, a repeat needs a fresh job ID and explicit new request.
No next slot is inferred or silently substituted. Up to 4,096 submitted job IDs
are retained, including pending requests cancelled before LOAD. Capacity
exhaustion fails closed. This in-memory protection is not a durable restart ledger.

## Absolute UTC and dispatch policy

`wtp_slot_utc_ns` converts `ScheduledSlot.start_time` plus its signed nanosecond
`start_offset` using checked integer arithmetic. It preserves subsecond offsets,
rejects pre-epoch base times, nonpositive resulting starts and overflow. A caller
can use an even-minute WSPR base and a one-second offset, matching the existing
WsprryPi scheduling convention; the scheduler does not change that choice.

Host and device UTC validity are independent. `WtpScheduleClock::utc_now_ns`
returns no value when host UTC is untrusted. `WtpSystemScheduleClock` requires an
explicit validity-provider callback; reading the system clock alone never
establishes synchronization. Wiring that provider into production clock policy
is future integration. Device time must already be independently provisioned;
the backend still checks GET_CLOCK and validates ARM's returned clock mapping.
No clock-setting or diagnostic Console operation is added.

The typed `WtpSchedulePolicy` defaults are:

| Field | Default | Meaning |
| --- | --- | --- |
| `preparation_ms` | 5,000 | Allowance for fresh status, CLAIM, LOAD and clock observation |
| `arm_submission_ms` | 1,000 | Reserved interval for submitting ARM before device minimum lead |
| `clock_step_tolerance_ms` | 100 | Maximum cumulative host UTC departure from anchored monotonic time |
| `maximum_wait_ms` | 86,400,000 | Maximum requested start distance; at most one day |

Dispatch time is requested UTC minus preparation allowance, ARM submission
allowance and negotiated minimum ARM lead. That combined interval must fit CAPS'
maximum ARM horizon. Requests already closer than the combined interval are
rejected at submission. After dispatch, normal wake-up/processing delay consumes
the preparation allowance; a request past the last preparation/ARM cutoff is
rejected, never shifted into another slot. Timing allowances are host policy,
not measured USB latency guarantees. Slow operations may still miss the window
and fail safely. The pinned fragmented-endpoint test uses an explicit 8,000 ms
preparation allowance while retaining the 1,000 ms ARM reserve.

Waiting uses at most 10 ms increments and is bounded by the requested slot. The
sole session owner refreshes STATUS every five seconds during this wait, keeping
the connection within the Pico's 30-second idle budget without early ownership
or job submission. Each read uses the normal bounded transaction deadline; stop
and reload are rechecked after that read completes. A failed observation blocks
new work until explicit recovery. Slow reads still consume the original slot's
preparation allowance and cannot shift its start.
Host UTC is checked against its committed monotonic anchor during waiting and
again at the backend's final ARM gate, after any status reconciliation. A host
clock jump, invalid observation, stalled/regressing time or exhausted submission
allowance prevents ARM. Clock-step tolerance cannot bypass the separate absolute
ARM cutoff. Once that local gate approves handoff, host wall-clock changes cannot
retime the remote job. Device clock admission and actual ARM rejection remain
authoritative. The report's `arm_handed_off` means the local gate approved the
submission; it is not proof of bytes delivered, acknowledgment or RF execution.

## Stop, reload and unresolved outcomes

Pending reload invalidation atomically competes with the transition into
preparation. If invalidation wins, the request is discarded with no CLAIM/LOAD.
Once preparation commits, later invalidation is deferred; the frozen request
finishes and its report identifies an observed deferred reload. The caller owns
application of that reload and creation of any next request. No mutable global
configuration is read while the job is being prepared or executed.

Stop remains distinct: before preparation it cancels the local pending request;
after commitment it signals the backend's bounded owned cleanup. A stop racing
with the backend's schedule-reset step is rechecked before LOAD. Terminal
completion/cancellation after submission uses the backend's matching inactive
output evidence, including completion-versus-ABORT races.

Reports separate Complete, Cancelled, Invalidated, Failed and Blocked. Cancellation
or invalidation before preparation makes no remote RF-output claim; inspect
`execution.cleanup_attempted` to distinguish local pending cancellation from
remote cleanup. A failed or unknown cleanup latches Blocked. New requests,
repeated run calls, stop and invalidation cannot clear it. Explicitly reopen the
same selected endpoint and call `connect`, then `recover`; successful authoritative
cleanup restores Idle. Connection alone does not clear Blocked. Changed identity,
missing evidence and latched safety faults cannot authorize a new job. Recovery
never reloads or rearms the failed request.

Backend transactions/cleanup retain their bounded observation budgets. Failed
controller preparation already attempts cleanup; the scheduler performs another
bounded cleanup observation to obtain a typed result. Thus a failed preparation
may consume two cleanup budgets. No unbounded retry or automatic reconnect exists.

## Validation and remaining work

From `src`:

```sh
make wtp-scheduler-test SUDO=
make wtp-scheduler-test SUDO= WTP_SCHEDULER_BUILD_DIR=build/wtp-scheduler-sanitized \
  WTP_SCHEDULER_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_SCHEDULER_LDFLAGS='-fsanitize=address,undefined'
make wtp-scheduler-interop-test SUDO= PICO_SOURCE=WTP-Client/build/pico-reference
make wtp-backend-test wtp-protocol-test wtp-plan-test wtp-usb-test SUDO=
make semantics-make-regression-test semantics-test-portable SUDO=
```

Ordinary tests use virtual host/device clocks and a scripted peer with real
framing, Session, backend, compiler and controller. They include a complete
162-event WSPR job, early exact-UTC ARM, lease renewal, frozen requests, repeats,
clock failures, pending/deferred reload, concurrent notification at commitment,
stop and blocked recovery. Optional interoperability verifies the existing Pico
source pin and runs its actual endpoint/job service with a software clock/engine.
It opens no physical USB device. macOS/Linux CI includes the ordinary scheduler
tests; a local macOS pass does not establish a Linux runtime pass.

## Documentation impact

Updated this developer contract, the saved
[execution prompt](development/wtp-slice6-prompt.md), review record and progress
links. The separate Wsprry_Pi_Docs backend and timing guidance was reviewed
read-only and remains unchanged because no production operator behavior changed.
Its future follow-up must distinguish early preparation from on-device RF start,
explain host/device clock prerequisites, pending versus committed reload, missed
slots and blocked recovery, alongside endpoint selection and USB permissions.

The [production integration](wtp-production-integration.md) now connects this
scheduler to persisted USB/network selection, application lifecycle and the gated
UI. [Network integration](wtp-network.md) adds TLS without a second scheduler.
Physical timing, USB/DTR/unplug, services and RF still require their own evidence.
The earlier slice records describe historical scope rather than current missing
software integration.

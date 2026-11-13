# Issue 470 I2C Band Selection Implementation Prompt

Implement WsprryPi issue [#470](https://github.com/WsprryPi/WsprryPi/issues/470): optional Pi-local I²C band selection using 8574-family expanders, with Simple and Advanced/Binary modes.

**This is a Pi-only task. Do not modify WsprryPico.** Pico-connected expanders are separate work. Reading an unchanged Pico checkout and using it in hardware-free interoperability fixtures is permitted; changing its source, configuration, documentation, firmware, or devices is not.

Planning revision: 2026-11-13. The recommendations and both decisions in
section 12 are approved, including the scoped `WSPR-Transmitter` changes and
Pi-local amplifier lead/tail controls with 200 ms defaults. The approved plan
was committed and pushed separately; that publication does not authorize later
commits, pushes, or CI runs. This revision is a plan-only review; application
implementation awaits a separate instruction to start.

## 1. Scope and authority

Implement the feature in `/Users/lbussy/GitHub/WsprryPi`, including its first-party `WsprryPi-UI` component, appropriate tests, and engineering documentation.

Update affected Pi operator documentation in `/Users/lbussy/GitHub/WsprryPi-Docs`. Verify the actual repository path and read its instructions before editing; do not create a replacement repository because an older instruction names a different path.

Before implementation:

- Read applicable `AGENTS.md` files.
- Inspect branches, complete working-tree status, staged and unstaged changes, and affected component boundaries.
- Preserve all existing user changes.
- Read issue 470 and its developer implementation comment. This prompt overrides the comment wherever necessary to enforce the Pi-only boundary.
- Trace configuration, band selection, amplifier/PTT, scheduling, cancellation, shutdown, I²C ownership, and the separate Pi WTP endpoint execution path.
- Use Impeccable for UI design, implementation, and visual review.

No staging, commits, pushes, issue replies, pull requests, publication, installations, service operations, system configuration changes, or live hardware/RF operations are authorized.

Do not implement Fleet Operations changes, Pico expander support, native multi-bit GPIO encoding, unrelated expander protocols, SWR/power telemetry, or amplifier circuitry.

## 2. Supported hardware and modes

Support only verified-compatible 8574-family eight-bit expanders, in both forms:

- Logic-output breakout, using the [Adafruit PCF8574 breakout, product 5545](https://www.adafruit.com/product/5545), as the documented example.
- Eight-channel relay board, using [KRIDA Electronics Amazon ASIN B083LFPCK8](https://www.amazon.com/dp/B083LFPCK8), as the candidate example. Its actual revision, chip, channel mapping, polarity, and electrical connections require verification before board-specific qualification claims.

Use one full-port-byte driver and shared pattern generation for both forms. Do not require mechanical relays to obtain binary logic outputs.

Deliver generic PCF8574 and PCF8574A support for both forms. Initially expose
identity P0–P7 mapping and configurable inversion, without a named KRIDA preset.
Withhold definitive KRIDA wiring instructions and any board-specific permutation
until the actual revision is verified. Generic relay-use software fixtures are
required for acceptance; physical board verification is a separate qualification
gate, not evidence to invent or a prerequisite for implementing generic support.

Provide:

| Mode | Behavior |
|---|---|
| Simple | Each band selects one of eight logical outputs or is explicitly unassigned. All other outputs are logically inactive. Multiple bands may share an output. |
| Advanced/Binary | Each band has an arbitrary eight-bit logical pattern from 0 through 255, inclusive. Any combination is valid, including zero and all eight outputs. |

`0x00` is a configured pattern, not an unassigned marker. Binary mode is not restricted to BCD.

P0 is bit 0; P7 is bit 7. Display bit order as **P7 … P0** and provide the corresponding numeric value.

Preserve independent Simple and Binary assignments when switching modes. Do not convert or overwrite one assignment set merely because the operator explores the other mode.

## 3. Configuration and activation contract

Keep I²C selection disabled by default and preserve existing valid native Band GPIO and frequency-suffix behavior.

Make routing explicit: disabled/manual, existing native GPIO, or the new I²C selector. Preserve existing installations’ effective routing when the new configuration is absent. Permit only one active automatic band-selection route; do not silently activate native and I²C selectors together.

An explicit native GPIO frequency suffix must not override an active I²C route or create a second selector. Reject conflicting active requests with an actionable explanation.

Define a typed configuration containing:

- Route and Simple/Binary mode.
- Supported chip variant and board/channel mapping.
- Explicit adapter selection using the existing durable Linux identity policy.
- Seven-bit device address.
- Independent Simple and Binary per-band assignments.
- Per-output inversion mask, with all-normal/all-inverted conveniences.
- Explicit startup/shutdown idle pattern.
- Between-job behavior: return to idle or hold the last successfully selected pattern.
- Release and settling intervals in whole milliseconds.
- Pi-local amplifier lead and tail in whole milliseconds, both defaulting to 200 ms.
- An optional intermediate pattern and its timing only where explicitly configured and meaningful for the attached circuit.

Define logical 1 as an asserted output. For identity mapping:

`physical_byte = logical_pattern XOR inversion_mask`

If a verified board uses a permutation, define the inversion mask in logical-output order and apply the documented permutation afterward. Apply inversion and mapping exactly once for target, intermediate, idle, and cleanup writes.

Do not assume logical zero disconnects a filter or represents a safe receiver code. Make the operator’s idle selection explicit. Preserve all eight pattern bits; do not allocate one to PTT.

Represent an unconfirmed idle pattern separately from every byte value,
including zero. Require an explicitly chosen/confirmed idle byte before saving
an enabled I²C route. Default between-job behavior to return to idle; hold-last
remains an explicit choice. Default the intermediate step to disabled, with its
duration zero. Never describe any offered byte as a universally safe setting.
Changes to idle, inversion, mapping, or board form invalidate the draft's idle
confirmation; saving unchanged confirmed settings does not require reconfirmation.

New selector-domain changes must:

- Be validated and committed atomically while idle and unowned.
- Preserve an immutable active configuration for the running process.
- Require an explicit application restart to activate.
- Latch transmission inhibition until restart, including an A/B/A sequence of saves.
- Never test or switch the newly configured outputs during save.
- Leave unrelated native configuration activation behavior unchanged.

Do not restart a service automatically.

Trace and update defaults, parsing, validation, internal copies, persistence, serialization, UI population, and runtime consumption together. Reject malformed types, unknown fields in the new closed configuration object, invalid ranges, unsupported profiles, and conflicting active allocations before persistence or selector writes.

The absence of the new object preserves legacy native table/suffix resolution.
Once explicitly configured, disabled/manual suppresses automatic selection and
rejects explicit native selector suffixes; native selects the existing native
resolver; I²C selects only the new resolver. Retain inactive native assignments
without claiming their pins or applying them. Reject an I²C-enabled candidate
containing active local frequency entries with explicit native selector suffixes
before persistence. Do not erase those entries to make the save succeed.

Use the existing canonical band catalog/correlation for every realized RF-on
frequency. Permit different bands to share one pattern across a job; reject
unassigned bands, unresolvable frequencies, or different resulting patterns.
Simple assignments are explicit unassigned values or integer channels 0–7;
Binary assignments are explicit unassigned values or integer bytes 0–255.
Reject booleans, fractional values, unknown band names, and out-of-range values;
never coerce zero to unassigned. Retain and validate both mode maps on save.

Treat the selector object, routing activation, and the existing Pi PTT
enable/pin/polarity used by an enabled or boot-active I²C route as one activation
domain. Saving those PTT values while I²C is active must not hot-swap its GPIO
owner. Preserve the existing PTT activation behavior outside that domain.
Confirmations bind to the complete circuit profile (adapter/address/chip,
mapping/inversion, idle/intermediate values, timing, and PTT allocation);
material changes invalidate the relevant confirmations. Apply the same typed
validation/confirmation requirements to INI and API inputs, not just the UI.

Save only after both RF and post-RF selector work are quiescent and ownership
is released; RF-complete alone is insufficient. Under the shared admission
lock, persist atomically, then latch process-local inhibition before admitting
new work. Persist/readback failure preserves the active snapshot and earlier
inhibition, reports uncertainty, and requires authoritative readback. If a
write may have changed the saved activation domain, latch inhibition even when
readback fails; only a failure proven to precede persistence may leave a
previously clear latch clear. An
identical normalized save does not create a new restart requirement, and no
save clears an existing latch. Reloads/file changes cannot hot-activate the
domain or bypass A/B/A inhibition. Startup uses the validated saved domain.
Unavailable hardware does not prevent a structurally valid offline save;
binding/address ownership checks that require opening hardware occur at startup,
never during save.

Use the timing design in section 5.1. Its numbers are software admission policy,
not measured relay requirements. Initial release/settling values are draft
suggestions only; require explicit operator confirmation of timing suitability
before activating I²C routing. An unavailable board specification prevents a
board-qualified timing claim, not generic configurable software support.

## 4. Driver and I²C ownership

Separate pure pattern resolution, selector lifecycle, and physical transport. Keep the selector independent of the RF-generation backend.

Use a single owner for the selector state and writes. Maintain:

- The intended logical pattern.
- The last successfully written full port byte.
- Whether the commanded state is currently known.
- Fault and initialization state.

Write the complete eight-bit port value in one supported transaction. Do not perform per-bit read/modify/write transactions, reconstruct output latches from input reads, or add register-based direction initialization.

Explicitly support PCF8574 addresses `0x20–0x27` and PCF8574A addresses `0x38–0x3F`. Any additional 8574-family variant requires verified transaction, electrical, and address compatibility. Reject XL9535 and other unsupported families.

Follow the current Linux adapter-identity contract:

- Use metadata-only discovery.
- Preserve durable USB identity and detach/rebind refusal.
- Never substitute another adapter after resolution fails.
- Never probe addresses or issue discovery writes to identify hardware.
- Never use forced access to bypass kernel ownership.
- Reject known address conflicts and serialize cooperating process access.

Do not claim that advisory locking excludes arbitrary noncooperating programs using `i2c-dev`.

Account for coexistence with Si5351 on the same bus: distinct addresses, compatible bus speed, transfer serialization where necessary, and no selector activity during RF-sensitive execution. Do not silently change system bus configuration. Use a speed supported by every attached device; PCF8574 requires no more than 100 kHz.

Prefer existing reusable identity facilities without unrelated component changes. If a necessary change reaches `src/WSPR-Transmitter`, identify the exact interface need, explain why parent-level integration is insufficient, and obtain any approval required by repository instructions before broadening component scope.

### 4.1 Shared-bus ownership design

Use one process-wide registry of Linux adapter leases, shared by the selector,
the local Si5351 backend, and the Pi endpoint Si5351 backend. Put reusable bus
primitives in `src/WSPR-Transmitter` without dependencies on application globals,
configuration, UI, PTT, or selector policy. This component extension is approved
within the scope recorded in section 12; do not implement a parent-only imitation while
leaving a production Si5351 path outside coordination.

- Resolve the existing durable identity first. Key an adapter lease by the
  resolved character-device major/minor and captured sysfs/USB binding, not by
  the operator's string alone. Numeric and identity aliases of one bus share
  one lease. A detach/rebind invalidates all associated handles and jobs; it
  never silently creates a replacement binding for an active owner.
- Acquire a nonblocking exclusive advisory `flock` on an open descriptor for
  the resolved `/dev/i2c-N` node before any device transaction. Keep that
  descriptor in the shared lease until every dependent handle and in-flight
  operation is retired. Separate device handles retain ordinary `I2C_SLAVE`
  selection and exact transfer checks. The conservative lease reserves the
  whole adapter against other cooperating processes for its lifetime, while
  allowing this process's distinct-address devices to share it. No lock files,
  forced slave selection, bus reconfiguration, or administrator action is
  introduced. Failure/unsupported locking refuses dependent RF.
- Use an additional timed in-process transaction guard on that lease. Hold it
  across each complete Si5351 operation, including register-pointer write plus
  read, and across each full-byte expander write. Preserve the existing STOP
  semantics; do not substitute `I2C_RDWR` as incidental optimization. Preserve
  same-device/cache serialization. Replace the existing global cross-adapter
  Si5351 mutex with per-device serialization plus the shared per-adapter guard
  only when equivalent production-path coverage passes. Do not hold a global
  mutex over a blocked adapter and claim other adapters remain independent.
- Add a narrow transaction-scope seam to `Si5351Transport` and invoke it from
  `Si5351Device` around complete operations. The Linux implementation uses the
  common lease/guard; native macOS and explicitly injected fake transports
  retain their own semantics. Update paired adapters/tests for the new seam.
  Use the same primitive in the parent-owned selector driver. A transport-only
  per-syscall lock cannot protect the existing two-syscall register read.
- The existing backend `Config.device_adapter` seam is available to the Pi
  endpoint, but `WsprTransmitter::createBackend()` constructs its local
  transport internally. Coordinate the native Linux factory itself so local,
  endpoint, and standalone component users all participate. Do not claim that
  wrapping only the endpoint factory solves shared ownership.
- Maintain address claims inside the registry. Different devices cannot claim
  the same address on one adapter. Local/endpoint handles for the same RF
  device may share its claim only through the application's single RF-owner
  token and mutually exclusive execution/configuration lifecycle; otherwise
  refuse the duplicate. Reject kernel-owned addresses through ordinary slave
  selection. Advisory locks cannot exclude noncooperating userspace programs
  or unrelated kernel-device traffic; document that boundary.
- The parent selector owner and existing local/endpoint authority admit exactly
  one preparation/execution/cleanup epoch. No selector writes, background
  hardware discovery/configuration transactions, or idle housekeeping run during
  the reserved execution
  interval, including keyed RF gaps and normal PTT tail. Si5351's required RF
  transactions and required metadata-only binding validation remain allowed.
  Cleanup can write the selector only after RF
  stop and independent PTT release are established. Saves never acquire a
  transaction guard or open hardware. Reads of saved settings and software-only
  status remain available.
- Register the adapter/address claims before either RF engine performs startup
  quiescence or configuration. Serialize local and endpoint startup under one
  RF-owner token; a second constructor cannot independently quiesce a device
  already reserved by preparation, execution, or cleanup. Backend replacement,
  endpoint reconfiguration, takeover, and destruction observe the same owner.
- Lock ordering is execution ownership, then device-operation serialization,
  then adapter transaction guard. Never wait for/join a worker while holding
  a lock it needs. Cancellation first invalidates the epoch and releases PTT
  independently; it must not wait for an I²C guard to inhibit RF. Registry
  metadata locks are short-lived and are not held over syscalls or waits.

Require configured/confirmed bus speed no greater than 100 kHz for a bus
containing PCF8574, including Si5351 coexistence. Use trustworthy read-only host
metadata where available; otherwise require an explicit operator declaration
of the externally configured speed and label it as declared, not measured.
Refuse a known excessive speed or absent declaration. Do not claim portable
`i2c-dev` speed discovery or set adapter-wide timeout/retry/speed controls.

References: [Linux userspace I²C interface](https://www.kernel.org/doc/html/latest/i2c/dev-interface.html)
and the chip datasheets in section 8. Validate advisory locking on the supported
Linux host with hardware-free filesystem/descriptor fixtures and separately
qualify actual adapter behavior before physical use.

## 5. Fail-closed sequencing and execution coverage

Do not add an I²C write to the existing transmit-start notification and assume that creates a safety gate. The current Pi callback path can log selector failures and continue to amplifier activation.

Implement a preparation path whose failure prevents RF admission:

**RF disabled → amplifier/PTT inactive → release interval → optional intermediate pattern → complete target-byte write → settling interval → amplifier lead → scheduled RF.**

Release and settling are separate intervals, not aliases for amplifier lead or
tail. The current Pi has callback-driven Amp Control but no configurable lead
or tail durations (`config_types.hpp`, `scheduling_runtime.cpp`). Pico's timing
controls are not a Pi timing contract. Section 5.1 therefore defines approved
Pi-local timing within the new selector domain, as recorded in section 12;
leave existing native-only installations' activation behavior unchanged.

At startup, establish RF inhibition and PTT inactivity, initialize the selected expander to its configured idle pattern, and permit dependent transmission only after initialization succeeds.

Before startup idle, observe the release interval; after its successful write,
observe settling before marking initialization ready. Startup is outside an
accepted job's preparation budget and cannot borrow its scheduled RF time.
PTT operations return checked receipts; the current log-only GPIO helper is
not an admission acknowledgement. If requested PTT assertion/release fails,
inhibit RF, retain the failure, and do not change the selector while RF or requested PTT
inactivity is unconfirmed. No independently controlled PTT being configured
must be reported as such, rather than as a successful physical release; the
documented external enable/interlock requirement remains applicable. Lead/tail
holds still apply as configured software intervals without asserting a GPIO.

Resolve selection from the actual job’s transmitted frequencies, including custom frequencies, WSPR offsets, modulation shifts, and applicable correction. Do not use an unrelated browser selection or a WSPR dial frequency as the RF frequency.

Freeze the resolved pattern and configuration for the accepted job. Every RF-on event must be compatible with one selection for the complete job. Reject jobs requiring a pattern change during uninterrupted execution.

When I²C selection is active, unassigned or unresolvable frequencies must refuse transmission. They must not reuse the preceding band’s pattern.

Complete preparation before the scheduled RF start. Include its cost in admission and refuse insufficient lead time; do not move the requested RF window or switch filters after RF has started.

Cover every relevant Pi RF entry point:

- WSPR and other supported local modes.
- Direct and managed test-tone paths.
- Repeated scheduling and configuration reloads.
- Jobs received by the Pi’s own WTP endpoint.
- Cancellation, STOP, shutdown, and error cleanup.

The Pi WTP endpoint must use the same owning selector state and preparation gate, rather than bypassing it through its separate engine.

A Pi acting as a controller must not operate its local expander for jobs targeting another Pi or Pico. Unsupported remote-selector combinations must be refused explicitly.

Apply local selector admission only to work executed on this Pi, including its
inbound WTP endpoint. Do not reject a remote job based on this Pi's unassigned
band, and do not initialize or open a local selector solely for controller-only
remote work. A local endpoint may initialize its own configured selector for
its own execution independently. Never serialize this Pi's selector settings
or patterns into a remote job or use them as remote filtering evidence.

During a job, retain selection through keyed gaps and normal amplifier tail. At normal completion, stop RF before releasing PTT, then apply the configured between-job behavior.

On cancellation or fault:

- Inhibit further RF and attempt independent PTT release.
- Attempt configured idle cleanup without concealing earlier failures.
- Mark uncertain selector state unknown.
- Bound retries and invalidate stale pending work.
- Prevent queued stale writes after cancellation or shutdown; handle already
  submitted transactions as specified in section 5.2.
- Require successful reinitialization before reuse.

For this implementation, recovery may require explicit application restart; do not add an automatic retry loop that resumes transmission after an uncertain write.

A failed cleanup may leave an externally powered expander latched. Preserve that limitation in status and documentation.

### 5.1 Timing budget and admission

Keep the Pi WTP endpoint's advertised minimum arm lead at **2,000 ms**, maximum
arm uncertainty at **500 ms**, and output-disable timeout at **5,000 ms**.
Do not change capabilities, WTP messages, shared schemas, or Pico to accommodate
the selector. Clock uncertainty is an independent admission check, not extra
time to spend or permission to move the requested RF start.

Use these finite whole-millisecond policy values for an enabled I²C route:

| Quantity | Allowed range | Default / initial draft value | Meaning |
|---|---|---|---|
| Release `R` | 0–1,000 ms | 50 ms | Minimum PTT-inactive time before changing selector outputs |
| Intermediate dwell `I` | 0–1,000 ms | Disabled, 0 ms | Minimum time after an explicitly enabled intermediate write |
| Settling `S` | 0–1,000 ms | 50 ms | Minimum time after the completed target write |
| Amplifier lead `L` | 0–1,000 ms | 200 ms default | Minimum asserted-PTT time before first RF |
| Amplifier tail `H` | 0–1,000 ms | 200 ms default | Normal-completion hold after final RF; faults bypass this hold |
| Selector operation allowance `W` | Fixed 100 ms | Not operator-editable | Guard wait, identity checks, and one full-byte write together |
| Dispatch reserve `G` | Fixed 300 ms | Not operator-editable | Unspent scheduling margin before requested RF |

Validate `R + I + S + L <= 1,500 ms` atomically. Reserve two selector operation
slots even when the intermediate step is disabled, so configuration validation
and minimum lead do not depend on an optimization:

`B = R + I + S + L + 2W + G <= 2,000 ms`.

With the initial values, `B = 800 ms`; maximum admitted configuration is 2,000 ms.
These are chosen software limits, not inferred mechanical specifications or
guaranteed Linux response times. Confirm release, settling, and amplifier
timing against the attached circuit before use; zero explicitly requests no
additional hold for that interval. Reject disabled intermediate steps with a
nonzero dwell, enabled steps without a pattern, and over-budget combinations.
The approved 200 ms lead/tail defaults do not come from a Pi hardware measurement.

Stage backend compilation, frequency realization, ordinary RF-disabled backend
configuration, and startup idle initialization before entering this budget.
The existing WTP `prepare()` configures/realizes before ARM; the local
`WsprTransmitter` instead configures its backend in its worker, so add the
narrow staged-preparation/launch seam described in section 12. Revalidate
realized frequencies and the frozen selector configuration before accepting
the job. No early compilation implies permission to switch a filter on LOAD.
RF-disabled backend configuration already needs exclusive hardware ownership;
acquire a preparation reservation before that work, including on LOAD, without
selecting a band or asserting PTT. Keep the reservation bound to the prepared
job until ARM, replacement, valid release/abort, or the existing lease-expiry
path retires it. Atomically promote it to execution ownership on accepted ARM.
The synchronous `prepare()` return cannot report success with a still-running
configure worker: own copied inputs and resources until completion, use a finite bounded wait
within the caller's preparation deadline, and return failure on expiry without
blocking independent cancellation or later admitting a late success. A late
configuration result poisons/cleans its epoch before any backend reuse.

For an absolute job timeline target `T`, map the target once to a monotonic deadline and
retain the existing UTC/start-window checks. Admission requires at least `B`
remaining and exclusive execution ownership. WTP also retains its independent
2,000 ms ARM check. Complete release, intermediate/target writes, and settling
by `T - L - G`. Assert PTT only with a valid preparation receipt and enough
remaining time for the full lead. The reserve is scheduling slack, not another
operator delay. Immediately before RF, require a current receipt, elapsed lead,
valid clock/window, no cancellation, and no inhibition. A late operation or
missed deadline refuses RF even if an I²C ACK eventually arrives. Never shorten
settling/lead, switch after RF starts, or move an accepted absolute target.

WTP permits leading/trailing RF-off intervals. `T` remains the job's advertised
timeline origin, not necessarily the first RF-on event. Complete selection
before `T` conservatively; preserve all event offsets and the existing silent
start hook without treating that hook as observed RF. Assert PTT only for jobs
with RF-on events, and require at least `L` before the first actual RF enable.
Keep one pattern and PTT ownership through the entire accepted timeline,
including keyed gaps and trailing RF-off intervals. Start normal tail after
confirmed completion of that timeline, so `H` is a minimum hold after final RF.
All-RF-off jobs and local skip windows perform no band selection or PTT
assertion; preserve their existing supported semantics and required RF
quiescence checks. A path that does not support an RF-off-only job refuses it
explicitly without selector/PTT writes. They do not resolve a fictitious zero-Hz band
or pattern.

Recheck the current monotonic time after admission and immediately before each
write/hold/PTT step; time spent inside `schedule()` or other dispatch code counts.
A schedule refusal before its ARM response is an admission failure; an accepted
job that later misses preparation must report the existing missed/failure
outcome and release PTT. It must not be silently rescheduled or treated as a
successful job with no RF. Lead/settling use elapsed monotonic time, including
for zero-duration holds; UTC uncertainty never shortens them.

Immediate local modes/test tones wait for the same preparation sequence before
RF; they have no externally promised UTC start. Repeated jobs get a fresh
epoch/receipt and admission calculation, even if their pattern is unchanged.
Do not skip release or settling on the assumption that a retained pattern or
ACK proves the external circuit's state. A later scheduler occurrence may be
considered normally after a timing-only refusal; uncertain I²C state latches
inhibition until explicit restart and successful initialization.

Normal completion stops RF, retains selection through `H`, releases PTT, waits
`R`, then either writes idle once or keeps the last successful pattern. With
the approved maxima, tail plus release plus one idle allowance is at most
2,100 ms. RF/backend stop time is accounted independently; this calculation
does not establish that the 5,000 ms WTP disable timeout always succeeds.
STOP/fault/shutdown bypass normal tail, invalidate the epoch, inhibit RF,
attempt PTT release, then observe `R` before best-effort idle. Never extend the
WTP timeout or report successful cleanup merely because its deadline expired.

Preserve the separate **100 ms RF completion-acknowledgement ceiling** in
`WTP-Server::RfEngine` and `JobService::poll()`. Do not keep the engine Running
while waiting for a 200 ms PTT tail or up to 2,100 ms of selector cleanup; that
would make a normal job fail the existing completion contract. At final RF end,
obtain the backend's RF-disable/cleanup receipt and publish the RF terminal
report within the existing acknowledgement contract. If RF confirmation itself
misses that bound, keep the existing failure behavior; the selector budget
does not grant an extension.

The parent owns a separate post-RF cleanup receipt/worker. It retains exclusive
local hardware ownership through tail, PTT release, release interval, and idle
write, even when WTP has already reported the RF job complete. The Pi endpoint
engine's terminal `disable()` may acknowledge already-confirmed RF inactivity;
it must not describe pending selector cleanup as successful or admit a new
preparation. Both local and endpoint preparation check this retained ownership.
Expose cleanup-pending/fault separately in Pi-local status; a later selector
cleanup fault inhibits future RF without rewriting an existing WTP terminal
record. Successful WTP RF completion alone is not full selector cleanup.

WTP/1 has **ABORT**, not a network STOP operation. `abort_job()` rejects a
Complete/Missed job with `InvalidState`; preserve that result. A controller
cannot use ABORT to cancel a completed RF job's remaining tail. Normal tail
finishes under the parent owner; the Pi-local STOP/shutdown/End now controls
can still cancel it. Do not invent a wire operation or turn a rejected ABORT
into a successful physical action.

Extend `WtpPiAuthority` to observe accepted ABORT and the existing local control
paths while preserving service validation, leases, and replay behavior. The
engine's nonterminal `disable()` must invalidate the matching selector epoch
and request independent PTT release **before** waiting for backend stop/join;
waiting for `handle()` to return first is insufficient. When RF has already
completed successfully, automatic terminal `disable()` acknowledges its frozen
RF receipt without cancelling normal tail. An accepted ABORT racing that RF
completion cancels the original epoch once its accepted result is known.

Capture the boot/job/owner/epoch association before forwarding the request;
perform post-response actions only for that still-matching association, once.
A cached successful ABORT response is not a fresh cancellation command.
Rejected ABORT, different-job ABORT, request-ID conflicts, retained LOAD/ARM
replays, and reconnects cannot allocate epochs, write outputs, restart jobs,
or affect a newer epoch. Never wrap `response.ok` alone with hardware effects.
Observe local STOP/shutdown directly even when WTP ownership was released.

Retained post-RF ownership gates every authority transition: CLAIM and new
LOAD/ARM, local schedule/tone, configuration/save/reload, backend/endpoint
replacement, recovery, and local takeover. Continue software status, renewal,
and protocol-valid release/replay operations without new hardware effects.
An existing owner's new LOAD receives Busy through the admission callback
while cleanup is pending, before `prepare()` can overwrite hardware. RELEASE
or lease expiry does not drop the parent's cleanup resources, clear a selector
fault, or authorize another RF owner. Finish current waits for cleanup; End now
cancels it. Recovery of WTP sessions alone cannot clear the restart-only latch.
Publish selector pending/fault independently of WTP terminal state and retain
the original job identity until its cleanup worker retires.

Keep listener/service polling responsive by doing no selector I/O or timed
tail wait in `poll()` or terminal RF-only `disable()`. Parent/engine callbacks
must not reenter an authority mutex held across `service_.handle()` or `poll()`;
use an independently cancellable owner and immutable snapshots/receipts, then
reconcile authority state after the call. Active RF stop still retains its
existing bounded disable contract and reports an unresolved stop honestly.
This uses existing service admission and engine interfaces; it requires no
`WTP-Server` or Pico source/schema change.

### 5.2 Blocking transactions and cancellation limits

The 100 ms allowance is a software admission deadline. A synchronous Linux I²C
syscall may outlive it; elapsed time, a future timeout, or closing a descriptor
elsewhere does not prove that an in-flight write was cancelled. Use one owned
I/O worker with at most one operation in flight, an epoch-tagged queue, and
deadline/cancellation observation independent of that worker. Do not detach a
worker and release/reuse its handles, retry a short/failed write, or admit RF
after a late completion. Never start queued stale operations.

On timeout/cancellation, invalidate the receipt immediately, release independent
PTT, and retain unknown state and the bus lease until the in-flight operation
has actually returned. Only then may a fresh cleanup operation attempt idle,
once and with its own deadline, after RF and requested PTT inactivity are
confirmed and `R` has elapsed. A failed independent PTT release prevents the
idle write; preserve that failure rather than assuming a disconnected filter.
If quiescence cannot be established, retain the
fault and report cleanup unresolved; do not dispatch another writer. RF stop
must not depend on the selector's I/O worker. Si5351 shares the bus, so software
cannot promise its disable transaction will complete while that bus is wedged.

An already submitted write may physically complete after cancellation. The
requirement to prevent stale writes applies to undispatched work and reuse;
it cannot promise revocation of a kernel/device operation already in flight.
Document that limitation and the independently safe PTT/hardware-enable
requirement. Physical qualification must check adapter timeout behavior and
external inhibition; no software-only test qualifies bounded physical shutdown.

## 6. Pi/Pico contract boundary

Do not change WsprryPico source, firmware, documentation, settings, schemas, generated assets, or devices.

Do not advertise Pi-local expander support as a Pico capability. Preserve the existing Pico selector editor and behavior.

Keep WTP wire formats, shared Control/1 schemas and descriptors, and independent transport/storage versions unchanged. Do not add a new shared-control version, downgrade path, alternate carrier fallback, or relaxed parser.

Implement through Pi-local configuration and execution wherever possible. If safe implementation requires a shared contract change or a Pico modification:

- Do not make that change.
- Explain the exact dependency and affected consumers.
- Report the remaining implementation as blocked at that boundary.
- Do not substitute incomplete validation or claim the issue complete.

An unchanged Pico checkout may be used as the companion in production-path integration fixtures. Record its exact revision.

## 7. User interface

Extend the existing Pi I/O configuration workflow using Impeccable and the established Bootstrap layout, typography, themes, controls, and validation patterns.

Present routing first, then I²C hardware details and per-band assignments.

- Simple mode: a channel selector and explicit unassigned choice.
- Advanced/Binary mode: eight labeled logical bits and synchronized numeric representation.
- Common controls: polarity, idle behavior, release, and settling.
- Require explicit idle confirmation and timing suitability confirmation before
  enabling the route. Show the calculated preparation budget and identify a
  sum exceeding 1,500 ms beside the timing fields. Include the approved Pi
  amplifier lead/tail controls with 200 ms defaults; show units and their
  independent purpose. Keep bus-speed declaration distinct from
  measured/discovered information.
- Clearly label physical channel numbering where relay channels 1–8 correspond to P0–P7.

Keep advanced details in inline disclosures rather than introducing a separate application or dashboard.

Use explicit save for the complete selector configuration. Mode exploration and preview must remain local drafts and must not trigger the existing generic autosave path.

While the saved or boot-active route is I²C, include its PTT enable/pin/polarity
drafts in that same explicit-save activation domain. Prevent the existing Amp
Control autosave handlers from committing only those fields or changing the
active GPIO owner. Keep unrelated native-only autosave behavior intact.

Preserve drafts after validation errors, unavailable hardware, and uncertain saves. Show restart-required and transmission-inhibited states beside the relevant controls.

Distinguish:

- Saved settings.
- Active settings.
- Intended pattern.
- Last successful byte write.
- Unknown or stale state.

Do not label an I²C ACK or port read as verified relay position, verified filtering, or RF safety.

Do not add an interactive live-output test button.

Render and inspect desktop/mobile layouts in light and dark themes, including keyboard navigation, focus, field errors, disabled controls, long adapter identities, restart-required state, and hardware-fault feedback.

## 8. Documentation and electrical boundaries

Update Pi operator documentation and relevant Pi engineering documentation alongside implementation. At minimum, review:

- `WsprryPi-Docs/docs/User_Interface/Setup/Pi_IO/index.md`
- Relevant transmitter and Linux I²C setup instructions.
- `WsprryPi/docs/development/linux-i2c-identity.md`
- Relevant Pi scheduling, configuration, and WTP endpoint documentation.

Add separate wiring guidance for the logic breakout and verified relay-board form. Explain:

- Pi-facing SDA/SCL and pull-ups must remain at compatible 3.3 V levels.
- A relay board’s supply voltage does not establish I²C compatibility.
- Required level translation, grounding/isolation, relay power, and backfeed prevention depend on the actual board.
- PCF8574 outputs are quasi-bidirectional, with weak-high and strong-low behavior.
- Logic outputs are not relay-coil drivers, dry contacts, or RF amplifiers.
- External buffering and receiver thresholds require verification.
- I²C should remain local unless a separately qualified physical arrangement supports otherwise.

PCF8574 powers up high. Software cannot guarantee a safe pattern before initialization, after a crash, or during power loss. Require an independently safe amplifier/PTT or hardware-enable arrangement for the connected system. A complete-byte write does not guarantee simultaneous relay movement or glitch-free external decoding.

Use the actual chip datasheet and board documentation:

- [PCF8574 datasheet](https://www.ti.com/lit/ds/symlink/pcf8574.pdf)
- [PCF8574A datasheet](https://www.ti.com/lit/ds/symlink/pcf8574a.pdf)
- [Adafruit wiring guide](https://learn.adafruit.com/adafruit-pcf8574?view=all)

Ship no G59 preset in this implementation. The issue’s original band table and
developer example disagree about code 1 being 60 m or 160 m. Configurable
mappings and explicitly labeled test fixtures proceed without deciding which
table is correct; a future preset requires separate verified evidence.

Build operator documentation using its repository’s virtual environment and documented Sphinx workflow. Inspect affected rendered pages and replace screenshots only when materially inaccurate. Do not modify Pico documentation.

## 9. Software validation

Read `docs/simulated-backend.md` before application-level testing. Use explicit simulated RF execution and an explicit fake selector transport.

Neither selecting the simulated backend nor running ordinary tests may open physical I²C hardware, even when a saved configuration enables the selector. Do not rely solely on missing hardware or permissions to make tests safe.

Physical selector transport is Linux `i2c-dev` only. Native macOS selector/HID
support is out of scope. On other platforms, retain the saved draft and refuse
physical I²C-route activation explicitly before opening hardware; portable
fake-transport tests remain supported. A simulated RF run with an enabled
selector requires explicitly injected fake selector **and fake PTT** ownership;
without those injections, refuse dependent execution before any hardware open.
During simulation, never automatically create a physical selector from saved
settings, silently fall back to no selector, or expose new CLI/INI/environment
simulator controls.
Ordinary read-only settings/status operations remain available in this state.

Add meaningful tests covering:

- Both board forms and both modes.
- All eight output positions.
- Shared-band assignments.
- `0x00`, `0xFF`, and multi-output patterns such as `0x05`.
- Per-output inversion, identity mapping, exactly-once conversion, and refusal
  of unverified mapping profiles. Add nonidentity board fixtures only when a
  verified profile is actually included; do not invent a KRIDA permutation.
- Separate preserved mode assignments and persistence round trips.
- Missing assignments versus valid zero.
- Strict refusal before selector writes.
- Existing native GPIO and frequency-suffix regressions.
- Both address variants and unsupported-profile rejection.
- Exact full-byte transactions, failed/short writes, adapter loss, and uncertain state.
- Startup idle, release/settling ordering, insufficient lead, normal completion, hold/idle behavior, cancellation, configuration changes, and shutdown.
- No stale writes or RF restart after failure.
- Local and Pi endpoint execution, including isolation from jobs targeting remote devices.
- Exact timing-budget boundaries, integer overflow/type rejection, both
  intermediate-step states, all timing confirmations, early/late completion,
  unchanged-pattern repeated jobs, lead/tail separation, and cancellation
  during every interval. Test 1,500 ms combined holds as valid and 1,501 ms as
  refused before writes; preserve the independent WTP 2,000 ms ARM boundary.
- Bus identity aliases, duplicate-address refusal, same-RF-owner handover,
  contention between cooperating processes, complete pointer-write/read
  exclusion, detach/rebind, lock failures, and separate-adapter independence.
- A deliberately blocked fake transfer whose deadline expires before return:
  immediate independent inhibition/PTT release, no queued stale write, no
  receipt from late success, no premature lease reuse, and unresolved cleanup
  until actual completion. Exercise this through both production RF paths.
- Normal endpoint completion with the default 200 ms tail and maximum cleanup
  duration: preserve the 100 ms RF acknowledgement ceiling, responsive local STOP,
  retained selector ownership after RF terminal reporting, rejection of new
  preparation while cleanup is pending, and latched inhibition if cleanup
  later fails. Network STOP is unknown; post-completion ABORT remains refused.
  Rejected/stale/replayed ABORT cannot affect another job or cleanup epoch.
- Accepted ABORT racing RF completion, local STOP after protocol RELEASE,
  deferred lease expiry, Finish current/End now takeover, recovery, endpoint
  replacement, configuration saves, and a new LOAD from the current owner
  while cleanup is pending. Exercise the actual authority/service/engine path,
  not just a selector-owner unit model; assert no reentrant-lock deadlocks.
- Leading/trailing RF-off intervals, RF-off-only/skip requests, multiple bands
  sharing one pattern, and different-pattern refusal. Preserve timeline origin
  and offsets while proving the full lead precedes first RF and no selector
  cleanup runs inside the accepted timeline.
- Startup ordering between both RF engines and the selector, PTT receipt
  failures, no-PTT status/interval behavior, active/inactive routing, retained
  native drafts, domain-bound PTT changes, offline save, unchanged save, A/B/A,
  persistence/readback failure, and out-of-process INI reload. Simulated RF
  with missing fake selector/PTT injections refuses before physical access.

Use observable fake I²C/PTT/RF events and deterministic clocks where possible. Verify actual production orchestration, not merely a second model of the intended sequence.

Run applicable existing targets from `src`, after inspecting their recipes and selecting a supported host profile:

```sh
make band-gpio-default-convergence-test band-gpio-rotation-test \
  selector-shutdown-cleanup-test test-tone-selector-plan-test \
  linux-i2c-identity-test SUDO=
```

On macOS, run:

```sh
make semantics-test-portable SUDO=
```

On the appropriate Linux host, run:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
```

Portable success is not a substitute for the full Linux profile.

Run affected standalone component tests if component source changes are authorized. In `WsprryPi-UI`, extend and run:

```sh
npm run test:unit
npm run test:browser
```

Add checks that configuration exploration and saves cannot issue selector writes or silently activate a different route. Include the repository’s hardware-access audit for application simulations.

## 10. Production interoperability and physical qualification

Because this work integrates with Pi WTP admission and execution, run both checked-in hardware-free source-pair recipes in `.github/workflows/debian-non-hardware.yml`, even though Pico remains unchanged:

- `wtp-network-loopback`: include `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`.
- `wtp-usb-management-pair`: build the actual unchanged Pico `usb_management_driver`, run `wtp-usb-pair-client-build` and `wtp-usb-management-test`, and execute `WsprryPi-UI/tests/wtp_usb_pair_test.js` through the production pair.

Select and record the exact Pico source and pinned dependencies explicitly. Do not silently switch to an older companion, weaken validation, or edit Pico to obtain passing results.

Add feature-specific Pi endpoint coverage using simulated RF and the fake selector. Existing interoperability success alone does not prove the new sequencing.

Failed, timed-out, cancelled, or skipped required checks remain unresolved. If an unchanged companion prevents validation, report the blocker without modifying it.

Physical testing is not authorized by this prompt. Record a separate qualification procedure for later approval, covering both actual board forms with RF initially disabled:

- Power-up and power-loss states.
- Chip identity, mapping, polarity, pull-up rails, and electrical compatibility.
- Release, settling, and PTT ordering.
- Bus disconnection and failed cleanup.
- External enable/interlock behavior.

Any later RF test requires explicit authorization and exact frequency, mode, duration, output path, attached hardware, and stopping procedure. Software tests do not qualify relay contacts, filter response, RF output, or physical timing.

## 11. Adversarial review and completion

Review the implementation, tests, and documentation adversarially before declaring completion. Specifically challenge:

- RF paths that bypass selector admission.
- Errors that are logged but do not prevent transmission.
- Zero patterns mistaken for missing assignments.
- Polarity applied twice or in inconsistent coordinate systems.
- Native/I²C routing conflicts.
- Mode changes that overwrite assignments.
- Saves or previews that switch hardware.
- Late writes after cancellation.
- Unknown state treated as successful cleanup.
- Simulated runs reaching physical transports.
- Pi-local hardware following remote-device jobs.
- Pico or shared-contract scope expansion.
- Unsupported electrical or qualification claims.

Fix actionable findings, rerun affected checks, and repeat the review until no actionable findings remain. If a finding cannot be resolved within scope or available evidence, report it as an explicit blocker; do not call the assessment clean.

The completion report must state:

- Implemented behavior and important files/components changed.
- Exact Pi and unchanged Pico source revisions, including tested uncommitted changes.
- Commands run and their pass/fail/skipped results.
- UI workflows and desktop/mobile visual evidence reviewed.
- Documentation updated, considered unchanged, and still required.
- Remaining hardware qualification and other unresolved validation.
- Final branches and working-tree state.
- Confirmation that Pico was not modified.
- Actual commit/push status and confirmation that no installation, service
  operation, or live hardware/RF action occurred. Distinguish previously
  authorized plan publication from any implementation publication authority.

Distinguish software implementation from physical qualification. If required work remains incomplete, identify the exact next unfinished step.

## 12. Approved implementation decisions

Both decisions were explicitly approved by the maintainer on 2026-11-13.
Those approvals remain valid for the stated scope. No additional product
decision is identified by this plan review; implementation still must prove
the production-path gates and report any newly discovered scope dependency.

1. **Approved: the narrow `src/WSPR-Transmitter` extension.** Approved scope is
   reusable Linux adapter leases and complete-operation guards, their
   transport/device/factory integration, and typed staged-preparation,
   pre-RF-admission, and synchronous completion/cleanup handoff at the
   `WsprTransmitter` facade. The parent supplies selector/PTT policy; the
   component carries immutable prepared-job tokens, realized frequencies,
   absolute deadlines, and cancellation epochs without learning band tables
   or application configuration. A refused/expired callback prevents RF;
   completion acknowledgement follows physical-backend cleanup before the
   parent may change selection. Existing notification callbacks retain their
   reporting role. Local backend creation and worker preparation are private,
   so parent-only code cannot inject shared ownership or acknowledge every
   RF/cleanup boundary through the current API. Keep standalone builds, native
   macOS, explicit fakes, and native-only application behavior intact. Run
   component and parent tests, plus both unchanged-Pico pairs. No unrelated
   refactoring or shared Pi/Pico contract change is included. The maintainer's
   explicit approval satisfies the repository's `src/` component policy for
   this scope; further scope expansion still requires separate approval.
2. **Approved: Pi-local amplifier lead/tail controls for the enabled I²C route.**
   Adopt the section 5.1 ranges and 200 ms defaults for both lead and tail,
   with timing suitability confirmed by the operator for the circuit. Store
   them in the new Pi-local selector object, activate on restart, and apply
   identically to local and
   inbound Pi WTP jobs. Do not reuse Pico controls or change shared schemas.
   This adds behavior absent from the Pi today. Zero remains a valid explicit
   setting within the approved range; it is not the default. Do not infer a
   timed amplifier contract from the existing notification callbacks.

Hardware identity, actual wiring, electrical compatibility, bus-speed evidence,
and suitable circuit timing remain requirements before physical operation or
board-specific claims. They do not require choosing a KRIDA or G59 preset to
start generic software implementation. No physical operation is authorized.

Planning basis: Pi `949a5c3e6dad6f4bbe465d92055c7243832052dd` on `devel`;
`src/wtp_endpoint/capabilities.hpp`, `tone_engine.cpp`,
`src/WTP-Server/src/job_service.cpp`, `src/scheduling_runtime.cpp`,
`src/config_types.hpp`, and `src/WSPR-Transmitter/src/{wspr_transmit.cpp,
wspr_transmit.hpp,si5351_device.cpp,si5351_transport.hpp,
si5351_transport_linux.cpp,wspr_transmit_backend_si5351.hpp}`. Reinspect these
contracts and the current source pair at implementation start. This is a design
and validation contract, not implemented or physically qualified behavior.

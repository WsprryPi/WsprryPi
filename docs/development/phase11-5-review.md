# Phase 11.5 target resource and contention review

Latest R2 closure, September 12, 2026: WsprryPico's coordinating
`docs/development/phase11-5-r2-continuation-review.md` and `phase11-5-r2-closure-result.json`
close **R2 7/7 jobs** and retain **R1 5/5** through an explicit source-impact
assessment. Phase 11.5 is **2/6 families**, with R3–R6 open and no full accepted
configuration. The selected candidate is Pico source
2e43110f05304efdc2ae25c298baa0ef6426955b, physical 138 MHz/divider 1/RAM/listener on.

Three browser Tone jobs and actual production-owned QRSS ETE/33 s retain their
049cc929 firmware identities. The latter used unchanged production source
6f65d5c7d202569102459ab68d7c9ea079b96f35 and executable SHA-256
122ed0e4bd752e457419c4df5433c3fca1a4a88677a3db3ebd7e60e783ba5d1c.
After a WSPR LOAD OOM exposed transient Pico buffer allocations, the repaired
2e43110 image completed USB FSKCW/35 s, DFCW/17 s and WSPR/110.592 s during
full N300/USB360 observation. Both failed original assessments remain preserved.

The final run's administrative STATUS response took at most 2.218080153 s,
within its five-second deadline, while its largest request-start gap was
2.322236389 s. The host sent the next request 104.156236 ms after the delayed
reply. The user explicitly accepted one-Hz polling while no reply is outstanding,
with in-flight delays retained as telemetry. A separate amended offline audit
passed, retaining the original strict two-second-gap failure. This changes the
campaign's administrative assessment; the production scheduler and RF timing
implementation were unchanged. All eight browser actions and fourteen GETs passed.

The opt-in helper now admits only the exact QRSS ETE production plan when
explicitly requested, publishes its pre-dispatch binding atomically, and records
failed load completion for the observer. Default invocation remains RF-off.
Twelve helper tests passed. Coordinating Pico checks passed all 53 CTest groups;
147 Phase 11.5 Python tests were discovered, 146 passed and one private-fixture
test was skipped locally. The new-image private audit passed on wspr5; all nine
mutations were rejected and intact evidence passed again. The production QRSS
review separately rejected nine mutations. Final assessment found no open
R2 findings.

Both Picos and original configurations were restored, Empty/inactive/unowned.
A boot: 587c672d4267e467649bb43765542284; B unchanged:
feffcd075ab6cb0b74e7e0c2fde6c87f. Host networking, permanent time.local/GPS-PPS
and installed WsprryPi PID 1957 were verified restored/unchanged. CONFIG writes
are 34/34. No SDR calibration, per-band RF or spectral qualification is claimed.
Documentation Impact: this companion report, opt-in test helper and its tests
changed; the production application, installed binary and operator UI did not.

The following entries retain their original historical scope.


Earlier three-Tone execution, September 12, 2026: the coordinating Pico
`phase11-5-r2-amended-review.md` records R1 5/5 on clean firmware
049cc929143bdec6ec32817f6df0c73a9637cdf5 at physical 138 MHz/divider 1/RAM/listener
on. Three browser-owned ten-second Tone jobs passed the full N300/USB360 audit.
R2 remains 3/7 jobs complete; four modes and actual production-owned/USB-reference
submission remain open. Phase 11.5 remains 1/6 families with no accepted configuration.

The unchanged production source 6f65d5c7d202569102459ab68d7c9ea079b96f35 observed
317 STATUS requests in N300, maximum start gap 1.914 s and native TLS
write-to-response 1.797 s. All eight browser actions/fourteen GETs were captured.
The three launch delays were 15, 8 and 8 microseconds of post-enable software
telemetry. This does not qualify electrical-edge UTC accuracy or spectral output.
The installed executable and PID 1957 were unchanged. Both Picos, original
configurations, host networking and permanent time.local/GPS-PPS were restored;
cumulative configuration writes are 30/32.

The full R1 repeat was broader than the requested affected-check revalidation.
Future fixes must identify invalidated assertion IDs and reuse unaffected evidence;
a helper/documentation change or new revision label does not automatically restart
R1. Remaining R2 work should reuse the unchanged frozen candidate's valid baseline.

Validation: eleven Pi load-helper tests passed; coordinating Pico validation
passed 129 local tests and one private evidence test on wspr5, including nine
completed-evidence mutations and repeated intact audits. Historical failures below
remain preserved. Documentation Impact: this companion review records the new
measurements and evidence-reuse rule; no Pi source, operator UI, installed service
or separate operator-documentation repository changed.

The following entries retain their original historical scope.

Current R1 execution, September 12, 2026: R1 remains blocked in the coordinating
WsprryPico `docs/development/phase11-5-r1-review.md` and result JSON. Four exact
artifact/layout checks passed; inhibited startup acquired its address but failed
time-server DNS readiness before production/browser intervals began. Counts are
1/5 R1 assertions passed, 1 blocked, 3 not run; the overall phase remains 0/6
families closed with no accepted configuration. A native client also timed out;
a later captured query succeeded. The intermittent cause remains unresolved.
Both Picos and host networking were restored; installed WsprryPi PID 1957 was
preserved. No RF jobs ran.

This R1 slice adds an explicit N browser profile to
`src/tests/phase115_production_load.py`: page/capabilities/status/config
initialization, six distributed Refresh actions and one reload, with action
start/finish records. The old periodic status plus page/CSS/JS workload remains
S and retains its original behavior. Ten hardware-free load-driver tests pass;
the new profile was not exercised on target because readiness failed first.
The production executable remains source 6f65d5c7d202569102459ab68d7c9ea079b96f35,
SHA-256 122ed0e4bd752e457419c4df5433c3fca1a4a88677a3db3ebd7e60e783ba5d1c;
no production application or installed executable changed.

Documentation Impact: this companion review and Pico R1 plan/evidence pointers
changed. Operator behavior and WTP/browser API contracts are unchanged. The
historical planning and implementation records below retain their original scope.


Current planning update, September 12, 2026: WsprryPico's authoritative
`docs/development/phase11-5-plan.md` now groups the work into six families:
resources/baseline; normal RF execution; saturation/reclamation;
authority/interruption; network/storage/autonomous lifecycle; sustained mixed
operation. Its `phase11-5-acceptance-ledger.md` records 0 of 6 revised families
closed and no accepted configuration. The historical 2/20 count belongs to the
older exact image; it is not a current acceptance count.

The candidate source is `e20ae8bea2d5237af017dbd5f73bfe9332ce144e`, physical
138 MHz. Its bounded RF-idle check passed, using production source
`6f65d5c7d202569102459ab68d7c9ea079b96f35`; TX credit-wait/preservation counters
were zero. Active RF contention and historical STATUS-stall causation remain
open. The latest recorded setup was restored; this documentation task performs
no live-device verification or operational action.

Normal operator interaction is separate from the old synthetic page/CSS/JS
reload and periodic browser polling stress. Idle production 1 Hz cadence and
state-specific scheduler keepalives remain distinct. Per-request service,
resource, authority and RF timing requirements remain; earlier failures keep
their original criteria. All 20 legacy cases are mapped in the Pico plan, with
physical coverage focused on distinct resource and contention paths.

**Implementation remains outstanding.** This update changes documentation only.
The Pi production load driver and Pico helpers/20-case validator still implement
the legacy campaign. A later code-authorized slice must add the revised profiles,
subcase evidence/closure accounting and concrete packets without bypassing old
safety checks. Current source/executable identities must be bound deliberately;
metadata-only edits do not require rebuilding firmware or repeating hardware.

Documentation Impact: this companion development review and the coordinating
Pico plan/ledger/review pointers are updated. WTP/network/browser API contracts,
operator behavior, source, tests, runners and installed services are unchanged.
The separate operator-documentation follow-up remains the paths listed in the
Pico plan, after measured acceptance. The chronological evidence below is retained.

Status: **OPEN**. WsprryPico coordinates the joint plan, case register and target
measurement definitions in its `docs/development/phase11-5-*.md` and
`phase11-5-register.json`. No physical configuration is accepted yet.
Phase 11.4 remains closed within its bounded inhibited matrix and eight-hour soak.

## Scope and source identity

The user confirmed this division on September 11, 2026:

- 11.5 accepts resources and contention for each PIO clock selected for 11.6,
  binding memory, both stacks, refill/launch and contention to exact firmware
  and clock, with deadlines recalculated for every tested clock.
- 11.6 performs per-band/per-mode conducted acceptance at those selected clocks.
  Alternatives are investigated only for failure or unresolved selection;
  selecting another clock repeats the affected 11.5 checks before acceptance.
- 13 owns the systematic band x mode x clock comparison, final supported
  configurations, filters, spectral qualification and release firmware.

The selected candidate is 138 MHz, system clock equal to PIO sample clock,
PIO divider 1. The accepted list is empty. Physical 132 and 150 MHz checks are
untested; the earlier inhibited 150 MHz soak does not qualify either physical
worker timing or RF. This work does not perform a comprehensive RF clock sweep.

The Mac checkout began clean on devel at
`89f23e5d10c8a46ead9f37c7cefa8867280ac4df`. After direct user authorization it
fast-forwarded to `a4eb591813b19ece8bba30f6ca072066670c7d1d`. The incoming scheduler
fix `923ab570fe53ef2ccca7d12e519c9dc36adf7e93` sends bounded STATUS keepalives
while awaiting a future start. It belongs in the campaign's production baseline.
The wspr5 source also reported a4eb591; its independently hashed installed
executable differs from that source identity and has not been replaced.

Pi owns production integration, load generation and host evidence. Pico owns
firmware, WTP and the target measurements. No protocol, transport fallback,
identity bypass, global trust change or UI redesign is introduced here.

## Findings and changes

The runtime test consumed `tls-fixtures` without depending on their generating
TLS target. The initial parallel run failed on a missing runtime-rotation
certificate; serial TLS then runtime tests passed. `wtp-network-runtime-test`
now depends on `wtp-tls-test`. The repaired parallel invocation passed, including
2,012 production configuration/runtime checks. This is a fixture dependency
repair, not a change to production scheduling or recovery.

Current network guidance incorrectly described the full WTP device ID as the
normal hostname and MAC suffixes as optional aliases. `docs/wtp-network.md` now
uses the accepted last-six-station-MAC default, retains independent full-device
validation and certified custom names, and states the DHCP plus mDNS workflow.
No static-address requirement is introduced.

Pico's review found an expensive core-1 stack scan on every RPC, overlapping
service-gap/poll interpretations, incomplete heap/fragmentation observations and
missing matched refill evidence. Its scoped instrumentation and evidence checker
address parts of those findings; physical memory, stack, launch/tail, observer
cost and contention gates remain open. SDK allocator panic-on-exhaustion remains
an explicit unresolved acceptance limitation, not a claimed graceful rejection.

## Validation and evidence boundaries

Hardware-free checks run on the Mac from `src`:

- `make -j4 wtp-production-test wtp-network-runtime-test wtp-api-test
  wtp-network-process-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
  WTP_NETWORK_BUILD_DIR=build/phase11-5-network`: PASS after dependency repair.
  This includes TLS tests, generated ephemeral fixtures, API and process checks.
- `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j1 semantics-test-portable SUDO=`:
  PASS for the explicitly simulated portable subset; it is not the Linux full
  physical-backend semantics profile.
- `make -j1 wtp-network-interop-test SUDO= WTP_NETWORK_BUILD_DIR=build/phase11-5-network`
  with explicit Pico and Mbed TLS source paths: PASS against clean Pico
  `ce1c339a976e795e90c38c4a57578f9c8ed75615` (repeated after the initial
  ab87031 pass) and Mbed TLS
  `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The build uses Pico's hash-checked
  alert-delivery overlay, not an unpatched upstream TLS approximation. Named
  and direct-IP identities, shared management, finite jobs, lost-response replay,
  ownership, boot/device mismatch and same-session recovery passed, including
  the actual 60-second future scheduled wait. macOS could not bind the second
  loopback IPv4 address; that actual rebind subcase was explicitly skipped while
  injected-address checks ran. It remains a native-Linux/physical fixture gate.
- Reciprocal Pico-owned actual-client and network interop: both PASS against
  clean Pi `76fd1018868551b93ca46bef2e7c2ef28fb75993` (85.98 seconds).
  The later companion changes update only this evidence and the Pico source pin;
  the production client and scheduler sources are unchanged.

Logs and original failed attempts remain in the coordinating checkout's ignored
`build/phase11-5`. Injected transport failures and host device-tree diagnostics
are part of the portable tests, not observations of a transmitting Pico.
No installed service, binary, INI, network interface or Pico firmware was changed.

Authorized P0 readbacks on wspr5 verified both exact Pico serials as inhibited,
empty, unowned, output false; identities and boots are recorded in the joint
plan. User-confirmed wiring is 60 dB fixed attenuation per combiner input, 50 ohm
attenuator loads and no filters. A separate user-requested GPSDO status read
reported both outputs already enabled at 10 MHz LOW, with satellite/PLL lock;
no output setting was changed. These observations are not an RF campaign.

## Documentation Impact

Updated: this companion review and current network configuration guidance;
the coordinating repository holds the plan, metric definitions, clock register
and historical 11.4 supersession pointers.

Considered and unchanged: normative WTP, shared browser API, architecture and UI.
No interface code changed, so no Impeccable or UI visual workflow was required.

Still required after measured acceptance, in the separate read-only
Wsprry_Pi_Docs repository:
`docs/Advanced_Operations/ini_configuration/transmitter_backends.md`,
`docs/Command_Line_Operations/transmitter_backends.md`,
`docs/User_Interface/Setup/Transmitter/index.md`,
`docs/User_Interface/Operations/index.md`, `docs/Advanced_Operations/rest_api.md`
and `docs/User_Interface/Maintenance/network_safety.md`.
Only measured supported combinations should be published there.


## Coordinated physical pilot disposition

Pico P1b at clean firmware ce1c339a976e/138 MHz submitted one finite 10-second
135.5 kHz Tone job. It failed the frozen 25% predecessor reserve requirement
(1,910/16,384 words) and later reported Failed/DEVICE_FAULT, with authoritative
output false and no owner. The maximum worker service gap also exceeded its
2,849,391 ns budget. The two remaining jobs were not submitted. The actual Pi
production client was not the pilot controller; no Pi executable or service
change was involved. Both source/interop validation and this failed physical
USB diagnostic remain distinct evidence.

Pico B retained its original inhibited firmware and boot, empty/unowned/inactive.
The normal restorer refused the failed pilot. Recovery-only authorization is
pending in the coordinating task; the failed state and original captures are
preserved. The authoritative records are Pico phase11-5-pilot-attempt1.json
(host parser failure before flashing) and phase11-5-pilot-attempt2.json (physical
margin and terminal failure). No clock is accepted and no threshold was relaxed.


## Recovery completed; unqualified terminal repair

Separately authorized recovery restored Pico A's original inhibited firmware
802c91a7b86e-dirty with boot 4571042e06f139bc185e862482082291. Both boards are
empty, unowned and authoritatively output inactive; Pico B's original boot is
unchanged. Host interfaces/routes, boot, installed executable and PID 1957 match
preflight; the Wi-Fi recovery timer remains active/enabled. Failed pilot evidence
and units remain preserved. Pico's phase11-5-recovery-result.json is authoritative.

A new host regression reproduced the mismatch between the physical stream's
existing 100-microsecond finite-tail acknowledgement allowance and JobService's
nominal-end watchdog. Pico repairs this internal contract with a capped local
engine allowance; nonlocal engines retain their old watchdog and no extra
waveform samples are permitted. The refill reserve/service-gap budgets remain
unchanged. This repair is not flashed or physically qualified; 11.5 remains OPEN.


Final actual-server interoperability also PASS against clean repaired Pico
713cb16749c5647b6ac1326c18d97add9c975935. An overlapping invocation failed TLS
initialization while the reciprocal fixture occupied the same fixed loopback
port; its isolated rerun passed, and both attempts remain recorded. The actual
second-address macOS skip remains open. Reciprocal Pico-owned gates both PASS
against clean Pi 76fd101 (86.04 seconds). Main-checkout changes since that pin
are evidence and test configuration only; production sources are unchanged.
All repaired Pico builds remain unflashed and unaccepted.

## SRAM remediation candidate, September 11

Pico clean `0d9bb44a6b91679bd174c39ac068d5d9cc74e9c5` places the finite waveform
renderer in initialized SRAM, retaining the 138 MHz clock, original sample
algorithm, diagnostic workload and thresholds. Its linked checker rejects
flash dependencies and incomplete instruction coverage. Console INFO and the
new pilot-v2 packet bind renderer placement explicitly. This is a candidate,
not a physical pass; the failed P1 result and inhibited restoration remain
unchanged. No new clock/configuration is accepted.

The companion actual-server fixture now pins that clean Pico revision. Its
native host gate passed the actual 60-second scheduled wait and finite-job,
shared-management, ownership, partial-I/O, lost-request replay and changed-identity
cases. macOS actual second-IPv4 address rebind remains skipped (Errno 49); the
injected-address checks passed. An initial invocation from the repository root
found no Make target; the documented `src` invocation passed. Neither invocation
opened hardware. Pico-owned client gates used unchanged clean Pi production
source `76fd1018868551b93ca46bef2e7c2ef28fb75993` and also passed.

Pico's `phase11-5-remediation-review.md` records three adversarial assessments,
repairs and negative tests; `phase11-5-remediation-images.json` binds artifacts.
The prepared `phase11-5-remediation-pilot.md` requires new explicit flash/RF
authorization. Prior P1/recovery approvals do not cover its new image/jobs.

Documentation Impact: updated this evidence record and the actual-server source
pin. Production WTP/USB/TLS behavior, browser API/UI and normative protocol are
unchanged. Wsprry_Pi_Docs stays read-only; its operating-envelope publication
waits for physical acceptance. Phase 11.5/11.6/13 ownership and all open resource
and conducted-RF gates remain as previously recorded.

## Authorized P2 physical result

The user subsequently authorized the frozen P2 flashing/RF packet. Pico A ran
exactly three 10-second 135.5 kHz Tone jobs on clean 0d9bb44a6b91, SRAM renderer,
138 MHz, divider 1, and passed all frozen pilot checks. Minimum full-buffer
reserve was 7674/16384 (46.84%; minimum 25%), short reserve 2060/2312 (89.10%),
and maximum worker service gap 2.176 ms (limit 2.849391 ms). All three jobs
completed and released, with 7,902 DMA IRQs, three tail completions, 7,896 matched
running refills and no recorded DMA errors or fault events.

Pico A was restored to original inhibited 802c91a7b86e-dirty, new boot
0b1cb103440c63757e5f326660be75d2. Pico B remained on original inhibited firmware
and boot 4e2fb851c08b278dd4b977104d2c2aaa. Independent readbacks verified both
empty, unowned and output false. Host boot/radios/services remained unchanged;
the new bounded supervisor finished successfully. No repeat RF run was needed.

Pico's phase11-5-remediation-result.json and updated review bind exact image,
packet, device/boot, raw evidence and final restoration. Independent raw-wire
and Console reconstruction passed, as did six adversarial evidence mutations.
Original failed attempts remain retained. This passes only the three-job
resource diagnostic; it does not accept a clock, close full A-G/resource gates,
prove an isolated XIP cause, or qualify spectra, filters, other bands/modes or
clocks. The accepted-configuration list remains empty.

Documentation Impact: updated the companion evidence record only. No production,
protocol, browser API/UI or operator-documentation behavior changed. Full
resource acceptance and the previously listed Wsprry_Pi_Docs follow-up remain.


## Closure continuation: instrumentation and production timer

Pico clean source `3eac6ec030963a5318515ce5f4683acf6fa88506` instruments the linked
newlib entry points, including stdio and transient realloc paths, and provides
idle-only nullable allocation probes. The companion actual-server fixture pins
that source. The first companion interop attempt reported a host dry-run missed
start; its original failure is retained. The fixture now logs missed-start timing
without changing the simulator or acceptance thresholds. The diagnostic repeat
passed the complete finite-job, 60-second wait, replay/ownership/identity cases.
The first miss remains unclassified beyond the host dry-run terminal report; a
passing repeat does not erase it. macOS second-IPv4 rebind is still skipped.

Review found the bounded-tone WebSocket watchdog used RF duration as a deadline
from host submission even though WTP schedules the RF start at least eight seconds
later. The scheduling result now supplies a WTP-specific host cleanup delay:
finite RF duration plus the negotiated preparation lead and five-second nominal
confirmation allowance. The committed RF duration and one-nanosecond off tail
remain unchanged. Other backends retain their previous duration-based timer;
explicit operator stop remains immediate. Overflow rejects before committing.

A new production regression exercises the actual tone scheduler, parent bridge,
WTP application and scripted peer: ten seconds RF-on completes at its scheduled
slot, before the adjusted cleanup deadline, with the original off-tail encoding.
Fixture iterations corrected the explicit unqualified-mode setting, ensured the
injected runtime dies before its fake clock, and asserted the actual LOAD body
and off-tail schema. Failed test attempts are retained; they were hardware-free.

The tone response regression then exposed a real build dependency collision:
`build/dep/test_tone_response.d` named the release object, so changing the shared
result header did not rebuild the older debug object. Dependency files now live
beside each debug/release/backend-profile object. Existing objects missing those
files rebuild once. A real-compiler regression extracts the production Make rules
and verifies both modes, two profiles, header ABI changes and missing-file
migration. Rebuilt production (6,847 checks), tone request/response, WebSocket
lifecycle and WTP UI tests pass. Historical shared dependency files are retained.

Pico P3 subsequently ran three finite ten-second 135.5 kHz Tone jobs at 138 MHz
on that instrumented image and restored its original inhibited firmware. The
Pico-owned raw audit and final independent readbacks are separate evidence.
This remains a bounded diagnostic; full A-G/N, stack/heap gates and the accepted
configuration list remain open. No installed WsprryPi binary/configuration,
existing service, radio, GPSDO or operator-manual repository was changed.

## Production idle session repair

The parent event loop now observes a healthy Idle/Ready WTP session at most once
per second. Skipped WSPR windows use the existing serialized worker for the same
read-only observation. This prevents the device's five-second idle connection
timeout while preserving explicit recovery, foreign ownership and pending-job
policy. No reconnect, LOAD, ARM, ABORT or RELEASE is added by this poll.

Application regressions passed 39,276 checks, and production regressions passed
6,853 checks. They cover the actual bounded-tone scheduling bridge, skipped
windows, idle refresh, foreign ownership and transport failure. The native
server fixture now pins the clean stack-guard Pico source `598a5ad7fa827f24029949fae684dc837127e599`.
Native TLS and final clean executable validation follow this source freeze.

The actual native TLS suite passed against Pico `598a5ad7fa827f24029949fae684dc837127e599`, including an eight-second idle connection retained by production STATUS, the unchanged 60-second scheduled wait, finite jobs, replay, ownership and identity recovery. Two prior invocations stopped at source-pin validation before any interoperability case; their logs are retained. Native second-IPv4 rebind remains unavailable on macOS and requires the isolated Linux fixture. No additional actionable issue remained in the source re-review.

## Final prepared pair and isolated load tooling

The actual-server pin now selects clean Pico
`6e2ddc9e476986046de74d18cbcc3a2f6b64d142`. Both native interoperability
directions passed with the production application from clean
`fb0a2eb50c1ea1792324139412990341592db452`; detached source snapshots preserve
that pair while test/evidence metadata advances. The separate Linux build at
`/home/pi/phase11-5-closure-fb0a2eb/source/src/build/bin/phase115-pi-fb0a2eb`
has SHA-256 `08af5ad6dd21592a7ff90d898dd971a1e740cbb3836f65e74ba3816b960f4507`.
It was built with `BACKENDS=simulated ANCILLARY_GPIO=0` and has not been installed
or connected to a Pico in this stage.

The opt-in `src/tests/phase115_production_load.py` launches that exact program
inside the approved client namespaces, with a validated private WTP INI, zero
correction, ancillary outputs disabled, and separate host API ports. It observes
the host API and generates manual-equivalent authenticated Pico status/page/asset
GETs. It submits no RF jobs. A clean exit is explicitly only a capture requiring
an independent WTP wire/rate audit. USB and host health remain independent.

`src/tests/network/phase115_tls_observer.c` is a separately built LD_PRELOAD
evidence observer, never a production link or installed component. It records
connection identities, timestamps, successful plaintext I/O and peer fingerprints
with serialized durable writes. Synchronous logging is part of host observer
cost. It does not turn process exit or a missing record into inactive output.
On wspr5, its library SHA-256 is
`4e9586fbe7be90936c503359bd7fdd6355247aff264323df74a3653d9bb9adc0`.
The Linux loopback test passed eight concurrent mutual-TLS streams and exact
plaintext reconstruction; six corrupted-log variants were rejected. The first
fixture omitted mutual client authentication and failed the peer-certificate
requirement; the corrected fixture passed. This was hardware-free.

Adversarial review repaired a false-success finish after an exception with a
zero-exit child, bounded browser response time independently of partial input,
and validated the explicit 135.5 kHz policy and correction settings. Three
load-driver regression methods pass, including scope drift, default no-op and
wrong-peer failure with a clean child exit. Neither the load driver nor its
observer has produced physical N/A-G acceptance yet.

Pico's N0 fixture was restored after a retained asynchronous-cleanup verification
failure and read-only reconciliation. Both Picos retain their original inhibited
boots and are empty, unowned and output false. Installed WsprryPi remains PID
1957, with unchanged binary/INI, and the recovery timer and both test radios'
power saving are restored. The separate N1 lifecycle/management packet awaits
the missing authorization required by section 5 of the original request.

Repeat source/evidence review found no additional actionable issue in this
prepared scope. Full A-G execution, resource/stack/timing/observer-cost evidence,
sustained mixed load and physical restoration acceptance remain open. No clock
configuration is accepted. No browser API/UI, WTP identity contract or operator
manual was changed; the previously listed operator-documentation follow-up
remains pending actual acceptance.

## N1 inhibited execution and host corrections

The first N1 attempt loaded the exact 6e2ddc9 inhibited image and passed a
360-second quiet observation with reconstructed USB evidence. The actual
fb0a2eb application then exited before readiness: the private INI generator had
lowercased canonical keys, and its Python preflight was case-insensitive. Both
boards and the host fixture were restored. No RF job was submitted. The load
validator now uses exact key spelling; regression cases reject lowercased
sections. The actual Linux executable also round-tripped the corrected WTP
settings and inactive policy through its config API in an isolated loopback
namespace with the simulated runtime override. Two regression-harness assertion
failures were corrected and retained separately from the device campaign.

The corrected N1 continuation retained the prior configuration-write counts
and original six-hour deadline. Its fresh inhibited quiet observation passed.
The actual controller then completed 180 seconds with no device/observer fault,
but independent plaintext analysis found only 175 nominal STATUS requests
(0.9722 Hz). Median spacing was 1.025904 s, maximum 1.087699 s. The acceptance
sequence stopped before browser load or RF; this failed rate result is retained.

The production idle scheduler now anchors its one-second slots instead of
shifting each slot by reply and caller-loop delay. It skips missed slots after
a stall and performs at most one observation per invocation. It preserves the
existing no-reconnect, no-submission, foreign-owner and active-work gates.
A 180-second simulated-clock regression with 25 ms per-exchange foreground cost
requires at least 179 observations and checks that a long stall causes no burst.
The affected suites pass: 39,834 application checks, 6,853 production checks,
existing UI source tests, and four load-validator tests. New target execution
must bind to the rebuilt executable; the fb0a2eb result cannot qualify it.

Documentation Impact: this development review and the Pico-owned N1 evidence
and continuation records. Product configuration/API contracts and operator UI
are unchanged. Operator-manual follow-up remains in the joint Phase 11.5 plan;
that repository is unchanged. Phase 11.5 and physical clock acceptance remain
OPEN.


The corrected executable SHA-256 is
`122ed0e4bd752e457419c4df5433c3fca1a4a88677a3db3ebd7e60e783ba5d1c`.
On inhibited Pico 6e2ddc9, it achieved 179 STATUS reads in 180 seconds and a
0.638056-second maximum native-write-entry-to-response interval. The first
browser interval then failed: its serial status/page/style/script burst took
6.437 seconds and delayed the next five-second status poll. That attempt is
retained. The load scheduler now services status first and fetches one reload
asset per five-second slot, keeping all declared request counts and thresholds.
A regression uses the measured reply costs and rejects overload. The corrected
combined interval passed raw-wire auditing: 179 controller STATUS reads, 36
browser status reads and six requests each for page/style/script; maximum
controller response interval 1.311965149 seconds. The inhibited quiet-memory comparison also passed (568 bytes retained delta).
Physical A2 later failed when a USB INFO reply took 2.964660695 seconds under
nominal TLS load; the Pico-owned reply-priority repair requires target repetition.
This is not a physical RF qualification claim.

Observer format 2 records native write entry, success/failure and successful
read bytes. It supports the complete 65,552-byte WTP frame and retains version-1
decoding. Eight concurrent Linux loopback streams and corrupted/lifecycle records
were checked. Scheduler queue time remains outside this observer's latency
metric. Adversarial review also required preserving errno after logging a failed
write; the frozen nominal capture observer predates that correction and records
no failed writes. Negative-call testing must use the corrected observer.

The corrected observer passed its Linux loopback regression: 3,450 durable
records across eight TLS streams, complete 65,552-byte payloads, and six rejected
corruptions. All six load-driver regressions passed. The optional shared browser
control slots are prepared for A3; their fake-clock regression preserves all 36
status requests and six complete reloads while scheduling 13 control requests.
That regression does not establish physical A3 acceptance. The actual A2 rerun
continues to use the previously frozen browser-priority driver and observer.

A follow-up review found the isolated configuration regression still imported
the removed global binary hash. It now retains its original fb0a2eb artifact
pin locally. Its run-entry import and namespace rejection were exercised; the
previous actual fb0a2eb configuration round-trip remains historical evidence.
The later candidate staging failure was file ownership: a root tar extraction
preserved the Mac owner ID on TLS credentials. Contents were unchanged; the
Pico campaign records the corrected owner and preserved failed process.


The preserved N1s physical A2 attempt failed browser cadence on Pico firmware
4ca4494 at 138 MHz: the first status request took 6.013358513 seconds, followed
by a 2.103315077-second page fetch. No RF job was submitted; Pico and host
restoration completed. The next load-runner revision prioritizes an already-due
status poll over an asset or control grant after a slow response. It preserves
all 36 status and 18 asset requests per N180 interval and the existing one-second
sampling lateness limit. Seven deterministic runner tests pass, including one
5.5-second status response with all requests retained. The observed 6.013-second
response still fails the unchanged timing check; this scheduling repair does
not relabel the failed target result. New target evidence remains required.

Documentation Impact: records the N1s failure and the corrected scheduling
order. The installed application, production binary, protocol limits and RF
acceptance status are unchanged.


## Asset scheduling during physical RF

The Pico A3 run preserved a real browser scheduling failure: an asset took
2.508 seconds after starting with only 1.139 seconds before the next status
slot. Status polling then exceeded its unchanged one-second lateness bound.
A regression using the measured response sequence fails the previous runner.
The correction defers an asset when its observed maximum duration will not fit
within the existing one-second status scheduling allowance. It still requires all status polls and every asset
inside the same thirty-second reload window; it does not reduce load, change
the lateness threshold or excuse a response that exceeds the status deadline.
Eight hardware-free runner regressions pass, including the preserved 6.013-second
response failure. The existing production binary is unchanged.

Documentation impact: this note records the evidence, scheduling-only scope and
unchanged acceptance thresholds. Actual target acceptance remains in the Pico
case register; this source change does not mark a hardware case passed.


## N1t final failed attempt and restoration

The follow-up `asset-slack` attempt using driver source `6703818` also failed
browser cadence: a stylesheet request at N+42.899 seconds took 3.260 seconds,
so the N+45-second status poll exceeded the unchanged one-second allowance.
Previous measured asset cost did not bound the next response. The scheduling
change is not a demonstrated solution to the remaining contention failure.
No further unchanged RF retry followed.

One job completed in each of three failed A3 attempts. Exact reconciliation
confirmed Complete/inactive/unowned, and guarded release retained all three
records before restoring Pico A's original inhibited image and configuration.
Pico B remained unchanged. Normal host networking and the recovery timer were
restored; installed `wsprrypi.service` retained PID 1957. The full private archive
`phase115-n1t-preserved-final.tar.gz` has SHA-256
`e13a5388017d2a2065e1028c823abd509a0f7028b07ed5192014c660a50b8cb6`.

The coordinating Pico repository records `phase11-5-n1t-result.json` and the
subsequent activity-snapshot source repair. Its main loop no longer copies full
status/history merely to read current activity. Host checks pass; new target
measurements remain necessary. Phase 11.5 remains OPEN with A1/A2 closed (2/20),
A3 failed and no accepted clock configuration. The production binary was not
replaced, and no Phase 11.6/13 RF qualification is inferred.

Documentation Impact: updated this development evidence and the coordinating
Pico result/register. Operator settings, protocol and normal workflow are
unchanged, so no operator-documentation change is required for this repair.


## N1u: inhibited conditioning failed production cadence

The approved activity-candidate retest used exact Pico source `4058d3a4a951`,
its inhibited 150 MHz image, the unchanged `6f65d5c7d202` production binary and
`6703818` load driver. The load process completed, but the unchanged raw auditor
rejected a 2.696719-second production STATUS gap against the two-second bound.
There were 179 STATUS requests within N180, one connection and one logical
session. The slowest native TLS write-entry-to-response interval was 2.592629 s.

Independent USB240 checks passed, as did all 36 browser status requests and
six root/style/script requests apiece. These do not waive the production failure.
Full A2/A3 work stopped; the physical candidate was not flashed and no RF job
was submitted. Original inhibited A, unchanged B and normal host networking were
verified restored. The installed service retained PID 1957.

The private archive `phase115-n1u-preserved-final.tar.gz` has SHA-256
`636cf62e56264d3856bfe0202772b907e793ba5c28453c7f1800b4e409624d69`.
A hash-verified local copy reproduced the same auditor failure. The coordinating
Pico result is `docs/development/phase11-5-n1u-result.json`. No new case closed;
the older candidate's A1/A2 passes remain historical and no configuration is
accepted. Packet-level evidence is absent, so the isolated stall cannot yet be
attributed to retransmission or firmware servicing. No root-cause repair or
unchanged retry is claimed.

Documentation Impact: development outcome/evidence updated in both repositories;
operator behavior and settings are unchanged. No operator-documentation change
is required for this result.


## N1v–N1y: timing audit repairs and observed delivery gaps

The coordinating Pico execution prompt and result are in
`docs/development/phase11-5-status-stall-prompt.md`,
`docs/development/phase11-5-status-stall-review.md` and
`docs/development/phase11-5-status-trace-result.json` in WsprryPico.
Three Pico-owned tooling defects were repaired: STATUS start cadence used TLS
write returns, Console diagnostics inherited WTP's signed 32-bit numeric limit,
and an added internal-trace reader could consume INFO sampling time while
reading a burst. WTP parsing and all acceptance thresholds remain unchanged.
Return-only observer evidence cannot establish request-start cadence.

The actual Pi production binary remains source `6f65d5c7d202`, SHA-256
`122ed0e4bd752e457419c4df5433c3fca1a4a88677a3db3ebd7e60e783ba5d1c`,
with load driver `6703818`. Pico firmware remains exact inhibited source
`4058d3a4a951`, 150 MHz, UF2 SHA-256
`0a7d54673e7171ee10275272701de5fbb3cecdc91c097a18eeae496c0922c7b9`.
No production-client or firmware behavior was changed in this repair.

N1v completed a dual-capture N300/USB360 diagnostic. N1w failed before load
startup on the Console parser defect. N1x collected 568 continuous trace events
but stopped after a trace burst delayed an INFO cycle. Those failed attempts
and their successful restorations remain preserved. The repaired N1y reader
completed N300/USB360 with 3846 consecutive trace events and passed independent
load/USB audits: 300 nominal production STATUS starts, maximum start gap
1.090124856 s, maximum native write-to-response delay 0.821016690 s, and maximum
INFO start gap 1.186896401 s. Browser counts were 60 status requests and ten of
each page/asset. Additional NETTRACE traffic makes this diagnostic distinct from
the frozen A2 workload; it earns no acceptance credit.

The internal and host traces identify three browser response segments across
N1x/N1y that were submitted successfully at the Pico boundary, absent from both
host captures, then delivered on retransmission. N1y also captured four client
output packets absent at both the AP capture and the continuous DUT input
boundary. This localizes delivery gaps without identifying the responsible
driver/radio/AP component. The original N1u STATUS stall was not reproduced and
remains unattributed. Its corrected start gap is 2.696674577 s and still fails;
all six historical N1t load intervals pass the corrected audit.

Final A boot is `bbabf4bdffb92c6bb0f04f11929c2262`, original inhibited firmware
and configuration restored, Empty/inactive/unowned. B remains unchanged and
inactive. Normal host networking and the recovery timer are restored; installed
service PID 1957 is unchanged. Cumulative configuration writes are eighteen.
No RF jobs were submitted; all diagnostic fixtures retained the original N1u
absolute cleanup deadline. Final archive SHA-256:
`b7308d25a27ee1abd511120a18a33201961a9667bb261c823df0964749570269`.

The coordinating repository's 57-test host suite and affected follow-up tests
pass. Adversarial review findings were repaired and reassessed. Zero new
acceptance cases closed: historical A1/A2 remain 2/20, no configuration is
accepted, and affected A2/A3 plus the remaining Phase 11.5 cases stay open.
Selected physical 138 MHz remains distinct from the inhibited 150 MHz reference;
physical 132/150 MHz remain untested. Clock changes during 11.6 repeat affected
11.5 checks; systematic band/mode/clock and filter qualification remains Phase 13.

Documentation Impact: development tooling/evidence in Pico and this companion
review. Operator settings, production behavior and normal workflow are unchanged;
no separate operator-documentation change is required.

## N1z: one bounded A2/A3 attempt, stopped at STATUS cadence

The user authorized the proposed single attempt using a fresh two-hour host
cleanup ceiling. The exact production binary `6f65d5c7d202` and browser driver
`6703818` were unchanged. All four A2 inhibited intervals on Pico firmware
`4058d3a4a951`, 150 MHz, passed, including a +16-byte matched quiet heap delta.
The exact 138 MHz physical candidate then failed its single N180 conditioning
interval: 177 nominal STATUS requests versus the minimum 178, and maximum
request-start gap 2,684,961,482 ns versus the two-second limit. No threshold
changed. The coordinated attempt stopped before physical A2 or A3; no RF jobs
were submitted and no retry was performed.

The physical USB240 audit passed. All 36 browser status requests and six each
page/style/script requests completed with HTTP 200 and met their timing bounds.
Two production STATUS responses took 2.581 and 2.470 seconds. Passive AP/client
captures show advanced server TCP sequence numbers before the corresponding
341-byte reply payloads first arrive, consistent with delayed retransmission
recovery. Additional device NETTRACE reads were deliberately absent; the exact
original submission/loss boundary and historical N1u cause remain unproven.
The coordinating result and evidence hashes are in WsprryPico:
`docs/development/phase11-5-single-attempt-result.json` and
`docs/development/phase11-5-single-attempt.md`.

Both boards are verified empty, inactive and unowned. A's original inhibited
firmware/configuration are restored, boot `5b1ae867c8b6888a7a671b7170d66aba`;
B remains unchanged. Host cleanup recorded no failures, the test radios and
namespace are restored, the recovery timer is active and installed service
PID 1957 is unchanged. Cumulative configuration writes are twenty. The complete
private evidence remains on wspr5, archive SHA-256
`c77e802e6eb8ea1f0dc5d9534681459d83de8c45c73caa6a7384754a71da0974`.

Zero new full acceptance cases closed; historical 2/20 remains, with no
accepted physical clock. The complete inhibited result is useful exact-image
progress, but does not substitute for physical A2 or resolve earlier failures.
The next unfinished step is the STATUS delivery blocker before another attempt.
The Pico host suite passed all 57 tests; offline audits reproduced the failed
criteria and checked restoration. Review found no further actionable evidence
record defect. No production implementation or operator behavior changed.

Documentation Impact: coordinating Pico development evidence and this companion
review updated. Operator documentation and UI are unchanged; no change to the
separate operator-documentation repository is required.


## R2 normal mutation lane and first execution — September 12, 2026

Current coordinating status: R1 closed 5/5 on e20ae8b; Phase 11.5 1/6 families.
R2 completed 0/7 jobs: its first Tone became MISSED_START, and six did not run.
There is no accepted configuration. The older R1 failure entries above remain
historical; Pico's current acceptance ledger and R2 review are authoritative.

The user explicitly confirmed unchanged wiring and authorized the bounded R2
host fixture. The first packet attempted three browser-owned finite Tone jobs
under N300/USB360 on physical 138 MHz/RAM/listener firmware. Only the first was
loaded/armed. Its shared local launch guard rejected it, the observer detected
the missed state, and dependent actor work stopped without retry. The normal
host load finished its bounded interval; this is not RF contention acceptance.
Pico A and original configurations were restored; Pico B remained unchanged.
Host networking and permanent time.local/GPS-PPS were restored; installed
WsprryPi PID 1957 and its binary remained unchanged. Counts reached 28/32 writes.

`src/tests/phase115_production_load.py` permits an explicitly identified R2
normal browser mutation lane. Mutations use gaps before the next scheduled
normal action, reserving five seconds for the transaction and one second margin.
All normal page/config/capability/status actions retain their original schedule,
request counts and deadlines. Pending grants are not repeatedly regenerated.
R1 normal observation and the historical synthetic stress profile remain distinct.
This does not implement or qualify production-owned R2 job submission.

Adversarial review checked starvation, final-interval release opportunities,
missing/late actions and legacy compatibility. Eleven helper tests passed,
including a simulated full-duration mutation schedule preserving all fourteen
GETs and every action deadline. Pico's independent raw-evidence audit confirmed
the missed job and guarded restoration; all twelve evidence mutations failed.

The user subsequently selected a launch window ending at the next UTC-second
boundary, with smaller delays reported as telemetry. Pico's shared contract and
firmware implement that rule and preserve full waveform duration after a late
start. The old frozen e20ae8b image retains its old strict guard. New source is
host-tested/cross-linked only; affected R1 and new R2 target checks remain before
acceptance. No new firmware was installed and no new RF job followed the miss.
The WsprryPi production application and installed service are unchanged.

Documentation Impact: this development review and Pico's R2 execution prompt,
packet/evidence records, review/result, ledger and WTP timing contract were
updated. No operator UI changed; visual review was not applicable.
Wsprry_Pi_Docs was considered and remains unchanged pending physically accepted
backend timing/resource limits. No unmeasured acceptance claim was published.

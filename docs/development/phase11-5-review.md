# Phase 11.5 target resource and contention review

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

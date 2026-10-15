# Si5351 readiness investigation — release item 6

October 15, 2026. **Release item 6 closed by explicit maintainer disposition.**
Software repairs and Linux validation passed, followed by two consecutive
identical inhibited wspr1 I2C checks of 1,000 reads each. The maintainer directed
closure if the second check passed; it did. The historical wspr4 SYS_INIT cause
remains unestablished and its failed records are preserved. This acceptance
closes the bounded I2C release item; it does not claim general hardware or RF
qualification. Items 1–6 are complete within their recorded scopes. P15.5, RP1
work, final #423 review, operator-documentation publication, installation and
release remain separate.

## Identity and scope

- Work is directly on parent `devel`, base/HEAD
  `50097e652b4af70d510f4a29d4edf1ffa24d3f0a`. A read-only `git ls-remote origin
  refs/heads/devel` returned the same SHA before implementation.
- Initial `git status --short --branch`, `git diff --check` and
  `git diff --cached --check` passed. The punchlist was the only dirty file;
  nothing was staged. Its pre-existing Phase 15.4 insertion, completion and
  numbering are retained in [the losslessly compressed starting patch](si5351-readiness-evidence/starting-punchlist.patch.gz).
  The punchlist also acquired in-progress item-6 wording during inspection;
  that concurrent edit was preserved.
- The only changed component is ordinary parent-tracked
  `src/WSPR-Transmitter`: backend implementation/private declarations, existing
  fake-I2C transition/controller tests, and component README. No gitlinks or
  nested AGENTS.md apply. Device transport and planner source remain unchanged.
  The only parent-code change is a test fixture-directory rename in
  `src/tests/i2c_bus_inventory_test.cpp`, explained under Linux validation.
- These defects belong in the component that admits physical programming and
  owns readiness and cleanup. Parent-level changes would duplicate the backend
  policy and leave standalone users exposed. Public interfaces, frozen plans,
  calibration, drive/output/reference selections and experimental defaults are
  preserved. No new production settings or injection controls were added.
- At executor return, no commit, staging, push, branch/worktree creation in the
  working repository, or pull request had occurred. A disposable Linux source copy was used
  for tests. Source/file/executable identities accompany the validation record.
- During closeout, unrelated concurrent WTP work appeared, including parent
  configuration/consumer-radio source, network fixtures, `src/Makefile`, the CI
  workflow and UI management JavaScript. It was left untouched. The final
  working-tree receipt lists the exact paths. Linux evidence is pinned to the
  stated base plus this task's source overlay; it does not qualify those newer
  concurrent edits or the combined shared working tree. In particular, the
  tested Makefile/workflow hashes are the original base versions, not the
  concurrently edited working copies.

## Fresh review and historical evidence

Reviewed `si5351_device.{cpp,hpp}`, `si5351_planner.cpp`,
`wspr_transmit_backend_si5351.{cpp,hpp}`, transition tests, startup-quiesce
qualification tests, and component README. Reviewed the original
[execution prompt](si5351-iteration-prompt.md), [campaign report](si5351-results/README.md),
[review](si5351-results/review.md), [matched follow-up](si5351-results/matched-off.md),
`completion.json`, capture manifest, and raw diagnostic text.

The old campaign records experimental 2 m and guarded-default 40 m failures
reporting register 0 = 193 (`0xC1`). RF-disabled diagnostics retained two such
reads in 100, then 11/500 split and 11/500 repeated-start reads at different
indices. Later matched defaults passed both bands. Those passes do not explain
or supersede the failures. Module/part identity was unknown; historical
`throttled=0x50000` is not evidence of a contemporaneous power cause. New wspr1
bus-only observations are separated below; no new wspr4 observation was obtained
here. Historical identities and manifests
remain unchanged, including their unsuccessful attempts.

The [inventory-isolation report](si5351-inventory-isolation-report.md) closes a
management responsiveness issue, with a bounded separate scan process. It does
not bound execution-path I2C, resolve the board's readiness, or authorize device
access now.

Reviewed parent integration from `fbb80fa`, `085ef70`, `f51d7b0`, `3444c9b` and
`50097e6`: Test Tone lifecycle/generation fences, watchdog/restoration feedback,
Phase 15.4 shared resources and fixture repairs remain intact. Native WTP uses
the same backend, frozen Si5351 realization, output-enable admission and cleanup
receipt. Controller cleanup already appends a separate cleanup error to a
primary execution failure; it must receive both intact.

## Manufacturer contract

Primary references retrieved October 15, 2026:
[Skyworks AN619 rev. 0.8](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf),
registers 0–2 and PLL/reference selection; and
[Si5351A/B/C-B datasheet rev. 1.3](https://www.skyworksinc.com/-/media/SkyWorks/SL/documents/public/data-sheets/Si5351-B.pdf),
sections 5, 6.1 and 6.6. These describe the nominal Si5351A contract; the actual
campaign part/package has not been identified. AN619 covers 10-MSOP/20-QFN;
16-QFN requires AN1234 and separate compatibility review.

Register 0 is live status: bit 7 SYS_INIT, 6 PLLB unlock, 5 PLLA unlock, 4 CLKIN
loss, 3 XTAL loss, and bits 1:0 revision. Register 1 is sticky history; register
2 masks interrupts, not readiness. `0xC1` includes SYS_INIT, PLLB unlock and
revision 1; it is not a sticky-only report or proof of chip provenance. This
backend uses PLLA and XA (crystal or external TCXO), so post-programming mask
`0xA8` remains unchanged. Unused PLLB/CLKIN and revision bits do not block that
path. The initialization admission checks SYS_INIT alone, since PLL lock may
require programming. AN619 advises against ordinary RAM access before SYS_INIT
clears. The datasheet permits split status reads; repeated-start diagnostics
also saw the historical indication. Neither transaction style proves electrical
validity. Revision bits cannot identify a chip or clone.

## Hypothesis and evidence table

| Hypothesis | Evidence and contrary evidence | Smallest distinguishing check | Physical permission? | Permitted conclusion |
| --- | --- | --- | --- | --- |
| Valid status versus failed/malformed I2C | Old active read used a zero-initialized value for both valid status and transport failure; both read styles historically saw C1 | Fake register-selection failure, failed/short read and raw-status matrices; independently observe bus under an approved run | No for fakes; yes for bus | Diagnostic software defect confirmed; historical C1 authenticity remains unproven electrically |
| Initialization ordering | Ordinary initialization/startup/drive writes preceded the first readiness read | Busy→ready, persistent busy and cancellation tests assert only emergency disable writes before admission | No for tests; yes for reproduction | Specification-supported ordering repair; not a demonstrated cause of later active SYS_INIT |
| Reference/PLLA behavior | C1 does not assert PLLA unlock or XTAL loss; selected path is XA/PLLA; no reference waveform evidence | Fake A8/20/08 cases; approved reference-level/lock observation during failures | Yes for electrical checks | Gate interpretation retained; no demonstrated reference diagnosis |
| Unknown device/variant | No verified markings, package, provenance or applicable variant register map | Obtain identity and compare correct manufacturer register map before probing | Operator identity confirmation; yes for probing | Cannot accept a clone/variant limitation or infer identity from revision 1 |
| Concurrent ownership | Process mutex serializes individual transactions only; no historical proof of execution-wide or cross-process exclusivity | Establish all process/driver owners and one execution owner before approved diagnostics | Yes for target inspection/changes | Not excluded; no new lock framework or exclusivity claim |
| Power/wiring/electrical | Historical throttle flags, no contemporaneous rail/SDA/SCL/reference measurements | Separately controlled rail/reference/bus observation; then one-factor substitution if separately approved | Yes | No demonstrated power/module defect |
| Cache/partial writes | Errors invalidate cache; reset/output-disable writes bypass elision; active failure also occurred without bursts | Existing partial-burst/scalar/cache tests plus failed/short transport coverage | No for fakes | Software cache contract retained; no proof of exclusive cache ownership on bench |
| Defaults versus experiments | Experimental 2 m and all-experiments-off 40 m failed; later matched defaults passed | Fake default active retunes and inhibited 2 m changes; bounded guarded-default physical comparison | Yes for physical | Cannot blame experiments alone or promote any experimental strategy |

## Confirmed repairs

1. **Status versus transport.** A typed private readiness observation separates
   ready, valid non-ready and transport failure. Errors include operation phase,
   plan/request identity, selected PLL/reference and raw register 0 only when a
   read succeeded. Tone failures include event/tone indices. Failed selection,
   short read and failed read retain transport operation and error text; no
   invented zero status is reported. A polling failure retains the first busy
   sample and any subsequent transport failure.
2. **Initialization admission.** Execution first attempts direct all-output
   inhibition, then performs up to 50 cancellable SYS_INIT reads with the existing
   1 ms waits before crystal-load, startup, drive or tone programming. PLLA/XA
   readiness remains a separate post-programming gate. Before admission fails or
   is cancelled, cleanup performs emergency output inhibition and optional
   readback only; ordinary idle RAM programming is skipped. Startup quiescence
   remains a direct disable operation without readiness/programming; dry-run
   and configure remain zero-I2C. Accepted writes, register confirmation and
   measured RF inactivity are distinct evidence levels.
3. **First fault and cleanup.** Tone errors are copied before cleanup can clear
   device last-error. A simultaneous stop cannot convert an observed readiness
   or programming fault into a clean cancellation. Cleanup captures each failure
   before later operations, reports disable/readback uncertainty separately from
   idle programming, closes the device and retains its receipt through repeated
   cleanup without reopening. Configure begins a fresh receipt. Active readiness
   faults return directly to disabling cleanup; no active polling, recovery or
   re-enable follows a fault. Output admission and finite-duration anchors remain.

The limits are attempts and requested waits, not a strict wall-clock timeout:
blocking Linux open/ioctl/read/write and mutex acquisition may exceed them.
Cancellation is checked between transactions/waits; it cannot interrupt an
arbitrary kernel stall. No stronger transport deadline or ownership guarantee
was introduced. No retry extension or delay tuning was used to obtain a pass.

## Deterministic coverage and executor reassessment

The existing fake adapter now places transport faults by register/occurrence;
controlled waits exercise busy/ready and cancellation. No real device is needed.
New coverage includes all revision values, unused PLLB/CLKIN, SYS_INIT, PLLA/XA
and combined faults; failed address selection, register selection, short scalar
write, short/failed read; initialization ordering; primary/cleanup separation;
readback/disable/idle failures; simultaneous stop and first fault; repeated
cleanup receipts; and absence of re-enable. Existing tests retain interrupted
and partial tone/burst programming, cache invalidation, non-elided reset/output
writes, dry-run zero access, frozen calibration/drive, envelope deadlines,
finite scheduling and ownership admission.

The extra emergency disable moved the original transaction-count cancellation
fixture before its intended first-tone event. Its occurrence was advanced by
one so it still interrupts tone inhibition; initialization cancellation has its
own direct assertion. An executor adversarial pass found cancellation could
hide a simultaneously observed fault; it now tests both stop states for all
cleanup fault cases and retains the fault. A second pass reproduced a stop
arriving during the default output-enable register read that still allowed a
subsequent enable write. Both default and WTP paths now check cancellation after
that read/admission and before the write; tests cover all three initialization,
default-read and WTP-admission boundaries. The pre-repair cancellation fixture
fails deterministically and its output is retained. Follow-up assertions also
caught cancellation being reported as an enable failure; cancellation now stays
a clean interruption when no transport error was observed, while real I/O
failures retain precedence. Polling tests vary later valid status and introduce
a subsequent failed read to check retention of the first valid sample.
A final pass found that cleanup after a configured-but-unexecuted job reset a
failed receipt, allowing a repeated cleanup to report success. A deterministic
[before-repair test](si5351-readiness-evidence/loaded-cleanup-before-repair.log)
reproduced it; all configured-job cleanup paths now retain their result until
configure starts a fresh job. Component and parent validation were rerun.
This is executor review only; the coordinating conversation's independent review
has not occurred.

Validation commands/results and identities are recorded below and in the
[evidence directory](si5351-readiness-evidence/). Physical qualification remains
outstanding regardless of those results.

## Linux validation

Hardware-free Linux tests run as uid/gid 65534 in an isolated Debian 13.6 ARM64
container, GCC 14.2.0, C++20. The image, dependencies, source hashes and sandbox
properties are in [environment.txt](si5351-readiness-evidence/environment.txt),
[container.json](si5351-readiness-evidence/container.json) and
[source-sha256.txt](si5351-readiness-evidence/source-sha256.txt). Network mode is
`none`, no mounts/devices, all capabilities dropped and no-new-privileges set.
There is no host source bind mount. `HOME=/tmp`, `SUDO=` and
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` apply. The image added only missing Avahi
headers before starting the isolated test container; no host dependencies or
operational hardware were changed.

From `/tmp/readiness-source/src/WSPR-Transmitter/src`:

```sh
strace -f -e trace=openat,open,ioctl -o /tmp/si5351-component.strace \
  make -j2 si5351-planner-test si5351-transition-test startup-quiesce-test \
  si5351-startup-quiesce-qualification-test si5351-min-drive-qualification-test \
  transmission-controller-contract-test SUDO=
```

**PASS**, all six targets; see [component.log](si5351-readiness-evidence/component.log).
Only fake qualification tests ran; no live harness was invoked. The log includes
representative valid-C1 and unavailable-status primary errors, separately
retained cleanup I/O errors and output uncertainty.

Negative control: `git show HEAD:src/WSPR-Transmitter/src/` plus each original
backend filename produced an unchanged base `.cpp/.hpp` in `/tmp/si5351-negative`.
Compiled with `g++ -std=c++20 -g -O0 -pthread -I src -I
src/WSPR-Transmitter/src -c .../wspr_transmit_backend_si5351.cpp -o .../backend.o`,
then linked final transition/controller/device/planner objects with that old
backend (`g++ -pthread ... -o /tmp/si5351-negative/test`).
`strace -f -e trace=openat,open,ioctl -o /tmp/si5351-negative.strace
/tmp/si5351-negative/test` exited **1 as expected**, with regression assertion failures;
[retained output](si5351-readiness-evidence/base-negative-control.log.gz) includes
initialization admission, transport evidence, cleanup retention and cancellation
regressions (and the intentionally changed inhibition-sequence fixture).
The focused default-enable cancellation test also failed before its repair:
[before-repair log](si5351-readiness-evidence/cancellation-before-repair.log).
Compilation errors are not counted as negative-test evidence.

From `/tmp/readiness-source/src`, both commands passed with the same environment:

```sh
strace -f -e trace=openat,open,ioctl -o /tmp/si5351-parent-final.strace \
  make -j2 JOBS=2 semantics-test SUDO=
strace -f -e trace=openat,open,ioctl -o /tmp/si5351-integration-final.strace \
  make -j2 JOBS=2 startup-quiesce-parent-test test-tone-lifecycle-test \
  websocket-lifecycle-test wtp-pi-si5351-realization-test \
  wtp-pi-tone-engine-test wtp-pi-authority-test SUDO=
```

**PASS**, full Linux default physical+simulated semantics (not the portable
subset), plus all six explicit integration targets. Full semantics also includes
inventory process/isolation, configuration, cleanup lifecycle, GPIO policy and
UI/source regressions. [Parent full log](si5351-readiness-evidence/parent-full.log),
[integration log](si5351-readiness-evidence/parent-integration.log), and
[exact commands](si5351-readiness-evidence/commands.txt) retain details. Expected
injected startup/cleanup failures, deassert warnings and websocket rejection
messages are present; they are not physical failures. No UI rendering was needed
because UI source/presentation is unchanged.

The unchanged `scripts/ci/assert-no-transmitter-hardware.sh` and an explicit I2C
ioctl check passed against component, base-negative, full-semantics and integration
traces: [audit receipt](si5351-readiness-evidence/hardware-free-audit.txt).
An earlier audit rejected a **temporary regular-file** metadata fixture at
`/tmp/.../dev/i2c-8`; [its receipt](si5351-readiness-evidence/audit-before-fixture-rename.txt)
is retained. The parent test's fixture directory was renamed to `device-fixture`,
preserving every assertion and rerunning full semantics. The audit was not
filtered or weakened, and no adapter access or I2C ioctl occurred in Linux tests.
Complete compressed traces remain locally under ignored
`dist/si5351-readiness-20261015`; hashes are in the environment record.

The diagnostic's ten fake cases passed via
`PYTHONDONTWRITEBYTECODE=1 python3 docs/development/si5351-readiness-evidence/diagnostic_test.py`.
They cover successful reads, SYS_INIT, read/select/combined-transfer faults,
inhibition refusal/write failure, retained cleanup failure and no clock or enable
writes. `bash -n` passed for the supervisor. Task source hashes match the isolated Linux copy; the complete tested build
inputs are identified separately from concurrent working-tree changes.
Final whitespace and Markdown local-link checks passed. No local macOS C++
compilation/linking was attempted. Preliminary setup/compiler mistakes and
intentional negative failures are disclosed in commands.txt.

Not run: portable semantics (full Linux profile used), new CLI simulation
scenarios (simulator unchanged), installed candidate/service deployment,
transmissions, tones, GPIO qualification, receiver/GPSDO operation or RF
measurement. Live wspr1 bus diagnostics below are separate from the hardware-free
audits and do not run the candidate C++ application.

Final executor assessment rechecked live/sticky/revision interpretation,
initialization versus emergency inhibition, primary/cleanup/cancellation ordering,
loaded-job and executed-job cleanup receipts, no stale enable, partial-write cache
invalidation, mandatory safety writes, frozen calibration, WTP admission and Test
Tone integration. No actionable in-scope software finding remained after the
recorded repairs and final reruns. Kernel stalls, cross-process ownership and the
unidentified historical module remain explicitly unproven. The later maintainer
acceptance below does not invent a causal diagnosis or hardware-support limitation.

## Physical scope update — wspr1, I2C only

The maintainer selected `wspr1`, confirmed an attached Si5351 and dummy load
without an SDR, initially permitted transmission, then explicitly clarified:
this work addresses I2C bus communication, not transmissions, and requires no RF
measurement. The operative physical scope is **RF-inhibited I2C communication
and readiness diagnostics only**. No transmission, receiver or GPSDO operation
is planned. The earlier proposed RF run card is withdrawn; no RF test ran.

Read-only preflight identified Raspberry Pi Model A Plus Rev 1.1, ARMv6/32-bit,
Linux `6.12.109+rpt-rpi-v6`, `/dev/i2c-1`, and an active installed
`wsprrypi.service` (PID 463 at inspection), executing
`/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini`.
Installed binary SHA-256:
`e56f8abe0db12175803f0563722503533c912c45fdd9860b4f667d6ee0f21599`;
INI SHA-256:
`9f659e3e217ebf48573a68d0fc7d06cac2d45b1c984e7c5adc23f9dd424bbad5`.
The initial generic INI reader rejected a duplicate `[Band GPIO]` section;
preflight used non-strict parsing without modifying the file. It performed no
I2C transaction. Installed settings and ownership were recorded before the
separately authorized device access below. No installed binary/configuration
replacement occurred.

A different host/module cannot by itself explain the historical wspr4 module's
intermittent indication. Register acceptance, cleanup/readback and electrical RF
behavior remain distinct, but no RF measurement is required for the present I2C
investigation. Any untested RF qualification is recorded as outside this bounded
scope, not as a prerequisite to collecting communication evidence.

Saved configuration is `Transmit=false`, backend `gpio`, Enable on Boot Never;
Si5351 bus/address 1/0x60, saved 25 MHz `external_tcxo`, CLK0, power level 1,
PPM 0.0. These are saved settings, not verified module markings or electrical
reference identity. They will not be applied or changed. The old 27 MHz harness
is unsuitable for programming this different configuration and will not run.

### Reviewed bus-only run card

- wspr1, device `/dev/i2c-1`, address `0x60`; no bus scan, reset,
  clock/PLL/reference/drive programming, output enable, correction change or RF.
  Attempt 1 only read the inhibition precondition. Attempt 2 first writes the
  standard all-output inhibition value `3 = 0xff` and confirms it, then repeats
  that inhibition/readback at cleanup. Mode/frequency/duration:
  **no transmission, 0 seconds RF**.
- [Exact diagnostic source](si5351-readiness-evidence/wspr1-inhibited-status.py)
  is bound to hostname wspr1. Its only two-byte write is `[3, 0xff]`;
  other writes are single-byte register pointers for reads. It requires register
  3 to read `0xff` before sampling; failed inhibition/readback ends sampling.
  It performs at most 500 interleaved split/combined
  live-status read pairs with requested 2 ms waits. SYS_INIT or any malformed/
  failed transport ends sampling immediately; no replacement retry. Other
  status bits are retained without pretending the unused clock chain is locked.
- A 20 s userspace elapsed check and `timeout --foreground --signal=TERM
  --kill-after=5s 60s python3 -u wspr1-inhibited-status.py` supervise the run;
  neither guarantees interruption of a blocked kernel I2C operation. Signals
  request cancellation and final readback; every sample/fault is flushed to JSONL.
- Sampling requires exclusive device access. The installed daemon is running,
  even though its saved backend is GPIO and Transmit is false. To exclude
  discovery/native-WTP interference, the concrete service operation requested
  is `sudo systemctl stop wsprrypi.service`, verify inactive/no I2C descriptor
  owner, run this single diagnostic as unprivileged pi, then
  `sudo systemctl start wsprrypi.service` only after the diagnostic has exited.
  A trap records restoration on ordinary failure; a kernel-blocked surviving
  process is an explicit blocker to restoration/competing access, never a
  reason to claim cleanup succeeded. These service operations require separate
  explicit authorization under AGENTS.md; the subsequent maintainer reply
  granted it for this diagnostic.
- Preserve installed binary/INI bytes and hashes, original service state, every
  failed attempt, script hash and exact invocation. Final register-3 readback
  and process closure are separate cleanup evidence; neither is RF measurement.
  No receiver, GPSDO, cabling change or power-cycle is involved.

The maintainer explicitly authorized the necessary wspr1 operations with
“Do whatever you need to do on wspr1” after reviewing the service-stop request.
The bus-only scope remains in force. The exact
[supervisor/restoration wrapper](si5351-readiness-evidence/wspr1-run.sh) binds
installed-file hashes and the initial active service state. It runs the
unprivileged diagnostic once per attempt from
`/home/pi/si5351-readiness-20261015-item6`,
`/home/pi/si5351-readiness-20261015-item6-r2` and
`/home/pi/si5351-readiness-20261015-item6-r3` respectively.
The approved attempt sources/hashes and restoration receipts are retained
separately. The current diagnostic is identical for attempts 2 and 3; the
standalone wrapper is attempt 2's source, and attempt 3's exact wrapper is retained
in its own evidence directory.

### Hardware results and disposition

1. **Attempt 1, precondition refused (20:34:17–20:34:28 UTC):** register 3 read
   `0x00` before and after, so the script refused all status sampling. No register
   value was written and no transmission was requested. This is an unmet
   inhibition precondition, not a reproduced SYS_INIT or transport failure.
   The complete [attempt record](si5351-readiness-evidence/wspr1-attempt1/)
   is retained. Service active was restored (PID 463 → 14699); binary and INI
   SHA-256 checks passed.
2. **Attempt 2, inhibited I2C (20:36:24–20:36:38 UTC):** standard `3 = 0xff`
   inhibition was accepted and register 3 read back `255` before and after.
   **500 split + 500 repeated-start live-status reads passed** in 3.277791 s.
   Every register-0 byte was `17` (`0x11`), with no SYS_INIT or transport failure.
   Under the nominal register map this includes revision 1 and unused CLKIN
   loss; the PLLA-loss and XA-loss status bits were clear. This does not identify the
   module variant from revision bits. See the
   [complete attempt record](si5351-readiness-evidence/wspr1-attempt2/).
   Service active was restored (PID 14699 → 15034); installed binary/INI hashes
   again matched. Si5351 outputs remain inhibited at register 3 = `0xff`; the
   earlier enable-mask value `0x00` was deliberately not restored.

3. **Attempt 3, identical repeat (20:50:48–20:51:02 UTC):** requested by the
   maintainer with explicit direction to close the item if it passed. The exact
   same diagnostic SHA-256
   `12fa4bb0fffb0bf5c3647d636f63f70bdf8d081da25b843c0bfc6bc2c3848681`
   ran once in a new directory; the supervisor differed only in that directory.
   **500 split + 500 repeated-start reads passed** in 3.102751 s. Every live
   status byte was again `0x11`, with no SYS_INIT, transport or cleanup error.
   Register 3 was `0xff` before and after; service active was restored
   (PID 15034 → 16133), no diagnostic process remained, and installed binary/INI
   hashes matched. [Raw data and restoration receipts](si5351-readiness-evidence/wspr1-attempt3/)
   retain the complete attempt and exact scripts.

All three attempts requested **zero RF-on time**. No PLL, frequency, drive,
reference, PPM, output-enable, receiver or GPSDO programming occurred. Temporary
service stop/start was explicitly authorized; its ordinary installed GPIO backend
lifecycle is not GPIO qualification. No candidate binary was installed. The first
refused attempt remains preserved, alongside both successful inhibited attempts.
Si5351 output inhibition remains `3 = 0xff`.

### Maintainer acceptance and closeout

The initial executor return left item 6 open because the old wspr4 `0xC1` cause
was not established. The maintainer then explicitly directed one identical repeat
and closure if it passed. Attempt 3 met that criterion, so **item 6 is complete
by accepted maintainer disposition**, based on the repaired software, passing
Linux coverage and two consecutive inhibited I2C checks (2,000 status reads total).
The old wspr4 failure is not reclassified as a pass or assigned an unsupported
cause. Further investigation of that original module is no longer an item-6
closure prerequisite under this accepted scope. RF measurement was not required
or performed.

The coordinating conversation's independent review and any later commit/push
remain separate publication work; this acceptance does not assert that those
steps have occurred. No additional C++ repair was needed for the repeat, so
existing source-bound Linux results remain applicable to this task's source
snapshot. Concurrent unrelated working-tree changes remain excluded.

## Executor return state (before publication authorization)

The maintainer subsequently authorized commit and push. The state below and
the manifest's publication flags describe the preserved pre-publication record;
Git history and the exact-head hosted checks identify subsequent publication.

Branch `devel`, base/HEAD `50097e652b4af70d510f4a29d4edf1ffa24d3f0a`.
Task changes are the component backend/private header, transition tests and README;
one parent inventory-test fixture path; this review/evidence; the dated historical
report pointer; and item-6 punchlist disposition. The starting Phase 15.4
punchlist patch remains preserved. All task changes remain unstaged, uncommitted
and unpushed. Hosted CI was not triggered, and no PR/release was created.

The [manifest](si5351-readiness-evidence/manifest.json) binds final tested source,
executables, software results, raw bus observations and artifact hashes.
[Working-tree state](si5351-readiness-evidence/working-tree.txt) includes concurrent
unrelated WTP/UI/build/CI edits left untouched; the combined dirty checkout has
not been qualified by this isolated run. Its independent coordinating review and
any later authorized publication remain separate. Items 1–6 are complete within
their accepted scopes; consolidated operator docs and #423 stay open.

## Documentation Impact

- Updated: component README, this maintainer record and validation evidence;
  punchlist item 6 is complete under the maintainer's repeat-pass acceptance.
  The historical report has a dated pointer only, preserving original
  measurements, failures and manifests.
- Considered unchanged: `docs/simulated-backend.md`, inventory-isolation record,
  `docs/bounded-tone.md`, parent/native WTP and all UI sources. No configuration,
  persistence, protocol, presentation or operator control was changed, so no
  Impeccable render workflow was needed in this backend-only slice.
- Read-only operator review: sibling `WsprryPi-Docs/AGENTS.md` and
  `docs/Command_Line_Operations/transmitter_backends.md`, `cw_modes.md`, and
  `docs/Advanced_Operations/ini_configuration/runtime.md`. The actual sibling
  is `WsprryPi-Docs`, despite the older parent AGENTS.md underscore spelling.
  Its branch/working tree were preserved.
- Still required, separately authorized: Si5351 backend documentation must
  explain initialization versus active faults, transport-status distinction,
  failed cleanup/output uncertainty, and operator recovery through established
  ownership/quiescence without automatic active resume. Document levels 1–4
  as retained 2/4/6/8 mA drive; explain Si5351 fades as full-amplitude duty-cycle
  chopping, deadline/slice limits, and absence of an analog-envelope/spectral
  guarantee. Runtime startup safety remains immediate attempted all-output
  inhibition, not a measured RF-off promise. Reconcile the native Si5351
  qualification wording with the maintainer's accepted readiness disposition.
  No sibling files were edited or published. Consolidated docs and #423 remain
  open.

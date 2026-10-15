# Test Tone lifecycle and status review

Release punchlist item 4; October 15, 2026. Starting source:
`61542aff2e6965b8773053a2429f9b42b863bf98` on `devel`, independently matched to
`origin/devel` before editing. The release punchlist already had user edits;
those edits, including subsequent checklist formatting/status edits, are
preserved separately from the implementation.
During execution, `devel` advanced to `bfa2378a694087c5964f4137519747005f811dde`;
review confirmed that commit changed only the punchlist, with no source changes.
The subsequent uncommitted Phase 15.4 entry and renumbering remain user-owned
and are excluded from this task's staged punchlist changes.

## Initial review and ownership map

The initial findings were source findings, not live RF reproductions:

- The existing WebSocket command mutex already serialized Start, End, bounded
  commands and their replies. HTTP and ordinary operator Stop still shared
  scheduler state outside that protection.
- End joined the watchdog while holding the command mutex that an expired
  watchdog needed. Bounded rollover also joined while holding state locks.
- A generic Stop cleared scheduler ownership independently of the server's
  deadline. An unqualified old expiry could end a successor.
- Tone startup was asynchronous, while both acknowledgment and UI claimed
  activity. End hid restoration errors behind a generic successful stop.
- Committed frequency/selector checks already existed and were retained.

The scheduler now owns a recursive lifecycle mutex. Its acquisition orders
complete Start, End, operator Stop, cleanup and restoration transactions.
Reentrancy is limited to the existing End-to-configuration path and scheduler
helpers. The owner retains the pre-tone mode/restoration category and immutable
explicit frequency/selector plans. Each accepted transaction gets a unique
process-local generation; changed runtime observations get increasing revisions.

| Path | Coordination and effect |
| --- | --- |
| Start | Lifecycle, then configuration lock; validation/planning before disruption; reserve local output; join predecessor without configuration lock; install a generation-capturing tone callback; commit and request startup. |
| Tone callback | Observation mutex only; record STARTING/COMPLETE/FAILED/CANCELLED for its generation; never join, broadcast, restore or take lifecycle/configuration locks. |
| Polled WTP completion | Try lifecycle coordination before consuming the completion and selecting its callback, so an old report cannot dispatch through a successor's callback. The joined worker never acquires this lock. |
| Main-loop reconciliation | Try lifecycle lock; join finished tone, clean selectors/LED/output ownership, restore eligible prior flow; publish frozen status after releasing lifecycle locks. Failed startup does not resume prior work. |
| End | Lifecycle, then configuration lock; release configuration lock around transmitter join; cleanup before restoration; matching ended generation can replay its result without repeating effects. |
| Operator Stop | Same lifecycle lock for WebSocket and HTTP; disable/persist as requested, fence restoration and callbacks, join/clean/release only with known inactivity. |
| Reload/PATCH | Configuration application tries lifecycle coordination and defers when busy or a tone owns the runtime. Slow inventory validation releases both transaction/update locks. Saved edits and repair/reset do not replace the running tone's backend/mode/selector snapshot. |
| Watchdog | One reusable server worker; monotonic deadline and scheduler generation. Wait-state lock is released before command lock. End cancels without joining. |
| Server stop | Command/lifecycle ordering, cancel and quiesce its owned bounded generation with no restoration; join worker after releasing all locks it needs. Repeated start/stop is serialized. |

`set_config_mtx` is not held while a Test Tone transaction joins its worker.
Ordinary callbacks that request configuration use a nonblocking lifecycle
acquisition and leave deferred work rather than waiting for a joining owner.
WebSocket result delivery retains command ordering but is outside scheduler
lifecycle ownership. Broadcast, legacy send and keep-alive paths use nonblocking
sends; partial/non-writable frames close the peer. No transport queue/framework
or reusable transmitter component was introduced.

A scheduler completion that wins before Stop may restore prior flow; Stop then
stops it. If Stop wins, later End/expiry/callbacks cannot resurrect that work.
Process exit rejects new starts and prohibits restoration. An ordinary End may
restore a CLI direct tone: `stopped=true` never means all RF is confirmed off.

## Deterministic coverage and adversarial repairs

`src/tests/test_tone_lifecycle_test.cpp` calls real scheduler, HTTP and
WebSocket dispatch entry points. Gates/condition variables force ordering;
future deadlines and a 30-second process timeout detect hangs. Concrete
transmitter execution is suppressed; typed events and post-commit failure hooks
exercise the parent orchestration. The reusable transmitter currently rejects
simulated TONE execution, so these tests do **not** claim a real simulated tone
worker ran. No component change was made to bypass that contract.

Coverage includes competing Starts; both Start/End orders; repeated/concurrent
End; HTTP Stop against Start/restoration; stale generations after Stop and a
successor; deferred reload across WSPR/QRSS/FSKCW/DFCW; early completion/failure
before acknowledgment; direct-tone restoration exceptions; reservation rejection
and release on partial startup; bounded cancellation/rollover; watchdog creation
failure; pending watchdog destruction; and repeated server lifecycle. Existing
WTP production tests retain finite-only admission, RF duration versus host
cleanup allowance, and unresolved-output behavior.

The expired-worker gate holds the watchdog immediately before its command-lock
acquisition while End must finish. This exercises the old deadlock ordering in
the repaired implementation; the original binary was not run to reproduce the
hang. A slow-peer fixture fills a socket send buffer and requires broadcast to
release its lock. A delayed result hook proves HTTP Stop completes while a
WebSocket Start reply is held, followed by ordered Start/End replies and echoed
client IDs.

Review/repair iterations found and resolved:

1. Watchdog join cycles and unfenced old deadlines: reusable worker,
   cancellation and scheduler generations.
2. Callback/join cycles and startup/terminal races: callback observations only,
   scheduler reconciliation, owner-specific status snapshots.
3. Slow inventory validation serialized unrelated configuration updates:
   release lifecycle/update locks during validation, retain revision checks.
4. Rejected configuration restoration was described as successful: inspect
   inhibition and keep failure distinct from successful tone cleanup.
5. Finite completion before deadline caused a false watchdog stop failure:
   replay the matching terminal owner without duplicate effects.
6. A restoration exception lost `stopped=true`: preserve actual tone cleanup,
   clean partial restoration and report restoration failure separately.
7. Old/duplicate/passive outcomes or runtime-before-acknowledgment delivery
   could overwrite state or strand timers: revision fencing, optional client
   IDs, correlated timer settlement and conservative legacy recovery.
8. Recovery End could retain Start's pending latch; timeouts retained spinners:
   settle both controls while preserving drafts and uncertain-outcome recovery.
9. Startup rollback could hide uncertain cleanup behind a failure-only state:
   report unknown cleanup and block new Start until recovery establishes safety.
10. The generic local inactivity query deliberately returns unknown for WTP:
    preserve this stop transaction's WTP CleanupResult for tone cleanup and
    reservation release; test successful finite cleanup followed by another Start.
11. Delayed CW launches checked their generation outside the transaction gate:
    order the real launch entry point with lifecycle operations and invalidate
    launches armed by a predecessor callback before the tone takes over.
12. WTP completion consumption and callback selection could straddle a new
    Start: order polled completion dispatch under the scheduler lifecycle gate,
    without making a joined worker acquire that gate.
13. Original light-theme semantic text had insufficient contrast: use existing
    Bootstrap emphasis colors and neutral text for informational states.
14. Recovery End could report unchanged output after actually cleaning up:
    return its explicit cleanup result, retain inhibition and never resume prior
    work. The fake WTP peer loses an ARM reply, proves Start stays blocked, then
    recovers and verifies the truthful End response.
15. Runtime observations or delayed Start replies could re-enable a pending
    End: retain pending controls while recording newer evidence, then reconcile
    it when the End reply settles. Unit and real-browser regressions cover this.

A fresh final source/contract review after these repairs and validation found
no unresolved actionable issue in this scope. It checked command/state/lifecycle/
configuration lock order, worker joins and callbacks, stale generations, WTP
cleanup evidence, restoration failure, pending controls, legacy attribution,
and the complete parent/UI diff. Physical behavior remains outside this assessment.

## Validation environment and commands

Disposable Docker container `wsprrypi-tone-item4`, Debian 13.6 Trixie ARM64,
image `wsprrypi-issue412-qualification:debian-trixie`
(`6ceba8b34e64`), GCC 14.2.0, libgpiod C++ headers, Node 20.19.2,
PHP 8.4.24, Chromium 151.0.7922.169. Tests run as `nobody` with no container
network, added capabilities, transmitter devices or privileges. The repository
is mounted read-only and copied into `/tmp/work`; build output stays there.
No macOS C++ compilation/linking occurred. WTP running status is polled as
runtime evidence; its successful stop CleanupResult is retained per transaction
rather than substituting the deliberately conservative generic local-output query.

From `/tmp/work/src`:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j1 \
  test-tone-request-test test-tone-frequency-plan-test \
  test-tone-selector-plan-test test-tone-response-test \
  websocket-lifecycle-test test-tone-lifecycle-test \
  wtp-production-test wtp-pi-control-test wtp-pi-config-transaction-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j1 \
  guarded-mode-change-persistence-test band-gpio-rotation-test \
  selector-shutdown-cleanup-test non-wspr-repeat-policy-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j1 semantics-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 strace -f -e trace=openat,open \
  -o /tmp/tone-lifecycle.strace ./build/bin/test_tone_lifecycle_test
../scripts/ci/assert-no-transmitter-hardware.sh /tmp/tone-lifecycle.strace
```

From `/tmp/work/WsprryPi-UI`:

```sh
npm test
npm run test:browser
```

Local JavaScript/whitespace checks:

```sh
node --check WsprryPi-UI/data/site.js
node WsprryPi-UI/tests/wspr_band_frequency_correlation_test.js
node /Users/lbussy/.agents/skills/impeccable/scripts/detect.mjs --json WsprryPi-UI/data/site.js
git diff --check
git diff --cached --check
```

Final results on the repaired source snapshot:

| Validation | Result |
| --- | --- |
| Listed focused request/plan/response, WebSocket/lifecycle and WTP targets | Passed; WTP production reported 323 checks, including unresolved cleanup and explicit recovery. |
| Guarded-mode persistence, band rotation, selector shutdown and repeat policy | Passed. |
| `semantics-test` | Passed, full default physical/simulated capability profile with hardware access disabled; portable subset was not substituted. |
| Lifecycle `strace` and hardware-access assertion | Passed: no prohibited transmitter hardware access observed. |
| UI `npm test` | Passed, including manifest/identity, correlation and existing unit suites. |
| UI `npm run test:browser` | Passed: 16 Test Tone workflow groups plus conditional GPIO, CW duration and support-intake fixtures. |
| JavaScript syntax, local correlation regression and final whitespace checks | Passed. |
| Impeccable detector | Exit 2 with exactly three pre-existing advisory debug colors, detailed below; no new findings. |
| Optional band default convergence target | Failed for non-Pi platform/stock-path assumptions, detailed below; not part of the required semantics aggregate. |
| Pi, installation, service, RF and physical runtime checks | Not run; outside authorization. |

Final container logs were `/tmp/tone-complete-focused.log`,
`/tmp/tone-complete-semantics.log`, `/tmp/tone-complete-audit.log`,
`/tmp/tone-final-ui-unit.log` and `/tmp/tone-final-ui-browser.log`.
Earlier iterations exposed invalid fixture CW settings, the validation-lock
regression, and a JSON initialization error; all were repaired before closure.
The existing conditional-GPIO browser fixture also failed identically on the
starting revision because isolated Chromium reports offline. Conditional-GPIO
and CW browser fixtures now explicitly mock online state; conditional-GPIO also
resets the network-pause latch between independent scenarios. Product autosave
behavior was not changed.

The optional `band-gpio-default-convergence-test` was attempted and failed on
this non-Pi host: it assumes a recognized Raspberry Pi GPIO platform and an
absolute `/home/pi/WsprryPi/config/wsprrypi.ini` stock-file path. This is outside
the canonical full semantics aggregate. No Pi access, fixture deployment or
hardware qualification was substituted. Focused guarded-mode persistence,
rotation, selector shutdown and repeat-policy results are recorded separately.
Expected test diagnostics include missing Pi device-tree metadata, deliberate
request/transport failures and inhibited cleanup; these are not physical-device
observations.

## UI and Impeccable evidence

Preserved Bootstrap/Zephyr layout and the Maintenance modal. Preview remains
separate from accepted/starting/active/failed/ended/restored/unknown feedback.
Matching requests omit repeated frequency explanations; differing committed
values, selector metadata and recovery guidance remain visible. Active means
controller runtime confirmation, never measured RF. End displays scheduler,
managed-idle, CLI direct-tone restoration or failure without hiding it behind
`stopped=true`.

The production-handler unit fixture covers request attribution, authoritative
frequency/selector validation, definitely-unsent versus possibly-sent failures,
timeouts/quarantine, disconnect/reconnect, stale sockets, duplicates, passive
clients, unavailable catalog, End recovery and runtime-before-acknowledgment
ordering. The focused real Chromium modal fixture exercises 16 workflow groups,
including production socket callbacks for disconnect/reconnect and stale/passive
outcomes, with mocked control/runtime transports, Tab navigation and polite live
regions. A newer observation cannot enable a duplicate pending End.

Two batched visual inspection rounds covered 1440x1000 and 390x844, light and
dark, accepted startup, active committed differences, CLI restoration and
uncertain cleanup. 200% text enlargement was checked with scrolling/focus to End
and Close. No horizontal or result-text overflow was observed. Initial contrast
and lingering-spinner findings were repaired together, then confirmed. The
fixture generates 17 screenshots per run; representative reviewed PNGs and all
state/viewport measurements are in [test-tone-lifecycle-evidence](test-tone-lifecycle-evidence/).
The final behavior repair reran all browser checks; the five retained images
were refreshed and inspected again without further visual changes.

The required detector reported three advisory pre-existing debug-console
literal colors (`#1f6feb`, `#9a6700`, `#cf222e`) outside DESIGN.md. They are in
unchanged `LOG_LEVEL_STYLES`, not this operator status surface. No new detector
finding or unrelated palette cleanup was introduced. Local skill/cache metadata
is excluded from the commit. This focused review does not close issue #423.

## Documentation Impact and qualification boundary

Updated: `docs/bounded-tone.md` and this maintainer review/evidence.
Considered unchanged: `docs/simulated-backend.md`; no new simulator capability,
selection, time mode or injection control was added.

Read-only operator-documentation review:
`../WsprryPi-Docs/AGENTS.md` and
`docs/User_Interface/Maintenance/index.md`, particularly lines 130-158.
The consolidated operator-documentation item (originally item 10, now item 11
in the user's working punchlist) must replace the blanket “Test Tone ended” description with
acceptance/runtime confirmation and restoration-specific feedback; explain
scheduler ordering across HTTP/WebSocket Stop, revisioned delivery and legacy
uncertainty; mention deferred saved configuration/repair application. Keep the
existing preview/frequency-source guidance. The static frequency-source image
still depicts the same controls; it does not need replacement merely for age.
An image that specifically shows the old successful execution/End wording would
need replacement when that documentation is updated. No sibling repository
was written, built, committed or pushed.
The older `test_tone.png` shows the former Utility presentation; the reviewed
frequency-source screenshot retains the current controls. Neither provides a
new execution-status example. Screenshot replacement remains tied to material
inaccuracy in the consolidated documentation work, not this software completion.

No physical transmission, GPIO operation, RF measurement, installation, service
operation, Pi access or system configuration change was performed. Outstanding
physical qualification includes backend startup/cleanup reliability, electrical
behavior, selectors/LEDs, RF accuracy/spectrum/duration, installation and service
lifecycle. Software evidence does not establish Wsprry Pi 4.0 release readiness.

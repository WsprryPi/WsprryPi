# CI review baseline and coverage map

Phase 1 freezes the accounting and execution map. The Phase 2 browser-harness
implementation and its validation gates are recorded below; Phases 3–7 remain
unimplemented.
No test, workflow, product behavior, installed service, device, or RF path is changed
by this phase. This is an engineering baseline, not release or hardware qualification.

## Source identity and reproduction

The frozen Pi source is `94ff1590368f6ecbeb431e64f04af3f44f0ddb8e`, the verified
`origin/devel` when this work began. The primary checkout was clean and remains on
`devel`. All task work uses branch `codex/ci-review-simplification` in
`/Users/lbussy/.codex/worktrees/ci-review-simplification/WsprryPi`; retain both for
later phases. Do not reset the baseline when advancing phases.

The selected Pico is `74dd300b6688984359c13ed576443fff356b5033`. No companion
changes are required for Phase 1. The workflow pins Mbed TLS to
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` with recursive submodules, and the
read-only RP1 provider to `b52ab7b796e44d8690b2a91bccb21ae5d8b670df`.
The independently checked-out network/USB Pico refs otherwise default to moving
`devel`; recording both resolved revisions is mandatory. A common ref name alone
cannot establish that both jobs consumed the same source.

Run from the dedicated worktree:

```sh
python3 -B scripts/ci/review_inventory.py --check docs/development/ci-review-baseline.json
python3 -B scripts/ci/review_inventory.py --revision 94ff1590368f6ecbeb431e64f04af3f44f0ddb8e
python3 -B scripts/ci/review_inventory.py --revision 21d54a3bd201b96522ed318be763e75d89ea7256
```

The [ledger](ci-review-baseline.json) gives every inventoried path, immutable Git
blob, mode, bytes, physical lines, cohort, family, proposed disposition, owning
phase, lexical test signals, and wait-signal line numbers. The small
[measurement helper](../../scripts/ci/review_inventory.py) reads Git blobs rather
than executing Make or test code. `--check` reproduces the recorded source; it
intentionally does not compare a future HEAD against old counts. Use a separate
current snapshot for the after-total. Neither an unstaged worktree edit nor an
untracked helper enters a committed-revision snapshot; record candidate changes
separately until committed.

## Frozen scoring rules

| Cohort | Files | Lines | Treatment |
| --- | ---: | ---: | --- |
| Active infrastructure mixed envelope | 143 | 31,971 | Frozen reduction denominator |
| Unscheduled/opt-in infrastructure | 13 | 2,950 | Zero deletion credit |
| Protected source/test/build context | 217 | 52,968 | Not disposable harness; charge relocated/new harness here |
| Protected data/provenance | 8 | 761 | No generated/data deletion credit |
| Total inspected file ledger | 381 | 88,650 | Not a reduction denominator |

The active envelope comprises code under `.github/workflows`, `scripts/ci`,
`scripts/tests`, and `WsprryPi-UI/tests`, excluding the 13 explicitly named
inactive entries, plus `src/Makefile`, `WsprryPi-UI/package.json`,
`src/tests/ui_source_regression_test.cpp`, and the WSPR reference regression
runner. It includes case logic and assertions mixed with scaffolding. **31,971
lines is not 31,971 lines of disposable boilerplate.** The helper's literal
scope/cohort functions are the reproducible inclusion rules. UTF-8 `splitlines`
counts comments and blank lines; non-UTF-8 code fails measurement rather than
counting as zero. Non-code data and vendored source do not earn
code credit. First-party component test/build files, direct `test.cpp` sources,
parent helper headers, and WSPR golden vectors remain visible as protected context.
Other production code, lockfiles, third-party implementations, operator prose,
and generated build trees are outside that denominator. Companion fixtures are
listed below as dependencies rather than added to Pi's starting denominator.

For later scoring, let B = 31,971, D = raw lines removed from the active baseline
code, U = uncredited removed dormant sections, formatting/compression, and
comment-only or blank-line reductions within those files, and A = all
new/replacement/relocated harness code, including this phase's helper and code
outside the original roots or repositories. Credited saving is `(D - U - A)/B`.
Report raw physical-line change alongside this adjusted figure.
For example, removing 1,000 active lines and adding a 200-line shared helper
earns 800 lines; moving those 1,000 lines elsewhere earns zero; deleting all
2,950 inactive lines earns zero. Removing 100 dormant lines inside an active
file gives D=100, U=100, A=0 and earns zero. Deduct each exclusion once. Removing the
2,950 inactive lines contributes neither D nor A. Moving code into production,
fixtures, generated output, dependencies, or Pico is charged to A; it is not a
saving. Pure formatting, minification, blank/comment removal, and dormant-section
removal receive zero credit. Retain line-range evidence for those adjustments.
Newly activated unique coverage counts in the after-total even if inconvenient.
The helper preserves the frozen inactive path list; future snapshots need an
explicit activation/dormant-section/relocation adjustment ledger, not a blind
comparison of its cohort totals. New dependencies require the same adjustment.
The JSON ledger and this explanatory document are disclosed data/documentation
overhead, not executable simplification. Count the new helper's committed code
and file as Phase 1 executable overhead; do not hide it by its special cohort.

A 50% credited reduction requires at least 15,986 net eligible lines removed,
leaving at most 15,985 equivalent lines after additions. No feasibility claim is
made from a file envelope alone. File count is an independent measure: reaching
50% means at most 71 equivalent active files, counting new helpers, without
combining unrelated suites merely to improve the number. Neither target permits
removal of distinct cases, carriers, platforms, safety obligations, or recovery
assets. Code, count, and time percentages must be reported separately.

Historical reproduction at `21d54a3bd201b96522ed318be763e75d89ea7256` yields exactly
141 files / 31,766 lines, 535 Python `test_` function definitions, and 1,938 lexical
JavaScript assertion calls. Current figures are 143 / 31,971, 535, and 1,953.
The 205-line net drift is the new PPS browser test (178), vendor test (15),
Makefile changes (9), and source-regression changes (3). The new PPS C++ behavior
test is protected context. No historical numerical correction is needed; source
drift explains the difference. Test definitions and lexical assertion calls are
not executed-case counts or efficacy measures.

## Execution graph and required contexts

There is one workflow, `debian-non-hardware.yml` (497 lines), eight job definitions,
nine matrix-expanded jobs, 69 declared user steps, and 75 expanded user steps.
Setup/post/container actions are additional runtime steps. Jobs depend on the
scope job; a classifier failure admits full validation, cancellation does not.
Every ordinary full job is required evidence, not a candidate for a job-count
quota. The workflow uses `contents: read`, SHA-specific cancellation groups,
and explicit profile selection. Broad application `make test` is privileged and
is not the canonical non-hardware suite.

| Execution owner | Dispatch and actual evidence | Context, boundaries, and failure propagation |
| --- | --- | --- |
| Choose validation scope, 5m | Generator `--check` → `ci-change-scope-test` → Python classifier tests; `control_contract_pair_test.py`; complete-diff classifier | Ubuntu; synthetic Git histories/source pairs; docs-only skips allowed only by fail-closed policy. Unknown comparisons, protected inputs, modes/types, and mixed changes admit full work. A green scope-only run is not a full sample. |
| macos-simulated-profile, 15m | `usb-logging-test`; explicit `semantics-test-portable`; TLS; WTP component/plan/USB/backend/scheduler/status/application/production and Pi server/control/config/fleet targets; container fixture tests | macOS 15 ARM, HID selection, portable simulated-only profile. Local protocol, PTY, crypto, and injected runtime tests; no production Pico pair in this lane. Portable semantics deliberately excludes full physical-profile runtime/cleanup executables. |
| macos-si5351-profile, 20m each | Matrix release/factory → scripted HID/protocol/planner/transition/startup → binary/symbol/early-CLI audit → inventory and power admission | Both macOS 15 ARM and Intel, `BACKENDS=si5351 ANCILLARY_GPIO=0`, hardware access disabled. Tests native transport compilation/refusal, not physical HID/Si5351 output. Matrix `fail-fast: false`. |
| wtp-network-loopback, 45m | Selected Pico + pinned TLS → contract parity → UDP worker/production application in private network namespace/TLS observer/catalog/TLS/runtime → `wtp-network-interop-test` and `wtp-config-interop-test` → actual Pico HTTP/browser drivers → source-selection mutation → routing/rollback → rendered Pi controls | Ubuntu 24.04, real Pi client/coordinator and Pico endpoint/crypto/serialization/journal in software. TLS/routing and browser fixtures are separate evidence. Network namespace uses privilege only for isolated routes. Failure artifacts are conditional and may be absent. |
| wtp-usb-management-pair, 15m | Selected Pico parity → CMake actual `usb_management_driver` → `wtp-usb-pair-client-build wtp-usb-management-test` → settings VM and `wtp_usb_pair_test.js` | Ubuntu 24.04, firmware disabled host build, production Pi/Pico management/framing/ACK/journal over software transport. `WSPRRYPI_GPIO_DRIVE_REQUIRED=1` prevents missing-drive bypass. Not physical USB qualification. |
| strict-i2c-profile, 10m | Ubuntu container without gpiod → explicit Si5351 release/factory → unprivileged `nobody` profile and file audit → isolated install-path fixture | Strict Linux I2C build, absent device/refusal, temp prefix and fake systemctl. Specific negative device-opening probe intentionally unsets the hardware guard; retain its nonexistent-device scope. This is not an installed-host service test. |
| ubuntu-gcc13-release, 20m | GCC 13 warnings-as-errors → six sequential release profiles → profile-report and early-CLI assertions → shutdown-window fault harness | Profiles: rpi-gpio, rp1-gpclk, si5351, simulated, si5351+simulated, all four. Each early-CLI invocation has 6 options × 3 logging modes × 3 repeats = 54 subprocesses; six Linux + two native macOS contexts yield 432 dispatches, not 432 distinct obligations. |
| non-hardware-validation, 20m | Debian trixie container → debug/WTP/frequency tests → full semantics → npm unit/manifest/browser → RP1/provider → support/installer → transmitter → WSPR reference/package consumer → simulators → hardware trace audit | Linux default/full profiles, libgpiod present; injected hardware boundaries and explicit simulator. Real age/OpenSSL portions coexist with fake failure cases. Five traced cohorts are audited; other jobs are not thereby proved device-free. |

Recipes and prerequisite edges remain authoritative in the
[workflow](../../.github/workflows/debian-non-hardware.yml),
[parent Makefile](../../src/Makefile), and
[UI scripts](../../WsprryPi-UI/package.json). npm chains and shell `set -e` normally
propagate nonzero status; a later final print cannot establish earlier case
execution without inspecting those paths. Standalone component boundaries remain:
WTP-Client `test` runs protocol/session plus Python vectors via codec driver;
its `interop-test` builds selected Pico reference objects; WTP-Server `test`
runs server core; Chipset-Offsets runs its dedicated binary; WSPR-Transmitter
runs explicit backend/controller/startup contracts; WSPR-Reference runs core
executables/CLI checks and an installed package consumer. Parent targets reach
these via recursive Make/CMake. Preserve standalone entry points and extraction
potential. Si5351 backend harness, RP1 admin probe, and GPIO/Si5351 startup/min-drive
qualification sources are explicitly included as protected context. Live qualification
targets build executables but do not run them; their guarded physical entry points
and recovery duties must be preserved separately from fake qualification unit tests.
Never execute the backend harness/admin probe as harmless CI.
Other component `test`/`portability-test` targets are manual unless
shown as prerequisites; compilation is not execution.

Pico's invoked host fixtures are `usb_management_driver`,
`consumer_session_driver`, and `consumer_bootstrap_driver`. Its network browser
scripts are `chrome_startup.test.mjs`, `ui_process_cleanup.test.mjs`,
`consumer_sessions.test.mjs`, `operator_cleanup_browser.mjs`,
`progress_browser_integration.mjs`, and `consumer_recovery_browser.mjs`.
They depend on that selected checkout's CMake, network bootstrap-web npm lock,
and production handlers. `PROGRESS_BROWSER_NO_CAPTURE=1` suppresses capture,
not the test. Pico fixture internals are external context, not Pi deletion credit.

### Confirmed repetition and count limits

* Debian invokes `wtp-plan-test` and `wtp-fleet-test` in two separate Make calls
  with the same job/profile/source: two confirmed redundant invocation sites.
* Network explicitly invokes `wtp-tls-test` and reaches it again through
  `wtp-network-runtime-test`: one confirmed repeated suite execution. Further
  `wtp-tls-build` prerequisites are build work, not another executed TLS suite.
* `wtp_ui_test.js` is reached through npm unit, `wtp-production-test`, and
  `wtp-api-test`; `wtp_management_test.js` through npm unit and config tests.
  Preserve distinct target users until exact source/env equivalence is proved.
* WSPR-Reference is configured/built by the workflow, then configured/built again
  by its runner. This is repeated build dispatch, not doubled behavioral cases.
* There are 15 UI files declaring a `class Cdp...` and 12 declaring
  `function freePort`; these are lexical consolidation candidates, not proof
  all 15/12 are active or interchangeable. The active browser family has 16 files.
* WSPR-Reference's two Type 2 coverage-observation executables explicitly use
  `|| true`. Their results are observations, not passing required scenarios.
  Do not silently promote or discard them without resolving their obligations.

No trustworthy global executed-case total is emitted across C++, shell, browser,
PHP, Python subtests, and paired adapters. Preserve this as an instrumentation
limitation, not a zero count. Stable evidence units currently available are the
nine required contexts, explicit suite/target invocations, the 18 CLI option ×
logging combinations per transport with three repeats, both production carriers,
and the obligation map below. Phase 5 must expose stable scenario/result IDs and
conditional skips at existing entry points before Phase 7 scores case retention;
do not substitute 535 definitions or 1,953 assertion sites. The workflow graph
and per-file ledger permit auditing invocation changes without a fake global sum.

## Functional and safety obligations

These 15 stable IDs describe required outcomes. A row is not a claim of complete
coverage of all its subcases. Current source evidence and identified weak points
must survive consolidation. Static/injected/production evidence remain distinct.

| ID | Obligation and executable evidence | Evidence boundary / removal gate |
| --- | --- | --- |
| O01 | Exact current control contract: generator/parity tests; `wtp-config-test`, config interop, USB management/pair | Closed shape/type/version/limits/identity/revision/refusal-before-write must pass through both production carriers, with journal/config byte preservation on negatives. Generator/source spelling alone is insufficient. |
| O02 | Independent WTP framing, storage, authentication formats: WTP-Client protocol/session/vectors, USB CDC, TLS and Pico reference tests | Keep independent format versions and invalid framing/ACK cases; never relax validators or fall back to older peers to preserve green CI. |
| O03 | Network production interoperability: network runtime, interop and config interop; Pico HTTP/browser drivers | Must build actual intended Pi/Pico sources plus pinned TLS. Mocked UI responses cannot replace this row. |
| O04 | USB production interoperability: pair client + actual Pico USB driver + `wtp_usb_pair_test.js` | Keep settings, serialization, ACK, journals, malformed inputs, GPIO strengths and legacy producer provenance. Software pair evidence is not physical USB. |
| O05 | Capability advertisement, negotiation and runtime consumption: generator, backend factory, catalog/API and both pairs | Reject unsupported carrier/version and contradictory descriptor before mutation. Preserve source-parity and selected-source failure tests. |
| O06 | Trust/session/replay/stale revision: TLS tests, Pi authority/control/identity/revocation/config transaction, Pico session/recovery and carrier negatives | Real crypto/handler evidence plus injected failures; keep identity/boot/profile bindings and absence of writes, rather than matching error prose alone. |
| O07 | Plan/admission/schedule/cancel/failure/cleanup: WTP plan/backend/scheduler/status/application, lifecycle/cleanup, tone and GPIO policy tests | Full semantics and portable subset differ intentionally. Keep fail-closed output and terminal-state assertions, including interruption and cleanup failures. |
| O08 | Platform/backend isolation: six GCC profiles, native ARM+Intel Si5351 audits, strict I2C, runtime factory, hardware trace audit | Profiles are distinct contexts. Five trace cohorts are a bounded audit, not universal hardware-access proof. Keep missing/empty/unreadable trace and grep-error refusal. |
| O09 | Configuration/defaults/custom values/units: dial semantics, guarded persistence, INI roundtrip/production browser, calibration and band tests | Actual application in private namespace plus injected semantics. Preserve INI bytes on reads/refusals, explicit save/apply and custom values. |
| O10 | Installer provenance/path/ownership/dry-run/rollback: installer dependency, RP1 install/update/reconcile, container/export, UI publication and strict install fixtures | Mostly isolated fake commands/filesystems; strict request/command allowlists and fault points needed before consolidation. No claim of live service/install qualification. |
| O11 | Support encryption/privacy/input bounds: support runtime/collector/intake, age roundtrip, key/signing provisioning | Preserve real age/OpenSSL checks and closed parsing; fixture-only success cannot replace cryptographic evidence. Unique collector/runtime-install scripts are HOLD, not dead by assumption. |
| O12 | Trust/receipts/retention/deletion/recovery: support manifest/publication/trust/inspection/processing/retention suites and mocked routing rollback | Require no mutation on rejected inputs, exact immutable receipts, interrupted atomic operations and path boundaries. Keep recoverability assets until consumers are known. |
| O13 | Operator draft/save/failure/replay: npm unit and browser, Pico controls/progress/recovery, production application load | Mocked browser workflow, actual application and Pico handlers protect different boundaries. No Phase 1 UI change or fresh human visual qualification. |
| O14 | CI admission/source/lifecycle/failure: change-scope, control parity, source-selection mutation, browser process tests, dependency regressions | Unknown paths/commands fail closed; inner waits must be bounded. Build-only, skipped, cancelled or missing runs do not satisfy execution. |
| O15 | WSPR encoding/planning/timing/reference provenance: golden vectors, transmitter contracts, plan placement, tone/QRSS regressions, reference runner/package consumer, simulator | Exact wire/symbol vectors remain; JSON/protocol assertions should parse structure. Simulation does not establish physical timing/frequency/RF. Keep intended real-time smoke checks and distinguish nonfatal observations. |

## DELETE / REWRITE / RETAIN / HOLD inventory

The JSON ledger assigns each of the 381 paths exactly once. Its four
`DELETE_AFTER_REPLACEMENT` entries are **conditional DELETE**, not an immediate
safe deletion. There are 83 REWRITE, 278 RETAIN, and 16 HOLD entries. These are
path dispositions, not numbers of cases or allowed removals. Family membership
is explicit in the helper and ledger; the following table supplies consumers,
obligations, reasons, gates and implementation responsibility. Per-file exceptions
in the ledger take precedence over the family default. RETAIN means preserve
current evidence, allowing a separately proved equivalent replacement later.

| Family (all ledger members) | Consumer / execution | Proposal and gate | Obligations / phase / credit |
| --- | --- | --- | --- |
| admission-orchestration (5) | Workflow and CI scripts; full CI, five trace cohorts | REWRITE dispatch/simulator structure; RETAIN `assert-no-transmitter-hardware.sh` fail-closed audit and the already structured/fresh `verify-simulated-wspr.sh`. Do not remove contexts or admission policy. | O08/O14/O15; 3,5; active only |
| browser (16) | npm browser plus explicitly named network job scripts | REWRITE shared CDP/readiness/ownership with bounded inner requests and exact response predicates; preserve per-scenario workflows and process tests. | O09/O13/O14; 2; active only |
| ui-unit-fixture (46) | npm manifest/unit, Make targets, imported HTTP/PHP/JS fixtures; five inactive HOLD exceptions | RETAIN behavioral cases, public-input fixtures and provenance. Consolidation only after import/router/env consumers mapped. | O01/O05/O09/O13/O14; 2,5,7; inactive zero |
| carrier-pair (15) | Production network/config/USB adapters and shared request/case producers | RETAIN production path and negatives; shared assertions must still run both carriers and preserve byte-level no-write checks. | O01–O06; 7; protected efficacy |
| source-policy (1) | Parent full and portable semantics | Conditional DELETE `src/tests/ui_source_regression_test.cpp` only after each useful requirement is mapped to firm behavioral/architectural evidence. Keep explicit architecture rules with justification. | O07–O10/O13/O14; 3; active minus dormant sections |
| installer (14) | Parent dependency and install fixtures; three Pico installer HOLD files | REWRITE shared fixture commands/filesystem builders with strict unexpected-command refusal and command ordering assertions. Keep dry-run, interruption, provenance and rollback cases. | O10/O14; 4,5; inactive zero |
| rp1-installer (9) | Debian RP1/update/reconcile/release acceptance and PPS vendor prerequisite | REWRITE fixture duplication; conditional DELETE `rp1_gpclk_diagnostics_test.py` source expectations after executable diagnostics/refusal replacement. | O07–O10/O14; 3,4; active only |
| support-bundle (21) | Debian support chain; two unscheduled HOLD files | REWRITE fixture setup; conditional DELETE `support_bundle_contract_reconciliation_test.py` and `support_bundle_production_encryption_metadata_test.py` only after parsed, executable production-boundary evidence replaces source/prose assertions. | O10–O12/O14; 3,4; inactive zero |
| script-boundaries (19) | Scope/parity/source-selection, six-profile early CLI/logger, macOS/strict I2C audits, container/UDP/publication tests and two RP1 fixture JSONs; Apache renderer/proxy and php2shell HOLD exceptions | RETAIN distinct admission, provenance, binary-audit and operational boundaries; source-selection inner-wait repair remains Phase 3. Three unscheduled tools require consumer/unique-coverage decisions before removal. | O05/O08–O10/O14; 3,5,7; inactive and data zero |
| build-policy (9) | Parent Make and scripts regression targets | REWRITE dependency/timestamp/build assertions; preserve dirty-source, header/profile/object invalidation and genuine compiler-failure sensitivity. | O08/O14; 6; active only |
| parent-behavior (118) | Parent executable/Python/JS tests and helper binaries; includes manual probes | RETAIN unique runtime contracts and fixtures. Compilation and manual probes are not ordinary CI execution. No blanket deletion of `src/tests`. | O01–O15; 7; outside denominator except source-policy above |
| network-production-fixture (7) | Network CMake/real Pico bridge and HTTP/TLS helpers | RETAIN production-path fixtures; CMake REWRITE only for proved build reuse/source identity. | O01–O06/O14; 6,7; outside denominator |
| component-build-test (99) | Parent Make/package dispatch plus standalone component builds/tests/data | REWRITE mixed parent Make/package dispatch in 5; component Make/CMake in 6 only if needed. RETAIN golden vectors, attribution, standalone boundaries and helper headers. Three direct component `test.cpp` entries HOLD for scheduling/coverage mapping. | O02/O07/O08/O14/O15; 5–7; only explicitly eligible runner/Make/package files count |
| routing-rollback (2) | CI mocks `rollback_test.py` importing `live_target.py` | RETAIN: helper also has live network/service recovery duties. Never run the live helper as a harmless test. | O10/O12/O14; 7; outside denominator |

There is no unconditional DELETE candidate established by Phase 1. The complete
13-path inactive list and reasons are in ledger `inactive_reasons`: Apache
renderer/proxy, collector, php2shell, Pico bundle/deploy/fixture, runtime support
install, installed log disconnect/resume, opt-in native host fixture, Si5351 UI,
and firmware UI. Unscheduled does not mean useless. Installed log-disconnect
cleanup can restart Apache; resume requires retained installed journal data.
`WSPRRYPI_NATIVE_HOST_FIXTURE` is an opt-in router consumer. Pico bundle/deploy
are reached by unscheduled `pico-usb-installer-test`; runtime support install by
its focused target. Check imported fixtures, operator/manual documentation,
recovery duties and unique coverage before any Phase 5 deletion or activation.
The three additional HOLD component sources have protected-context accounting;
they do not enlarge the 13-file cleanup score exclusion.

## Synchronization and diagnostic review

The ledger indexes lexical wait/deadline signals per file and source line; these
are a review queue, not a defect count or automatic classification. Words in
comments and real timing contracts occur alongside synchronization code. Do not
mass-delete sleeps or infer readiness from a passing slow rerun. The following
cross-language mechanisms carry explicit follow-up gates; remaining per-file
signals retain their current behavior until their consumer-specific classification.

| Mechanism / consumer | Current behavior and category | Deterministic follow-up and preserved contract | Phase / obligations |
| --- | --- | --- | --- |
| `src/tests/rp1_pps_calibration_test.cpp` FakeCapture/CaptureSession | Explicit sample/worker condition-variable handshake; actual transition predicates and phase-prefix output. Three inner `wait` calls have no local deadline; CI job timeout is the final watchdog. | Retain queue handshake and explicitly supplied completion timestamps. Bound local stages with monotonic budgets and last transition/calls/state diagnostics; deliberately gate omitted completion/cleanup. Do not restore arbitrary short sleeps. This is residual hang protection, not proof the recent failure persists. | 3,4; O07/O09/O14 |
| `src/tests/network_process.py` and its tests | Bounded readiness with monotonic deadline, fragmented/coalesced bounded pipe reads and child state; deliberate stalled-child sleep is fault stimulus | Retain pattern; use controlled stalled transition when extending tests. A timeout-path fault delay is not automatically brittle synchronization. | 3; O03/O14 |
| `scripts/tests/pico_source_selection_test.py` | `select(15)` followed by text `readline()`; readiness can be partial, so the inner read may outlive the readiness budget. Command timeout 300s and cleanup wait 10s are separate outer bounds. | Reuse bounded byte/line reader and one readiness deadline; gate partial line, missing newline, child exit and terminate/kill cleanup while retaining edited-source and compiler-failure probes. | 3; O03/O14 |
| UI CDP/free-port/startup copies | Per-command pending promises and HTTP probes can outlive polling bounds; free-port reservation/release races; copied readiness/cleanup | One bounded request/operation API, actual ready predicates, observation before action, owned process-group teardown; controlled delayed readiness, lost/reordered acknowledgment, exit and cleanup failure. Port discovery must verify the intended process identity. | 2; O13/O14 |
| `browser_fixture_process.js` and tests | Process-group ownership and bounded leader exit already exist; settle/probe delays include 250ms/50ms waits | Preserve child-group reaping and primary plus cleanup errors; replace assumptions with predicates only where delays stand in for state. Retain justified escalation grace budgets. | 2; O13/O14 |
| Build dependency/timestamp tests | Sleeps around 1.05/1.1s ensure filesystem timestamp ordering, not runtime readiness | Prefer controlled timestamp changes and observable rebuild/object identity; test actual header/profile invalidation and compiler failures on each supported filesystem. | 3,6; O08/O14 |
| `scripts/tests/logger_shutdown_test.py` | 150ms nanosleep holds a mutex to reproduce a shutdown window; entry wait and subprocess bounds serve different duties | Intentional fault window: establish entered critical section with gate and release/interrupt deliberately; preserve prompt shutdown outcome and finite final watchdog. Do not remove the race scenario for speed. | 3; O07/O14 |
| `verify-simulated-backend.sh` | Fixed `/tmp` trace plus compare/grep can accept stale output or missing structured semantics | Fresh ownership/freshness, parse typed trace/event sequence and terminal cleanup; actual simulator execution and no-hardware audit remain. No production trace-path control added casually. | 3; O07/O08/O15 |
| `verify-simulated-wspr.sh` | Already removes traces, requires nonempty output, parses JSON schema/backend/mode/counts/frequencies, and uses 10s TERM timeout plus 2s kill escalation | RETAIN these stronger assertions and bounded execution. Do not duplicate the weaker-backend-script finding or replace the typed checks with grep. | 7; O07/O08/O15 |
| WSPR reference runner | Exact wire vectors are good contracts; JSON paths use substring checks, some verbose prose/spacing expected | Parse JSON and assert closed shape/values; keep exact wire/symbol contracts. Separate nonfatal observations from required behavior; no blanket text-check deletion. | 3; O15 |
| C++ simulator realtime / production scheduling deadlines | Five-second external bound for realtime executable; logical-time and real-time tests have distinct purposes | Controlled clocks for expiry/state logic, retain real elapsed-time smoke tolerance and product deadlines; never widen safety limits just to pass. | 3,7; O07/O15 |
| Pico `operator_cleanup_browser.mjs`, startup/process cleanup fixtures | Operator `until` polls every 40ms with a 20s wall-clock bound; CDP already has a 20s per-command timer. Imported startup tests deliberately model cold launch, stalled HTTP, partial DevTools file and child exit. | Preserve those fault scenarios and imported process-group cleanup; propagate a monotonic stage deadline into inner probes rather than resetting budgets. These companion observations do not justify calling every CDP copy unbounded. | 2,7; O03/O13/O14 |
| Make/CMake, workflow, Pico companion lifecycle | Job budgets above; subprocess/build and browser cleanup have nested operations; companion scripts execute their own timeouts | Audit each affected inner operation when consolidating; preserve kill/reap, bounded logs, cancellation and missing-ack fault cases. Companion changes need paired source identity and their own worktree. | 2,4–7; O03/O04/O14 |

RP1 `FakeRunner.run` currently returns success for an unconfigured command tuple
(`responses.get(..., CommandResult('', '', 0))`). That can hide a new privileged
command or changed ordering. Phase 4 must default unexpected commands to failure,
with declared fixtures, complete call assertions, and negative proof before
consolidating. Other fakes are not assumed equally weak: signing-key provisioning's
fake OpenSSL explicitly rejects unknown verbs with status 64 and retains real
OpenSSL integration. Preserve that stronger boundary.

Every changed wait needs separate launch/readiness/operation/ack/cleanup budgets,
a monotonic total bound covering inner calls, expected condition, last state,
elapsed/budget, child exit/signal, bounded scrubbed diagnostics, and preserved
primary plus cleanup errors. Fault gates must establish the intended stalled
stage before asserting timeout category, safe terminal state and cleanup.

## Time and reliability baseline

Timestamps come from Actions run and **attempt-specific** job/step APIs. Aggregate
means sum of job `completed_at - started_at` for actual successful jobs,
unweighted by billing multiplier. It includes job setup/cleanup but excludes
queue time where distinguishable; it is not billed minutes or CPU time. Full run
elapsed uses run start to last completed job, including orchestration gaps. Step
seconds round to API resolution and do not exactly sum to job time.

| Evidence | Scope / source | Elapsed | Aggregate | Interpretation |
| --- | --- | ---: | ---: | --- |
| [41823344474](https://github.com/WsprryPi/WsprryPi/actions/runs/41823344474), attempt 1 | Historical full success, Pi `21d54a3bd201b96522ed318be763e75d89ea7256`, Pico `3a7515b000b6f881256e080749be7352e36b5d96` | 23m09s | 85m05s | Nine successful contexts; historical anchor, not current-source pass |
| [41756355277](https://github.com/WsprryPi/WsprryPi/actions/runs/41756355277), attempt 1 | Earlier full success, Pi `e20c25f2c9839e30ba975cbb5d539fda78ae300d` | 21m02s | 75m51s | Individual historical observation only; not comparable enough for improvement/p95 claims |
| [41857510695](https://github.com/WsprryPi/WsprryPi/actions/runs/41857510695), attempt 1 | Frozen Pi `94ff159…`, both recorded Pico `74dd300…` | No complete full sample | Do not sum as full success | Six jobs succeeded; all three macOS jobs cancelled with no steps. Attempt 2 did not establish a full success. No product test failure established from those cancellations. |

Historical anchor job seconds: scope 12, macOS simulated 362, macOS ARM Si5351
296, macOS Intel Si5351 589, network 1,371, USB 148, strict I2C 211, GCC 1,172,
Debian 944; sum 5,105s. Network is the critical path. Anchor network step costs:
setup/dependencies/checkouts about 24s, parent TLS/production pair block 1,037s,
Pico HTTP/browser 83s, source-selection/build-failure proof 53s, routing 15s,
rendered browser controls 155s. These combined blocks do not reveal pure compiler
versus test seconds; instrumentation is needed before attributing a precise build
saving. GCC's six-profile block is 1,155s. Debian includes dependencies 60s,
debug 174s, WTP 140s, frequency/tone 114s, semantics 176s, browser 101s, RP1 32s,
support 81s and reference/component/consumer work. Do not label all block time
compilation or assume removing duplicate tests halves the critical path.

At the frozen source, successful Linux job durations are scope 11s, strict 214s,
USB 173s, network 1,431s, GCC 776s and Debian 724s. Network parent block 1,075s;
Pico HTTP/browser 89s; source-selection proof 58s; routing 17s; rendered browser
157s. The Linux subset totals 3,329s, explicitly **not** a full-CI aggregate.
Cancelled macOS jobs have apparent start/end spans but no steps; those spans must
not be treated as runner execution or added to a timing comparison. The combined
run view after rerun reuses earlier Linux jobs; use `/attempts/1/jobs` and
`/attempts/2/jobs` rather than mixing attempts.

There are insufficient same-source, same-companion, same-runner/cache full
successes for a stable distribution or proven flake rate. A rerun can differ in
Pico revision, provisioning, cache and queue state. Documentation-only examples
are excluded. Step data does not reliably expose queue/provisioning separately.
A 50% target against the historical anchor would mean at most 11m34.5s elapsed
and 42m32.5s aggregate, reported independently; those are illustrative targets,
not validated current budgets or promised savings. Establish comparable before/
after samples for Phase 7 rather than credit runner speed or skipped work.

Reproduce evidence with:

```sh
gh api repos/WsprryPi/WsprryPi/actions/runs/41823344474
gh api repos/WsprryPi/WsprryPi/actions/runs/41823344474/attempts/1/jobs --paginate
gh api repos/WsprryPi/WsprryPi/actions/runs/41857510695/attempts/1/jobs --paginate
gh run view 41857510695 --job 126685518840 --log
gh run view 41857510695 --job 126685612171 --log
```

Both current-base network and USB logs identify Pico `74dd300…`; required recipes
completed successfully on that pair. This is existing CI evidence inspected in
Phase 1, not newly executed local integration or physical qualification. The
feature branch is absent from push filters, so validation after push must use
explicit workflow dispatch against the feature ref and immutable Pico SHA.
No workflow policy change is needed to make that run execute.

## Concrete reduction sequence and acceptance gates

| Phase | Work | Required evidence before advancing |
| --- | --- | --- |
| 1 | Freeze this ledger, execution/obligation map and timing limits | Reproduction, arithmetic, reviewed source identities, adversarial accounting checks, commit/push and actual feature-ref CI scope/result inspection |
| 2 | Consolidate 16 active browser scenario files' CDP/startup/HTTP/port/cleanup plumbing; leave scenario assertions readable | Same operator scenarios and contexts, strict request shapes, delayed/fragmented/missing acknowledgment and cleanup fault gates; charge shared helper and companion changes |
| 3 | Replace four conditional DELETE files' useful checks, parsed simulator/reference results and timing assumptions | Requirement-by-requirement replacement map, controlled mutations prove sensitivity, behavioral negatives preserve no-write/safe cleanup; architectural rules retained explicitly |
| 4 | Consolidate installer/RP1/support fixture factories and strict runners | Dry-run purity, unexpected-command refusal, provenance, rollback/interruption, encryption/privacy/trust/retention and unique negative cases remain |
| 5 | Repair graph duplication, explicit suite manifest/results, unscheduled-target decisions | No missing required cases, both real carriers, all platforms/profiles, stable IDs and skip accounting; inactive cleanup disclosed with zero credit |
| 6 | Reuse compatible builds/objects and eliminate repeated configuration | Header/profile/source invalidation, selected-source mutation and real compile-failure proof; no stale or unintended companion artifact reuse; cold/warm timings separated |
| 7 | Adversarial efficacy/safety qualification and final scoring | All required contexts/pairs passed on intended final refs, closed obligations and fault cases, adjusted code/count/time deltas plus uncertainty; no hardware claims from simulation |

Priority is removing repeated setup, not reducing negative scenarios. Browser
family contains 6,493 lines, installer 4,660, RP1 4,438 and support 3,976, but these
include protected assertions and inactive members: those sums are candidate
inspection envelopes, not additive guaranteed savings. Phase 2/4 must measure
actual repeated scaffolding, then Phase 3/5/6 reduce source-policy/dispatch/build
cost. If firm assertions and preserved contexts leave less than 50% genuinely
removable, report the shortfall; never manufacture the target through dormant
deletions, case loss, hidden dependencies or source compression.

## Validation and documentation impact

Phase 1 changes only this baseline, its ledger, its read-only inventory helper,
and the development index. Operator behavior and UI are unchanged; companion
operator documentation was considered and needs no change. No component source,
protocol schema, test dispatch, workflow, host, service, container, firmware or
RF configuration is modified. New helper overhead is disclosed above.

Validation includes immutable snapshot reproduction, historical reconciliation,
ledger arithmetic/path/reference checks, failed-check tampering and invalid-ref
checks, source/recipe inspection, attempt-specific CI/log review, and whitespace
checks. No local C++ build, browser rendering or physical qualification is needed
for the documentation/measurement change. Independent adversarial review found omitted manual transmitter harness sources,
missing wait primitives, one unmapped inventory family, ambiguous dormant-line
accounting wording, and an overbroad simulator criticism. Corrections expanded
protected context (no denominator change), indexed wait/select/readline/join/acquire,
added script-boundaries consumers, gave anti-gaming arithmetic examples, and
retained the already structured WSPR simulator verifier. A further adversarial
check closed an encoding loophole: undecodable code now refuses measurement
rather than appearing to contain zero lines. Reproduction and the second independent
adversarial assessment verified all six corrections; no actionable Phase 1
finding remained. The 145-line helper is one new executable infrastructure file,
charged as Phase 1 overhead in the later after-total. Actual feature-ref run,
commit identity and remaining CI limitations are reported with the phase result;
this baseline is not retroactively rewritten to disguise a failed or absent run.


## Phase 2 browser infrastructure

The frozen ledger and source denominator above are unchanged. Phase 2 replaces
13 active copied CDP clients with one shared implementation in
`WsprryPi-UI/tests/browser_harness.js`. All 14 active browser launchers use it;
`integrated_review_browser.js` and `page_warning_browser.js` retain their imported
scenarios and use the shared evaluation/readiness/render operations. Ten active
copies each of port allocation, JSON/status probing, and polling become shared
primitives. The existing process-group helper remains a separate owned-process
boundary. No new runtime package or large browser framework is introduced.

Disposition at implementation: REWRITE those infrastructure blocks, process
verification, and `logging_stream_fixture.js` readiness/cleanup; DELETE only
superseded copied machinery after migration checks; RETAIN scenario policy,
production pair adapters, the safe PHP router, capture matrices, and npm/Make
entry points; HOLD the unscheduled Si5351/firmware browser tools. No unused test
or dormant section is deleted for credit. The support-intake exported cleanup
contracts remain available. Browser fixture policy does not enable privileged,
journal, proxy, transmitter, or other application endpoints in the safe router.

Startup uses fresh registered profiles and their own DevTools discovery file,
validates the endpoint and usable page, and logs executable/version. The caller
still navigates its owned page and waits for actual scripts/state. Existing
60-second network/USB/Fleet/recovery startup budgets and support-intake's
12-second policy are explicit. HTTP operations consume the remaining stage
budget rather than introducing a shorter arbitrary probe cutoff. Native dialogs
are rejected unless the caller supplies its exact confirmation allowlist;
recovery retains explicit accept/cancel behavior. CDP framing/errors/exceptions,
disconnects, child exit, stage cancellation, and disposal reject outstanding
operations. Owned groups are checked for live writers (excluding zombies),
terminated/escalated with bounded waits, then profiles are removed. A failed
launch preserves ownership across the caller's finalizer; verified no-PID spawn
failure removes its empty profile while retaining the original launch error.

O08, O13, and O14 gain stronger lifecycle/failure evidence; O01–O07 and O09–O15
retain their scenarios and contexts. Four core browser scenario functions are
byte-identical to Phase 1. The 32-case conditional-GPIO matrix, 18 tone workflows,
160 page-warning combinations, integrated layout/theme/keyboard/accessibility
checks, both UDP-editor surfaces, maximum-history bounds, and logging recovery
cases remain. The new focused suite adds 17 socket/process fault tests; counts
are not a global executed-case total. There are still eight job definitions and
nine expanded required contexts. npm browser dispatch adds one focused harness
invocation without removing a scenario invocation.

Visual synchronization waits for required state or shown events and settles
fonts, finite animations, and paint frames. Deliberate failure/latency fixtures
and USB Logs' two negative pause/online observation windows remain intentional
test stimulus/observation, not readiness proof. The maximum UDP history is a
load case: the original source took 13.556s locally despite a nominal five-second
poll count, because inner calls were unbounded. It now acknowledges the final
production cursor, then asserts all 4,160 retained rows and the 4,096-source
limit once, under a separate 60-second load deadlock watchdog. No product timing
contract or safety deadline is relaxed. Errors include runtime stage,
elapsed/budget, last transition/observed result type, call site, and bounded
browser exception details; request payloads are not dumped.

| Code/count measure | Phase 1 delivered | Phase 2 candidate |
| --- | ---: | ---: |
| Active infrastructure files, including Phase 1 helper | 144 | 146 |
| Active physical lines, including Phase 1 helper | 32,116 | 31,559 |
| Active copied CDP classes / shared class | 13 / 0 | 0 / 1 |
| New Pi helper plus focused-test physical lines | 0 | 596 |
| Charged companion source additions outside frozen Pi roots | 0 | 54 |
| Unused cleanup credited | 0 | 0 |

Phase 2's raw Pi physical reduction is 557 lines. Conservatively excluding all
84 removed blank/comment lines gives 473 Pi lines of credit. The companion
startup correction adds 54 source lines (including replacements); all are
charged, while its 17 removed lines earn zero credit outside the frozen Pi
roots. Phase 2 therefore earns 419 lines (1.31% of the frozen 31,971-line
denominator). Charging Phase 1's 145-line tool leaves 274 cumulative credited
lines (0.86%). The Pi after-total is 31,559 physical lines including that tool;
the conservative companion-adjusted after-total is 31,613, before the blank/
comment exclusion. Pi active file count increases from 144 to 146; two existing
companion harness files are also modified, with no new companion source file.
New helpers/tests are fully charged. No file-count reduction or achievement of
the overall 50% target is claimed.

Local validation uses Node 26/macOS for real socket/process faults and an
isolated Node 22/Debian PHP/Chromium container for `npm test`,
`npm run test:browser`, the selected-Pico UDP editor, UDP Logs, and USB Logs with
`LOGGING_REAL_STREAM=1`. The initial Pico source was `74dd300b6688984359c13ed576443fff356b5033`.
The first feature CI run,
[41879882300](https://github.com/WsprryPi/WsprryPi/actions/runs/41879882300),
failed its inherited Pico no-page startup test before the Pi browser checks:
the 750ms deadline expired before any endpoint probe. It also had a macOS
simulated runner-acquisition cancellation. Neither is recorded as a pass.

The narrowly required companion correction is committed as
`1c30709186fabb64aa250da96d08914296ff3d89`, on its own
`codex/ci-review-simplification` branch/worktree at
`/Users/lbussy/.codex/worktrees/ci-review-simplification-pico/WsprryPico`.
`tests/chrome_startup.mjs` and `chrome_startup.test.mjs` now gate negative
deadline cases on observed probes/stderr, and gate partial-file publication on
the startup reader's incomplete-discovery counter. Tests advance an injected
logical deadline after establishing that state. Normal startup remains a
monotonic 60 seconds; a separate real watchdog remains finite. The real
21-second cold-start check remains. All 14 startup/process-cleanup tests pass
on macOS and Linux, and independent review closes the partial-state sensitivity
finding. The engineering README explains the clocks. Firmware, protocols,
operator behavior, and production resources are unchanged. Both final carrier
jobs must explicitly select this new immutable companion source; neither
repository is delivered on devel/main by these feature-branch pushes.

Representative desktop/mobile/light/dark PPS, tone, support, and logging
captures are inspected; production UI assets and operator behavior are unchanged.
The component README documents the shared test APIs and local requirements.
Operator documentation was considered and does not require changes.

Independent adversarial review and corrective reassessments cover failed-launch
ownership, no-PID cleanup, port collisions, inherited budgets, strict mode,
exception details, callback lifetime and cancellation. The complete actual
network/USB recipes and all required CI contexts must still be inspected for
the final pushed feature revision; local fixtures or historical green results
do not substitute for those gates. Final CI outcome/source identities and timing
limitations belong with the execution result. No comparable full-success timing
improvement is claimed from the prior partial baseline or local runs.

The next paired run [41884960203](https://github.com/WsprryPi/WsprryPi/actions/runs/41884960203) passed both production interoperability blocks and the corrected Pico startup tests, but the final Pi network browser exposed a missing shared-client constructor default: `dialogError` was undefined rather than the existing consumers' explicit null success state. The constructor now initializes that state, and the expected-dialog socket test asserts it after acknowledgment. The strict consumer assertion is preserved. Independent reassessment found no further issue; the final revised source still requires paired CI. The two added correction lines are charged in the totals above. Two macOS ARM contexts in that run executed no steps because GitHub could not acquire hosted runners.

## Phase 3 behavioral assertions

Phase 3 continues from Pi `c5d0beaac4a5f883c76e7a582d98ac00fcf896ad` and Pico `1c30709186fabb64aa250da96d08914296ff3d89` in the established feature worktrees. The frozen ledger remains unchanged. No Phase 4 consolidation, primary-checkout synchronization, deployment, service operation, physical USB, GPIO or RF qualification is included.

| Disposition | Checks and surviving obligation owners |
| --- | --- |
| DELETE | `scripts/tests/support_bundle_contract_reconciliation_test.py`: source/prose lint is replaced by the actual collector's closed eleven-field manifest, actual fourteen-field immutable receipt producer, and actual signed intake validator's missing/extra/type refusals. `support-bundle-collector-contract-test` replaces the old Make/workflow invocation, activating the existing collector fixture in ordinary CI. Documentation was reviewed directly; headings and prose spelling are not runtime contracts. |
| REWRITE | `src/tests/ui_source_regression_test.cpp`: retire 66 redundant assertion groups and the self-asserting ten-line version fixture; retain 823 lines of additional source-policy guards. Production-browser conditional GPIO/CW/Tone/support/runtime/Spots suites and `update_check_comparison_test.js` own the replaced observable outcomes. Source checks alone are not behavioral qualification. |
| REWRITE | `rp1_gpclk_diagnostics_test.py` executes the actual passive collector dispatch and bounded-command helpers with an exact recording command boundary. Unsafe provider ownership/permissions and missing timeout refuse inspection. Existing provider diagnostics tests own the actual identity formatter. Hardware-facing admin-probe source restrictions remain additional static policy. |
| REWRITE | Production encryption metadata is compiled and executed against independent public metadata. WSPR reference JSON is parsed and compared to independent closed objects/types; exact wire/symbol vectors remain. Collector/receipt/intake assertions exercise actual production boundaries. |
| REWRITE | Source selection shares the bounded byte/line reader; malformed, partial and closed readiness are tested after explicit child fault acknowledgment. PPS queue waits use monotonic stage deadlines and last-state diagnostics. Logger shutdown uses an entered/released predicate-mutex gate instead of a 150ms sleep. Dependency tests control timestamp ordering and verify rebuilt behavior instead of sleeping. |
| REWRITE | QRSS removes old output before each actual run, rejects absent/symlink/empty traces, and checks typed identities, events, frequencies, logical duration and cleanup. QRSS and WSPR participate in the same fixed-trace lock; deterministic output comparison and the hardware-access audit remain. Screenshot capture settles rendering; support captures use immediate scrolling so screenshots actually show the selected panel. |
| RETAIN | Exact confirmation phrases, current schema/version/identity boundaries, WSPR symbols, independent carriers, platform profiles, explicit simulated selection, no-hardware audit, source provenance and privacy/architectural restrictions. No required context is removed. |
| HOLD | Remaining source guards, including backend WebSocket ownership/re-arm, pushed-status dispatch, passive Tone rejection/status reconciliation, build provenance, Apache boundaries, polling/identity resources, update-panel presentation, GPIO autosave wiring and stock defaults. `support_bundle_ui_test.js`, `network_safety_ui_test.js` and `responsive_shell_logs_test.js` retain residual source policies where executable coverage is incomplete. These are additional guards, not proof that every static assertion has become behavioral. Other inactive tests remain unchanged and earn no reduction credit. |

The retired UI source groups are identified against the Phase 2 revision, using inclusive original line ranges: 186–206, 224–227, 255–287, 306–398, 402–432, 442–468, 478–516, 609–611, 648–662, 701–731, 767–947, 1070–1126, 1133–1140, 1150–1200, 1208–1217, 1250–1258, 1345–1355, 1437–1440, 1472–1477, 1496–1550, 1557–1588, 1636–1665, 1669–1710, 1723–1752 and 1759–1763. Only complete `require` groups in those ranges are removed; declarations are pruned only when unused. The ten-line self-asserting fixture at 732–741 earns zero credit. Original 1711–1722 were explicitly retained after adversarial reassessment exposed a replacement gap. O07/O09/O13 are owned by production browser handlers and existing runtime semantics; O08/O14 by bounded lifecycle/dependency/source-selection execution; O11/O12 by actual producer/validator/collector boundaries; O15 by typed actual simulator and reference outputs. O01–O06 production network/USB obligations remain required without schema changes.

The one production correction is in `WsprryPi-UI/data/site.js`: unknown nonempty GitHub comparison relationships now fail as malformed data, rather than becoming a successful no-update result. Tests observe the actual failure pipeline, failed footer and absence of a success-cache entry. Valid relationships and operator procedures are unchanged. No public test setting or production API was added.

Independent adversarial review found and corrected an insufficient dependency timestamp setup, an initially ineffective logger gate, a missing malformed-comparison outcome, two unjustified source-group retirements, a unit-parser serialization coverage gap, screenshots captured during smooth scrolling, missing dirty-build orchestration sensitivity, and a root-only RP1 fixture assumption. The RP1 recording fixture now uses an existing root-owned regular file without executing it, and passes as uid 65534 with an additional nonroot-ownership refusal case. Reassessment retains the unresolved static policies above rather than claiming their replacement. Disposable candidates demonstrated intended assertion failures for a wrong compiled encryption recipient, missing Make dependency inclusion, an extra receipt field, a validator accepting extra fields, missing logger shutdown predicate locks, an unknown GitHub comparison, truncated unit-bearing CW frequency serialization, bypassed network-safety confirmation, and omitted dirty-build orchestration metadata. Separate trace-boundary faults demonstrated refusal of stale output and an integer masquerading as a JSON RF boolean. Compiler errors, missing dependencies and fixture-startup failures were corrected before recording these as detected defects.

Local checks pass: `npm run test:unit`, `npm run test:browser`, `node src/tests/update_check_comparison_test.js`, `python3 -B src/tests/wtp_network_process_test.py`, focused Debian Make targets for PPS/private receipt/intake/collector/encryption metadata/update/source-policy/Make semantics/object dependencies, the actual WSPR reference regression runner, ten actual logger shutdown/drain checks, and actual QRSS/WSPR scripts followed by `assert-no-transmitter-hardware.sh`. Debian builds use separate `build/phase3-bookworm` outputs after an initial mixed-toolchain link failure; that failed build is not validation evidence. The source executable was also compiled with warnings enabled and executed. The required full feature CI and exact paired source results are recorded below after execution.

Impeccable review uses the established design without CSS/template changes. Automated coverage retains five pages, four modes, two themes, three viewports, 200% text, keyboard/accessibility/contrast checks, and 160 page-warning combinations, alongside the eighteen Tone workflows, 32 GPIO scenarios and seventeen CW scenarios. Exported desktop/mobile Maintenance and support success/refusal captures were visually inspected for readable wrapping, visible consent/failure actions and absence of horizontal clipping. Screenshots are temporary verification artifacts, not new operator documentation.

The first full feature run, [41904185640](https://github.com/WsprryPi/WsprryPi/actions/runs/41904185640), exposed an inherited ARM macOS `--list-i2c-adapters` three-second timeout after the native binary audit passed. No documented product latency contract justified that cutoff. Retaining it initially was an incorrect interpretation of the requirement to preserve genuine timing contracts. The CLI oracle now waits for actual process/pipe completion, validates successful exit and the expected output/closed inventory JSON, and reports observed duration without a latency assertion. A separate sixty-second hang watchdog limits one failed cohort to a minute within the twenty-minute native job; sampling and kill/reap have separate three-/two-second diagnostic/cleanup bounds. No production deadline is changed. The actual discovery call's original stalled runtime stage is unestablished and remains HOLD; this is a correction of test policy, not a claim to have fixed a production hang.

The old three repetitions were probabilistic race sampling. All eighteen distinct option/logging cases remain in every existing profile; six Linux and two native macOS contexts now perform 144 CLI dispatches rather than 432 (288 fewer, 66.7%). Shutdown race sensitivity belongs to the actual Linux logger's entered/released predicate-mutex test. Native macOS retains actual CLI lifecycle coverage, not a claim of deterministic native race proof. Independent FIFO-gated completion stayed pending past the former three-second cutoff and passed only after explicit release and exit zero. A child producing output but never exiting failed the watchdog, preserved output/state diagnostics, and was killed and reaped. A fresh isolated native ARM release passed all eighteen CLI cases (observed maximum 0.434s); the Debian simulated executable also passed. These durations are observations, not acceptance thresholds. This removes a timing oracle and its probabilistic repetitions; it does not start general Phase 5 execution-graph work.

Documentation Impact: this engineering map is updated; the simulated-backend guide, WSPR reference interface and operator documentation were reviewed for impact and require no procedure change. The cancelled operator-documentation request is not resumed. No sibling documentation repository is modified.

| Phase 3 measure | Phase 2 delivered | Phase 3 candidate |
| --- | ---: | ---: |
| Active Pi infrastructure files, including activated collector and Phase 1 helper | 146 | 146 |
| Physical lines in frozen active roots plus Phase 1 helper, excluding frozen inactive paths | 31,559 | 31,191 |
| Newly activated collector physical lines charged to after-total | 0 | 496 |
| Comparable active Pi physical after-total | 31,559 | 31,687 |
| Additional Phase 3 source lines outside those roots, including protected tests and production prerequisite | 0 | 372 |
| Required workflow definitions / expanded contexts | 8 / 9 | 8 / 9 |
| CLI dispatches across six Linux and two native macOS profiles | 432 | 144 |
| Inactive cleanup reduction credit | 0 | 0 |

The raw frozen-root reduction is 368 lines, but activating all 496 collector lines and charging all 372 additions outside those roots changes the result. Conservatively exclude 28 removed blank/comment lines, another 62 nonblank lines from the retired documentation lint (77 total, fifteen already counted), the ten-line self-asserting UI fixture, and nine further nonblank repository-instruction prose assertions. Phase 3 therefore has **negative credit of 609 lines**: `368 - 496 - 372 - 28 - 62 - 10 - 9`. Previous cumulative credit of 274 becomes **−335 lines (−1.05%)**, an equivalent 32,306 lines against frozen B=31,971. The 50% code target is not achieved; strengthening efficacy has an explicitly charged cost. File count stays at 146, versus frozen 143 and a target at most 71; a deleted lint file is offset by activating a genuine collector suite. All additions are counted, including 286 update-check lines outside the denominator. Time must be measured from the final full run; no local speedup or matched-run improvement is claimed.

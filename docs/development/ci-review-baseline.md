# CI review baseline and coverage map

This document preserves the Phase 1 baseline and records subsequent implementation and validation in the phase sections below. Baseline figures and source identities are historical; the latest delivered after-totals and tested source pair are identified in the latest phase section. Phases 2–5 are implemented and validated; Phase 6 compilation work is in progress; Phase 7 has not started.

This is software CI evidence, not release or physical hardware qualification.

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

The retired UI source groups are identified against the Phase 2 revision, using inclusive original line ranges: 186–206, 224–227, 255–287, 306–398, 402–432, 442–468, 478–516, 549–611, 648–662, 701–731, 767–947, 1070–1126, 1133–1140, 1150–1200, 1208–1217, 1250–1258, 1345–1355, 1437–1440, 1472–1477, 1496–1550, 1557–1588, 1636–1665, 1669–1710, 1723–1752 and 1759–1763. Only complete `require` groups in those ranges are removed; declarations are pruned only when unused. The ten-line self-asserting fixture at 732–741 earns zero credit. Original 1711–1722 were explicitly retained after adversarial reassessment exposed a replacement gap. O07/O09/O13 are owned by production browser handlers and existing runtime semantics; O08/O14 by bounded lifecycle/dependency/source-selection execution; O11/O12 by actual producer/validator/collector boundaries; O15 by typed actual simulator and reference outputs. O01–O06 production network/USB obligations remain required without schema changes.

The one production correction is in `WsprryPi-UI/data/site.js`: unknown nonempty GitHub comparison relationships now fail as malformed data, rather than becoming a successful no-update result. Tests observe the actual failure pipeline, failed footer and absence of a success-cache entry. Valid relationships and operator procedures are unchanged. No public test setting or production API was added.

Independent adversarial review found and corrected an insufficient dependency timestamp setup, an initially ineffective logger gate, a missing malformed-comparison outcome, two unjustified source-group retirements, a unit-parser serialization coverage gap, screenshots captured during smooth scrolling, missing dirty-build orchestration sensitivity, and a root-only RP1 fixture assumption. The RP1 recording fixture now uses an existing root-owned regular file without executing it, and passes as uid 65534 with an additional nonroot-ownership refusal case. Reassessment retains the unresolved static policies above rather than claiming their replacement. Disposable candidates demonstrated intended assertion failures for a wrong compiled encryption recipient, missing Make dependency inclusion, an extra receipt field, a validator accepting extra fields, missing logger shutdown predicate locks, an unknown GitHub comparison, truncated unit-bearing CW frequency serialization, bypassed network-safety confirmation, and omitted dirty-build orchestration metadata. Separate trace-boundary faults demonstrated refusal of stale output and an integer masquerading as a JSON RF boolean. Compiler errors, missing dependencies and fixture-startup failures were corrected before recording these as detected defects.

Local checks pass: `npm run test:unit`, `npm run test:browser`, `node src/tests/update_check_comparison_test.js`, `python3 -B src/tests/wtp_network_process_test.py`, focused Debian Make targets for PPS/private receipt/intake/collector/encryption metadata/update/source-policy/Make semantics/object dependencies, the actual WSPR reference regression runner, ten actual logger shutdown/drain checks, and actual QRSS/WSPR scripts followed by `assert-no-transmitter-hardware.sh`. Debian builds use separate `build/phase3-bookworm` outputs after an initial mixed-toolchain link failure; that failed build is not validation evidence. The source executable was also compiled with warnings enabled and executed. The full feature CI and exact paired source results are recorded below.

Impeccable review uses the established design without CSS/template changes. Automated coverage retains five pages, four modes, two themes, three viewports, 200% text, keyboard/accessibility/contrast checks, and 160 page-warning combinations, alongside the eighteen Tone workflows, 32 GPIO scenarios and seventeen CW scenarios. Exported desktop/mobile Maintenance and support success/refusal captures were visually inspected for readable wrapping, visible consent/failure actions and absence of horizontal clipping. Screenshots are temporary verification artifacts, not new operator documentation.

The first full feature run, [41904185640](https://github.com/WsprryPi/WsprryPi/actions/runs/41904185640), exposed an inherited ARM macOS `--list-i2c-adapters` three-second timeout after the native binary audit passed. No documented product latency contract justified that cutoff. Retaining it initially was an incorrect interpretation of the requirement to preserve genuine timing contracts. The CLI oracle now waits for actual process/pipe completion, validates successful exit and the expected output/closed inventory JSON, and reports observed duration without a latency assertion. A separate sixty-second hang watchdog limits one failed cohort to a minute within the twenty-minute native job; sampling and kill/reap have separate three-/two-second diagnostic/cleanup bounds. No production deadline is changed. The actual discovery call's original stalled runtime stage is unestablished and remains HOLD; this is a correction of test policy, not a claim to have fixed a production hang.

The old three repetitions were probabilistic race sampling. All eighteen distinct option/logging cases remain in every existing profile; six Linux and two native macOS contexts now perform 144 CLI dispatches rather than 432 (288 fewer, 66.7%). Shutdown race sensitivity belongs to the actual Linux logger's entered/released predicate-mutex test. Native macOS retains actual CLI lifecycle coverage, not a claim of deterministic native race proof. Independent FIFO-gated completion stayed pending past the former three-second cutoff and passed only after explicit release and exit zero. A child producing output but never exiting failed the watchdog, preserved output/state diagnostics, and was killed and reaped. A fresh isolated native ARM release passed all eighteen CLI cases (observed maximum 0.434s); the Debian simulated executable also passed. These durations are observations, not acceptance thresholds. This removes a timing oracle and its probabilistic repetitions; it does not start general Phase 5 execution-graph work.

Documentation Impact: this engineering map is updated; the simulated-backend guide, WSPR reference interface and operator documentation were reviewed for impact and require no procedure change. The cancelled operator-documentation request is not resumed. No sibling documentation repository is modified.

| Phase 3 measure | Phase 2 delivered | Phase 3 delivered |
| --- | ---: | ---: |
| Active Pi infrastructure files, including activated collector and Phase 1 helper | 146 | 146 |
| Physical lines in frozen active roots plus Phase 1 helper, excluding frozen inactive paths | 31,559 | 31,191 |
| Newly activated collector physical lines charged to after-total | 0 | 496 |
| Comparable active Pi physical after-total | 31,559 | 31,687 |
| Additional Phase 3 source lines outside those roots, including protected tests and production prerequisite | 0 | 372 |
| Required workflow definitions / expanded contexts | 8 / 9 | 8 / 9 |
| Additional C++ paths outside counted harness roots / fully charged added lines | 0 / 0 | 2 / 128 |
| Provider process/socket scenarios retained / total | 12 / 12 | 12 / 14 |
| CLI dispatches across six Linux and two native macOS profiles | 432 | 144 |
| Inactive cleanup reduction credit | 0 | 0 |

The raw frozen-root reduction is 368 lines, but activating all 496 collector lines and charging all 372 additions outside those roots changes the result. Conservatively exclude 28 removed blank/comment lines, another 62 nonblank lines from the retired documentation lint (77 total, fifteen already counted), the ten-line self-asserting UI fixture, and nine further nonblank repository-instruction prose assertions. Phase 3 therefore has **negative credit of 609 lines**: `368 - 496 - 372 - 28 - 62 - 10 - 9`. Previous cumulative credit of 274 becomes **−335 lines (−1.05%)**, an equivalent 32,306 lines against frozen B=31,971. The 50% code target is not achieved; strengthening efficacy has an explicitly charged cost. File count stays at 146, versus frozen 143 and a target at most 71; a deleted lint file is offset by activating a genuine collector suite. All additions are counted, including 286 update-check lines outside the denominator. Final observed time is recorded below; no controlled speedup is claimed.


### Phase 3 final CI evidence

[Final run 41907469373, attempt 1](https://github.com/WsprryPi/WsprryPi/actions/runs/41907469373) passed all nine required contexts on Pi `5cea3a15d6a291356443a3491c6e5522f691bbc7` paired with Pico `1c30709186fabb64aa250da96d08914296ff3d89`. Both completed carrier logs confirm those immutable checkout identities. The network job includes runtime, interop/config interop, actual public/encrypted Pico handlers, selected-source edits and genuine compiler failures; the USB job executes the actual Pi client/UI producer and Pico USB management driver. These are hardware-free software integration results. Both native macOS audits, strict I2C, portable/full semantics, UI/browser and support checks, and all six GCC release profiles passed. GCC logs confirm six eighteen-case CLI cohorts and the ten actual deterministic logger shutdown/drain checks; ARM macOS records eighteen CLI cases, observed maximum 0.018s. The implementation and subsequent documentation-only evidence commit have identical executable sources.

| CI time measure | Phase 2 final run | Phase 3 final run | Observed change |
| --- | ---: | ---: | ---: |
| Created to last completed job, including runner wait | 29m04s | 22m42s | −21.90% |
| Aggregate unweighted job execution | 88m14s | 89m15s | +1.15% |
| Required successful contexts | 9 | 9 | unchanged |

Elapsed time uses run creation through the last job's completion; aggregate time sums each job's completed-minus-started timestamps, including runner setup/cleanup. Runner acquisition remains in elapsed time rather than being counted as successful execution. These are different source/runner samples; the shorter wall time and slightly greater aggregate cost do not establish a controlled performance improvement or the 50% time target. The unsuccessful first candidate consumed 23m17s elapsed and 84m00s aggregate job time, with eight successful contexts and the ARM three-second cutoff failure. Both Phase 3 runs together span 43m13s wall time with overlapping execution and 173m15s aggregate job time. Failed-run cost is disclosed separately, not hidden in the final green sample.

Validation entry points executed include `npm run test:unit` and `npm run test:browser` inside the Debian browser container; `node src/tests/update_check_comparison_test.js`; `python3 -B src/tests/wtp_network_process_test.py`; `python3 -B scripts/ci/review_inventory.py --check docs/development/ci-review-baseline.json`; `python3 -B scripts/tests/logger_shutdown_test.py`; `python3 -B scripts/tests/rp1_gpclk_diagnostics_test.py` as root and uid 65534; and `bash scripts/tests/collect-support-bundle_test.sh` as root and uid 65534. The focused Debian Make command, run from `src`, was `make rp1-pps-calibration-test support-bundle-private-artifact-test support-bundle-intake-validation-test build/phase3-bookworm/bin/ui_source_regression_test support-bundle-collector-contract-test support-bundle-production-encryption-metadata-test update-check-comparison-test wtp-network-process-test semantics-make-regression-test object-dependency-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= OBJ_DIR_DEBUG=build/phase3-bookworm/obj BIN_DIR=build/phase3-bookworm/bin -j4` with hardware access disabled, followed by the built source-policy executable. An initial nonexistent focused target was corrected before recording success. Actual QRSS and WSPR verification scripts passed from an isolated simulated working directory and their two strace files passed `scripts/ci/assert-no-transmitter-hardware.sh`. The standalone WSPR reference runner's exact JSON/vector regressions passed. Native ARM release used explicit `SI5351_TRANSPORT=mcp2221a-hid`, `BACKENDS=si5351 ANCILLARY_GPIO=0`, OpenSSL pkg-config selection and isolated `build/phase3-native-arm` outputs; its eighteen-case CLI suite passed without device opens. The full final workflow was explicitly dispatched with `gh workflow run debian-non-hardware.yml --ref codex/ci-review-simplification -f pico_ref=1c30709186fabb64aa250da96d08914296ff3d89`.

Phase 3 is delivered with the retained static policies and original unexplained ARM runtime delay explicitly on HOLD. No claim is made that the original delay's production cause was found, that arbitrary delays are synchronization, or that a green rerun establishes absence of every race. Phase 4 has not started. No hardware, service, deployment, RF or operator-documentation changes were made.

## Phase 4 system fixtures

Phase 4 continues from Pi `89133f97f365941c1edee193f83323aa9c3dc4b0` in `/Users/lbussy/.codex/worktrees/ci-review-simplification/WsprryPi` on `codex/ci-review-simplification`. The frozen Pi base remains `94ff1590368f6ecbeb431e64f04af3f44f0ddb8e`; the intended companion remains Pico `1c30709186fabb64aa250da96d08914296ff3d89` in its preserved feature worktree. No UI or companion source is changed. The parent provider helper gains an internal typed fixture gate with its production wrapper and defaults preserved. A bounded failure-diagnostic addition to the existing RP1 trace stage is described below; no execution-graph or compilation work from Phases 5–6 is included.

| Disposition | Exact paths and boundary |
| --- | --- |
| REWRITE | `scripts/tests/installer_bootstrap_test.py`, `installer_dry_run_purity_test.py`, `installer_output_test.py`: share owned temporary-root registration and process-group completion/cleanup; preserve each family's source injection, environment, declared operational stubs, exact Bash quoting and output assertions. |
| REWRITE | `scripts/tests/rp1_gpclk_dkms_install_test.py`: replace default-success `FakeRunner` with exact declared inspections and ordered required effects; include cwd/passthrough/stage, persist caught refusals and assert required consumption during test cleanup. Migration and removal use explicit ownership builders and owned roots instead of borrowing another TestCase's lifecycle. Existing runtime, release and historical recovery document versions remain distinct. |
| REWRITE | `scripts/tests/picotool_build_test.py`, `picotool_install_test.py`: close permissive build and help callbacks; verify exact Docker build/export/security arguments, owned resource identities, stage ordering, validation timeout and required container cleanup. No Docker command from these fixtures reaches a real runner. |
| REWRITE | `scripts/tests/support_bundle_intake_manifest_preparation_test.py`, `support_bundle_intake_manifest_lifecycle_test.py`, `support_bundle_intake_publication_commit_test.py`, `support_bundle_intake_publication_push_test.py`, `support_bundle_intake_publication_verification_test.py`: share fresh metadata, fixed public vectors and strict deterministic OpenSSL adapters. Publication consumers own a dedicated fixture object; they no longer import or instantiate another test suite. Unknown crypto calls leave a persistent refusal checked before fixture deletion. Preparation success observes the required derive/sign/verify sequence. |
| REWRITE | `scripts/tests/support_bundle_intake_identity_ceremony_preflight_test.py`, `support_bundle_intake_trust_compilation_test.py`, `support_bundle_key_provisioning_test.py`, `support_bundle_signing_key_provisioning_test.py`: share the applicable owned roots, filesystem observations or fixed public vectors while retaining separate signing/encryption policies and real-tool checks. |
| ADD, fully charged | `scripts/tests/fixture_commands.py` (114 lines): offline exact command expectations plus owned shell-process lifecycle; `fixture_filesystem.py` (61 lines): registered physical temporary roots, exact Bash quoting and anchored no-follow snapshots; `support_intake_fixture.py` (165 lines): fresh intake metadata, deterministic crypto faults and isolated real bare-Git publication setup. These are coherent primitives, not a fake operating system or a universal fixture base class. |
| DELETE | Only replaced fixture bodies, duplicated encoders and test-to-test fixture imports/lifecycle borrowing. No standalone suite or original scenario is deleted; code moved into helpers is fully counted. |
| REWRITE | `src/rp1_provider_process.hpp`, `src/tests/rp1_provider_process_test.cpp`: correct demonstrated pre-exec false coverage with acknowledged child states before unchanged fault deadlines; retain the production wrapper and actual owned-child cleanup path. Keep socket streaming live through parent closure, reject startup EOF/malformed acknowledgments, and assert peer exit status. These additions outside the counted harness roots are fully charged; no protected-source deletion earns credit. |
| RETAIN / HOLD | Precompiled archive/ELF/package cases, dependency and service/INI shell fixtures, RP1 qualification/reconciliation/provider/update/release contracts, support inspection/processing/retention/deletion and their intentionally different fault adapters remain separate. Existing age/OpenSSL round trips and the pinned-provider contract are retained. Inactive Pico bundle/deploy/runtime-install and other unique/manual fixtures remain HOLD; none is removed or credited. The earlier unexplained ARM runtime delay remains HOLD, and its Phase 3 completion oracle is unchanged. |

The migrated O10/O12/O14 command boundary rejects unlisted calls, wrong cwd or passthrough, out-of-order mutations, missing required effects and attempts to relabel consumed mutations as repeatable reads. A response keyed by an unsupported context object is rejected instead of silently becoming success. Explicitly declared inspections may repeat. Tests cannot fall through from the fake runner to real administration. Shell fixtures still execute their deliberately selected installer functions and declared stubs; the shared lifecycle owns the session/process group, waits for actual completion and refuses successful leader exit with leftover group members. Failure, cancellation, kill/reap and pipe-close errors preserve the primary failure plus secondary cleanup errors. The thirty-second operation watchdog and two-second cleanup watchdog bound hung fixtures; they are not installer latency requirements or assumed synchronization. No production deadline or retry policy changes.

O10/O11/O12 filesystem observations retain type, mode, uid/gid, link count, mtime, device/inode and content digest. Every ancestor and tree entry is opened through anchored no-follow descriptors; `..` is refused, and nonblocking file opens prevent a file-to-FIFO substitution from hanging before identity validation. Symlink targets are recorded without traversal. Builders return fresh dictionaries and do not rewrite existing invalid inputs. Temporary-root cleanup is registered before further setup, and publication construction preserves setup plus cleanup failures. Publication Git is confined to owned repositories and closed local command forms; no clone/fetch/push or global configuration mutation is permitted. Ambient Git configuration is stripped from fixture commands, and local hooks are disabled for production calls into the fixture repository. Real publication concurrency, lock, exact-byte commit, rollback and confirmation cases remain.

Five production-derived recipient encoders become one fixed-vector lookup; the vectors are no longer calculated using the implementation under test. Canned OpenSSL remains only a deterministic fault boundary. All independent real age-keygen, Ed25519 signing, exact-manifest verification and lifecycle rotation tests execute with real tools on Debian; crypto compatibility is not inferred from the canned adapter. Signing, encryption, publication and retention policies are kept separate. O01–O09/O13/O15 retain their existing execution owners, including both required production carriers; no protocol/schema/identity/limit is relaxed.

All 291 original named scenarios across the fifteen migrated suites remain. Three new cases protect missing/caught command effects, link/special-file/substitution observations, and invalid metadata/case isolation, for 294 scenarios. The same named entry points remain scheduled. Shared shell launch implementations reduce from three to one, filesystem observation implementations from two to one, and support publication test-suite borrowers from two to zero; RP1 also stops calling another TestCase's setup. These are fixture-structure counts, not a claim of fewer distinct obligations or fewer workflow contexts.

Iterative independent adversarial review exposed and closed context-keyed failure responses becoming success, optional reads bypassing required order (including after consumption), background descendants surviving successful shell exit, unobserved crypto refusal, permissive Picotool arguments and ancestor symlink/FIFO substitution. Independent probes confirmed owned descendant termination, preserved primary/cleanup exceptions, unknown crypto refusal after an ignored exit, fresh nested metadata, no-follow special-file handling, and parent-component refusal. Four executing production-body mutations in disposable in-memory candidates failed for the intended reasons: omit APT installation, inject reboot, add `--privileged`, or omit container cleanup. No production source file was changed by these probes. All review findings were reassessed and closed before delivery.

Local validation passed all 294 migrated scenarios together on Debian with `WSPRRYPI_REAL_OPENSSL=/usr/bin/openssl` and `WSPRRYPI_REAL_AGE_KEYGEN=/usr/bin/age-keygen`, with zero skips. Native macOS passed the same 294-case collection with the four explicit real-tool opt-ins skipped; those checks passed separately on Debian. The full 127-case support Python collection passed with real tools and zero skips. Adjacent precompiled checks passed. Exact entry points include `python3 -B -m unittest discover -s scripts/tests -p 'support_bundle*_test.py'` with those real-tool variables; `python3 -B -m unittest discover -s scripts/tests -p 'installer*_test.py'`; `python3 -B scripts/tests/rp1_gpclk_dkms_install_test.py`; `python3 -B -m unittest discover -s scripts/tests -p 'picotool*_test.py'`; `python3 -B scripts/tests/precompiled_binary_test.py`; and `python3 -B scripts/ci/review_inventory.py --check docs/development/ci-review-baseline.json`. The 294-case collection comprises those fifteen migrated module names loaded together by the standard unittest loader. Whitespace checks pass. Full final CI and exact paired identities are recorded below.

| Phase 4 measure | Phase 3 delivered | Phase 4 delivered |
| --- | ---: | ---: |
| Active Pi infrastructure files, including activated collector and Phase 1 helper | 146 | 149 |
| Physical lines in frozen active roots plus Phase 1 helper, excluding frozen inactive paths | 31,191 | 31,341 |
| Activated collector lines included in physical after-total | 496 | 496 |
| Comparable active Pi physical after-total | 31,687 | 31,837 |
| Newly introduced fixture helpers / fully charged lines | 0 / 0 | 3 / 340 |
| Original migrated named scenarios retained / total | 291 / 291 | 291 / 294 |
| Migrated standalone suite entry points | 15 | 15 |
| Required workflow definitions / expanded contexts | 8 / 9 | 8 / 9 |
| Additional C++ paths outside counted harness roots / fully charged added lines | 0 / 0 | 2 / 128 |
| Provider process/socket scenarios retained / total | 12 / 12 | 12 / 14 |
| Inactive cleanup reduction credit | 0 | 0 |

Phase 4's raw physical increase is 150 lines, including three additional workflow diagnostic lines. All 340 helper lines are included, as are the three new cases and strict fault/effect guards. Conservatively excluding 24 removed blank/comment lines makes Phase 4 credited reduction **−302 lines**, after charging 128 added C++ lines outside the denominator without crediting their seventeen test lines or three helper lines removed. The exclusions are original `89133f97` lines: `installer_dry_run_purity_test.py` 26–27 and 47–48; `rp1_gpclk_dkms_install_test.py` 97, 2436 and 2459; `support_bundle_intake_publication_commit_test.py` 2, 4, 17–18, 23–24, 27–28, 39–40, 42–43, 89, 91, 96, 101 and 103. No unused/dormant cleanup is credited, and no scaffolding is moved outside the ledger. Previous cumulative credit −335 becomes **−637 lines (−1.99%)**, equivalent to **32,608 against frozen B=31,971**. The overall 50% code/file/time targets are not achieved. Removing duplicate setup does not offset the cost of stronger boundaries; no reduction is claimed by hiding those additions. Final full-run timing is measured below.

The first Phase 4 full run [41917027533](https://github.com/WsprryPi/WsprryPi/actions/runs/41917027533) on Pi `e7e4c3b1e51bf079b724b66ac196e99f36f445d6` stopped before the migrated fixtures: strace 6.13 reported `ptrace(PTRACE_LISTEN,pid:21207,sig:0): Input/output error` while tracing the unchanged RP1 provider-process stage. No preceding named test assertion failed; the original trace was not retained. The exact unchanged provider subprocess executable passed locally under both Bookworm strace 6.1 and Trixie strace 6.13. Tracer/child-stop/exit interaction is a hypothesis, not an established cause. REWRITE is limited to the existing workflow stage: print kernel/tracer identity, include process/signal events alongside every existing open/openat event, and print a bounded 120-line trace tail on failure while preserving the exact original exit status. The exit trap was checked with success and exit 17; the expanded trace passed the same Trixie provider test. No timeout, assertion, cleanup requirement, hardware audit or workflow context is removed or relaxed. The instrumented predecessor on Pi `dc9462dfaaa337227d090f81fbe02438cde3833a` passed all nine contexts in [run 41919509823](https://github.com/WsprryPi/WsprryPi/actions/runs/41919509823); this establishes nonreproduction, not repair of the original tracer fault. That run and the final corrected run record Linux `7.0.0-1017-azure` with strace 6.13; the original failing run did not record its kernel identity. The unexplained fault remains HOLD pending causal evidence.

A separate controlled adversarial probe reproduced false coverage: strace `-e inject=execve:delay_enter=100ms` held the unchanged stall and closed-pipe children inside unfinished execve, yet both timeout cases reported PASS. This is not a reproduction of the original PTRACE_LISTEN error. The correction uses a dedicated inherited readiness pipe: each child acknowledges its actual branch after exec, and the closed-pipe child closes both output descriptors before acknowledgment. An internal typed C++ gate runs within the existing owned-child cleanup boundary before starting the unchanged 50ms fault deadline; the public production wrapper passes a no-op and keeps its original defaults and behavior. Startup has a separate five-second hang guard with expected acknowledgment, stage, observed transition, child PID and elapsed-time diagnostics. EOF/malformed readiness are distinct negative cases and verify kill/reap. Guessed sleep60 and the generic five-second elapsed-time assertion are removed; the timeout cases require response-timeout and exit-timeout respectively. Socket drip starts with acknowledged buffered data, continues until parent socket closure, and uses a distinct five-second fixture guard whose expiry is a failed peer exit, never success. Its five-millisecond pacing is deliberate data stimulus for the actual absolute exchange deadline, not synchronization. The unrelated 300ms latency cutoff is removed. Owned-peer cleanup and successful child exit are asserted. All twelve original process/socket scenarios remain, with two new readiness-refusal cases. The controlled pre-exec delay now reaches the acknowledged states before the same 50ms faults. Deliberately omitting the gate, omitting acknowledgment, omitting output closure or resetting the absolute deadline each fails its intended assertion in disposable compiled candidates. This provides causal evidence for the fixture correction only; the tracer cause remains HOLD.

Bookworm passed `g++ -std=c++17 -Wall -Wextra -Werror src/tests/rp1_provider_process_test.cpp -o /evidence/provider-synchronized-bookworm && /evidence/provider-synchronized-bookworm`; Trixie passed the synchronized executable under normal tracing and controlled pre-exec injection, then rebuilt the final stricter source through the complete Make target set below. Independent native strict-warning validation also passed. The final local Trixie regression passed the workflow's complete RP1 target set under expanded lifecycle/open tracing with `make -j2 object-dependency-test rp1-gpclk-planner-test rp1-gpclk-lifecycle-test rp1-gpclk-transition-test rp1-gpclk-backend-test rp1-gpclk-linux-provider-test rp1-gpclk-development-policy-test rp1-gpclk-route-runtime-wiring-test rp1-gpclk-route-service-test rp1-gpclk-transmit-backend-test rp1-gpclk-diagnostics-test rp1-release-acceptance-test BIN_DIR=build/phase4-rp1-trixie SUDO=` and `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`; isolated output preserves existing builds. The unchanged hardware-access audit passed the resulting trace. The expanded tracer also passed all five existing audit refusal/error tests. Controlled defect probes run only in owned temporary copies and retain no product changes.

### Final Phase 4 CI qualification and time

[Final run 41921336612, attempt 1](https://github.com/WsprryPi/WsprryPi/actions/runs/41921336612) passed all nine required contexts on Pi `c37cc09df75411e5471b06223539207eacd90c6e` paired with Pico `1c30709186fabb64aa250da96d08914296ff3d89`. Both completed carrier logs confirm these immutable source identities. The network job executes runtime, interop and configuration interop against actual Pico handlers, crypto and persistence, public/encrypted HTTP, edited-source selection, genuine compiler failures, and route recovery. The USB job executes the actual Pi clients/UI producer and Pico USB management driver, including framing/ACK and refusal-before-writes. The logs confirm pinned Mbed TLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20` and read-only RP1 provider `b52ab7b796e44d8690b2a91bccb21ae5d8b670df`. The corrected RP1 fixture and unchanged fail-closed hardware audit pass in the final Debian job. All six GCC release profiles, three native macOS contexts, strict I2C, support/real-tool and portable/full semantics coverage passed. GCC retains six eighteen-case informational CLI cohorts and ten deterministic logger shutdown/drain checks. No required context was skipped, cancelled or timed out.

| CI time measure | Phase 3 final | Phase 4 final | Observed change |
| --- | ---: | ---: | ---: |
| Creation to last job completion, including runner acquisition | 22m42s | 25m51s | +13.88% |
| Sum of completed-minus-started job spans | 89m15s | 83m22s | −6.59% |
| Required successful contexts | 9 / 9 | 9 / 9 | unchanged |

Against the historical full-success anchor of 23m09s / 85m05s, final wall time is 11.66% higher and aggregate job time 2.02% lower. There is still no successful full sample for the frozen Phase 1 source and no controlled same-source/runner/cache distribution. These observations do not demonstrate a causal speedup or the 50% time target; the historical illustrative halfway values remain 11m34.5s elapsed and 42m32.5s aggregate. Code, files, scenario preservation and observed time are separate measures.

The failed first candidate consumed 26m34s elapsed and 75m50s aggregate job time, with eight successful contexts and the tracer failure. The diagnostic predecessor consumed 26m40s and 86m02s with all nine contexts successful. Across all three Phase 4 attempts there were 27 job contexts: 26 succeeded and one failed. The overlapping CI campaign spans 54m03s wall time and consumes 245m14s aggregate job time. Failed and diagnostic work are disclosed rather than hidden in the final green sample. The final implementation and documentation-only evidence successor have identical executable sources; no rerun-only root-cause fix is claimed. The original tracer fault and the earlier unexplained ARM runtime delay remain HOLD. Phase 5 has not started.

Documentation Impact: this engineering obligation map is updated. Installer, support-intake, simulated-backend and operator documentation were considered unchanged because no product behavior, command, installation procedure, UI or operator workflow changes. No operator-documentation work or sibling documentation repository edit is required. Impeccable/visual review is not applicable: browser/UI assertions and presentation are unchanged. No host, service, device, firmware, physical USB or RF operation was performed. Phase 5 has not started.


## Phase 5 execution ownership

Phase 5 continues from Pi `2c1a8acfb0bc64361e71d5d33113977f2423c261` in `/Users/lbussy/.codex/worktrees/ci-review-simplification/WsprryPi`, branch `codex/ci-review-simplification`, retaining frozen Pi base `94ff1590368f6ecbeb431e64f04af3f44f0ddb8e`. The companion remains `1c30709186fabb64aa250da96d08914296ff3d89` in its existing feature worktree, based on `74dd300b6688984359c13ed576443fff356b5033`. No primary checkout, production source, UI presentation, component implementation or companion source is changed. Compilation caching and object/library restructuring remain outside this phase.

| Disposition | Exact boundary and owner |
| --- | --- |
| DELETE invocations | In `.github/workflows/debian-non-hardware.yml`, remove Debian's second `wtp-plan-test` and `wtp-fleet-test`, network's explicit `wtp-tls-test`, Debian's explicit `test-tone-lifecycle-test` and `legacy-tone-frequency-test`, and the later transmitter-component `linux-i2c-identity-test`. Existing WTP, runtime TLS and semantics prerequisite owners execute the same cases in the same profiles. |
| REWRITE dispatch | In `src/Makefile`, split `wtp-production-cpp-test`, `wtp-api-cpp-test` and `wtp-config-cpp-test` from their unchanged public aggregate names. Debian uses C++ owners while unchanged npm unit coverage owns `wtp_ui_test.js` and `wtp_management_test.js`. Standalone aggregates retain both languages; shared `wtp-ui-test` prevents repeated UI execution within one joint Make invocation. New CI owners depend on existing public leaves. Mark missing phony state targets explicitly so same-name files cannot suppress execution. |
| REWRITE source selection | `scripts/ci/selected_source.py`: one scope-job checkout resolves the requested Pico ref. Only its validated immutable SHA reaches both carrier jobs, which require it before checkout and verify it afterward. Manual empty/invalid/unavailable refs fail rather than falling back; normal push/PR selection still defaults explicitly to devel. `scripts/tests/ci_execution_graph_test.py` executes identity, refusal, ownership and real Make-dispatch checks. |
| REWRITE activation prerequisites | `src/tests/guarded_mode_change_persistence_test.cpp`, `monitor_file_regression_test.cpp`, `privileged_network_transaction_test.cpp` and `privileged_network_admin_test.cpp` use uniquely allocated owned roots. Admin rollback asserts all three exact commands and complete fault-response consumption. `pico_usb_installer_test.cpp` uses entered/release gates for exclusion and actual status completion, and a monotonic descendant-cleanup guard. No product deadline is widened. Newly scheduled PHP maintenance CLI/ShellCheck calls receive explicit 30-second hang guards. |
| REWRITE renderer fixture | `scripts/tests/apache_privileged_network_policy_renderer_test.py` runs the actual renderer into owned temporary roots and validates the closed normalized directive sequence, not comment wording or source spelling. It retains missing/invalid-mode enforcement and stale-policy replacement. |
| ACTIVATE | Debian named owners below schedule 23 existing C++ executables and five existing script entry points. The six frozen-inactive files activated are `scripts/tests/apache_privileged_network_policy_renderer_test.py`, `php2shell_mitigate_test.py`, `pico_usb_bundle_test.py`, `pico_usb_deploy_test.py`, `pico_usb_bundle_fixture.py` and `support_bundle_runtime_install_test.sh`. The bundle fixture is an imported dependency, not an independently executed suite. Full activation cost is charged. |
| RETAIN | All eight job definitions/nine expanded contexts; both actual production carriers; native ARM/Intel and backend profiles; full/portable semantics distinctions; actual crypto, reference vectors, refusal-before-write and recovery coverage; named standalone component and public test entry points. Network/native contexts retain their Node/TLS evidence. |
| HOLD | Frozen-inactive `scripts/tests/apache_proxy_config_test.sh`; `WsprryPi-UI/tests/log_stream_disconnect_integration_test.sh`, `log_stream_resume_integration_test.py`, `native_host_fixture.js`, `si5351_ui_integration_test.js` and `wtp_firmware_ui_integration_test.js`. The installed scripts have real Apache/journal prerequisites; the native-host route is explicitly opt-in; legacy browser fixtures require lifecycle review. No automatic discovery activates them. |
| HOLD supplemental sources | `src/tests/privileged_network_runtime_wiring_test.cpp`, `support_bundle_web_server_wiring_test.cpp` and the mixed static sections of `apache_privileged_network_policy_test.cpp` remain supplemental source-policy checks, not substitutes for the scheduled behavioral owners. `support_bundle_collector_executor_test.cpp` retains its standalone fault fixture pending a focused synchronization review; production collector execution remains required through real support/age checks. Existing live qualification probes and component manual tools retain their original boundaries. The earlier tracer fault and ARM delay remain unexplained HOLDs. |

No test file is deleted. Unused/unscheduled cleanup contributes zero credited reduction. A declared or compiled target is not counted as executed coverage: local and final CI evidence must show the actual leaf executions and propagated results.

### Normal execution owners

| Context / owner | Required execution |
| --- | --- |
| Debian WTP C++ / npm | The existing WTP step owns plan/fleet and production C++; frequency/configuration owns API/config C++. `npm test` owns the two shared Node unit suites. Standalone `wtp-production-test`, `wtp-api-test` and `wtp-config-test` retain their complete C++/Node aggregates. |
| Network runtime / semantics | `wtp-network-runtime-test` owns its required TLS suite and generated certificate fixtures before runtime cases. Full semantics owns test-tone lifecycle, legacy tone frequency and delegated Linux I2C identity. Their distinct macOS/native and portable owners remain. Calibration, I2C selection and RP1 persistence subset invocations are retained: they execute distinct argument-selected branches, not the full default main. |
| `ci-focused-state-test` | `guarded-mode-change-persistence-test`, `band-gpio-default-convergence-test`, `band-gpio-rotation-test`, `selector-shutdown-cleanup-test`, `monitor-file-regression-test`, `gpio-line-resolver-test`, `non-wspr-repeat-policy-test`. Scheduler execution and GPIO boundaries are explicitly inhibited/injected; no physical backend qualification is claimed. |
| `ci-network-policy-test` | `support-request-guard-test`, `privileged-network-policy-test`, `backend-http-guard-test`, `websocket-upgrade-guard-test`, `privileged-network-transaction-test`, `privileged-network-runtime-test`, `privileged-network-admin-test`. Administration runs through recording adapters and owned files, not real Apache/systemd operations. |
| `ci-support-lifecycle-test` | `support-bundle-result-validator-test`, `support-bundle-runtime-test`, `support-bundle-download-file-test`, `support-bundle-checksum-file-test`, `support-bundle-archive-digest-test`, `support-bundle-download-preparation-test`, `support-bundle-job-directory-remover-test`, `support-bundle-startup-cleanup-test`. These preserve separate validation, ownership, identity and cleanup contracts. |
| `ci-installer-offline-test` | Owned-root runtime support installation; `pico-usb-installer-offline-test` executes the C++ installer-state fixture and both bundle/deployment Python suites; the actual Apache renderer fixture follows. The unchanged standalone `pico-usb-installer-test` also retains the firmware Node suite, while canonical Debian npm already owns that suite. No actual firmware/device installation occurs. |
| Maintenance / graph | `php2shell-test` executes offline audit/upgrade/planning fixtures. CI installs and requires ShellCheck before dispatch, so its two ShellCheck checks cannot become optional skips. `python3 -B scripts/tests/ci_execution_graph_test.py` requires PyYAML (`python3-yaml` explicitly installed in the scope job). This test-only external dependency is disclosed; no project code is relocated into it and it earns no reduction credit. |

Eight same-context leaf cohorts decrease from seventeen invocations to eight: plan 2→1, fleet 2→1, TLS 2→1, WTP UI 3→1, management UI 2→1, tone lifecycle 2→1, legacy tone frequency 2→1 and Linux I2C identity 2→1. Thus nine duplicate invocations are removed. This is a selected-cohort count, not a 50% reduction of full CI. Activation adds 23 C++ and five script suite entry points; the new graph regression is one more entry point with twelve named methods. Across the affected graph these additions exceed the nine removals by twenty suite dispatches. Source selection adds six validation commands on manual dispatch, five on other full runs; those are orchestration checks rather than test suites. Full workflow context count remains nine.

### Synchronization, failure propagation and adversarial evidence

Pico source identity uses completion dependencies, actual Git state and exact commit equality. No readiness delay, retry-until-green loop, stale success marker, old peer or carrier fallback is introduced. Source Git runs in its own session, disables external fsmonitor, retains the original 30-second operation hang guard, and bounds kill/reap cleanup separately at two seconds. Failures and interruption kill the owned group and preserve both primary and cleanup errors. Successful leaders with surviving group members are refused. A controlled probe of the early candidate helper demonstrated an escaped paused descendant after timeout; it did not demonstrate an unbounded pipe wait. Independent acknowledged-helper probes verified timeout, interruption and leader-exit cleanup after correction. These findings do not explain or repair the earlier strace fault.

The old Pico exclusion fixture could fail after its first operation legitimately completed before the second start attempt. A controlled observer waiting for that actual completion reproduced the false failure. The replacement acknowledges entry and holds the worker until the second attempt is refused, then explicitly releases it and observes completed status. Five-second fixture guards bound missing entry/release/completion; they are resource protection, not assumed operation duration or a product deadline. Descendant cleanup observes actual process state under the original one-second bound, now monotonic and diagnostic; its ten-millisecond polling interval only paces observations. The artificial twenty-second descendant sleep becomes an indefinite owned fault stopped by actual cleanup. Monitor checks retain bounded predicates for callbacks/states; their polling interval is not proof of completion, and filemon initializes its baseline synchronously before returning.

Graph regression tests execute the actual standalone Make dispatch rules with closed compiler/Node/Python/Bash adapters. Positive execution counts establish setup validity; controlled failures identify their intended child, and missing executables remain failures. Same-name goal files cannot hide public suites. Tests parse workflow structure and direct Actions context paths rather than source prose or whitespace. Equivalent dot/bracket/whitespace notation is accepted; fallback computations are rejected. Process fixtures reuse the existing owned-shell helper, with its trusted supervisor resolved before a mocked child PATH. Source-entry tests invoke the actual CLI main in-process so interruption reaches active Git cleanup instead of an outer process guard killing its owner. Direct native CLI record/verify also passed against the intended clean Pico worktree. Real temporary Git histories advance a moving ref after selection and prove both consumers retain the selected commit; dirty, untracked, missing, malformed and mismatched sources refuse publication/use.

Independent disposable mutations removing a required leaf, removing C++ execution, or restoring USB's moving ref fail the intended coverage/identity assertions. A false-acceptance mutation of the production task guard fails its exclusion assertion. Iterative review closed the source-Git descendant leak, malformed-ref help success in the first candidate, diagnostic-adapter defects, fixed-path/rollback-assertion gaps, and a regression oracle that initially accepted an echo in place of source verification. The final wiring test verifies actual interpreter/script/arguments and rejects skipped or tolerated carrier source checks; the workflow itself had not used the no-op. No production source or real host/device was changed by the probes.

### Local qualification and accounting

Safe local execution used a disposable Linux clone of Pi `2c1a8acf` with the task's explicit uncommitted candidate overlays, hardware access disabled, and the existing Debian tools plus PyYAML/ShellCheck. `make ci-focused-state-test ci-network-policy-test ci-support-lifecycle-test SUDO=` executed all 22 selected C++ leaves under open/openat tracing. `make ci-installer-offline-test SUDO=` executed the additional Pico C++ fixture, fourteen deployment and forty bundle cases, the runtime install fixture and actual renderer. `make php2shell-test SUDO=` passed all 55 cases with ShellCheck installed and zero skips. Actual standalone `make wtp-production-test wtp-api-test wtp-config-test SUDO=` passed, retaining one shared WTP UI invocation and the management UI suite. Both new traced cohorts passed the unchanged `assert-no-transmitter-hardware.sh` audit.

After final prerequisite corrections, the affected admin/transaction/Pico leaves and both Python installer suites were rerun under tracing and passed. The final twelve-method graph suite passed in Debian, including timeout/interruption/cleanup aggregation and source/dispatch refusals. `python3 -B scripts/tests/semantics_make_regression_test.py` and `python3 -B scripts/tests/ci_change_scope_test.py` passed, including all 24 classification cases. Official checksum-verified actionlint 1.7.12 passed workflow syntax/context validation with external ShellCheck/Pyflakes integration disabled. Independent native owned-Git probes passed; native execution of the whole graph test was unavailable without PyYAML, not reported as a pass. No hardware, installation, service, GPIO, physical USB or RF qualification follows from these software results. The final full CI and paired identities are recorded below.

| Phase 5 measure | Phase 4 delivered | Phase 5 delivered |
| --- | ---: | ---: |
| Frozen-root files plus Phase 1 helper, excluding frozen inactive paths | 148 | 150 |
| Physical lines in those roots | 31,341 | 31,881 |
| Core active files including collector and newly activated inactive files | 149 | 157 |
| Newly scheduled protected C++ files, additional conservative file charge | 0 | 23 |
| Conservative active-equivalent file count | 149 | 180 |
| New source/graph helpers / fully charged lines | 0 / 0 | 2 / 472 |
| Newly activated frozen-inactive files / lines | 0 / 0 | 6 / 1,530 |
| Newly activated protected C++ files / lines | 0 / 0 | 23 / 3,539 |
| Adjusted equivalent lines against frozen B=31,971 | 32,608 | 38,217 |
| Selected repeated-cohort invocations | 17 | 8 |
| Required workflow definitions / expanded contexts | 8 / 9 | 8 / 9 |
| Unused cleanup credit | 0 | 0 |
| Final full-CI successful contexts | 9 / 9 | 9 / 9 |
| Final CI elapsed, including runner acquisition | 25m51s | 24m57s |
| Final aggregate job spans | 83m22s | 80m21s |

Physical first-party source growth is 580 lines, including both new files. The larger activation adjustment is not a claim that the repository grew by that many physical lines: it charges 5,069 current lines of previously unscheduled coverage in full, including its prerequisite repairs. Phase 5 credited saving is `−540 − 5,069 = −5,609` lines. There are no additional removed active-root blank/comment exclusions. Previously charged collector lines (496) and Phase 4's 128 added C++ lines are not charged again. Cumulative credit changes from −637 to **−6,246 lines (−19.54%)**, yielding **38,217 equivalent lines**. Counting the 23 newly scheduled protected C++ files is a conservative file-equivalent adjustment, not new physical files. No project code is moved outside the ledger, no inactive deletion is credited, and no 50% code/file/time achievement is claimed. The final measured times and their limits are recorded below.

### Final CI evidence

[Run 41932990807](https://github.com/WsprryPi/WsprryPi/actions/runs/41932990807), attempt 1, passed all nine contexts on committed Pi `90a562910bfa45e548e5f9d6a6a4297a5ba46c43`. The scope job selected Pico `1c30709186fabb64aa250da96d08914296ff3d89`; both network and USB checkout-verification steps reported that exact commit before build and pairing. Network retained Mbed TLS `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`; Debian retained RP1 provider `b52ab7b796e44d8690b2a91bccb21ae5d8b670df`. No tested source changes were uncommitted. The documentation-only evidence successor changes no executable source or workflow.

The scope job executed all twelve graph/source regression methods and all 24 admission cases. Debian executed the three new focused owners and the offline installer/maintenance owner, including all 55 PHP cases without skips, fourteen deployment cases and forty bundle cases. Its final audit accepted all seven required traces, including both newly scheduled cohorts, with no prohibited transmitter hardware access observed. Existing full semantics, six GCC profiles, strict I2C, native macOS ARM/Intel and simulated-only contexts passed. Both actual production pair suites passed: network runtime/interoperability/configuration, public and encrypted HTTP controls, source-selection mutations and genuine build-failure handling; USB production framing/ACK, protected control, persistence and identity/refusal paths. Existing rendered browser checks passed in CI; no UI behavior, presentation assertion or screenshot was changed.

Final logs confirm each of the eight selected repeated cohorts executed exactly once: seven Debian producer completion markers and the network TLS completion marker. Their Phase 4 final logs recorded seventeen total executions. This verifies the nine removals (52.94% within this selected set), not a reduction of all suite dispatches or distinct safety cases. Required context outcomes are nine successes, zero failures, zero cancellations and zero timeouts. No required integration context was skipped. Existing intentional profile/subset distinctions remain explicit.

The run was created at `2026-11-04T23:20:54Z`; the final job completed at `2026-11-04T23:45:51Z`. Elapsed time, including runner acquisition, is 1,497 seconds (24m57s); aggregate started-to-completed job spans are 4,821 seconds (80m21s). Relative to Phase 4's final sample, elapsed time is 54 seconds lower (3.48%) and aggregate time is 181 seconds lower (3.62%). Relative to the historical 23m09s / 85m05s anchor, elapsed time is 108 seconds higher (7.78%) and aggregate time is 284 seconds lower (5.56%). These are single-run observations with different candidate sources and runner conditions, not controlled causal speedups or percentiles; there is no same-source green full-run frozen Phase 1 timing baseline. Phase 5 used one full-CI attempt across nine contexts, so its full-CI campaign cost equals this sample; local builds and adversarial probes are additional work, not included in hosted job time.

Independent adversarial review was repeated after corrections and found no remaining actionable Phase 5 blocker. The earlier strace fault did not reproduce here; its cause and the earlier unexplained ARM delay remain HOLD. The gated exclusion and owned-process corrections have their own controlled reproduction and sensitivity evidence above; this successful CI run is not evidence that either historical failure was fixed. Phase 6 remains unstarted.

Documentation Impact: this engineering execution/obligation map and developer test commands are updated. Operator, installer, service, mode, configuration and UI instructions were considered unchanged because only software testing/orchestration changes. No operator-documentation update or sibling-repository edit is required. No UI behavior, presentation assertion or screenshot changes require Impeccable review; existing browser qualification remains required in CI. Phase 6 has not started.


## Phase 6 compilation and dependency correctness

Phase 6 continues from Pi `58b303c879fff406659106149d6c0669c18b45d6` on `codex/ci-review-simplification` in `/Users/lbussy/.codex/worktrees/ci-review-simplification/WsprryPi`. Frozen Pi base remains `94ff1590368f6ecbeb431e64f04af3f44f0ddb8e`. Pico remains `1c30709186fabb64aa250da96d08914296ff3d89` in its existing feature worktree. No product/component C++ implementation, Pico source, protocol, UI assertion or presentation is changed. The eight component-local scheduler changes are identified below. All eight workflow definitions/nine required contexts and both actual production carriers remain required.

| Disposition | Exact boundary |
| --- | --- |
| DELETE compilation work | Remove repeated translation of identical parent network adapters within six binaries and eliminate no-change recompilation. TLS-only suites stop building the unused network interoperability client and its separate archive. No runtime test or source file is deleted. |
| REWRITE incremental targets | `scripts/network_build_rules.mk`, `scripts/build_identity.py` and the corresponding `src/Makefile` owners separate real object/binary targets from phony test execution. Six binaries share sixteen translation units with identical `WTP_NETWORK_FLAGS`; their original per-binary link options remain distinct. |
| REWRITE build scheduling | `src/Makefile` selects a resource-aware one-to-four-job budget only through explicit `ci-build-budget` setup. Five Linux jobs record that budget in `GITHUB_ENV`; native jobs and object-pool prebuilds select two jobs. Ordinary Makefiles never rewrite flags. Six GCC profiles stay serial; test dependency edges and shared CMake/credential/port ordering remain. |
| REWRITE component scheduling | Remove CPU-count/`MAKEFLAGS` injections from `src/{MonitorFile,LCBLog,PPM-Manager,Singleton,INI-Handler,Signal-Handler,Mailbox,WSPR-Transmitter}/src/Makefile`. The parent cannot preserve a jobserver when a component replaces it. No source/compiler/linker/test recipe or component hierarchy is changed. Standalone Make uses its standard serial default or the explicit/inherited caller budget. |
| REWRITE correctness fixtures | `scripts/tests/network_build_test.py` exercises directly included production rules with a real compiler and explicit gates. The Phase 5 dispatch fixture models the new compilation boundary while preserving execution/failure checks. Backend-capability invalidation uses controlled timestamps; the WTP archive concurrency fixture uses acknowledged entered/release gates instead of elapsed delays. |
| RETAIN | Existing standalone public targets, actual network and USB pair recipes, native ARM/Intel, strict I2C, backend profiles, full/portable semantics, source/pin/credential validation and existing CMake generation. `wtp-tls-build` remains the complete probe plus interoperability-client aggregate. |
| HOLD separate contexts | Configuration-client, existing USB-pair and standalone component archive builds retain their own compile contexts. Pico TLS-server and consumer core include paths and assertion flags differ, so their objects are not merged. Broader component/build-system restructuring and compiler caches are not introduced. Earlier tracer/ARM-delay findings and Phase 5 manual/supplemental boundaries remain HOLD. The inherited transmitter legacy debug/demo build fails through `config_handler.hpp` with missing `ini_file.hpp` on both baseline and candidate; this independent integration limitation remains HOLD. Its canonical hardware-free `make test SUDO=` target passes with the default transport profile. |

### Reuse and failure boundaries

The shared pool covers `wtp_tls_probe`, `wtp_tls_contract_test`, `wtp_plain_tcp_test`, `wtp_configuration_test`, `wtp_browser_api_test` and `wtp_usb_management_test`. Their compile flags, definitions, language level and ordered include search are identical; configuration-client's additional transmitter include directory and other distinct contexts do not enter this pool. Compile identities include the physical source location, OS/architecture, compiler executable/resolvable arguments and version, effective flags, relevant environment, rule/helper contents, response-file contents and supported include-root header names. A newly introduced earlier header therefore invalidates the selected code even if the formerly selected dependency is unchanged. Link identities additionally identify link options and known OpenSSL library files; a library-only change does not require recompiling unchanged objects.

Only supported ordinary compiler inputs receive reuse. Standard optimization/debug/warning/definition options and normal `-I`, `-isystem`, `-iquote`, `-idirafter`, `-include` and `-imacros` inputs are handled. Response files have explicit recursion refusal. Unhandled options, opaque sysroot/framework forms, unreadable include inventories or inventories beyond 50,000 entries retain conservative compilation and linking; their identity suffix records that boundary. Arbitrary `WTP_NETWORK_LDFLAGS`, overridden OpenSSL linkage or nonempty `LIBRARY_PATH` retain unconditional relinking. These paths preserve the former build behavior and receive no warm-link/compile saving claim. No compiler cache, restored build tree or test-success marker is used.

Objects record compiler-produced `-MD -MP` dependencies, including system headers, with explicit source prerequisites. Each successful object/dependency pair has SHA-256 publication state. Missing, empty, altered, malformed or mismatched dependency/state/object data is excluded from dependency inclusion and forces recompilation; validation-process failure stops Make. Temporary object, dependency, state and linked outputs are published only after success. A failed prerequisite prevents public artifact publication and test execution. Configuration-qualified binaries prevent switch-back from accepting a newer executable produced for different flags. Public artifact copies select that exact qualified binary every invocation.

`make -j2 wtp-network-unit-build SUDO=` builds API/configuration binaries without running tests. `make -j2 wtp-network-tools-build SUDO=` additionally builds the TLS probe/contract and plain-TCP binaries. Existing phony `wtp-api-cpp-test`, `wtp-config-cpp-test`, `wtp-tls-test`, `wtp-plain-tcp-test` and `wtp-usb-management-test` always execute their required suites. TLS tests consume `wtp-tls-probe-build`; the full public `wtp-tls-build` and actual network interoperability retain `wtp-network-client-build`. TLS-only contexts, including native macOS TLS validation, stop incidentally compiling the unused interoperability driver; this is removed compilation, not removed native paired-runtime evidence. No native paired-runtime qualification had been claimed by those TLS-only jobs.

The former Makefile selected parallelism automatically during parsing. Actual GNU Make 3.81 probes demonstrated that it could replace explicit `-j2` with a larger budget; a Linux inherited-environment case also overrode the requested count. Each of the eight component Makefiles independently replaced a two-job parent with eighteen jobs on a different jobserver pipe in the controlled native probe. Ordinary Makefiles now leave standard serial, CLI and inherited scheduling intact on every supported Make version. Automatic resource selection occurs only at the explicit `ci-build-budget` boundary: existing CPU/memory/GUI limits plus `BUILD_JOB_LIMIT` (one through four, default four) select a validated future-step `MAKEFLAGS` value in `GITHUB_ENV`. Missing/unwritable outputs or invalid budgets fail. Resource probes run once at setup, rather than during every ordinary Make invocation. `JOBS` is a CI-budget input; ordinary developer builds select parallelism through standard `make -jN` or inherited `MAKEFLAGS`. No active jobserver descriptors are serialized between CI steps. Native jobs use `MAKEFLAGS=-j2`; pool prebuilds use explicit `-j2`. Within an invocation, recursive consumers share the same jobserver and capacity. Existing bounded CMake two/four-job settings and shared runtime serialization remain unchanged. Six backend profiles never run concurrently against the shared generated capability header/export paths. Workflow deadlines, product deadlines and fault timing contracts are unchanged. The new regression waits for an acknowledged stalled compiler/archive transition and releases it explicitly; its hang guards protect resources rather than establish completion. The old 1.1-second capability timestamp delay and 200-millisecond archive race window are removed. Existing parent/component reuse outside this pool is not broadened across compiler/toolchain or configuration changes.

### Local evidence and accounting

The final native build regression passes fifteen methods covering cold/warm reuse with continued test execution, source/transitive/generated-header and library changes, compiler/flag changes and switch-back, missing/deleted/corrupt dependency state, unknown-option fallback, read-only introspection, deliberate compile/link failures, gated parallel consumers and interrupted-build recovery, plus real CI-budget output/refusals, actual parent/child jobserver identity/capacity and all eight actual component schedulers. Jobserver tests observe available kernel pipe/FIFO tokens and device/inode identity without consuming tokens or timing a negative observation. Source sets are evaluated by Make rather than inferred from assignment formatting. The actual TLS-only/public-aggregate split has a separate execution-owner case. Controlled library-function and header-value changes assert the resulting executed code, rather than accepting an unrelated build failure.

Independent adversarial review reproduced stale acceptance for custom `-L/-l` library replacement, corrupt dependency metadata and newly earlier headers under both ordinary and external `-idirafter` roots. Corrected real-compiler cases fail on the intended changed behavior and recover when restored. Independent forced interoperability-builder and TLS-suite failures propagate through the full public aggregate and actual suite owner. Repeated assessment closed the established scheduler overrides and found no remaining material issue within the supported/default and explicitly conservative boundaries. Six component `debug` builds and the Singleton test-binary build pass in a disposable Debian clone. The transmitter hardware-free canonical `make -j2 test SUDO=` passes in its default transport profile. Its untouched legacy debug/demo include failure was reproduced with both baseline and candidate Makefiles; it is not reported as repaired or as a passing build.

The Debian hardware-free image passes `python3 -B scripts/tests/network_build_test.py`, `ci_execution_graph_test.py`, `wtp_server_make_regression_test.py`, `object_dependency_make_regression_test.py`, `backend_capabilities_make_regression_test.py`, `build_metadata_make_regression_test.py`, `semantics_make_regression_test.py` and `ci_change_scope_test.py` from `scripts/tests/`, including all twelve graph/source methods and 24 classification cases. Native full graph execution is unavailable without PyYAML; Debian supplies that existing dependency, and the scope job explicitly installs compiler/Make, pkg-config and OpenSSL development prerequisites for real build checks. Official actionlint 1.7.12 passes workflow syntax/context validation with ShellCheck/Pyflakes integration disabled. Whitespace and frozen-inventory checks pass. Full final CI and exact production-pair source verification remain required before completion.

A separate explicit-budget parent debug study uses fresh output trees, the same candidate sources and metadata, Debian ARM64 GCC 12, all four backend capabilities and hardware access inhibited. One job takes 142.05 seconds; two take 82.71 seconds (41.77% lower in this sample). Both issue exactly 167 output-producing compiler-driver calls across 166 translation units; their command multisets and generated metadata/capability headers match. Docker exposes eighteen CPUs and 8,122,332 KiB memory; the build cap is two. No compiler-result cache is used, OS page caches/load are not reset, and aggregate peak RSS was not measured. This is one local observation, not the Phase 5-versus-6 default-policy comparison, a stable percentile or an overall-CI speedup. Preparatory runs with an unsuitable fixture-origin name are excluded from this comparison.

The same native command runs `make -j2 wtp-tls-test wtp-plain-tcp-test wtp-config-cpp-test wtp-api-cpp-test wtp-usb-management-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= JOBS=2 GUI_CLAMP=0` with an output-producing compiler-driver recorder. Both baseline and candidate use the same verified parent/recursive two-job budget and Apple compiler/OpenSSL 4.1.0 context in isolated trees. Earlier diagnostic samples used automatic host-derived scheduling and are not used for this controlled-budget table. Cold means fresh affected output directories; warm means a subsequent unchanged invocation, with no compiler-result cache and OS caches/load uncontrolled. Failed sandbox/socket or prerequisite-refused preparations and intermediate candidates are excluded. The final actual TLS run still reports all 34 cases; configuration/API/USB/TCP completion assertions also pass.

| Native affected-suite observation | Phase 5 cold | Phase 6 cold | Phase 5 warm | Phase 6 warm |
| --- | ---: | ---: | ---: | ---: |
| Translation units compiled | 38 | 16 | 33 | 0 |
| Output-producing compiler-driver calls | 12 | 22 | 7 | 0 |
| Links, including combined compile/link calls | 7 | 6 | 7 | 0 |
| Elapsed seconds, including required tests | 47.05 | 40.73 | 46.93 | 25.71 |
| User CPU seconds | 43.44 | 28.34 | 40.51 | 0.70 |

Cold translation count falls 57.89%, combining removal of unused TLS-only interoperability compilation with sharing of seven repeated translations across the six binaries. Splitting compile/link increases cold driver calls, which is disclosed rather than treated as fewer processes. No-change compilation and linking fall to zero while every required suite executes again; compiler identity probes still run. Local elapsed observations are 13.43% lower cold and 45.22% lower warm. These are scoped single samples, not full-CI savings, reliable percentiles, or an overall 50% result. The full public TLS aggregate and actual paired network job still compile the interoperability client when needed.

| Phase 6 measure | Phase 5 delivered | Phase 6 local candidate |
| --- | ---: | ---: |
| Frozen-root files plus Phase 1 helper, excluding frozen inactive paths | 150 | 151 |
| Physical lines in those roots | 31,881 | 32,439 |
| New build helpers outside that raw envelope / fully charged lines | 0 / 0 | 2 / 237 |
| Conservative active-equivalent file count | 180 | 183 |
| Adjusted equivalent lines against frozen B=31,971 | 38,217 | 39,014 |
| Required workflow definitions / expanded contexts | 8 / 9 | 8 / 9 |
| Unused-test cleanup credit | 0 | 0 |

Raw active-root growth is 558 lines; both new build helpers add 237 fully charged lines despite their paths outside the frozen active envelope. Two removed active-root comment lines earn no reduction credit. Phase 6 credited saving is `−558 −237 −2 = −797` lines; cumulative credit is **−7,043 lines (−22.03%)**, yielding **39,014 equivalent lines**. Existing Phase 5 activated coverage is not charged twice. Physical first-party source growth is 743 lines: the 52 lines removed from protected component build files earn no ledger credit, and the two comment removals are excluded from substantive savings. Three new physical files account for the file-count increase. No unused test is deleted or credited. No overall 50% code, file, elapsed-time or runner-time achievement is claimed.

Documentation Impact: this engineering map and developer build commands are updated. Operator, installer, service, firmware, mode, configuration and UI instructions were considered unchanged because product behavior is unchanged. No sibling operator-documentation update is required. UI presentation/assertions/screenshots are unchanged, so Impeccable visual work is not applicable; existing rendered checks remain required in final CI. No host/service/device deployment, physical USB, GPIO, timing or RF qualification is performed. The earlier tracer fault and ARM delay are not claimed fixed. Phase 7 has not started.

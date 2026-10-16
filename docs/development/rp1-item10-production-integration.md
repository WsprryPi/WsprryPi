<!-- SPDX-License-Identifier: MIT -->

# Item 10: published RP1 production integration

Status: **in progress; target acceptance remains open**. This is WsprryPi's
handback for RP1 roadmap item 18, not WsprryPi 4.0 release acceptance. Items
11–14 are excluded. No target, service, module, overlay, GPIO or RF operation
is authorized by this record.

## Inputs and evidence boundary

Starting consumer: clean `devel` / refreshed `origin/devel`, both
`a7523f8a831a8f29241595d2cd7729b9523c283b` (zero commits ahead/behind).
This newer baseline already maintains the published-release acceptance tool,
lock and renamed `rp1-release-acceptance-test`; the historical candidate names
in the execution request are no longer current entry points.

Provider: annotated tag `v1.0.0`, tag object
`52c5d42cf6e3f9c8a77fe9a03e1f012c3e8304a0`, commit
`8c16d3c237cb1267cbedafb5da2bb74132ca5de0`, tree
`4c1ad8acf513b36ba32e58a636942655accbb52d`. Corrected runtime ancestor:
`817f3dc6a194f4a92a7f94acf10147169e6439d6`. Module/DKMS/controller `1.0.0`,
Debian `1.0.0-1`, architecture `all` (source package; not a target-built module).
The [published lock](../../scripts/tests/fixtures/rp1-release-1.0.0.json)
binds all seven assets, both canonical UAPIs, both overlays and independent
`v1.0.0-rp1-gpio4` / `v1.0.0-rp1-gpio20` identities.

Fresh external evidence root:
`/private/tmp/wsprrypi-item10-20261016.Wy7Erq`. Original issue JSON, downloaded
assets, remote tag resolution, disposable provider checkout and generated logs
remain outside Git. Preserve this directory for the target handoff. The
authoritative provider checkout (`main`, `2c009a8`) is read-only; cross-provider
tests use a separate history-preserving checkout because they generate files.

## Acceptance matrix (defined before implementation)

All software rows bind the consumer above plus the final item 10 diff and the
locked provider/assets above unless explicitly historical. ARM64 Linux
containers have no target devices or host boot/module/service mounts. Target
rows apply only to positively identified BCM2712 Pi 5/CM5, their exact running
kernel/headers/signing policy, and independently to GPIO4 and GPIO20. Other
platforms are expected refusals/skips, not supported RP1 targets.

| Scenario | Expected result | Evidence and reuse | New check / disposition |
| --- | --- | --- | --- |
| Production `auto`; explicit `release` on `devel` | Immutable stable release resolves exact package and runtime source; no fallback after corruption | Installer policy fixtures; real publication and artifact inspection | PASS: actual production downloader/resolver plus offline selector mapping |
| Development `auto`, `devel`, exact commit, clean checkout | Preserve separate exact-source Experimental path | Existing selector/source/ancestry regressions | PASS: offline regressions and real exact-checkout development selection |
| Pi 5/CM5, other/unknown platform, opt-out | Positive model + compatible + BCM2712 detection; `false` skips | Python and shell fixtures | PASS; maintainer explicitly retained automatic Pi 5/CM5 installation with opt-out on October 16, 2026 |
| Fresh package; exact installed state | Normal Debian lifecycle; full inventory and module identities verified | Installer fixtures; provider's 22 disposable package/DKMS scenarios | PASS: fixtures and published-package inspection; target installation still required |
| Exact package installed outside WsprryPi | Verify without inventing ownership; refuse runtime adoption/removal | Existing exact/foreign-state fixtures | PASS: no ownership fabrication |
| Predecessor migration and inverse rollback | Owning cleanup/removal before clean successor or predecessor install | Provider publication lifecycle evidence; application migration fixtures | PASS: actual provider update boundary, release/development records; target migration/rollback open |
| Neutral administration | Route zero, no consumer endpoint/owner/lease, output disabled; receipt/binding exact | Activation/bundle fixtures and provider public JSON boundary | PASS against published source with injected host/module evidence; target activation open |
| Saved-route install/startup reconciliation | Explicit saved RP1 route only; fresh plans, idle restoration, preserve boot preference | Companion/startup fixtures; prior candidate target evidence is historical | PASS: companion/startup fixtures; target service/startup restoration open |
| GPIO4 | Distinct identity, generation fence, idle switch/removal and fail-closed execution | Planner/provider/backend/route tests; historical item 8 finite-TONE evidence | PASS software; exact published installation/route evidence open |
| GPIO20 | Same contract independently; no transfer from GPIO4 | Same suites with GPIO20 cases; historical item 8 evidence | PASS software; exact published installation/route evidence open |
| Update/removal/reinstall/interruption | Public provider plans; no repeated failed effects; pending removal preserves exact authority | Application fixtures and real provider UpdateHost boundary | PASS software; target installed-release lifecycle open |
| Foreign/mixed/orphan/changed ownership/stale plans | Refusal before effects; retain recovery evidence | Installer, update and startup negative fixtures | PASS required refusals; no supported-state claim from negative tests |
| Kernel/headers/module/signing/compression mismatch | Refuse incompatible source/build/binding/trust | Installer fixtures; unchanged provider disposable DKMS campaign | PASS software bundle/identity refusals; actual target-built hashes remain absent |
| Diagnostics/errors/support/STOP | Bounded process errors; truthful missing/failed state; responsive STOP and cleanup latches | Diagnostics/collector/lifecycle fixtures | PASS; support evidence is not qualification |
| Unavailable/rejected compatibility | No acquisition/transmission or backend/route fallback | Real manifest defaults to Unavailable, entries empty; consumer negative tests | PASS required refusals; operational eligibility remains unresolved |
| Application simulation, profiles, UI regressions | Software contracts pass with hardware access disabled | Canonical Linux workflow, simulator traces, UI unit/browser tests | PASS local semantics/audit/simulation/UI (browser cleanup caveat below); exact-head hosted profiles/CI required after push |

Historical candidate evidence remains at its original identities, including
[item 16](https://github.com/WsprryPi/WsprryPi/blob/c4736e55e34574349d94d5e974c337760d7f78a2/docs/development/rp1-item16-acceptance.md).
The 14 conducted finite-TONE cases qualify only their recorded source, builds,
routes and physical chain. The provider's 22 disposable lifecycle scenarios
are reusable for unchanged package/build/runtime inputs, not WsprryPi's new
published resolver path or an unmeasured target kernel/module/signing state.
Fresh comparison passed: relevant provider runtime/kernel/UAPI/overlay/build
inputs and consumer runtime, installer, companion, startup, collector and UI
inputs are unchanged from the accepted pair. All Debian data/control members
match independently (`reuse-verification.json`). Historical candidate package
SHA-256 `3baa570a8bc7657a26f934980ee7d3aa4e1bc9e372b28ff446f324f01328b590`
remains distinct from published package
`dd629b131f7937706d707cba14042a0a6feed59e31cced738821f9836429881f`.
Retained item 17 `lifecycle/result.json` records 22/22 passing scenarios in
image `sha256:533758c21afa2bb719702416d6d7ad601cde3a2a17e7e460dcdf482b87598ab3`;
its copied result and original source/environment provenance are retained in
the external evidence root. No native package campaign or RF campaign was
repeated. No historical candidate lock or acceptance record was rewritten.

## Required target gate and bounded authorization plan

No target operations have been performed. Proposed host: `wspr5` (identity and
current state must be read-only inventoried under a later approved plan; no
current kernel, ownership or RF-path facts are assumed). Freeze the final
consumer commit, locked downloaded release, exact predecessor source/package,
application binary/companion, configuration, startup-file ownership receipt,
provider ownership and journals, modules, boot files and service masks/state.
Record backups and hashes before any effects. Abort on foreign/mixed state or
unavailable owning rollback inputs.

A separately authorized **non-output** plan must bound installation through
the normal installer with explicit release selection on `devel`, exact package
and DKMS/module/signing verification, neutral activation, GPIO4 then GPIO20
idle route selection/removal, saved-route reconciliation, provider-owned
update/removal/reinstall/recovery and exact inverse rollback. Reboot is a
separate explicit permission. Remove stale `~/finished` before an authorized
installation; create it only after installer success. Stop on the first failed
effect, retain evidence and use only the provider's owning recovery plans.
Restore captured application/configuration/startup ownership, provider, route,
boot and service state and independently verify final restoration.

RF work is excluded from this plan: zero output attempts. A future RF request
must specify frequency, mode, duration, route, drive/correction, attached
hardware, physical load/attenuation/receiver path, GPSDO interlock, stopping
procedure and a finite attempt budget before authorization. Historical
campaigns provide no new attempts. The empty Unavailable compatibility
manifest cannot be promoted by publication; desired operational eligibility
needs provider-owned qualification metadata/a new release or an explicitly
accepted bounded disposition. No override is introduced here.

## Documentation Impact

Maintainer installation/release acceptance documentation requires current
publication, matrix and resolver evidence. Historical acceptance is unchanged.
The actual sibling is `../WsprryPi-Docs` (the older `Wsprry_Pi_Docs` path is
absent); its instructions and pages are inspected read-only. Items 11–12 own
`docs/Advanced_Operations/rp1_gpclk.md`,
`docs/User_Interface/Setup/Transmitter/index.md`, `docs/Install/index.md`,
`docs/Experimental/rp1_gpio.md` and the qualification matrix in
`docs/About_Wsprry_Pi/index.md`: explain published versus development source,
opt-out policy, neutral versus saved-route restoration, cleanup-first migration,
owning recovery, Unavailable compatibility and qualification limits. Update
support claims/capability matrix together only at the same accepted
qualification boundary. No sibling files or screenshots are changed here.
The inspected documentation revision is `01d9bef6f8d7a05ac2b3223f02270483f33caac4`
on clean `devel`. Maintainer docs updated here: `docs/rp1-gpclk-dkms-installation.md`
and `docs/rp1-provider-acceptance.md`. Runtime workflow and provider acceptance
requirements were reviewed and need no behavior change. Support status is not
promoted by this software work. Operator docs still require their separate
items 11–12 review/build/render; no final screenshots are published here.

## Validation and adversarial review

The change adds controlled production downloader/source resolution to
`accept_rp1_release.py --verify-publication`, offline substitution checks and
release-channel coverage of the actual provider-owned update JSON boundary.
No application behavior, component source, source selector, ownership,
qualification policy, execution authority or UAPI changes are needed.

Validation inputs are starting commit `a7523f8` plus this item 10 diff;
`tested-input-sha256.json` records exact tool/test/lock/installer bytes. The
installer digest remains
`c1bcafe8a76b71de55620522b9fde10627f42d64b027860d94dafe1da48d3ba6`.
Real release acceptance used read-only source mounts and packaging image
`sha256:533758c21afa2bb719702416d6d7ad601cde3a2a17e7e460dcdf482b87598ab3`,
whose recorded executable hashes/tool versions reproduced all seven assets.
`rebuild/acceptance.json` records publication and production resolver PASS,
qualification false and installation performed false. Application tests ran
as UID 65534 in disposable Debian 13.6 ARM64 image
`sha256:429ffeeffde94efc483f612b42551351c5e7f610565af16a8c080f339a4e36b8`,
LinuxKit `7.0.12-linuxkit`, GCC 14.2.0, Node 20.19.2, PHP 8.4.26,
Chromium 155.0.8059.39, libgpiod C++ headers available. No Mac C++ build ran.

Commands run from `src`, with hardware access disabled and empty `SUDO`:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 \
  rp1-gpclk-dkms-installer-test installer-dependency-test \
  rp1-gpclk-planner-test rp1-gpclk-lifecycle-test rp1-gpclk-transition-test \
  rp1-gpclk-backend-test rp1-gpclk-linux-provider-test \
  rp1-gpclk-development-policy-test rp1-gpclk-route-runtime-wiring-test \
  rp1-gpclk-route-service-test rp1-gpclk-transmit-backend-test \
  rp1-gpclk-diagnostics-test rp1-release-acceptance-test websocket-lifecycle-test SUDO=
make rp1-gpclk-update-contract-test RP1_PROVIDER_SOURCE=/work/provider SUDO=
make -j2 semantics-test SUDO=
make startup-quiesce-parent-test SUDO=
make -j2 support-bundle-collector-executor-test support-bundle-runtime-installer-test \
  backend-capabilities-regression-test backend-capabilities-make-regression-test \
  semantics-make-regression-test SUDO=
make -j2 debug SUDO=
../scripts/ci/verify-simulated-backend.sh
../scripts/ci/verify-simulated-wspr.sh
```

The full semantics/startup and focused RP1 audit runs are traced with
`strace -f -e trace=openat,open`; the RP1 suite's second execution is the
required device-access audit, not another unmotivated test repeat. The canonical
`scripts/ci/assert-no-transmitter-hardware.sh` consumes those three traces plus
both simulator traces. Final audit/simulator disposition is retained in
`linux-results`; no physical qualification follows from these commands.

| Check | Actual result |
| --- | --- |
| Real publication/download/rebuild/resolver | PASS: seven asset hashes/GitHub digests, annotated tag, tree, package/control inventories, UAPIs, routes, corrected ancestry and separate development selection |
| RP1 installer / release acceptance | PASS: 105 / 10 Python tests |
| Provider diagnostics / companion / startup fixtures | PASS: 3 / 10 / 20 Python tests; source diagnostics also pass |
| Actual cross-provider bundle/update | PASS: 3 methods; 16 lifecycle subcases across release/development and 6 required refusals; synthetic module evidence, no target-built module claim |
| Planner/lifecycle/transition/backend/provider/development policy/route wiring/service/transmit backend/process bounds/websocket | PASS: all selected C++ executables; no invented aggregate assertion count |
| Full Linux semantics | PASS, including runtime/cleanup, Test Tone, GPIO policies and 23 UI-publication Python tests |
| Simulator and hardware-access audit | PASS: deterministic QRSS and full WSPR (162 events, 110.591999892 logical seconds); no prohibited device access in five traces |
| Installer dependencies / dry-run purity / precompiled | PASS; purity 37 Python tests; dependency recipe includes maintained 15/37/13-test prerequisite suites |
| Collector/support/profile/Make dispatch | PASS: standalone collector shell regression and all listed focused targets |
| `WsprryPi-UI`: `npm test` | PASS: 17 maintained entry points, including 7/10 Python manifest/classification cases |
| `WsprryPi-UI`: `npm run test:browser` | Initial aggregate failed after route assertions on Chromium profile cleanup (`ENOTEMPTY`); affected route fixture repeated successfully, remaining CW/support fixtures passed separately. Five browser entry points exercised; Test Tone 17 workflows/21 renders, GPIO fixture 32 cases/30 renders |

Additional commands from the root: `bash scripts/tests/collect-support-bundle_test.sh`,
`python3 scripts/tests/installer_dry_run_purity_test.py`,
`bash scripts/tests/precompiled_installer_test.sh`; all pass on Linux. The real
release command is the documented acceptance command with `/provider`,
`/evidence/rebuild`, `/evidence/downloads` and `--verify-publication`.

Validation setup failures are preserved: test dispatch preceded copy/chmod
completion (no tests executed); provider bundle generation first lacked Git
safe-directory trust (corrected for the disposable checkout, 3/3 passed);
the simulator helper initially lacked its `make debug` prerequisite (built
before continuing). These are not passing results. Expected injected
refusal/cleanup warnings and absent Pi device-tree diagnostics remain in logs.
The browser cleanup race is an existing fixture limitation; its assertion pass
and successful focused rerun do not erase the original aggregate failure.

Impeccable 4.1.1 context, focused technical review and detector covered
config/maintenance views and their index/site scripts. Three detector palette
findings at `site.js:3650–3653` apply to developer-console log styles, not the
rendered operator controls; no UI change is justified here. Reviewed route
terminal and tone refusal/recovery screenshots at desktop/mobile,
light/dark, plus mobile 200% text. Tone fixtures also assert keyboard recovery
focus and polite live announcements. Screenshot filenames do not prove state;
the `RP1_Route_Required_*` and `RP1_Route_Progress_*` captures actually show
retained removal/service terminal states and are interpreted accordingly;
fresh visual progress-state acceptance is not claimed. This bounded review does not complete
#423 or constitute a full accessibility/contrast/performance audit.

## Issue reconciliation and adversarial assessment

#412 now records actual `v1.0.0` publication, current development selectors,
the confirmed automatic/opt-out policy, current-boot runtime administration,
fresh startup reconciliation and remaining publication/target gates. Its old
four-job CI evidence is labeled historical. #423 permits #412 to remain open
solely for publication after actual implementation/integration prerequisites
are accepted and no UI work remains; target acceptance is not that exception.
Both issues remain open; no comments or unrelated issue changes were made.
Original API bodies, exact unified diffs, updated bodies and independent reads
are retained as `issue-412-*`, `issue-423-*`, `issue-edits.json` outside Git.

| Severity / reproducer | Ownership | Repair and evidence |
| --- | --- | --- |
| P2: prior publication option validated metadata/rebuilt bytes but never called production `download_release` | Parent maintainer tooling | Added actual resolver/source/package check; real locked publication PASS and offline substitution rejection PASS |
| P2: real provider-boundary regression used only development ownership records | Parent integration tests | Exercise both channels, neutral/GPIO4/GPIO20/removed and unsafe states; 3 methods and all subcases PASS |
| P2: installation docs and #412 claimed no published release and reboot-only administration | Parent maintainer docs/issue criteria | Updated publication/runtime workflow, retained final publication requirement; originals/diffs preserved |
| Policy discrepancy: #412 opt-in wording versus integrated automatic Pi 5/CM5 installer | Maintainer decision | Explicit instruction on October 16 retained automatic selection with opt-out; no implementation default changed |

Separate bounded adversarial passes inspect the complete authorized diff,
actual bytes versus fixture identity, source/tag/digest/inventory substitutions,
Unavailable compatibility, ownership/mutation ordering, both routes, startup,
interrupted inverse recovery, stale plans, failure/STOP containment, issue/docs
consistency and evidence limits. Runtime/installer/component source is unchanged
from the accepted consumer; release-channel update tests and fresh regressions
retain those contracts. No provider defect or new execution authority was found.
No actionable finding remains in the authorized software changes. Required
target acceptance and operational-eligibility disposition remain unresolved
gates, not claimed passes. Items 1–9 are preserved; items 11–14 remain open.

The final commit containing this record identifies the reviewed diff. After its
authorized push, exact-commit CI (including hosted release backend profiles)
and independent remote parity must be obtained; record terminal run/head/job
identities in external `closeout.json` and the execution handback. Earlier CI
does not qualify that final head. Item 10 / RP1 item 18 remains **in progress**;
the next gated work is the non-output target preflight/authorization plan above.

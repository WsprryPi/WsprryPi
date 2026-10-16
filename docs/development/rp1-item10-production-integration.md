<!-- SPDX-License-Identifier: MIT -->

# Item 10: published RP1 production integration

Status: **complete within the maintainer-accepted installation/runtime scope**
on October 16, 2026. This is WsprryPi's handback for RP1 roadmap item 18.
Production RF operation remains **unqualified**. The maintainer explicitly
accepted the published manifest's empty `Unavailable` state for this bounded
closeout and excluded target rollback rehearsals. Items 11–14 and WsprryPi 4.0
release acceptance remain open. This record grants no new target or RF authority.

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

Software/publication evidence root:
`/private/tmp/wsprrypi-item10-20261016.Wy7Erq`. Original issue JSON, downloaded
assets, remote tag resolution, disposable provider checkout and generated logs
remain outside Git. Target installation evidence is in
`/private/tmp/wsprrypi-item10-inventory-20261016.ECXvu5`; both-route/reboot
acceptance is in `/private/tmp/wsprrypi-item10-acceptance-20261016.kXNHN2`.
The installed and target-tested consumer is clean `devel` at
`8abb0d1da9e9215bc39d8f072ab6651e2ed9e019`. This closeout changes only this
record and the punchlist; it does not replace the tested target software. The
authoritative provider checkout (`main`, `2c009a8`) is read-only; cross-provider
tests use a separate history-preserving checkout because they generate files.

## Acceptance matrix (defined before implementation)

All software rows bind the starting consumer plus the item 10 implementation
commit `8abb0d1` and the locked provider/assets above unless explicitly historical. ARM64 Linux
containers have no target devices or host boot/module/service mounts. Target
rows apply only to positively identified BCM2712 Pi 5/CM5, their exact running
kernel/headers/signing policy, and independently to GPIO4 and GPIO20. Other
platforms are expected refusals/skips, not supported RP1 targets.

| Scenario | Expected result | Evidence and reuse | New check / disposition |
| --- | --- | --- | --- |
| Production `auto`; explicit `release` on `devel` | Immutable stable release resolves exact package and runtime source; no fallback after corruption | Installer policy fixtures; real publication and artifact inspection | PASS: actual production downloader/resolver plus offline selector mapping |
| Development `auto`, `devel`, exact commit, clean checkout | Preserve separate exact-source Experimental path | Existing selector/source/ancestry regressions | PASS: offline regressions and real exact-checkout development selection |
| Pi 5/CM5, other/unknown platform, opt-out | Positive model + compatible + BCM2712 detection; `false` skips | Python and shell fixtures | PASS; maintainer explicitly retained automatic Pi 5/CM5 installation with opt-out on October 16, 2026 |
| Fresh package; exact installed state | Normal Debian lifecycle; full inventory and module identities verified | Installer fixtures; provider's 22 disposable package/DKMS scenarios; actual wspr5 installation | PASS: published package installed through the normal installer after owned predecessor removal; exact release ownership, bound files and running module identities verified |
| Exact package installed outside WsprryPi | Verify without inventing ownership; refuse runtime adoption/removal | Existing exact/foreign-state fixtures | PASS: no ownership fabrication |
| Predecessor migration and inverse rollback | Owning cleanup/removal before clean successor or predecessor install | Provider publication lifecycle evidence; application migration fixtures; actual wspr5 migration | PASS: owned development `817f3dc` removed before published `8c16d3c` installation. Inverse rollback retains software/disposable evidence; target rehearsal explicitly excluded by the maintainer |
| Neutral administration | Route zero, no consumer endpoint/owner/lease, output disabled; receipt/binding exact | Activation/bundle fixtures, provider public JSON boundary and installer receipt | PASS: actual neutral activation recorded generation zero before explicit saved GPIO20 restoration |
| Saved-route install/startup reconciliation | Explicit saved RP1 route only; fresh plans, idle restoration, preserve boot preference | Companion/startup fixtures and two actual wspr5 reboots | PASS: installer restored GPIO20; independent reboots automatically restored GPIO4 and GPIO20 with fresh controller sessions, completed checkpoints and transmission disabled |
| GPIO4 | Distinct identity, generation fence, idle switch/removal and fail-closed execution | Planner/provider/backend/route tests; historical item 8 finite-TONE evidence; published-provider idle target cycle | PASS: selection, effect-free repeated selection, explicit recovery/reselection, removal/reselection and reboot restoration; no new RF evidence |
| GPIO20 | Same contract independently; no transfer from GPIO4 | Same suites with independent GPIO20 target cycle and reboot | PASS: the same idle acceptance checks independently; final selected route GPIO20, output disabled |
| Update/removal/reinstall/interruption | Public provider plans; no repeated failed effects; pending removal preserves exact authority | Application fixtures, real provider UpdateHost boundary and 22 disposable package/DKMS scenarios | PASS within accepted scope: actual cleanup-first target migration and both-route recovery/removal; package reinstall/interruption/inverse coverage reused from the unchanged disposable lifecycle inputs, not claimed as new target runs |
| Foreign/mixed/orphan/changed ownership/stale plans | Refusal before effects; retain recovery evidence | Installer, update and startup negative fixtures | PASS required refusals; no supported-state claim from negative tests |
| Kernel/headers/module/signing/compression mismatch | Refuse incompatible source/build/binding/trust | Installer fixtures; unchanged provider disposable DKMS campaign; actual target artifact/binding inspection | PASS software refusals; running `6.18.34+rpt-rpi-2712`, matching headers and exact unsigned XZ module pair recorded below. Other installed-kernel builds are not runtime-qualified |
| Diagnostics/errors/support/STOP | Bounded process errors; truthful missing/failed state; responsive STOP and cleanup latches | Diagnostics/collector/lifecycle fixtures | PASS; support evidence is not qualification |
| Unavailable/rejected compatibility | No acquisition/transmission or backend/route fallback | Real manifest defaults to Unavailable, entries empty; consumer negative tests; explicit maintainer disposition | Required refusal coverage retained. Maintainer accepts installation/runtime integration only; production RF operation remains unqualified and requires qualifying metadata and an appropriate provider publication |
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

## Target acceptance and accepted compatibility disposition

The maintainer separately authorized installation, both-route idle acceptance
and two reboot checks on `wspr5`, instructed that the upgraded versions stay
installed, and excluded rollback rehearsals and RF. On October 16, 2026 the
maintainer accepted installation/runtime integration while explicitly retaining
production RF operation as unqualified. The published compatibility manifest
remains byte-for-byte unchanged: `defaultState=Unavailable`, `entries=[]`.
Production RF eligibility requires qualifying metadata and an appropriate
provider publication; this closeout adds no override, qualification entry or
transmission permission. Target rollback rehearsals are an accepted exclusion,
not a passing target check. Existing software and disposable lifecycle evidence
remain applicable under the unchanged-input rationale above.

Target: Raspberry Pi 5 Model B Rev 1.0, serial `8f9a6b193da1f85a`, Debian 13
ARM64, kernel `6.18.34+rpt-rpi-2712`, matching headers, GCC 14.2. The running
kernel's module signing is disabled; the installed pair is unsigned and XZ
compressed. The kernel and boot configuration were not upgraded. Runtime
acceptance applies to this running kernel only, not every installed kernel or
Pi 5/CM5 combination.

The clean target checkout was fast-forwarded to `8abb0d1`. The running
application moved from historical `c4736e5`; the owned development provider
`817f3dc` was removed through `rp1_gpclk_dkms_install.py remove --remove true`
before installing the published provider. The inventory found an already-failed
startup worker with a pending checkpoint for another boot/binding. That stale
checkpoint was retired after owned provider removal. Package preflight also
refused old route/development records; their obsolete controlled objects were
independently proved absent before those records and empty legacy directories
were retired. No package guard was bypassed. The normal installer then passed,
recorded neutral activation and restored the explicitly saved GPIO20 route idle.

Successful installer command, run in `/home/pi/WsprryPi` on wspr5:

```sh
rm -f ~/finished
sudo -n env TERM=xterm-256color REPO_BRANCH=devel JOBS=3 TERSE=true \
  INSTALL_RP1_GPCLK_DKMS=auto RP1_GPCLK_DKMS_SOURCE=release \
  PYTHONDONTWRITEBYTECODE=1 ./scripts/install.sh && touch ~/finished
```

`upgrade-completion.log` and `final-verification.json` in the installation
evidence root record installer success, the external completion marker, actual
release ownership, 20 matching binding files, four matching startup files,
running executable identity and the installed module pair. Earlier preflight
refusals remain in `upgrade.log` and `upgrade-final.log`.

Measured running-kernel artifact SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| Installed/running WsprryPi binary | `3bba8728be23f6bdee6656bdf1578290a3380ff03d886cb8bb92b92344386dc4` |
| Runtime binding | `232fc9be5119e3fbe5263b480c32e39e477d1d595545588e1e81a29601367e3f` |
| `rp1_gpclk_dkms.ko.xz` | `21b1c019b267a0939dfd48ee2d92b646163c8de6d62822e816eb3944f4c82843` |
| `rp1_route_controller.ko.xz` | `26655f915f9946777b675a80994b438c675fdbbbe567ffc7ffb8b64db6f8d768` |

The acceptance harness ran the installed public `runtime_provider.py`
`route-plan`/`route-ensure` verbs with independently checked plan digests and
matching requested/configured/persisted route arguments. Each route also ran
`runtime_route_client.py recover --execute`, fresh `switch ROUTE --execute`,
`remove ROUTE --execute` and reselection. Repeated selection preserved the
controller generation and application PID. Recovery reached route zero with
inhibition retained and the expected `recovery_required` classification;
fresh switching restored the application. Removal reached `neutral_ready`
with no consumer endpoint and restored the previously active service. Each
selected route reached `exact_ready`, no owner/lease/open endpoint, disabled
output and quiescent clock/GPIO/DMA observations.

Each route then received one `sudo -n systemctl reboot`. The installed startup
worker automatically obtained fresh activation/route plans and restored the
saved route idle; no manual activation was used after either reboot:

| Route | Idle cycle | Automatic reboot restoration | New controller session |
| --- | --- | --- | --- |
| GPIO4 | PASS | PASS, completed checkpoint, fresh boot | `8978216624902943367` |
| GPIO20 | PASS independently | PASS, completed checkpoint, fresh boot | `17963181067664120937` |

Raw commands/results and digest-checked summaries are in the acceptance root:
`accept-idle.py`, `gpio4-cycle-execution.jsonl`, `gpio4-reboot.jsonl`,
`gpio20-cycle.jsonl`, `gpio20-reboot-verified.jsonl`, `final-health.json` and
`result.json`. A local harness digest-encoding mistake stopped at read-only
planning before effects and was corrected before the successful cycles. One
post-reboot hostname lookup failed before reaching the host; later read-only
verification passed. Neither is hidden as a target pass or a repeated effect.

Final state: both upgraded versions retained, GPIO20 selected and idle,
`Transmit=false`, `Enable on Boot=Never`, unchanged binary/binding/boot
configuration, and successful application/startup/manager/Apache/chrony/gpsd
service results. Two reboots, zero RF attempts and zero rollback rehearsals.
These are installation and idle runtime observations, not RF measurements,
waveform/timing/frequency qualification or qualification of the physical chain.

## Documentation Impact

This closeout updates this acceptance record and `release_tools/Release Punchlist.md`.
The installation and provider-acceptance guides were considered and remain
unchanged by the closeout: no installer behavior or qualification policy changed.

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
on clean `devel`. The implementation stage updated
`docs/rp1-gpclk-dkms-installation.md` and `docs/rp1-provider-acceptance.md`.
Runtime workflow and provider acceptance
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
This closeout changes neither issue: #412 retains final publication tracking;
#423 still waits for the remaining release-scoped prerequisites in items 11–12.

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
No actionable finding remains in the authorized software changes. The separate
target evidence review checked exact versions and bound artifacts, independent
route results, new boot/controller identities, automatic worker completion,
disabled output and ownerless quiescent snapshots, unchanged boot configuration,
and absence of RF/rollback operations. No actionable target finding remains
within the accepted installation/runtime scope. The maintainer's bounded
compatibility disposition and rollback exclusion above close the remaining
item 10 decisions without claiming production RF eligibility. Items 1–9 are
preserved; items 11–14 remain open.

The final commit containing this record identifies the reviewed diff. After its
authorized push, exact-commit CI (including hosted release backend profiles)
and independent remote parity must be obtained; record terminal run/head/job
identities in external `closeout.json` and the execution handback. Earlier CI
does not qualify that final head. Implementation commit `8abb0d1` passed all
11 jobs in [run 38894865463](https://github.com/WsprryPi/WsprryPi/actions/runs/38894865463).
The documentation-only closeout must receive its own exact-commit CI result;
that cannot replace or broaden the recorded target source identity. Item 10 /
RP1 item 18 is **complete within the accepted installation/runtime scope**.
The remaining release work is items 11–14, with RF operation still unqualified.

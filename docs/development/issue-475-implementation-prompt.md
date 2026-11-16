# Issue 475 implementation prompt

Implement only the separately authorized slice of
[WsprryPi #475](https://github.com/WsprryPi/WsprryPi/issues/475), following the
[authoritative implementation plan](controller-only-linux-plan.md). This prompt
expands execution requirements without replacing the plan's scope or gates.
Preparation, review, commit, push, or issue creation does not authorize implementation.

## 1. Execution boundary

Use the plan's numbered slices 0–5. Do not silently begin the next slice or add
another platform/CPU/carrier claim. If the implementation request names no slice,
perform the read-only slice-0 prerequisite inspection and report the next bounded
step; do not interpret this prompt as unrestricted implementation authority.
An explicit authorization for multiple slices permits only those slices, in order,
with each predecessor gate satisfied before its dependent work starts.

The intended outcome is one controller runtime, without a local transmitter,
with native installation, unprivileged services, private state, truthful UI, and
qualified Linux/macOS/Windows host support. Build only the adapters and host
plumbing required to operate accepted #473 functions. Do not add radio modes,
management resources/carriers, recurrence evaluators, pairing methods, firmware
updaters, login systems, public hosting, or a Setup redesign.

This execution does not authorize staging, commits, pushes, PRs, issue comments,
publication, signing-credential access, or deployment unless the invoking request
separately does so. The `[skip ci]` authorization used to save these planning
documents is not an implementation validation waiver. No live installation,
service/configuration change, reboot, mutating sudo, GPIO/I2C operation, tone, or
transmission is implied. Privileged disposable-host qualification and physical
USB qualification need explicit host/operation authority.
Unprivileged production-process tests with private temporary configuration and
isolated software peers are permitted within an authorized implementation slice;
they do not authorize changing installed services or contacting live transmitters.

## 2. Inspect before editing

In every repository used, inspect branch, full working tree, staged and unstaged
diffs, untracked affected paths, actual remotes, nested instructions, and component
contracts. Begin with:

```sh
git status --short --branch
git diff --check
git diff --cached --check
```

Preserve existing work; do not reset, overwrite, clean, stash, or synchronize a
dirty checkout to make a gate pass. Confirm components are ordinary parent-repo
content. Record exact candidate revisions and tested uncommitted changes. Planning
hashes and historic CI are evidence pointers, not permanent dependency pins.

Before any implementation, verify #473's complete accepted integration and
operator-documentation evidence for phases 1–8 against the intended sources.
Read its final plan, approved decisions, pairing, authority migration, and shared
UI contracts. Incomplete or unavailable prerequisites block dependent work. Do
not copy an unaccepted feature checkout, recreate missing #473 features here,
or treat an issue's closed status as acceptance proof.

Inspect producer and all companion consumers before protocol-facing changes.
Reading WsprryPico for contracts/fixtures does not authorize modifying it. Confirm
explicit sibling edit authority first; without it, report the affected blocker.
Locate the real operator-docs repository, currently `WsprryPi-Docs`, and read its
instructions. Do not create a replacement for the historical alternate spelling.
Review whether previous Docs authority actually applies to #475; do not inherit
#473's cross-repository write authorization automatically.

## 3. Preserve the accepted product contracts

- Reuse #473's explicit `--controller-only` path and management/pairing/UI/recovery
  implementations. Keep ordinary Pi startup unchanged without that flag. Never
  infer the role from failed hardware initialization or device selection.
- The dedicated controller profile has zero physical or simulated local backends,
  ancillary GPIO, mailbox/MMIO/DMA, and inbound WTP execution service. Missing role
  selection refuses normal startup in that profile. Help/version remain safe.
  Keep source lists, generated assets, objects, and metadata isolated by profile.
- Separate controller readiness from target readiness. Empty enrollment and offline
  targets are valid states; invalid authority, security, or required listeners
  keep mutation admission closed. No local station validation or fabricated output.
- Preserve device-managed automatic Pi pairing, backend-only keys, generation
  revocation, stable identity, and exact device/boot/profile/revision bindings.
  No manual certificate workflow replaces #473 pairing. Existing independent WTP
  engineering credentials are not new management authority.
- Targets own recurrence. Controller/browser closure must not stop autonomous
  target schedules. Do not restore retired legacy Fleet assignments, evaluate solar
  policy on the host, or dispatch occurrences to substitute for target autonomy.
- Validate current closed contracts before field access; retain request bounds,
  supported carriers, authentication, Host/Origin/proxy/CSRF guards, uncertainty
  fencing, and authoritative readback. Never downgrade or relax validation.

## 4. Execute the authorized slice

### Slice 0 — prerequisite and platform inventory

Produce a bounded requirement-to-source/test/documentation map. For Linux first,
then macOS and Windows, select exact OS/runtime/toolchain/CPU baselines, native
runners, network/USB claims, dependency and browser-hosting choices, service
identities, configuration/state/log/temp paths, ACLs, and artifact ABI checks.
Include Linux amd64 first and inspect the plan's other CPU profiles independently.
Record unavailable runners as outstanding; do not quietly narrow the outcome.
For explicitly authorized slice 0, save the reviewed manifest and subsequent
evidence pointers in `docs/development/issue-475-platform-matrix.md`, linking it
from the plan. If only the default read-only inspection is authorized, put that
inventory in the response instead. Do not create this file while #473 prerequisites
are unmet merely to imply that slice 0 passed.

Identify POSIX/Linux dependencies in entry points, signals, clocks/suspend,
sockets/DNS-SD, workers, identity/CSPRNG, filesystem locking/durability, USB CDC,
logs, PHP/proxy hosting, and service lifecycle. Map narrow platform adapters.
Identify any reusable-component change and obtain required scope approval before
editing it. Resolve material conflicts with approved contracts before coding.

Gate: accepted #473 evidence and a complete, source-grounded platform manifest.
Slice 0 does not qualify a platform or authorize later slices.

### Slice 1 — runtime and hardware-free build

Reuse the accepted runtime and make only required separation/portability changes.
Reject incompatible explicit backend/GPIO/build selections, preserve ordinary Pi
validation, and isolate parallel/interleaved builds. Keep pure shared algorithms
only where existing remote functions need them. Audit constructors, diagnostics,
status/version/help, callbacks, workers, and teardown, not only main's scheduler.

Gate: the production executable runs unprivileged on the first Linux profile,
without station data, local backends, Pi dependencies, forbidden device access,
or real-time privilege requests. Other profiles need slice-2 native state/identity
adapters before their runtime gates can pass. An environment suppression flag or
fixture-only launch is insufficient.

### Slice 2 — state and transports

Wire explicit native paths and secure storage ownership without changing existing
locations/identities implicitly. Provision first-install identity once under the
service account; distinguish empty enrollment from lost established keys/state.
Never package initialized credentials or promise local detection of complete clones.
Preserve accepted recovery, atomic replacement, durability, locking, private keys,
and interrupted-operation evidence. Path/identity/platform migrations stay outside
scope unless separately approved with crash/rollback semantics.

Implement native networking, USB identity/interface binding/exclusive access and
CDC, permissions, cancellation/reconnect, and portable existing logging. Do not
mistake Linux sysfs/termios for native Windows/macOS support or add a kernel driver.
Gate: actual controller management, pairing, and recovery through production peers,
two-target isolation, private state, and accurate unavailable-carrier reporting.

### Slice 3 — controller browser startup

Use Impeccable before design/implementation and read existing PRODUCT/DESIGN
context. Preserve accepted #473 shared controls. Bootstrap from verified host
role/status/capabilities without local station requests, autosave, or missing-API
polling. Show controller identity separately from the selected remote target.
Keep remote controls enabled by that target's capabilities, not local output.

Inventory navigation/deep links/HTTP methods/WebSocket commands. Omit unsupported
local output, power/service, Pi-only maintenance, and firmware-flashing controls;
server-refuse direct calls. Preserve per-target drafts, late-response fencing,
explicit pairing/enable intent, empty/offline/unauthorized/loading/uncertain states.
Gate: real browser flows with desktop/mobile light/dark, keyboard/focus/ARIA, and
error/recovery evidence. Static mocks alone establish layout, not authentication
or runtime behavior. Fix relevant Impeccable findings and record dispositions.

### Slice 4 — artifacts, installation, and services

Provide verified matching controller/backend/UI artifacts and platform dependency
manifests. Use an explicit local candidate-artifact input for prepublication tests;
do not download a latest-channel Pi binary as fallback. Dispatch explicit Linux
controller installation before hardware checks; retain ordinary Pi installation.
Use native macOS/Windows entry points sharing declarative contracts.

Provision only the required configuration, private state, dedicated unprivileged
service account, native service, browser hosting/proxy, and narrow device permissions.
Preserve accepted network guards. Test effective identity and ACLs, not just unit
file contents. No broad sudo, root/admin runtime, world-writable devices, or access
to unrelated interactive-user secrets. Preserve configuration, pairing, identity,
journals, and reinstall ownership; refuse conflicting roles/unsafe paths.

Stage/verify replacements, preserve rollback assets, and never run an incompatible
old writer or erase accepted outcomes. Uninstall removes only owned service/files/
rules and preserves user state and its service-identity binding. Do not add purge,
transmitter conversion, credential import/reset, or unrelated package removal.

Gate: offline tests plus separately authorized native clean-host install/start/
stop/restart/update/rollback/uninstall/reinstall/reboot evidence for each claimed
profile. Linux success does not qualify macOS or Windows. Record missing signing
or notarization/distribution authority without bypassing it.

### Slice 5 — integrated acceptance and documentation

Reassess the complete final candidates, not separate stale slice snapshots.
Close every scoped gate and document each platform/CPU/carrier's actual support.
Unmet prerequisites, unavailable validation, or missing authority stay outstanding.
Do not describe partial platform delivery as the complete cross-platform outcome.

## 5. Validation required throughout

During read-only prerequisite inspection, inspect recipes and existing evidence
without starting builds, test services, installers, or native runners. For an
authorized implementation slice, run its focused checks and applicable gates;
both complete production carrier suites remain required for integrated acceptance
and whenever the shared-interface completion gate applies. A planning inventory
does not need to execute the implementation test matrix to report unmet prerequisites.

Use existing Makefile/test infrastructure from `src`, inspecting recipes first;
add meaningful focused targets where necessary and label them as new. Maintain
native build/runtime/installer-renderer CI and source/change classification.
Keep real privileged lifecycle checks in an authorized disposable-host harness.

Follow every acceptance item in the plan, including malformed configuration,
state corruption, permissions, symlink/reparse paths, disk failure, concurrency,
partial startup, port conflicts, signals/service stop, missing keys, reinstall,
artifact role/ABI mismatch, secret redaction, stale contexts, rejected methods,
origin/auth failures, lost replies, recovery, and no-target startup.

Run both complete `.github/workflows/debian-non-hardware.yml` recipes:

- `wtp-network-loopback`: `wtp-network-runtime-test`,
  `wtp-network-interop-test`, and `wtp-config-interop-test`.
- `wtp-usb-management-pair`: actual Pico `usb_management_driver`,
  `wtp-usb-pair-client-build`, `wtp-usb-management-test`, and
  `WsprryPi-UI/tests/wtp_usb_pair_test.js` through production Pi/Pico handlers.

Additionally run the actual controller runtime through production peers and native
platform adapters; existing pair clients alone cannot qualify new startup.
Select intended sources and pinned dependencies explicitly under current repository
policy. Preserve crypto, serialization, framing/ACK, journals, and negative cases.
Coordinate all affected schema/advertisement/descriptor/parser/generated/test
consumers together. Inspect current Pico control policy before version decisions;
preserve prerelease major 1 while applicable and obey the explicit stable-transition
policy afterward. Keep WTP, authentication, and storage independently versioned.

Run affected component standalone, UI/package, and parent regression tests.
Distinguish full Linux hardware-disabled semantics from the portable subset.
Read the simulation contract before target application simulation; the controller
itself must never obtain a simulated local output. Native and physical USB, RF,
hardware timing, installation, and service evidence remain separate categories.
Failed/cancelled/timed-out/skipped required checks leave gates open. After changes
to tested contracts or candidate sources, rerun affected checks, including both
required carrier suites for shared interfaces. Never substitute stale green CI.

## 6. Documentation, adversarial loop, and report

Review engineering and operator impact with each slice. Update authorized owning
docs concomitantly, describing actual support, role/path/ACL/dependency behavior,
restart/recovery, unavailable controls, and limits. Follow sibling virtualenv/Sphinx
and rendered-review rules. No sibling authority means an explicit documentation
blocker, not permission to omit it from the completion gate.

Before completion, assess the full authorized diff, production paths, tests,
documentation, platform claims, and plan compliance adversarially. Enumerate
concrete defects, contradictory requirements, unsafe assumptions, missing evidence,
and scope expansion. Fix authorized in-scope issues, run affected verification,
then perform another full assessment. Repeat until no known actionable issue
remains. Missing approvals or impossible/unavailable gates cannot be reviewed away;
report the exact blocker and next unfinished step without broadening the task.

Report implemented versus proposed behavior; changed files/components; actual
source revisions and dirty changes; exact commands/results and CI links; native
profile/carrier matrix; Impeccable workflows/captures/dispositions; outstanding
qualification; and per-repository branch/working-tree state. Include a Documentation
Impact section listing updated, considered-but-unchanged, and required documentation.
State whether any Git or host operations occurred and their explicit authority.
Mark TODO progress only from implementation evidence: `▧` not started, `⏳` in
progress, `✅` complete. Prompt preparation is not implementation progress.
Stop after reporting the authorized slice; do not publish, advance another slice,
or claim universal platform, installation, hardware, or release readiness without
the corresponding evidence.

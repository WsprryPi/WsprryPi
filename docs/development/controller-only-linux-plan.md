# Issue 475: controller-only installation after issue #473

Status: proposed follow-on implementation plan; no implementation or deployment
is authorized by this document. Updated 2026-11-15. Replaces the earlier standalone
Linux draft at this path.

Track [WsprryPi #475](https://github.com/WsprryPi/WsprryPi/issues/475).
Use the [implementation prompt](issue-475-implementation-prompt.md) for each
subsequently authorized slice. This plan defines scope and acceptance; the prompt
defines execution. Neither document by itself authorizes implementation.

The [slice-0 platform inventory](issue-475-platform-matrix.md) records the 2026-11-22 source selection, prerequisite fixture repair, selected qualification baselines, native adapter and service boundaries, evidence, and outstanding gates. It does not qualify controller installation or authorize a later slice.

The prerequisite is [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
and its [authoritative plan](device-owned-solar-scheduling-plan.md), as accepted
at execution time.

## 1. Intent, prerequisites, and authority

Deliver a controller-only WsprryPi installation with no local transmitter,
ordinary unprivileged operation, an accurate browser interface, and production
startup/install acceptance evidence. Extend the required host plumbing so the
controller can be installed on Linux, macOS, and Windows without Pi hardware
dependencies. Keep one application, executable name, and shared control contracts.

Execute after #473 is complete and its intended Pi/Pico changes and required
operator documentation are available on the accepted integration refs. At entry,
record accepted revisions, schemas, approvals, and requirement-to-evidence records
for all eight phases. Inspect the actual candidate; a checklist or dirty feature
checkout alone is not proof. Missing pairing, target adapters, authority migration,
or shared UI is a prerequisite blocker, not work to absorb into this follow-on.

Reuse #473's accepted controller runtime, target adapters, automatic device-managed
pairing, shared Setup/Scheduling UI, and recovery/authority rules. If an item below
already passes on that baseline, retain it and supply regression evidence; do not
build a second implementation. Changes now present on the issue branch are
candidate work, not accepted dependencies. Reinspect at execution time.

Planning checkout: local `devel` at
`505c6ddc12196c7b31c393f9ca01050faafd1737`. It predates accepted #473 delivery.
The phase-1 pairing/contract supplements reviewed on the issue branch are not
present on this local devel baseline; their accepted successors are entry-gate
dependencies, not files to copy or presumed delivered prerequisites.
Relevant sources include `src/{main,scheduling_runtime,web_server,web_socket}*`,
`src/wtp_integration`, `src/wtp_endpoint/identity.*`, `src/Makefile`,
`scripts/install.sh`, capability/build generators, and `WsprryPi-UI`.
Read applicable instructions, complete diffs, component boundaries, and final
#473 decision/pairing/contract documents before editing.

This plan does not reopen #473, change scheduling semantics, or authorize Git
actions, sibling edits, publication, real installations, services, hardware,
or transmissions. Implement only the subsequently authorized slice.

## 2. Scope and platform claims

Include these four outcomes and their necessary portability dependencies:

1. A production controller runtime with no local transmitter.
2. Platform installers, unprivileged service operation, and writable-state ownership.
3. Controller-specific UI startup and honest unavailable local controls.
4. Production startup, browser, installation, and service acceptance tests.

Additional platform work is bounded to building, hosting, transporting, storing,
and operating those existing functions. No new radio mode, remote-management
resource, carrier, recurrence evaluator, pairing method, firmware updater,
multi-user authorization system, Internet-hosting feature, or Setup redesign.
No controller-side solar evaluation or remote recurrence dispatch is introduced.
Legacy Fleet authority follows #473's completed migration rules; never re-enable
retired assignments or use a legacy loop to replace device-owned scheduling.

“Any platform” means a portable controller architecture with individually qualified host profiles, not an untestable promise for every operating system or CPU. Deliver in order: Debian-family Linux, macOS, then Windows. Establish exact minimum OS/runtime/toolchain versions and CPU pairs in slice 0; Linux amd64 is the first required profile. Audit Linux arm64, macOS Intel/Apple silicon, and Windows x64 separately. Windows ARM64 is outside the selected support scope, following the explicit user decision on November 24, 2026. macOS support selects version 26 on Intel and Apple silicon and version 27 on Apple silicon, following the subsequent user decision; the earlier macOS 15 minimum is superseded. Windows x64 qualification selects the available Windows 11 build 26300 and MSVC 19.51 baseline; the earlier build 26100/MSVC 19.44 proposal is superseded. Do not advertise a combination before its native gates pass. An unavailable runner/toolchain leaves that profile outstanding, not silently omitted from the requested cross-platform outcome. Other Unix systems, mobile hosts, and embedded operating systems require separate profiles and are not claimed here.

Native host qualification in slice 2 covers the no-output controller profile: private state and identity, management/pairing, both production software carriers, and encrypted recovery. It does not promise local-transmitter execution on macOS or Windows. Retain ordinary Pi regression coverage on the Linux profile and keep the full/portable semantics entry points unchanged. An explicitly selected controller-host subset is not a pass of ordinary-transmitter semantics; record failed timing checks separately without weakening launch deadlines or accepting output where admission refused.

## 3. Runtime and build contract

Preserve #473's explicit `--controller-only` startup contract. The ordinary Pi
build keeps its existing startup behavior when the flag is absent. The installer
always supplies the flag for a controller service. Role changes require restart;
failed hardware startup never selects this role.

Provide a dedicated hardware-free build profile using the same controller
implementation, not another runtime. It compiles no physical or simulated output
backend, ancillary GPIO driver, mailbox/MMIO/DMA code, or local WTP execution
listener. Retain only shared algorithms needed by accepted remote functions.
In this profile, omission of the role flag refuses normal startup rather than
attempting transmitter initialization; help/version remain usable.

Use explicit source lists and isolated objects, generated headers, and artifacts.
Reject unknown profiles and incompatible explicit backend/GPIO selections.
Zero backend capability is valid only for the controller profile; do not relax
ordinary transmitter validation. Build/version/status metadata distinguishes
host role, compiled capabilities, controller readiness, and target readiness.
A WTP management/client connection is not a local RF output.

Audit POSIX/Linux assumptions in entry points, sockets, signals, workers,
single-instance locking, clocks/suspend handling, process cleanup, filesystem
operations, dynamic libraries, and logging. Supply narrow platform adapters;
avoid application-wide rewrites. Keep platform implementations behind the same
tested semantics. Reusable-component edits require explicit boundary review and
the approvals and standalone tests required by repository instructions.

Before listeners admit mutations, validate configuration, stable controller
identity, pairing/catalog/recovery state, and required security services.
No station callsign, locator, transmitter selection, or local schedule is required.
No Pi model/serial probe, root requirement, real-time scheduling request,
GPIO/I2C access, inbound execution endpoint, or local transmitter construction
may occur, including global initialization, help/status paths, and teardown.
A valid first installation with an empty catalog is ready; offline targets do
not make the controller unhealthy. Invalid authority or a required listener/security
failure keeps admission closed. Distinguish optional discovery/log availability.

Retain #473's authenticated target operations, exact device/boot/profile/revision
bindings, request bounds, refusal-before-write behavior, and uncertain-operation
reconciliation. Management does not acquire RF ownership. Controller closure or
service restart does not stop autonomous target recurrence. Preserve accepted
remote-operation evidence and apply #473's defined cleanup to controller-owned
operations; never treat shutdown as authority to adopt/reset another device.

## 4. Identity, configuration, state, and transport portability

### Identity and configuration

Preserve existing stable controller identities. On non-Linux hosts, provide a persistent cryptographically generated controller identity conforming to the accepted identity contract; never derive trust from hostname, IP, MAC address, CPU architecture, or a hard-coded shared value. Use platform CSPRNGs and fresh boot/session identities. Missing or inconsistent identity/pairing storage refuses management until explicit recovery under #473. Do not distribute preinitialized identities or keys in packages/images. A complete credential-store clone cannot in general be detected locally: document clone exclusion and target-local pairing replacement recovery rather than promising a new clone-detection system. Distinguish first installation from lost established state: provision a fresh controller identity once through the accepted backend initialization, under the service account, before readiness. An empty enrollment needs no pairing keys; a saved enrollment with missing keys requires recovery. Reinstall must not treat missing established authority as permission to generate a replacement identity. Use the accepted encrypted backup/restore engine for supported computer moves. Preserve destination identity and authority, supported portable-setting rules, and explicit destination-review holds. Do not add platform-specific credential conversion or an implicit identity replacement.

Use the accepted [SQLite and process contract](sqlite-storage-contract.md). SQLite is authoritative for supported local settings and controller state; an existing INI is one-time migration input and is retired afterward. The backend owns initialization and validation, the existing UI edits ordinary settings, and the local validated `--settings` command edits advanced saved values. Platform installers select explicit native private state/runtime paths; neither browser input nor working directory selects them. Preserve current defaults, units, startup precedence, and one current settings schema.

Local-transmitter fields are not applied or rewritten. Invalid relevant settings
refuse startup. Preserve existing valid values on reinstall. Listener/security
changes follow accepted #473 activation rules; add no new live role switching.
Do not reuse local-station validation merely to obtain controller policy.

### Ownership and storage

Use platform-specific config, private state, logs, and temporary directories. Linux SQLite installations use the private application-generation root `/var/lib/wsprrypi/application-state`; existing independent identity and companion owners retain their accepted bindings. macOS uses a private service-owned Application Support directory, and Windows uses restricted ProgramData state. Fix exact paths in the platform manifest before coding. Pass them through the accepted storage owners, never by working-directory dependence or browser input.

Separate executable/configuration ownership from mutable catalogs, device-managed
pairing keys, authorization generations, receipts, journals, and cursors.
Daemon writes must not require write access to executable directories or the
whole configuration parent. Keep keys backend-only; no manual certificate,
fingerprint, upload, or repair workflow for #473 Pi pairing. Existing independent
WTP engineering credentials retain their accepted meaning.

Implement equivalent private permissions/ACLs, locking, atomic replacement,
durability, and recovery on every supported filesystem. Test interrupted writes,
disk-full/permission errors, symlink or reparse-point substitution, concurrency,
and cleanup. Do not replace corrupt/unknown state with an empty store. Preserve
existing locations and identities; path relocation needs a separately approved
migration with crash/rollback semantics. New installations need no migration.

### Network, USB, and browser hosting

Port sockets, cancellation, timeouts, DNS-SD discovery, TLS/crypto dependencies,
and logging without changing wire contracts. Treat absent discovery as an explicit
capability state; manual existing connection methods remain available. Never
downgrade authentication, carrier, or identity checks to obtain connectivity.

Implement native stable USB identity/discovery, CDC communication, interface
binding, exclusive access, disconnect/reconnect, and normal user/service permissions
on each OS. Linux sysfs/device paths and POSIX termios are not cross-platform
implementations. Use existing platform drivers where sufficient; do not add a
custom kernel driver or firmware change. Platform/carrier unavailability is
reported accurately and leaves its advertised-support gate open.

Package the existing PHP/static UI and protected HTTP/WebSocket hosting path.
Do not assume Linux Apache configuration, shell commands, symlinks, journald,
or PHP availability on another OS. Provide the smallest native hosting integration
that preserves accepted Host/Origin, proxy identity, trusted-network, CSRF, request
bounds, and secret isolation. Do not introduce an embedded-PHP rewrite or weaken
guards when a proxy is unavailable. Default exposure follows accepted #473 policy;
no new public-network configuration is added.

## 5. Installer and service contract

Use explicit controller role selection before hardware checks or dependency
installation. Keep the current Pi installation path unchanged. Linux extends the
existing installer; macOS and Windows get native install/service entry points
sharing declarative artifact/configuration contracts, not copied policy forks.

Validate OS/version/CPU, dependencies, artifact role/ABI, checksum, and matching
UI/backend assets before modifying installed state. Provide an explicit local
candidate-artifact input for qualification before release publication. Never
fetch a newest-channel Pi binary as fallback. Record dependency versions,
licenses, build provenance, dirty-source attribution, and artifact checksums.
Release signing/notarization and publication need their own credentials/authority;
unmet distribution requirements must be reported, not bypassed.

Privileged installation may create a dedicated service identity, install files, set narrowly scoped device permissions, and register the service. The controller backend and browser runtime are unprivileged under their native service identities: Linux systemd, macOS launchd, and Windows Service Control Manager. The separate maintenance lifecycle adapter may hold only the narrow native privilege required for admitted service replacement and protected recovery state; it exposes no arbitrary command or service executor. The current Linux recovery topology uses a separate root maintenance worker; #475 must qualify its unprivileged controller backend and browser independently. Use a dedicated least-privilege service identity, explicit paths/working directory, bounded stop/restart handling, and protected config/state/log ACLs. Do not grant administrator/root backend or browser runtime, interactive-user secrets, broad sudo rights, or world-writable device nodes. Verify hardening does not block required USB/network or durable writes. A foreground diagnostic invocation uses the same runtime.

Install/update is repeatable and preserves configuration, identity, pairing, and journals. Refuse a conflicting installed role, occupied deployment, unsafe paths, or incompatible storage version before destructive changes. No automatic role conversion, credential reset, or ownership adoption. Supported explicit computer moves use the accepted recovery engine and cannot convert a Pi role into a controller role. Stage and verify replacements; preserve the prior working artifact and state until successful activation. Rollback must not run an older writer against incompatible state or erase accepted remote outcomes.

Define role-specific uninstall ownership: remove only installer-owned service,
files, and permission rules; preserve user state by default. State deletion,
purge, and unrelated package removal are not included.
Retain or safely reestablish the service identity/ACL binding needed to reopen
that private state on reinstall. Never delete an account still owning retained
data or transfer its secrets to another user. Test uninstall/reinstall identity
and pairing continuity, not merely file preservation.

## 6. Controller UI contract

Use the accepted #473 shared Setup/Scheduling/management UI and Impeccable design.
Bootstrap from verified role/capability/status without requesting local station
configuration, starting local autosave, or polling missing transmitter APIs.

Represent the host as a controller, never a fabricated “This Pi” RF output.
Keep remote Signal Setup, Scheduling, Transmitter, and Device I/O controls where
the selected target supports them; absence of local output does not disable
remote controls. No-target, offline, unsupported, unauthorized, loading, pending,
uncertain, and ready states remain distinct. Do not select/pair/enable the first
discovered device automatically. Preserve target-bound drafts and reject late
responses after selection or identity changes.

Inventory every navigation item, deep link, HTTP method, and WebSocket command.
Hide unsupported local transmit/tone/backend/GPIO/inventory and host power/service,
firmware-flashing, or Pi-only maintenance actions. Provide nearby truthful
explanations where needed. Backend dispatch refuses them regardless of UI.
Retain accepted portable observation/logging features; do not build new support,
update, logging, or maintenance features. Direct requests cannot escape the role.

Preserve incumbent layouts, terminology, units, focus, keyboard, status announcements,
themes, and responsive behavior. Review desktop/mobile in light/dark through the
actual controller service. Static mocks are layout evidence only.

## 7. Ordered implementation slices and gates

0. **Prerequisite and platform inventory.** Verify accepted #473 delivery and its
   two-target autonomy, pairing, authority, and interface evidence. Map each
   requirement above to an owning source, platform adapter, artifact, test, and
   documentation page. Select exact platform/CPU baselines and runners, dependency
   and hosting/service choices, path/ACL tables, and carrier claims. Resolve any
   change to an approved contract before coding. Missing prerequisites block work.
1. **Runtime/build.** Reuse and harden the accepted no-output runtime; add the
   hardware-free build and narrow platform boundaries. Test ordinary Pi regression
   behavior and all early/cleanup paths. Gate: production controller starts
   unprivileged on the first Linux profile without station data, local backends,
   or Pi dependencies. Native identity/storage adapters needed by other profiles
   complete in slice 2 before those profiles can pass the runtime gate.
2. **State and transports.** Wire explicit paths, persistent identity, private
   durable storage, native network/USB, and portable logs. Gate: production target
   management/pairing/recovery succeeds without local-output selection, with
   two-target isolation and no weakened authority.
3. **UI.** Complete role bootstrap and route/control inventory using accepted
   #473 surfaces. Gate: production browser workflows and refusal tests pass,
   with desktop/mobile light/dark and accessible-state evidence.
4. **Install/service.** Deliver Linux first, then macOS and Windows integrations
   with verified matching artifacts. Gate: offline tests and authorized clean-host
   install, restart, update, rollback, and removal evidence for each supported
   profile; no generic cross-platform completion claim from Linux alone.
5. **Integrated acceptance and documentation.** Run final candidate suites and
   adversarial assessment; fix in-scope defects and repeat affected checks until
   no known actionable issue remains. Report any unavailable platform gate or
   authority as outstanding. Publish or change Git state only if separately asked.

Each slice requires subsequent authorization. Finish its tests and documentation
review before proceeding; slice completion is not product completion.

## 8. Required acceptance evidence

Use production binaries and adapters, with temporary state and isolated software
targets. New test targets must be documented as new. Do not weaken checked-in
tests or source-selection controls. Record exact commands, source pair/dirty
manifests, toolchains, platform/dependency versions, logs, and CI links.
Add maintained native build/runtime/installer-renderer jobs to the existing
validation infrastructure for each supported profile, preserving source pinning
and change-classification coverage. Keep privileged install/reboot tests in an
explicitly authorized disposable-host harness. Linux canonical pair suites remain
required; native runtime tests additionally qualify each platform's adapters.
Emulation/cross-compilation alone is not native runtime or installation evidence.

- Build without Pi hardware packages; inspect source/link manifests and native
  binary dependencies. Audit startup, requests, worker creation, and shutdown
  for forbidden device access, transmitter construction, power/service commands,
  and privilege requests. A suppression environment variable is insufficient.
- Test CLI/help/version, invalid/mixed arguments, missing role on controller build,
  one-time minimal/read-only/legacy-rich INI migration, SQLite startup and no fallback, bounds/precedence, supported-value preservation,
  corrupted or missing identity/keys/state, partial startup, listener conflicts,
  single-instance locking, permission failures, signals/service stop, and restart.
  No target mutation or execution dispatch before authority/readiness admission;
  bounded local initialization/recovery bookkeeping follows the accepted storage
  contract and must itself be covered by crash/failure tests.
- Through production HTTP/WebSocket paths test every allowed/forbidden operation,
  method/alias/deep link, malformed or superseded contract, stale identity/revision,
  revoked access, origin/proxy/CSRF failure, unavailable carrier, lost reply,
  retry/reconciliation, and pending operations during exit. Demonstrate no local
  output and at least two independently managed targets.
- Prove target recurrence continues after controller/browser closure and reconnect,
  using #473's software targets and simulated target backends, never a simulated
  local controller output. Retired legacy assignments remain retired.
- Run both canonical `pico-network-management-pair` and `pico-usb-management-pair`
  recipes from `.github/workflows/debian-non-hardware.yml`: network runtime,
  interoperability, and config interoperability; actual Pico `usb_management_driver`,
  `pico-usb-control-client-build`, `pico-usb-management-contract-test`, and
  `WsprryPi-UI/tests/pico_usb_control_pair_test.js`. Additionally exercise the controller
  runtime through those production peers. Preserve crypto, serialization,
  framing/ACK, and journals. Select intended immutable Pico/dependency sources.
  A legacy client, old green CI, mock, failed, skipped, or timed-out required check
  does not qualify this candidate.
- Any role/capability/schema or protocol-facing change requires coordinated
  producers, companion consumers, strict validators, generated assets, policy
  checks, documentation, and both carrier suites. Reinspect authoritative Pico
  control policy: preserve prerelease major 1 while that policy applies; after
  an explicit stable transition, obey its reviewed compatibility/version rules.
  Keep WTP/auth/storage versions independent. Missing sibling authority
  blocks the affected work; no companion edits are authorized by this plan.
- Run UI/package, affected component standalone, parent regression, and appropriate
  semantics tests. Distinguish full Linux hardware-disabled semantics from
  `make semantics-test-portable SUDO=`. Neither proves hardware.
- Run offline installer/renderer tests for platform selection, wrong role/ABI,
  safe paths, dry-run purity, interruption, ownership, and preservation. Then,
  only with explicit host-operation authorization, test real installers/services
  on clean disposable native hosts. Inspect effective runtime identity, actual
  config/state ACLs, proxy/HTTP/WebSocket behavior, pairing persistence, log/USB
  access, update rollback, port-conflict recovery, uninstall ownership, and reboot
  persistence. Follow repository installation-marker rules where applicable.
- Native physical USB/driver behavior and remote RF require separate authorized
  qualification. Software peers do not prove USB electrical/device behavior.
  State exact carrier qualifications per platform; no physical transmission is
  required or authorized by the software acceptance plan.

## 9. Documentation Impact and completion

Update this plan now; no product code, application UI, installer, or sibling docs
are changed during preparation. Engineering follow-up includes development/build
guidance, installer/service/artifact contracts, API role restrictions, storage
ownership, platform support, dependencies, and affected component READMEs.

Review actual sibling `WsprryPi-Docs` instructions and installation, Setup,
Operations, Scheduling, Transmitter/Device I/O, Maintenance, INI, and REST API
guides. Extend accepted #473 documentation only for implemented controller role,
platform paths/permissions, provisioning, unavailable host actions, and recovery.
No new manual pairing/certificate workflow. Sibling writes require explicit
authority; absent authority leaves operator delivery incomplete. Use its virtual
environment/Sphinx workflow, review warnings/links/renders, and use Impeccable for
affected rendered UI guidance. Replace only materially inaccurate screenshots.

Completion requires every scoped runtime/UI/installation gate and native evidence
for each advertised profile; list any remaining OS/CPU/carrier qualification
explicitly. Report modified components, exact passed/failed/skipped tests, visual
evidence, documentation updated/unchanged/outstanding, source revisions and dirty
changes, per-repository Git state, and any unperformed deployment or hardware work.
No release-readiness or universal-platform claim may exceed that evidence.

Preparing this plan does not execute it. No staging, commit, push, installation,
service operation, hardware access, or transmission is authorized.

## SQLite and separate-service reconciliation

Apply the accepted SQLite ownership, initialization/migration, advanced-settings command, diagnostic export, encrypted recovery, and persistent restore-hold contracts. Keep backend, HTTP/browser-WebSocket process, and independent maintenance lifecycle separate on every claimed native profile. Apache/PHP remain the current Linux asset/proxy integration; other native hosting/service/IPC adapters require their own qualified implementation. A Linux fixture or macOS portable build does not qualify Windows, installed services, or physical USB.

The ordinary Pi role remains hybrid: it can transmit locally and manage permitted remote targets at the same time. Controller-only is an explicit additional zero-output role, not a mutually exclusive transmitter-versus-fleet product architecture. Current `--no-web` operation retains backend and maintenance ownership. Recovery is available through the existing Maintenance page and local command without adding advanced settings controls. New platform work must preserve these behaviors rather than introduce another storage or recovery engine.

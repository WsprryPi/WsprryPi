# RP1-GPCLK-DKMS installation

WsprryPi can orchestrate installation of the independently maintained
[`WsprryPi/RP1-GPCLK-DKMS`](https://github.com/WsprryPi/RP1-GPCLK-DKMS)
provider. WsprryPi does not vendor its module source, reproduce its DKMS
lifecycle, edit boot configuration, or enable transmission. A source or release
that implements the reviewed runtime contract is first activated to route-neutral
controller and manager administration. Setup then restores an explicitly saved
`rp1-gpclk` GPIO4 or GPIO20 selection through the provider's managed route
transaction, with transmission disabled.

## Startup integration

The selected application checkout installs `runtime_reconcile.py`, its
application-owned installer support, `wsprrypi-rp1-reconcile.service`, and the
`92-rp1-reconcile.conf` application drop-in through
`scripts/install_runtime_reconcile.py install`. The normal installer and
`scripts/copy_exe.py` invoke that helper. It records a fixed file inventory under
`/var/lib/wsprrypi/rp1-startup-files.json`, preserves foreign files, and permits
exact interrupted-installation retries. `remove` removes only that recorded
inventory; neither operation starts a service or selects a route.

Normal reboot recovery requires a provider supporting version-3 post-reboot
activation plans and an application binary with the separate reboot-idle flag.
A provider-only update does not supply this orchestration. See the
[runtime workflow](runtime-route-workflow.md) for route and idle policy. Provider
updates and rollback still use exact source/bundle provenance and the existing
installation workflow; historical session values in installation provenance do
not assert current-boot ownership.

## Selection

`INSTALL_RP1_GPCLK_DKMS` accepts exactly:

- `auto` (default): install only on a positively identified Raspberry Pi 5 or
  Compute Module 5 with BCM2712 compatibility;
- `true`: request installation on any platform, without bypassing provider
  hardware, resource, ownership, or build checks; or
- `false`: deliberately omit provider installation.

`RP1_GPCLK_DKMS_SOURCE` accepts exactly:

- `auto` (default): select a published release for WsprryPi `main`/release
  installation, or the DKMS `origin/devel` tip for a named non-production
  WsprryPi branch;
- `release`: select the latest eligible published stable release;
- `devel`: resolve upstream `origin/devel` once to a full commit;
- `commit:FULL_40_CHARACTER_SHA`: select an exact upstream commit; or
- `checkout:/absolute/path`: select the clean HEAD of an authoritative local
  checkout.

Abbreviated commits, relative or symlinked checkout paths, dirty or unrelated
repositories, unavailable branches or commits, and ambiguous WsprryPi source
channels are refused. An explicit source selector can resolve an otherwise
ambiguous WsprryPi source channel, but never weakens source verification.

When provider installation is selected, package handling adds `dkms`, `kmod`,
`device-tree-compiler`, and the exact running-kernel package
`linux-headers-$(uname -r)`. Before provider apply, the installer requires that
package to be installed, requires `/lib/modules/$(uname -r)/build` to resolve to
the matching `/usr/src/linux-headers-$(uname -r)` tree, and verifies the DKMS,
compiler, module, checksum, and Device Tree tools used by the reviewed runtime
profile. Missing or mismatched prerequisites fail before provider mutation.
These kernel-development packages are not added for automatic installations on
non-Pi-5 systems or when `INSTALL_RP1_GPCLK_DKMS=false`. `build-essential` and
`python3` remain ordinary WsprryPi installer prerequisites on every supported
platform.

Examples:

```sh
sudo INSTALL_RP1_GPCLK_DKMS=false ./scripts/install.sh

sudo INSTALL_RP1_GPCLK_DKMS=true \
  RP1_GPCLK_DKMS_SOURCE=commit:0123456789abcdef0123456789abcdef01234567 \
  ./scripts/install.sh
```

## Published releases

No eligible RP1-GPCLK-DKMS release is currently published. Production/main
selection therefore fails closed until an eligible immutable release is
published and validated under the contract below; it does not fall back to
development source. WsprryPi candidate acceptance and integration must precede
provider freeze/publication. After publication, WsprryPi must separately adopt
and validate the published artifacts before its own release. See the
[RP1 dependency order](../release_tools/Release%20Punchlist.md#rp1-dependency-order).

Release selection enumerates published, immutable, non-draft, non-prerelease
semantic versions rather than following a `latest` redirect. A candidate must provide
exactly one Debian package plus `SHA256SUMS` and
`rp1-gpclk-dkms-installation-manifest-v1.json`. The selected manifest binds:

- repository, release tag, immutable tag commit, product and Debian versions,
  and release-channel classification;
- package filename, package/DKMS/kernel-module identities, and package SHA-256;
- installed UAPI path and SHA-256;
- `rp1-gpclk-route-manager`;
- route-neutral, output-disabled package behavior with no module load, overlay
  application, boot-configuration edit, or service operation; and
- a canonical checksummed inventory of every package data member.

The installer validates authoritative asset URLs, GitHub asset digests when
present, tag resolution, the checksum file, manifest consistency, Debian control identity, safe archive paths and
symlinks, complete package inventory, and UAPI identity before package-manager
mutation. If the highest selected release is corrupt, validation stops; an
older release is not silently substituted. Historical releases without this
manifest are not eligible.

An exact installed release is verified idempotently. Foreign, mixed,
development, active, enrolled, manager-bound, or different-version state is
refused and must be handled by its owning migration or recovery procedure.
An exact release that was already present is accepted but never claimed as
WsprryPi-owned.

The pre-mutation inventory also rejects opt-in runtime-controller residue,
including its binding, manager fragment, tools, and journals,
endpoints, runtime overlays, and the owned WsprryPi application-inhibition
drop-in. WsprryPi reports the paths but does not remove independently owned
runtime state. Use the RP1-GPCLK-DKMS runtime cleanup workflow; foreign or
ambiguous service overrides remain untouched.

## Development sources

Development selection clones or validates the authoritative repository,
records a full commit, checks out that commit detached when applicable, rejects
tracked or untracked changes, and revalidates checkout identity immediately
before mutation. The upstream preflight derives the development module version
from the exact checkout's canonical source header and binds that header by hash.
WsprryPi does not infer a development version from release metadata
or supply a version to the upstream installer.

The maintained exact-source installer must provide the explicit
`runtime-controller` DKMS profile through a route-neutral installation without
loading either module. A single DKMS instance owns
the consumer and controller; WsprryPi verifies the returned exact commit,
canonical version, kernel, UAPI, both installed-file and decompressed-ELF hashes,
null route, and quiescent unowned state before recording success. Development
provenance records version-derived GPIO4 and GPIO20 compatibility identities
separately. The `1.0.0` identities are `v1.0.0-rp1-gpio4` and
`v1.0.0-rp1-gpio20`. The historical `v0.9.0-pi5` value is accepted only as
captured `0.9.0` predecessor removal authority. Exact commit, kernel, route,
hashes, enrollment, and qualification remain separate facts.

Runtime-capable source selection additionally requires commit
`840afebc667cf9202701246b84d437d95581cdd3`, which provides the single-owner
DKMS runtime-controller profile, binding version 3, and authenticated passive
package preservation and interrupted inverse-deployment recovery during runtime
retirement, or a selected commit/tag
whose Git history contains it. A development checkout or immutable release that
cannot prove that ancestry is rejected before provider mutation or runtime bundle
creation.

An upstream `devel` tip predating that minimum is refused. Select the exact
reviewed commit explicitly until the owning provider branch includes it. This
does not create a production release. The separate
[unpublished candidate acceptance](rp1-candidate-acceptance.md) command never
installs a package or changes ordinary source selection.

A repeated development installation is a no-op only when WsprryPi's existing
v2 ownership record and the complete inactive provider inventory still match
the newly resolved commit, source tree, version source, UAPI, compatibility
identity, running kernel, DKMS registration, source destination, module bytes,
and retained upstream rollback tools. The installer revalidates the ownership
record after those checks before reporting success. It never adopts an exact
foreign installation. Missing, changed, mixed, active, routed, enrolled, or
runtime-profile state still requires its owning migration or recovery workflow.

Runtime-profile residue accounting includes `runtime_activation.py`, both
device endpoints and loaded-module paths, the manager socket, both WsprryPi
application drop-ins, the complete runtime-admin directory (including
activation archives and `last-deployment.json`), the binding, private UAPIs and
overlays, readiness schema, scripts, and units. Installed consumer and controller
artifacts are provider inventory owned exclusively by DKMS, not runtime-deployment
residue.
Foreign or unowned residue is preserved and blocks installation. Exact state
already recorded by WsprryPi is revalidated rather than silently adopted. If
neutral runtime deployment is interrupted after the exact provider has a valid
WsprryPi v2 ownership record, a repeated installation may enter the same bound
deployment and recovery workflow; it does not treat that partial state as a
foreign installation or publish v3 readiness before final neutral proof.
The selected immutable source must advertise `rp1-gpclk-runtime-update-v1`
during read-only source resolution, before package, application, or provider
mutation. An unsupported source fails with an instruction to select a compatible
provider. Release installations therefore require a provider release containing
this interface; development installations still resolve their explicit selection
to an exact commit.

The installed provider is preferred for update preparation. If it lacks the
interface or its executable has already been removed, the selected exact source
may provide the compatibility adapter. The provider owns binding-v3 predecessor
compatibility, controller and journal interpretation, recovery, retirement, and
its deployment inverse. WsprryPi supplies its recorded source, binding, artifact
and deployment identities, and verifies the public response and plan digest.
It does not interpret the opaque plan contents or archive provider journals.
Provider recovery and archive handling remain within the provider lifecycle.

After verified runtime absence, WsprryPi performs the ordinary provider/DKMS
inventory check, revalidates retained rollback paths and hashes, invokes the
recorded provider rollback when needed, and installs the selected exact source.
Foreign state and changed ownership remain refusals.

## Plan, record, and safety boundary

Before package mutation a real installation displays the WsprryPi channel,
detected platform, selection reason and override state, requested and resolved
source, immutable tag or commit, version and expected hashes, lifecycle command
class, and the output-disabled boundary. With `DRY_RUN=true`, the standard
installer command wrapper reports RP1 plan resolution as informational context
and the application command as a skipped execution item. It does not invoke
Python, download or clone provider inputs, run upstream preflight, inspect a
package, or mutate provider state. Debug mode shows the exact helper command.
During a real run, the command wrapper owns the status display and suppresses
ordinary helper output. If provider apply fails, its last 20 output lines,
bounded to 16 KiB and stripped of terminal-control bytes, are copied into the
console report and retained installer log before temporary planning state is
removed. The same bounded diagnostic capture applies to pre-update runtime
recovery and final neutral activation failures.

Only when WsprryPi actually mutates an empty provider state and then verifies
the result does it atomically create the root-owned, mode-0600 v2 ownership
record at
`/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json`. A release record embeds
the validated release manifest and hashes every DKMS module artifact present at
ownership creation. Later DKMS artifacts for additional kernels are accepted
only at canonical module paths with the same recorded module version; every
originally recorded artifact must remain byte-identical.
A development record binds the exact source, kernel, installed module,
upstream evidence, rollback record, and captured upstream rollback entrypoint.
Failed provider installs and dry runs create no ownership record. A stale record
blocks a new provider installation rather than being overwritten.

Before package mutation or provider verification, and before replacing application
files or restarting the service, WsprryPi requests provider update preparation.
The same workflow applies whether the selected source is unchanged or newer.

The public `update-plan` / `update-execute` pair advances through bounded,
digest-reviewed steps. WsprryPi checks the versioned envelope, recorded identity,
canonical plan digest, and public postconditions, revalidating its own ownership
record before each call. It replaces application files only after `prepared`
proves the runtime deployment absent. Preparation never authorizes transmission.
The provider may restore the service intent captured before its deployment;
WsprryPi continues to own the subsequent application installation lifecycle.

The provider handles an exact selected idle route, initial neutral activation,
completed removal at later generations, supported interrupted recovery/removal,
and validated post-reboot retirement. A failure stops the installer and preserves
its recovery evidence. The installer does not automatically retry a failed effect;
a new invocation obtains a new plan. If inverse deployment removed the installed
helper before interruption, the selected compatible source can finish the exact
retained inverse bound to recorded deployment ownership. Unknown contracts,
unproven state, foreign artifacts, unsafe endpoints, or changed ownership fail
closed. Structured provider failures retain their diagnostic detail in the
installer's bounded error output.

After the application binary, configuration, service, and exact route companion
then exist, the installer builds the opt-in bundle from the same immutable source
commit and the exact DKMS-installed module pair. Binding version 3 records each
module's canonical path, installed-file digest, decompressed-ELF digest,
compression, version, kernel, and build-note digest. The bundle contains no
module payload and never writes `/lib/modules`. The installer independently
validates its binding and complete artifact inventory,
and calls the upstream runtime provider in this order: `inspect`, `plan`,
`ensure`, `inspect`, `activation-plan`, `activation-ensure`, and
`installation-status`.
Both mutation calls receive only the digest returned by the immediately reviewed
plan. The provider publishes an installation receipt only after exact neutral
readiness: administration eligible, execution unavailable, no route, owner or
lease, and output disabled. WsprryPi checks the receipt against its reviewed
bundle and deployment/activation digests.

The bundle must be self-contained for these pre-deployment calls: its bootstrap
set includes the route client imported by the provider and activation tools,
and the route manager imported by provider update execution. Validation checks
local imports inside functions as well as top-level imports. All standalone
helpers must match their digest-bound deployment payloads.
The route-manager socket and service units are digest-bound deployment payloads,
not assumed host prerequisites; the installed WsprryPi route companion and the
DKMS-owned module pair are external bound prerequisites. A stock
`Transmit Backend = gpio` configuration or an operator-selected
`Transmit Backend = si5351` configuration is valid for neutral inspection and
is not rewritten during installation. Only an explicit GPIO route selection
changes the backend to `rp1-gpclk`, selects its saved pin, and forces
transmission off.

Only after that proof is the provider record upgraded to v3 with the readiness
contract, binding and artifact-set digests, source commit,
product/kernel/compatibility identities, reviewed deployment and activation
plan digests, activation request ID, controller session and zero generation,
neutral state, null route, and disabled-output state. This records WsprryPi
orchestration; it does not transfer ownership of upstream files, journals,
modules, units, or systemd state.

The receipt records the original activation controller session and generation
zero. It is installation provenance, not an assertion that the live generation
must remain zero. Route selection and removal advance that generation; the
provider validates the resulting journal chain through its update interface.
Missing or changed evidence remains a refusal.

After neutral administration succeeds, the installer restores any explicit
saved RP1 route, finishes website and Apache publication and requires
`wsprrypi.service` to become active. It reloads
systemd after neutral activation removes any temporary inhibition drop-in, then
starts the service idempotently so cached unit conditions cannot suppress the
restored application before the route-restoration step. With web
mode enabled it also
requires the installed loopback `/wsprrypi/version` proxy endpoint to respond.
Passive neutral readiness accepts a later stable start, stop, or restart of the
bound application service. Strict activation completion still verifies its
captured restoration intent, and route preflight checks current application
state before restoring a saved route. An owned uninstall uses the same provider
update interface; an older installed provider must first be upgraded using a
compatible exact source.
A systemd start request that returns success but is skipped by a false unit
condition is an installation failure, so no success banner or configuration
URLs are printed. `--no-web` installations require only active service state.

The record proves only that this WsprryPi workflow installed the recorded
provider identity. It is not a signature, route selection, hardware or kernel
qualification, GPIO permission, transmission authority, or RF evidence. The
older v1 installation record did not distinguish a newly installed provider
from an idempotently accepted pre-existing provider, so it is deliberately
insufficient for automatic removal.

Provider installation and activation stop at route zero. After activation and
service startup, `runtime_reconcile.py install` reads the installed configuration.
An explicit `Transmit Backend = rp1-gpclk` with `Transmit Pin = 4` or `20`
restores that route using the installed provider's `route-plan` and `route-ensure`
commands. Setup requires disabled transmission, exact ownership and binding,
unchanged configuration and service policy, and final matching route evidence
with no owner or lease. A refusal fails setup and retains bounded failure details
in the installer log. A saved GPIO value for another backend does not select a
route. This installer action is separate from normal reboot reconciliation.

Completed reboot checkpoints are scoped to boot and installed binding; a
completed record from a replaced binding cannot suppress reconciliation. The
installer records a new completed checkpoint only after successful restoration.
Concurrent startup workers defer to the reconciliation already in progress.

When RP1 is not explicitly configured, setup remains at route zero. A later
explicit operator selection of GPIO4 or GPIO20 invokes upstream `route-plan`;
WsprryPi retains the reviewed digest
and calls `route-ensure` only for the same route and current preflight
generation. Only this route transaction changes the application backend to
`rp1-gpclk`, persists the selected pin, and keeps WsprryPi idle. The production
module uses the root-owned mode-0600 endpoint with `output_inhibit=0`; application
policy and operator confirmation still gate every transmission. A saved or
default GPIO value alone is not installation-time route consent. Because the provider
endpoint is route-owned, `/dev/rp1-gpclk` is normally absent at this neutral
stage. **Setup > Transmitter** keeps GPIO selectable as **GPIO (route required)**
and exposes the route panel so the operator can make the first explicit choice;
the missing endpoint still keeps transmission unavailable until that transaction
completes and runtime readiness is reconciled.

## Ownership-aware uninstall

Before WsprryPi application and service teardown, uninstall recovers an exact
v3 neutral runtime while the recorded application instance still exists. If
that pre-teardown recovery fails, uninstall stops before changing the
application. The exact route, activation, and deployment recovery sequence then
removes runtime residue and the owned provider while the captured application
service and companion still exist, allowing the provider to restore the state it
owns. Ordinary WsprryPi application and service teardown runs only after that
removal succeeds.
Missing, legacy, malformed, symlinked, insecure, foreign,
mixed, active, configured, enrolled, manager-bound, or identity-drifted state
is preserved with an operator-facing reason. A matching release is revalidated
against its complete recorded manifest and module-artifact hashes, then removed
with the normal Debian package lifecycle. A matching development installation
is removed only through the exact upstream `development-rollback` entrypoint
and rollback record captured during installation. WsprryPi never invents a
direct DKMS, module unload, overlay, route, boot, service, GPIO, or RF removal
sequence.

If runtime preparation or provider removal fails, uninstall stops before
application teardown. The provider and ownership record are preserved, the
overall uninstall fails, and no uninstall-success message is reported. Failures
in later independent application teardown remain cumulative: safe teardown
continues where possible, but the overall uninstall still fails.

For a release package, WsprryPi durably retains the exact ownership record in
an adjacent `.removal-pending` file before retiring the active record. The
provider's own read-only removal guard must then permit the package transition
before APT runs. A pending removal blocks new installation and runtime
deployment. On failure WsprryPi restores the active ownership record without
overwriting concurrent state. An interrupted invocation can restore its exact
authority on the next `remove` request; if package/DKMS state is already absent,
it verifies absence before retiring that evidence. Changed, foreign, or partial
state that cannot satisfy owning validation remains preserved for recovery.

Final ownership retirement requires successful canonical removal and verified
package, DKMS, consumer/controller module, source, overlay, route, enrollment,
and manager absence. A lifecycle failure fails the overall uninstall and leaves
recovery to the recorded owning procedure. Successfully rolled-back development evidence remains under
`/var/lib/wsprrypi` as uniquely named audit/recovery evidence and does not block
a later installation.

`REMOVE_RP1_GPCLK_DKMS` accepts exactly:

- `auto` (default): remove only a matching, inactive provider proven by the v2
  ownership record;
- `true`: explicitly request the same ownership-aware removal; it does not
  bypass any provenance, identity, inactivity, or upstream lifecycle check; or
- `false`: preserve the provider even when WsprryPi ownership is proven.

For example, to remove WsprryPi while deliberately retaining an owned provider:

```sh
sudo ACTION=uninstall REMOVE_RP1_GPCLK_DKMS=false ./scripts/install.sh
```

`DRY_RUN=true ACTION=uninstall` passes the planned helper argv through the
standard command wrapper, does not start Python, and does not inspect or mutate
provider or ownership state. Debug mode displays the exact `remove --debug`
argv. No failure selects a fallback backend.

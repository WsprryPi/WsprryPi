# RP1-GPIO-CLK-DKMS installation

WsprryPi installs the independently maintained
[`WsprryPi/RP1-GPIO-CLK-DKMS`](https://github.com/WsprryPi/RP1-GPIO-CLK-DKMS) provider.
The package and DKMS name remains `rp1-gpclk-dkms`; its kernel-module and UAPI
names are unchanged.
WsprryPi does not vendor its module source, reproduce its DKMS
lifecycle, edit boot configuration, or enable transmission. A source or release
that implements the reviewed runtime contract is first activated to route-neutral
controller and manager administration. Setup then restores an explicitly saved
`rp1-gpclk` GPIO4 or GPIO20 selection through the provider's managed route
transaction, with transmission disabled.

## Lifecycle ownership

WsprryPi authenticates the selected provider source, requests a public provider
plan, waits for its result, and validates its contract and postconditions.
RP1-GPIO-CLK-DKMS owns provider identity, version transitions, package/source operations,
runtime preparation, rollback, and removal. Its provider-owned receipt is
`/var/lib/rp1-gpclk-dkms/provider-installation.json`. The receipt identifies
WsprryPi as the invoking client. Application-owned provider receipts are
unsupported and block installation; they are never adopted or relabeled.

The consumer requires canonical UAPI hashes, runtime/lifecycle interfaces and
capabilities from `config/rp1-provider-requirements.json`. Product versions are
derived by the provider from its canonical header. Identical inputs are repeat
installations; changed inputs at the same version are replacements. Compatible
newer versions are upgrades. Downgrades require owning removal and reinstall.
The application does not parse provider Python files or pin one product version.

Provider effects use opaque, digest-bound plans. Unexpected receipt or plan
changes stop the operation. Errors preserve recovery inputs and stop application
teardown when provider cleanup has not completed. A successful managed uninstall
requires provider absence and disabled transmission postconditions.

Application file ownership is recorded separately. Uninstall retires unchanged
owned companions, generated default sidecars and backups, and completed startup
file receipts. Pre-existing or modified operator data, configured fleet records,
shared APT packages, and the shared `picotool` dependency are retained. Unknown
or modified application code is preserved and causes a reported cleanup failure.
Completed provider recovery evidence is retired by the provider after terminal removal verifies native absence. Unknown files, active locks or unresolved recovery stop cleanup and retain evidence.

## Startup integration

The selected application checkout installs `runtime_reconcile.py`, its
application-owned installer support, `wsprrypi-rp1-reconcile.service`, and the
`92-rp1-reconcile.conf` application drop-in through
`scripts/install_runtime_reconcile.py install`. The normal installer and
`scripts/copy_exe.py` invoke that helper. It records a fixed file inventory under
`/var/lib/wsprrypi/rp1-startup-files.json`, preserves foreign files, and permits
exact interrupted-installation retries. `remove` removes only that recorded
inventory; neither operation starts a service or selects a route.

Normal reboot recovery requires a provider supporting current post-reboot
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

- `auto` (default): select published `v1.0.0-rc.2`, independently of the
  WsprryPi branch;
- `release`: select the latest eligible published release, including prereleases;
- `release:TAG`: select an exact published release, including prereleases;
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

The installer reports the environment, build resources and Apache mode before running its normal APT dependency handler. These packages, including Git, are installed before RP1 source planning. RP1 planning and runtime recovery must then succeed before provider installation or application replacement. A planning failure can leave updated dependency packages installed; it does not replace the application or install the RP1 provider.

Examples:

```sh
sudo INSTALL_RP1_GPCLK_DKMS=false ./scripts/install.sh

sudo INSTALL_RP1_GPCLK_DKMS=true \
  RP1_GPCLK_DKMS_SOURCE=commit:0123456789abcdef0123456789abcdef01234567 \
  ./scripts/install.sh
```

## Published releases

An explicit release selector accepts `release:1.0.0-rc.1` or
`release:v1.0.0-rc.1`. Both select the published tag `v1.0.0-rc.1`.

Automatic selection uses `v1.0.0-rc.2`. Explicit `release` selection uses the
highest semantic version among eligible published releases, including prereleases,
from `WsprryPi/RP1-GPIO-CLK-DKMS`. The application authenticates the source and selects a
release; the provider verifies its manifest, checksums, package, compatibility,
and native installation identity. Failed selection or verification stops the
installation without falling back to development source.

Installer tooling is resolved once from the provider's maintained `devel` branch
and frozen at that exact commit. The selected package and runtime source remain
bound to their published release tag and manifest commit. Updating installer
tooling does not change the selected provider product version.

## Development sources

Development selection resolves a branch once to a complete commit. An explicit
local checkout must be clean, have the canonical repository origin, and have
attributable source history. The provider snapshots and revalidates its inputs,
builds for the exact running kernel, and retains the native removal authority.
A changed incoming payload can replace the same product version during testing.
Neither an unpublished commit nor a successful build is a release or hardware
qualification.

## Plan, record, and safety boundary

The public provider lifecycle owns its plans, installation receipt, retained
source and package recovery inputs, and native operations. WsprryPi preserves
the acknowledged opaque plan digest and rejects unexpected changes before the
next operation. A failed or interrupted effect stops the installer and preserves
its recovery inputs. Repeating installation revalidates actual ownership and
recoverable state before proceeding.

Only the provider compares incoming and installed versions. Identical inputs
repeat an installation, changed equal-precedence inputs replace it, and an
increasing compatible version upgrades it. Native package ordering is checked
independently. A downgrade requires explicit owning removal and reinstall.
Unknown or modified installed files remain protected regardless of the incoming
version.

Runtime activation is route-neutral. The application's saved route, if any,
is restored through the provider's managed route transaction with transmission
disabled. Successful installation does not authorize GPIO clocks, DMA, or RF.

## Ownership-aware uninstall

`REMOVE_RP1_GPCLK_DKMS=auto` removes the attributable provider installed for
WsprryPi; `true` explicitly requests owning removal; `false` preserves it.
Unknown, foreign, modified, or incomplete ownership stops required removal.
WsprryPi requests provider runtime preparation before application teardown and
requires verified provider absence afterward. It does not reconstruct a native
rollback or call DKMS directly.

Application-owned installed files have a separate receipt. Unchanged generated
sidecars, companions, caches, and fresh empty directories are retired. Existing
or modified operator data and configured fleet records are retained. Shared APT
packages and `picotool` remain available to other consumers. Modified application
code produces a cleanup failure. The provider retires its terminal journals, verified facade closures, predecessor recovery inputs, empty runtime directory and unused provider-created route group. Predecessor removal during upgrade retains recovery inputs. An authenticated current facade can remove the provider without another tooling fetch; older facades are verified before maintained removal tooling is fetched.

## RP1-GPIO-CLK-DKMS source selection

Use `auto` for the normal application channel, `release` for eligible published
artifacts, or an exact development selector for testing. The application requires
supported UAPI, lifecycle, and runtime interfaces rather than one provider
product version. Independent schema and protocol version identifiers remain
fixed compatibility contracts.

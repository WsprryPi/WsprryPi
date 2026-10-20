# Wsprry Pi Scripts

These scripts are used for install and development orchestration.

## Installer dry runs

Set `DRY_RUN=true` to inspect the selected install or uninstall workflow
without changing the checkout or system. Dry runs write no installer log,
temporary merge output, staged executable, installed configuration, package,
service, boot, web, GPIO, or provider state. Passing `debug` prints the planned
commands to the console with shell-safe quoting that preserves exact argument
boundaries. Successful dry runs end with an explicit notice that the selected
plan completed without applying changes; they do not report an installation or
uninstallation as completed.

## Low-memory build preflight

Before an approved installation changes packages, services, or compiled
artifacts, the installer records total and available RAM and inventories active
swap. Compressed RAM swap such as `/dev/zram0` is reported but is not counted as
independently backed capacity for the compile gate. Systems below 1.5 GiB of RAM
are forced to single-job compilation. They must also have this much *free*
file- or partition-backed swap:

- below 768 MiB RAM: 2 GiB
- from 768 MiB up to, but not including, 1.5 GiB RAM: 1 GiB

`ALLOW_TEMP_BUILD_SWAP` defaults to `auto`. When compilation is required on a
system below 1.5 GiB RAM and existing independently backed swap is insufficient,
the installer logs the automatic selection and creates invocation-owned
temporary swap. At or above 1.5 GiB, or when existing swap meets the target, no
temporary swap is created. These thresholds are build-resource policy, not a
guarantee that every build will succeed under other memory pressure.

Temporary swap can slow compilation and increase disk or flash writes. To
prevent its creation, explicitly opt out:

```sh
sudo env ALLOW_TEMP_BUILD_SWAP=false ./scripts/install.sh
```

With `false`, insufficient existing swap stops installation before packages,
services, or compiled artifacts are changed. `true` remains supported as
explicit permission to use temporary swap under the same thresholds; it does
not force allocation on larger systems or when swap is already sufficient:

```sh
sudo env ALLOW_TEMP_BUILD_SWAP=true ./scripts/install.sh
```

Only `auto`, `true`, and `false` are accepted when build-resource checks run.
Precompiled application installation skips these checks unless RP1 GPCLK DKMS
installation is selected, in which case provider compilation still needs them.

The installer allocates only the shortfall plus a small allowance for kernel
swap metadata, rounded up to 1 MiB, and requires another 512 MiB of free storage
as a safety reserve. It creates a
mode-0600 file with a unique name beneath the protected
`/var/lib/wsprrypi/build-swap` directory, preserves every pre-existing swap
device and file, and removes only its own swap when the installer exits. The
same invocation can repeat the preflight without creating a second file.

If disabling the temporary swap fails, the installer returns failure and
preserves the active file instead of risking its removal. The log gives the
exact path and recovery command; run that command only after confirming the
named file is the installer-owned path. `DRY_RUN=true` reports the planned size
and lifecycle without creating or enabling swap. Insufficient storage or a
failure to create or enable the temporary swap stops installation; the installer
does not proceed past a failed resource preflight.

## RP1-GPCLK-DKMS provider

`install.sh` uses `rp1_gpclk_dkms_install.py` to resolve and validate the
independently owned provider before package mutation. Automatic Pi 5/CM5,
explicit override/opt-out, published release, immutable development source,
digest-reviewed neutral runtime deployment/activation, deferred explicit
GPIO4/GPIO20 route selection, dry-run, exact-owned development no-op,
foreign-state refusal, and
ownership-aware uninstall behavior are
documented in [RP1-GPCLK-DKMS installation](../docs/rp1-gpclk-dkms-installation.md).
Dry-run uses the installer's standard command wrapper and never starts the
Python helper. Passing `debug` displays the safely quoted helper command. The
wrapper suppresses ordinary child stdout and stderr and owns all status and
failure diagnostics. Provider apply, pre-application-update neutral recovery,
and final neutral activation each retain a sanitized, bounded tail of child
output in the installer log when that command fails. On a repeat installation,
an exact owned selected route is first recovered through its bound runtime
route client. The resulting neutral controller is then digest-reviewed and
recovered to an inhibited, inactive activation-required state before provider
verification, package mutation, or application files and services are changed.
That exact inactive runtime deployment is removed through its reviewed removal
plan while application inhibition is retained. The exact provider is
subsequently verified and neutral administration is freshly activated after
the updated application is in place. If the removal process deletes the
installed provider before returning a structured failure, the same installer
invocation enters the digest-bound interrupted-removal recovery path; it does
not leave a repeat invocation to publish provider-only ownership. Structured
provider failures retain their error detail, conflicts, and remediation in the
bounded installer diagnostic. A different
selected provider source bypasses this same-source step and remains subject to
its predecessor-owned migration workflow. If route recovery crosses a boot
boundary, exact prior-boot journals are retired and the runtime deployment is
removed before the fresh activation so unchanged files cannot bypass the
required inhibition transaction. An interrupted removal is resumed only from
the pending plan digest recorded in WsprryPi ownership.
On a system not positively identified as Pi 5/CM5, automatic selection skips
the helper and all RP1/DKMS planning, apply, recovery, and activation status
lines. An explicit `INSTALL_RP1_GPCLK_DKMS=true` still reaches the helper's
fail-closed platform validation; `false` suppresses provider work everywhere.

Real installation success requires the resulting WsprryPi service to be active.
When web mode is enabled, Apache REST and WebSocket targets are rendered from
the effective installed `[Operation]` `Web Port` and `Socket Port`, including
valid values preserved from an earlier INI. The configured direct `/version`
endpoint and installed loopback `/wsprrypi/version` proxy must both respond. A
condition-skipped systemd start is not application readiness, and diagnostics
distinguish a service listener failure from an Apache proxy failure.

The canonical INI includes the optional `RP1 Drive mA = 2` setting. Upgrades
build the active file from that current schema, so configurations created
before the RP1 setting inherit the default without a startup warning. Explicit
operator values, including values that must still be rejected or repaired,
remain preserved for normal validation.

Run its offline hardware-free contract coverage with:

```sh
python3 scripts/tests/rp1_gpclk_dkms_install_test.py
python3 scripts/tests/installer_dry_run_purity_test.py
```

## Support-bundle collector

### Support-route guard

`SupportRequestGuard` protects support-bundle routes and other privileged HTTP
operations using the current eligible interface subnets, while continuing to
validate local Host/Origin identities. Apache overwrites the dedicated
`X-WsprryPi-Client-Address` header with its connection peer; the backend trusts
that identity only from an actual loopback proxy peer. Direct clients use their
socket peer, and generic forwarding headers are deliberately ignored. This is
network-location access control, not user authentication.

`collect-support-bundle.sh` retains its interactive behavior by creating a
bundle in the current directory. A trusted future backend may instead invoke
it with `--output-dir /absolute/private/directory`. The directory must already
exist, be writable/searchable by the caller, not be a symlink, and not be
group- or world-writable. The collector selects the archive filename and will
not overwrite an existing archive, checksum, or result.

The collector writes a private `<archive>.result.json` alongside the archive
and `.sha256` sidecar. Its stable schema records success or failure, artifact
filenames and SHA-256 digest, generation time, requested collection options,
I2C probe state, and whether unprivileged execution may have omitted privileged
diagnostics. The JSON and console output intentionally contain no configuration
secrets.

Every bundle also contains a point-in-time process/resource snapshot: a wide
system process list, process tree, systemd cgroup view when available, and
WsprryPi `/proc` evidence resolved from the service's systemd `MainPID`. The
summary reports current RSS, virtual size, PSS when readable, threads, tasks,
and open-file-descriptor count. Unavailable, stopped, permission, and process
race states are labeled explicitly rather than reported as zero. This snapshot
does not provide historical resource monitoring.

On systems using the external RP1-GPCLK-DKMS provider, the bundle includes
passive, kernel-specific DKMS and `modinfo` reports for both modules, loaded
module state, running-kernel header availability, WsprryPi installation-record
presence, and the runtime provider's read-only `inspect` result. Missing tools,
files, permissions, and nonzero inspection results are retained as diagnostic
evidence. The collector executes the provider only when it is a root-owned,
non-symlink regular file without group or world write access, and bounds that
inspection to 30 seconds. The ownership record itself is not copied, and these
reports do not load modules, change routes, authorize output, or establish
qualification.

I2C bus scanning is opt-in: `--probe-i2c` permits only `i2cdetect -y 1`.
Without it, the collector gathers passive I2C details but records that active
probing was skipped. Running as a non-root user is supported; restricted system
diagnostics are reflected in the result artifact.

Run the focused no-hardware regression coverage with:

```sh
bash scripts/tests/collect-support-bundle_test.sh
```

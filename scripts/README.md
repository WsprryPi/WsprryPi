# Wsprry Pi Scripts

These scripts are used for install and development orchestration.

## PHP2Shell configuration audit and partial mitigations

`php2shell-mitigate.py` checks Debian/Raspberry Pi OS web PHP configurations under `/etc/php/<version>/{apache2,fpm,cgi}` for the partial mitigations in the [PHP security advisory of 21 October 2026](https://www.php.net/archive/2026.php#2026-10-21-1). It requires Python 3.8 or later and is separate from the installer. It does not run PHP, send web requests, update packages, restart services, or operate WsprryPi or transmitter hardware.

**Install the PHP security updates from your distribution. Configuration changes alone do not fix PHP2Shell.** The upstream fixes are PHP 8.2.35, 8.3.36, 8.4.27, and 8.5.12. Distribution backports can retain an older upstream version; the script reports these as requiring vendor verification for both CVE-2026-109618 and CVE-2026-109619 instead of declaring them vulnerable or fixed. Package metadata does not establish which PHP binary the web server uses or whether running workers have loaded an update.

The script detects OS identity, version and codename from `/etc/os-release`, falling back to `/usr/lib/os-release` when needed, without executing either file. Its release policy was reviewed on 21 October 2026: Debian/Raspbian Bookworm and Trixie use the Debian support baseline through 30 June 2028 and 30 June 2030 respectively. [Bookworm support](https://www.debian.org/releases/bookworm/) and [Trixie support](https://www.debian.org/releases/trixie/) remain subject to vendor and architecture coverage. Bullseye and earlier are clearly labeled **UNSUPPORTED RELEASE** under ordinary Debian security/LTS coverage; Bullseye LTS ended 31 August 2026, and separately maintained [ELTS packages](https://www.debian.org/releases/bullseye/) require vendor verification. Unknown distributions, future releases, or conflicting version/codename information are labeled **UNSUPPORTED BY THIS SCRIPT**, which does not claim that the vendor has ended support. PHP branches outside their recorded upstream support dates are separately flagged; vendor support may differ. These warnings do not prevent explicitly requested partial INI mitigations on a compatible Linux configuration tree.

On a live Linux host, read-only `dpkg-query`, `apt-mark showhold`, and `apt-cache policy` queries identify installed PHP source packages, their full package versions, holds, and APT candidates for the host's configured repositories and architecture. Common, CLI, and extension packages built from the same PHP source are included, alongside Apache/FPM/CGI packages. Candidate versions are compared with the upstream fixed release for their own branch; prereleases, unrecognized branches and older possible vendor backports remain unverified. The script disables APT's persistent package-cache output and never runs `apt update`, downloads packages, or contacts a package repository. Candidates reflect existing local indexes, so stale indexes, missing security sources, pinning and mirror lag must still be considered.

The report prints `sudo apt update` with an instruction to rerun the audit afterward. For a recognized supported release, when an update is needed and every installed package in that PHP source branch has a verified patched candidate with known, non-blocking hold state, it prints the exact installed package names in both a dependency simulation and a targeted upgrade command. For example:

```sh
apt-get --simulate install --only-upgrade --no-remove libapache2-mod-php8.2 php8.2-cli php8.2-common
sudo apt-get install --only-upgrade --no-remove libapache2-mod-php8.2 php8.2-cli php8.2-common
```

The actual command lists all detected packages from that PHP source branch. Review the simulation before upgrading; dependency resolution, downloaded package availability, and unchanged APT policy are not established by a cached candidate check. The commands use the candidates current when run instead of pinning a potentially stale version. Unsupported/unrecognized releases, unverified candidate sets, or blocking/unknown holds suppress the upgrade command and produce a review finding. The script never overrides a hold or proposes an automatic major OS/PHP branch migration. It still prints package-policy inspection commands, and all commands in the report are instructions only, including when `--apply` is selected.

Audit without making changes, from the repository root:

```sh
python3 scripts/php2shell-mitigate.py
```

Apply the ordinary partial mitigations on the Pi:

```sh
sudo python3 scripts/php2shell-mitigate.py --apply
```

This creates `/etc/php/<version>/<sapi>/conf.d/99-wsprrypi-php2shell.ini` for every discovered web SAPI. It disables session upload progress, caps `post_max_size` at 8 MiB, keeps `upload_max_filesize` below that limit, and disables `fatal_error_backtraces` on PHP 8.5. Smaller positive request limits are preserved. Body/upload limits must use canonical decimal bytes or K/M/G suffixes; inspection refuses leading-zero/octal quantities, environment substitutions and expressions it cannot safely preserve. Other PHP settings, including shell functions used by the existing log viewer, remain unchanged. The 8 MiB profile is intended for WsprryPi; review compatibility before using it on a shared server with applications that accept larger uploads. A CGI configuration tree must have the same Debian `php.ini` and `conf.d` layout; custom launchers need separate review.

PHP also recommends `memory_limit=-1` as a temporary way to avoid the PHP memory-limit failure path. Removing that guard can exhaust Pi memory. This change requires a separate explicit option:

```sh
sudo python3 scripts/php2shell-mitigate.py --apply --allow-unlimited-memory
```

Use this option only with an appropriate worker/container/cgroup availability boundary. OS allocation failure can still reach the vulnerable code. `RLIMIT_AS` and `RLIMIT_DATA` are unsuitable substitutes. INI settings can be overridden by applications, pools, or directory configuration; review `php_admin_value` settings with the server administrator when locking `memory_limit` is needed. The script inventories possible overrides rather than changing them automatically. A later apply without the memory option preserves a previous explicit opt-in in the managed file.

`--sapi apache2`, `--sapi fpm`, or `--sapi cgi` limits the selection; repeat the option to select several. Without it, installed configuration trees are audited even if they are inactive. `--root /absolute/offline/root` supports read-only fixture or image audits; apply mode requires root on a live Linux filesystem. `/var/www` is inspected for `.htaccess` and `.user.ini`; repeat `--web-root /absolute/custom/document/root` for additional web roots. Apache/nginx configuration and selected FPM pool directories are inventoried for PHP overrides and possible pre-request diagnostic sources, including settings in inactive files. Findings show file and line locations without exposing their values. Fix invalid/deprecated per-request settings after reviewing the [stale-state advisory](https://github.com/php/php-src/security/advisories/GHSA-6wmf-4rv5-p3rp); hiding diagnostics does not mitigate that path.

The audit reads `php.ini` and alphabetically ordered `conf.d/*.ini`, reports relevant scoped INI settings, and checks again after applying. Server scanning covers `.conf`/`.load` files, standard FastCGI/SCGI/uWSGI parameter files, and extensionless site files in `sites-available`/`sites-enabled`; unrelated keys, certificates and binary files are not opened. It does not fully resolve web-server includes or custom configuration filenames, alternate `PHP_INI_SCAN_DIR`/`PHPRC` paths, custom per-directory INI filenames, application `ini_set()` calls, or active virtual hosts. It reports directory symlinks it does not traverse. Inspection fails on unreadable, non-regular, non-UTF-8, or larger-than-2-MiB selected configuration files rather than silently skipping them. Validate the actual serving SAPI and effective request settings separately. No public `phpinfo()` or diagnostic endpoint is created.

Apply mode refuses foreign files at its managed filename, symlinked write paths, hardlinked managed files, and untrusted directory/file ownership or write permissions. Every destination is preflighted before writing. Replacing an owned snippet makes an exact, mode-0600 `.bak-<UTC timestamp>-<unique suffix>` copy beside it; unchanged files are not rewritten. Writes are atomic per file, not transactional across all PHP trees. If a later write fails, the output identifies the files already written. Existing `php.ini`, package snippets, Apache sites, FPM pools, and application files are preserved.

After applying, review the reported conflicts, install vendor updates, and arrange a **full restart of every serving PHP worker**. For discovered Apache module trees the report prints `sudo systemctl restart apache2.service` once; for each FPM tree it prints the versioned command, such as `sudo systemctl restart php8.2-fpm.service`. These commands are conditional on that SAPI actually serving the site: installed configuration trees can be inactive, and the script does not query or operate systemd. For CGI it reports that a persistent FastCGI supervisor must be identified separately; one-shot CGI has no persistent PHP worker. A graceful reload can leave old workers alive. Confirm the actual web SAPI, patch level, effective settings, and WsprryPi web/log workflow after the restart. The request-size settings do not configure web-server/proxy decoded-body limits, field/part/header/nesting limits, cgroups, or application error handling; review those separately using the [OOM advisory](https://github.com/php/php-src/security/advisories/GHSA-w8hw-w9fx-j79w).

To undo a first apply, remove only the exact managed snippet paths reported by the script. To undo a replacement, restore the corresponding reported backup to its original snippet path with root ownership and mode 0644. Review the restored settings, arrange the same worker restart, and audit again. Removing a snippet restores the underlying configuration, which may be less secure; keep vendor PHP updates installed.

Exit status `0` means the static checks found no outstanding findings, `1` means review/update/mitigation work remains, and `2` means inspection or application failed. Unsupported OS/PHP coverage, unknown package/hold metadata, unverified installed fixes or candidate sets, and configuration conflicts produce status `1`. A successful apply can also return `1` when unlimited memory was not selected. Even status `0` does not establish runtime protection or replace updates.

Run the offline fixture tests from `src`:

```sh
make php2shell-test SUDO=
```

When ShellCheck is installed, the suite also checks the generated shell commands without executing them. Otherwise that check is reported as skipped; the remaining Python tests still run.

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

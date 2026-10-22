# Installer command reporting

`scripts/install.sh` and its sourced support-bundle provisioning helpers use
the same command-reporting policy.
It distinguishes installation operations from probes, captured data, temporary
rendering, and housekeeping. It is a source and hardware-free test review, not
evidence of a successful installation on a Raspberry Pi.

## Wrapped operations

These operations use the existing `exec_command` wrapper:

| Area | Operations |
| --- | --- |
| Application executable | Stage the executable, back up the old executable, publish the replacement at its full destination path, stop for rollback, restore or remove a failed replacement. |
| Precompiled input | Set staging permissions, copy the supplied executable, and set executable permissions. |
| Route runtime | Create its installed directory, install the application companion, and invoke runtime-reconciliation installation or removal. |
| RP1 helper preparation | Protect staging directories, copy or download the selected helper, and set its permissions. |
| Temporary build swap | Create its directory, protect and allocate the file, format and enable swap, then disable swap and remove the file. |
| Configuration | Replace template fields and log directives, publish merged or fallback INI output, back up/stage/publish Apache proxy changes, change the legacy audio blacklist, and write the I2C fallback boot setting. |

The normal executable destination is `/usr/local/bin/wsprrypi`. Its publication
reports `Complete: Install executable at /usr/local/bin/wsprrypi.` only
after the rename succeeds. A failed rename reports failure, retains the prior
executable, and follows the existing service-recovery path.

The wrapper still controls status presentation, child-output suppression, and
dry-run execution. Logging destinations and bounded failure-detail capture remain part of the
installer contract.

After installation confirmation and build-resource checks, the installer reports the environment and Apache mode, then runs the existing APT handler before RP1 planning or source preparation. This installs Git before either operation needs it. Planning failures include the same bounded helper diagnostics as provider application failures. Source builds clone the selected repository after package installation. Support-bundle encryption validation loads its helper after this checkout exists and completes before RP1 provider application or WsprryPi application installation. Precompiled inputs reuse the checkout prepared during artifact validation; any additional artifact-specific runtime packages use the same APT handler without repeating index refresh or dependency repair. Dry runs neither install packages, clone sources nor load the encryption-validation helper.

## Direct calls retained deliberately

- **Read-only probes and captured data:** Git metadata, package and service
  status, filesystem metadata, OS/hardware identity, connectivity probes, and
  runtime-readiness queries feed control flow or captured values. The wrapper
  suppresses child output and skips execution in dry runs, so mechanically
  wrapping these would change their contracts.
- **Precompiled helper results:** `run_precompiled_helper` captures package or
  version output separately and sends failures through `logE`. Its output must
  not contain command-wrapper progress messages.
- **Temporary allocation and rendering:** `mktemp` returns paths; `mawk`,
  `awk`, and `printf` render INI/proxy content into temporary files. Their
  destination-file publication is wrapped. Wrapping a command and redirecting
  the wrapper itself into the configuration would corrupt the file with status
  messages; the audio/I2C changes instead pass input to wrapped `tee` commands.
- **Housekeeping and recovery-file retention:** removal of owned helper
  directories, staging files, successful-run executable backups, incomplete
  service/proxy renders, and empty temporary-swap directories remains direct.
  The active-swap and failed-executable-recovery retention gates are preserved.
- **Logging bootstrap and internals:** log-file creation/ownership, log output,
  and the wrapper's own failure-capture file creation cannot recursively invoke
  the wrapper that depends on them.

Existing package installation, compilation, normal service management, UI
publication, support-bundle provisioning, and RP1 provider operations already
use the wrapper. Python implementation details behind wrapped helpers were not
converted into separate shell progress steps.

## Validation and documentation boundary

Relevant existing suites cover precompiled installation and recovery, installer
dry-run purity, temporary-build-swap ownership and failure cleanup, route
companion installation, service recovery, INI migration, Apache proxy settings,
and RP1 installer contracts. Added coverage checks real wrapper output for
executable publication and rollback, failed-publication reporting, configuration
file integrity, and I2C write failure without a success or reboot claim.

`scripts/tests/installer_bootstrap_test.py`, included in `installer-dependency-test`, streams the installer into Bash with no existing checkout and a fixture PATH containing no Git. The real APT handler invokes mocked package-manager operations that make Git available before planning and source preparation. Filesystem fixtures cover encryption-tool validation, failure propagation and diagnostics, precompiled runtime dependencies and checkout reuse, existing-checkout installation, and dry runs without packages, services, or hardware access.

Relevant hardware-free entry points, run from `src` on a supported Linux host:

```sh
bash ../scripts/tests/installer_dependency_test.sh
python3 ../scripts/tests/rp1_gpclk_dkms_install_test.py
python3 ../scripts/tests/route_application_test.py
bash ../scripts/tests/apache_proxy_config_test.sh
```

Inspect each suite before running it. Mocked dependencies and commands exercise
failure reporting and recovery without establishing live installation success.

See the [precompiled-installation contract](precompiled-installation.md) for
installation semantics and the [operator manual](https://wsprry-pi.readthedocs.io/en/devel/Install/index.html)
for the installation workflow and console transcript. Source checks do not
establish installation, service, GPIO, swap or RF correctness on a target.

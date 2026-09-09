# Installer command-wrapper audit

This audit covers the shell command sites in `scripts/install.sh` and checks
the sourced support-bundle provisioning helpers for the same reporting pattern.
It distinguishes installation operations from probes, captured data, temporary
rendering, and housekeeping. It is a source and hardware-free test review, not
evidence of a successful installation on a Raspberry Pi.

## Corrected reporting gaps

These operations now use the existing `exec_command` wrapper:

| Area | Operations |
| --- | --- |
| Application executable | Stage the executable, back up the old executable, publish the replacement at its full destination path, stop for rollback, restore or remove a failed replacement. |
| Precompiled input | Set staging permissions, copy the supplied executable, and set executable permissions. |
| Route runtime | Create its installed directory, install the application companion, and invoke runtime-reconciliation installation or removal. |
| RP1 helper preparation | Protect staging directories, copy or download the selected helper, and set its permissions. |
| Temporary build swap | Create its directory, protect and allocate the file, format and enable swap, then disable swap and remove the file. |
| Configuration | Replace template fields and log directives, publish merged or fallback INI output, back up/stage/publish Apache proxy changes, change the legacy audio blacklist, and write the I2C fallback boot setting. |

The normal executable destination is `/usr/local/bin/wsprrypi`. Its publication
now reports `Complete: Install executable at /usr/local/bin/wsprrypi.` only
after the rename succeeds. A failed rename reports failure, retains the prior
executable, and follows the existing service-recovery path.

The wrapper still controls status presentation, child-output suppression, and
dry-run execution. This change does not redesign its logging destinations or
the existing bounded failure-detail capture mechanism.

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

The following commands passed from `src` in the AArch64 Trixie build container,
running as UID/GID 65534 with networking disabled, the repository and container
root read-only, and executable temporary filesystems for mock command fixtures:

```sh
bash ../scripts/tests/installer_dependency_test.sh
python3 ../scripts/tests/rp1_gpclk_dkms_install_test.py
python3 ../scripts/tests/route_application_test.py
bash ../scripts/tests/apache_proxy_config_test.sh
```

The dependency suite includes 14 precompiled-binary tests, the precompiled
installer shell tests, INI schema migration checks, 37 dry-run tests, 13 service
recovery tests, and the mocked temporary-swap suite. The RP1 and route suites
passed 98 and 9 tests, respectively. Expected injected errors remain part of
the passing failure-path coverage. The first container attempt could not
execute its mock command files on the temporary filesystem; the rerun used
explicit executable temporary mounts. Fixture updates initialize wrapper
presentation state and use the real wrapper where commands were previously
invoked directly.

Host checks also passed from the repository root, using Homebrew Bash and
ShellCheck:

```sh
/opt/homebrew/bin/bash -n scripts/install.sh scripts/tests/precompiled_installer_test.sh scripts/tests/build_resource_preflight_test.sh
/opt/homebrew/bin/shellcheck -x -P SCRIPTDIR scripts/install.sh scripts/tests/precompiled_installer_test.sh scripts/tests/build_resource_preflight_test.sh
git diff --check
```

`docs/precompiled-installation.md` retains the correct command and installation
semantics. The separate operator documentation at
`Wsprry_Pi_Docs/docs/Install/index.md` contains an older example transcript,
including `Make app executable`; it should be refreshed to show current
executable staging, backup, and destination publication messages. That
cross-repository edit is outside this change. No UI assets or screenshots
were changed, and no live installer, services, GPIO, swap, or RF were exercised.

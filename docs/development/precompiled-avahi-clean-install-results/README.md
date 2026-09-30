# Precompiled Avahi compatibility and clean installation

Date: 2026-09-30. Branch: `devel`.

**Status: correction, automated validation, clean-card precompiled installation,
repeat installation, post-reboot admission/discovery and final adversarial
assessment passed on wspr4.**

The [execution prompt](../precompiled-avahi-clean-install-prompt.md) defines
the approved work. The
[previous installed-service campaign](../wtp-installed-service-results/README.md)
records the original precompiled rejection and the separate successful source
upgrade. That historical failure is retained.

## Correction

The full ELF inspection now accepts these exact runtime library names:

| SONAME | Debian runtime package |
| --- | --- |
| `libavahi-client.so.3` | `libavahi-client3` |
| `libavahi-common.so.3` | `libavahi-common3` |

The existing installer runtime dependency group already includes
`libavahi-client3`, which depends on `libavahi-common3`. Its package selection
did not require a production change. Package ownership was confirmed on the
original Trixie arm64 wspr4 before shutdown. Unknown SONAMEs, ELF/ABI and loader
checks, runtime symbol validation, non-root execution and publication/recovery
behavior retain their existing control paths.

No application, UI or `src/` component source was changed.

## Off-device preservation and shutdown

Before replacing the card, the managed application was stopped and retained
data was copied to the private, physically local Mac directory:

`/Users/lbussy/wspr4-preservation/2026-09-30-clean-install`

The copy includes complete home/root trees, eight repositories with their Git
indexes and working files, retained builds and qualification evidence, system
configuration, local software, boot/module assets, application state, web files,
backups and logs. Sensitive inventories, host configuration and archives stay
outside Git.

Both transferred archive SHA-256 values matched the source; gzip integrity
checks passed. All **37,931 critical entries**, including **33,967 regular
files**, matched their source manifest, including contents, file types,
owner/group/mode and symlink targets. Both archives were readable, containing
51,575 members in total. The private `verification.json` records these checks.

The authorized shutdown command succeeded. wspr4 subsequently stopped resolving
through mDNS, and SSH to its previous address `192.168.1.68` timed out. Its
previous wireless network was **Bohica**. The user inserted and booted the
replacement card. No previous host configuration or application state was
restored before capturing the clean baseline.

## Automated validation

Before the production correction, the expanded Python regression suite failed
with the original Avahi rejections (16 subcase errors and one failed diagnostic
assertion). After correction and review repairs:

| Command/check | Result |
| --- | --- |
| `make installer-dependency-test SUDO=` from `src`, with Homebrew Bash 5.3.20 | PASS |
| `make installer-dependency-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` from `src` on wspr4 | PASS |
| Precompiled helper Python suite | 15 tests passed; armv6/aarch64 and Bookworm/Trixie subcases covered |
| Precompiled installer fixture | PASS; both OS runtime package sets, Avahi runtime inclusion and development-package exclusion |
| INI upgrade schema fixture | PASS |
| Installer dry-run purity suite | 37 tests passed |
| Service install recovery suite | 13 tests passed |
| Build resource preflight fixtures and dependency contract checks | PASS |
| Python AST parsing of changed Python files | PASS |
| `bash -n scripts/tests/precompiled_installer_test.sh` | PASS |
| `git diff --check` | PASS |

These fixture tests do not install packages or operate actual system services.
The suite's deliberately injected swap/runtime-validation failures are expected
negative cases. They are not failed host operations.

The complete final fixture output is retained in
[source-tests.log](source-tests.log) and
[linux-source-tests.log](linux-source-tests.log). The Linux fixture invocation
explicitly selected a profile that needs no development headers; it did not
compile the application or alter the installed native backend selection. This
is installer coverage, not a full Linux semantics result.

## Independently prepared native candidate

The native release candidate was compiled on wspr5 in
`/home/pi/precompiled-avahi-build.Ilbhvf`, using unchanged production source at
`2f67b0003f055bb4a7cc7c72fdf71a2b9c4cab19`. It reports
`3.2.0-devel+2f67b00`, compiled backends
`rpi-gpio,rp1-gpclk,si5351,simulated,wtp`, with ancillary GPIO enabled.
Its SHA-256 is
`65d7ccb3da27b0255d94714bcca80b6f21fe8faf7ef1212308a9d37d67081d19`.
The transferred Mac candidate matched that hash.

The default release profile used two build jobs and the Makefile's warnings as
errors. Avahi development/runtime packages were downloaded and unpacked inside
the private build directory; no packages were installed on wspr5. Build options
provided the private header and library paths explicitly:

```sh
make -j2 release SUDO= AVAHI_AVAILABLE=1 \
  AVAHI_CFLAGS="-I$stage/dependencies/usr/include -DWSPRRYPI_HAVE_AVAHI" \
  AVAHI_LIBS="-L$stage/dependencies/usr/lib/aarch64-linux-gnu -lavahi-common -lavahi-client"
```

Compilation passed. The initial collection step referenced `bin/wsprrypi`
instead of `build/bin/wsprrypi`; artifact collection was corrected before any
target installation. A shallow checkout initially produced the fallback
version `0.0.0`; fetching full tag history and rebuilding version metadata
produced the correct version above. The accepted candidate is the latter build.
The [build log](build.log), [version rebuild](version-rebuild.log),
[compiler](compiler.txt), [ELF inspection](elf.txt),
[staged package metadata](staged-package-info.txt) and
[version output](version.txt) are retained.

The [real ELF comparison](validator-comparison.json) confirms the original
validator rejects both Avahi SONAMEs with exit status 1, and the corrected
validator accepts the same candidate with exit status 0. This inspection did
not execute a transmission. wspr5's installed binary hash, complete package
inventory and service state matched their pre-build snapshots; its managed
service remained active with PID 1966 and zero main exit status.

The corrected installer checkout staged on the Mac has the same base HEAD plus
only the authorized helper/test/reference changes. Its
[tracked file hashes](installer-source-files.json) and
[source diff](installer-source-diff.json) bind the live installation to the
reviewed bytes. The binary was compiled from the unchanged production baseline,
not from a future installer-only commit.

The diff record preserves the exact unified patch as a JSON string, with its
SHA-256; decoding that field reproduces the original bytes. This keeps required
blank context lines from becoming whitespace errors in the evidence file.

## Adversarial assessment

The first source assessment found two coverage weaknesses in the new tests:
the installer package fixture exercised only Bookworm's optional runtime set,
and the exact unknown-library diagnostic assertion was outside its subtest
context. The fixture now exercises both Bookworm and Trixie sets; each rejected
library diagnostic remains within its architecture/release subtest so one
regression does not suppress the remaining cases.

The second source assessment found no actionable production issue: the change
adds only the two exact SONAME mappings; unsupported Avahi ABI names and other
unknown libraries remain rejected. The complete installer regression suite
passed again after both test repairs. Candidate preparation issues were
corrected and reassessed before accepting its version, hash and actual Avahi
linkage.

Live review identified an evidence-collector incompatibility: Python's default
strict INI parser rejected the canonical repeated `Band GPIO` sections. The
collector was repaired to merge repeated sections, consistent with the existing
configuration contract, and all snapshots were collected again. It did not
rewrite production configuration. The initial stock-GPIO WTP connection was
refused for the already documented unsupported server route; the explicit
Si5351 configuration subsequently passed admission and discovery. No fallback
backend or gate bypass was introduced.

The final assessment reviewed production code, the complete supported
architecture/OS regression matrix, native ELF rejection/acceptance, package
deltas, source identity, markers, installed state, configuration/file retention,
protocol ownership cleanup, independent DNS-SD resolution and evidence scope.
No actionable finding remains in this correction and acceptance scope. Final
Python syntax and whitespace checks passed; the complete Linux installer suite
passed after the collector repair.

The final artifact review also checked the commit boundary: captured `.log`
files are normally ignored, so the six reviewed evidence logs are explicitly
included. Documentation links are checked against staged or existing tracked
files, not only files available in the working directory. No private backup or
executable is included.
Compiler/readelf output and installer logs had trailing whitespace or blank
lines at EOF; the committed copies normalize only that whitespace. The
[normalization record](evidence-text-normalization.json) preserves original and
committed hashes and confirms unchanged non-whitespace content. Raw originals
remain in the retained Pi stages. Staged whitespace and link checks passed
after these artifact repairs.

## Clean-card live acceptance

The [clean baseline](clean-baseline.json) was captured before the installer
checkout or executable was copied. It records Raspberry Pi OS Lite reference
2026-09-15 (`pi-gen` stage2), Debian Trixie 13.7, AArch64, kernel
`6.18.50+rpt-rpi-v8`, on a Raspberry Pi 4 Model B Rev 1.1. All checked
application paths were absent, and `wsprrypi.service` was not found. Compiler
tools were already present in this operator-prepared OS baseline; application
development headers and `libavahi-client3` were absent. This qualifies a fresh
WsprryPi installation on that image, not an assertion that its package set was
an untouched factory image.

The replacement card uses **Bohica**, address **192.168.1.120**, through
**wlan1**. SSH connected using existing host-key verification. The source
checkout was streamed through gzip directly to wspr4. GNU tar warned about
Mac-only provenance attributes; all 1,352 tracked file hashes and the standalone
binary hash matched before execution, as recorded in
[transfer-verification.json](transfer-verification.json).

Both installations used the supported normal command, with `TERM=xterm` and
`REPO_BRANCH=devel` passed explicitly. Each runner removed the old invoking-user
home marker and created `/home/pi/finished` only after success:

```sh
rm -f ~/finished
sudo env TERM=xterm REPO_BRANCH=devel ./scripts/install.sh \
  --binary-source local --binary-path /home/pi/wsprrypi && touch ~/finished
```

| Live case | Result and evidence |
| --- | --- |
| Fresh precompiled installation | PASS; exit 0, new home marker, exact executable hash/version, enabled/running managed service, successful non-root library resolution and Apache configuration. [Installer log](fresh-install.log), [installed state](after-fresh-install.json). |
| Dependency selection | PASS; 23 runtime/web packages added, including `libavahi-client3`; existing common library supplied the second SONAME. No compiler/application development package added, no application compile/build-swap step. Chrony replaced `systemd-timesyncd`. [Package delta](fresh-package-changes.json). |
| Stock startup | PASS; local Transmit false, Enable on Boot Never, stock GPIO configuration. The GPIO WTP adapter remains unavailable as documented; this is not a Si5351 capability restriction. [Stock endpoint](fresh-stock-endpoint.json). |
| Configured Si5351 endpoint | PASS; normal HTTP configuration selected Si5351 and wlan1, with local Transmit false and port 31417. HELLO/CAPS/STATUS/GET_CLOCK and CLAIM/RELEASE passed; CAPS reports WSPR, TONE, QRSS, FSKCW and DFCW. [Configuration transaction](select-si5351.json), [protocol results](fresh-endpoint.json). |
| Independent DNS-SD | PASS; wspr5 resolved `_wtp._tcp.local.` to the endpoint-specific SRV hostname, 192.168.1.120, port 31417, with `txtvers=1` and `binding=plain`. [Fresh resolution](fresh-dns-sd.txt). |
| Repeat installation | PASS; exit 0, refreshed marker, exact parsed config, binary, service unit, stock INI, packaged web files and package set retained. Admission and discovery passed again. [Verification](repeat-verification.json), [installer log](repeat-install.log), [protocol](repeat-endpoint.json), [resolution](repeat-dns-sd.txt). |
| Host reboot | PASS; host boot changed from `5f819509-a5b0-433f-864f-6a392a650a6d` to `a88742df-1813-43d1-b505-e6b601241eda`; device identity and config retained, WTP boot identity changed, services and admission/discovery recovered. [Verification](boot-verification.json), [installed state](after-reboot.json), [protocol](boot-endpoint.json), [resolution](boot-dns-sd.txt). |
| Apache HTTP paths | PASS; installed `/wsprrypi/`, version proxy and config proxy returned 200 after reboot. Application/UI version metadata agree. [HTTP checks](apache-http.json). This is HTTP availability coverage, not browser workflow acceptance. |

The final service has zero main exit status and local output is disabled. No
owner or job remains; output is known inactive. The post-reboot clock reported
synchronized with normal leap state and about 40.3 ms uncertainty. No LOAD or
ARM was sent, and no RF transmission was requested. The fresh card retains its
new device identity; the old takeover journal and host configuration remain in
the verified private backup and on the original card.

The read-only [state collector](capture_state.py) and bounded no-job
[endpoint probe](endpoint_probe.py) support reproduction. Retained source/build
manifests are acceptance evidence, not additional sidecar requirements for
ordinary precompiled installation.

## Documentation Impact

- Updated [the precompiled installation reference](../../precompiled-installation.md)
  to explain automatic Avahi runtime dependencies and the source-only header
  requirement.
- Added the execution prompt and this acceptance record in the parent repo.
- Reviewed `../Wsprry_Pi_Docs/docs/Install/index.md`; its default source-build
  workflow is unchanged. No operator UI or screenshots changed.
- Added clean/repeat/boot evidence and a dated closure link from the historical
  failure record. No operator-documentation follow-up is required for this
  compatibility correction.

## Repository and device boundary

The closure commit contains the authorized installer/helper tests and
documentation/evidence changes on `devel`, with no `src/` or UI component
modifications. All validation described above completed before that commit;
publication and independently verified remote parity are reported separately.
wspr4 remains healthy on the new card, with Si5351 selected, local Transmit false,
WTP port 31417 and interface wlan1. See [final wspr4 state](final-wspr4.txt).
wspr5's installed service, binary and package state were preserved while
preparing the isolated native candidate; it remains active with its original
PID 1966 and binary. See [final wspr5 state](final-wspr5.txt).
Full Linux semantics, RF tests, fleet scale, browser-to-device workflows and
discovery topology changes are separate qualifications and are not claimed by
this installer regression result.

## Remaining limits

- Live installation acceptance covers this Trixie arm64 Pi 4 image and local
  binary mode. Other supported OS/architecture pairs have automated ELF/package
  coverage; they were not freshly installed in this campaign.
- Release download mode still awaits published assets and separate live
  acceptance.
- The stock GPIO backend is suitable for existing local operation; its WTP
  server adapter remains unavailable. Installed WTP acceptance here used
  Si5351, with all five backend-reported modes.
- Broader active-job failure injection, eight-target/mixed-fleet scheduling,
  browser-to-device takeover and discovery topology/expiry testing remain
  separate campaigns. No RF qualification was attempted by this installer fix.

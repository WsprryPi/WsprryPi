# Execution prompt: precompiled Avahi compatibility and clean installation

Work in `/Users/lbussy/GitHub/WsprryPi` on `devel`. Implement the narrowly scoped
installer correction, validate it on wspr4 using a clean SD card, perform
adversarial review and repair until no actionable finding remains, then commit
and push to `origin/devel` and report the supported results and remaining gates.

## Requirements and authorization

The supported local/precompiled install currently rejects these actual linked
libraries on Debian Trixie:

```
libavahi-client.so.3
libavahi-common.so.3
```

The authoritative failure and successful source-upgrade comparison are in
`docs/development/wtp-installed-service-results/`. Its tested application source
was `ff632d58dfcd6ada32470467e45769d0ba4aa218`; the evidence was subsequently
committed as `2f67b0003f055bb4a7cc7c72fdf71a2b9c4cab19`.

The user authorizes implementing the fix, normal installation/reinstallation,
required service operations and reboots on wspr4, adversarial assessment,
commit and push. RF is already authorized on 20m if a focused runtime check
needs it; the user's RF acceptance criterion is a corresponding signal on the
SDR. wspr4's Si5351 board is connected, while its RF output is uncabled.
wspr5 has the SDR and GPSDO, and has no Si5351. Compilation and read-only
controller/receiver inspection on another available Pi may prepare artifacts;
preserve other hosts' installed services and configuration.

**First preserve wspr4 off-device. The user will physically insert a clean SD
card. Shut down wspr4 only after the preservation copy is verified; do not claim
clean-image acceptance until that new card is running and has been inspected.**
The user confirmed the current Wi-Fi network is **Bohica**, not Bohica-IoT.
Use hostname `wspr4` and the existing user/access arrangement (`pi`, SSH and
sudo). Prefer a clean Raspberry Pi OS Lite 64-bit Trixie installation matching
the supported host family, and record its actual OS/kernel/architecture after
boot rather than assuming an image identity.

## 1. Preserve before the SD-card handoff

Inspect repository state and applicable AGENTS.md instructions. Preserve dirty,
staged, untracked and ignored data on wspr4, including older validation
checkouts. Do not clean or reset those trees.

Create a private, durable Mac archive outside the Git repository and cloud-synced
folders. Preserve the full home tree, root's state, system configuration,
installed/local custom software, boot/module assets, WsprryPi runtime state,
web files, UI backups, campaign evidence and logs. Include:

- All `/home/pi` repositories, indexes, local changes, builds, scripts, archives,
  qualification campaigns, cached helpers and SSH/setup files.
- `/root/.wsprrypi-wtp/revocation-v1` and other root state.
- `/etc`, `/usr/local`, `/boot`, installed kernel modules, `/var/lib/wsprrypi`,
  `/var/www`, `/var/backups`, relevant network/time state and package inventory.
- The installed-service rollback archive and its source/build at
  `/home/pi/wtp-installed-validation.yTbN3G`.
- The earlier capability and endpoint validation trees, including their retained
  native executables and source manifests.

Record the archive scope, file inventories, repository states, service state and
SHA-256 values. Compare transferred archive hashes with wspr4's hashes. Read every
archive member and compare retained critical files with their saved hashes.
Keep private host configuration, keys and Wi-Fi credentials out of Git and tool
output. Confirm the Mac destination's resolved physical location.

Quiesce the application for preservation, then complete the authorized shutdown.
Keep the original SD card recoverable. Pause physical testing until the user
reports the new card is booted. Continue independent local implementation when
it does not depend on the new device.

## 2. Implement the smallest correct installer change

Inspect the complete precompiled path before editing:

- `scripts/precompiled_binary.py`: ELF/ABI checks, shared-library allowlist,
  package reporting, full runtime validation and version reporting.
- `scripts/install.sh`: staging, dependency resolution, unprivileged validation,
  publication/rollback, service readiness and completion behavior.
- `scripts/tests/precompiled_binary_test.py` and
  `scripts/tests/precompiled_installer_test.sh`.
- `scripts/tests/installer_dependency_test.sh`, installer dependency groups and
  the existing `installer-dependency-test` target in `src/Makefile`.
- `docs/precompiled-installation.md` and relevant operator documentation.

Add exact Avahi SONAME-to-runtime-package mappings for the supported Debian
Bookworm/Trixie and armhf/arm64 combinations. Verify the actual Debian package
ownership rather than guessing. The runtime packages are `libavahi-client3` and
`libavahi-common3`; the former's transitive dependency must also be accounted for.
Review the current static dependency selection and helper-reported packages for
clean-host correctness; change that path only if the existing normal resolver
cannot supply the required libraries.

Preserve strict rejection of unknown SONAMEs, mismatched ABIs/loaders, missing
libraries/symbol versions and unsafe runtime-user selection. Preserve native
version reporting, non-root validation, atomic executable publication and
rollback. Accepting these libraries must not introduce application build/header
requirements for precompiled installation or weaken any RF/ownership policy.
Do not broaden into component changes or unrelated installer refactoring.

## 3. Automated validation

Add regression coverage that fails before the fix and passes afterward for:

- Both Avahi libraries on armv6/aarch64 and Bookworm/Trixie.
- Correct runtime package mapping and de-duplication, including each Avahi
  library separately and the normal combined dependency set.
- An otherwise valid Avahi binary with an unknown library remains rejected.
- Existing architecture, floating-point ABI, loader, absent-library, missing
  symbol and version-reporting checks remain effective.
- The real installer dependency resolver retains Avahi runtime packages and
  omits application compiler/development packages for precompiled installation
  on wspr4's non-RP1 profile.

Use the existing test targets from `src`, choosing Bash/tooling compatible with
the Mac where needed. Run focused Python/helper tests and
`make installer-dependency-test SUDO=` with an appropriate modern Bash. Inspect
what targets run; mocks and source tests are separate from installed acceptance.
Run syntax and final staged/unstaged whitespace checks. Broaden testing only to
resolve a concrete finding or required gate.

## 4. Prepare the precompiled candidate independently of the clean target

Use a retained or freshly built native executable whose production source and
runtime profile are verified. Prefer a normal release artifact including the
stock/default backend profile so an out-of-box installation can use the stock
INI. Build on a compatible existing Pi if necessary, in a private checkout,
without transmitting or replacing its installed service.

Record source identity, compiler/profile, binary hash, architecture, actual
linked Avahi SONAMEs and reported version. Preserve the old validated binaries.
Do not install build tools/development headers on the clean wspr4 merely to make
a precompiled artifact work. If a constrained binary profile requires explicit
backend configuration, record that qualification boundary and cover the normal
stock configuration with an appropriate artifact.

Package the corrected installer checkout and standalone binary for transfer.
The tested checkout may contain the authorized uncommitted fix; record a source
manifest/diff and tie final committed source to it. Do not imply a pre-existing
binary was compiled from a new installer-only commit.

## 5. Inspect the clean SD card before installation

Reconnect through the wspr4 alias, verify the new host identity, and handle the
expected SSH host-key change by verifying the new card's key through a trusted
local channel. Do not silently suppress host-key checking for arbitrary hosts.
Record OS release, architecture, kernel, hostname, machine/boot identity,
network/interface and package inventory.

Before copying any old application state, prove the clean baseline has no
WsprryPi binary/service/configuration/web root, takeover journal, device catalog
or assignment state. Record compiler/development packages already present in
the image. Do not restore the old `/etc`, INI or journal into the clean baseline.
Old data stays available in the verified private Mac preservation archive.

## 6. Normal fresh precompiled installation and repeat installation

Use the corrected checkout on `devel`, the explicit local-binary option and the
normal installer. Provide `TERM=xterm` and `REPO_BRANCH=devel` explicitly when
running noninteractively. Follow the exact marker contract:

```sh
rm -f ~/finished
sudo ./scripts/install.sh --binary-source local \
  --binary-path /home/pi/wsprrypi && touch ~/finished
```

Monitor `~/finished` outside the checkout and capture installer exit status/logs.
A marker alone is insufficient: verify the installed executable hash/version,
service command, active/enabled state, zero exit status, config/stock schema,
web publication, Apache configuration and runtime readiness. Verify required
runtime libraries resolve and no application compilation occurred. Compare
before/after package sets; identify image-provided build packages separately
from packages added by the installer.

Verify stock local transmission remains disabled at startup. Select the known
Si5351 settings through a fresh normal configuration transaction for endpoint
checks, with local Enable off and default WTP port 31417.
Use the actual active station interface on the replacement card (the old card
used `wlan0`; the replacement is on `wlan1`). Use read-only
HELLO/CAPS/STATUS/GET_CLOCK and a bounded CLAIM/RELEASE to prove the
fresh installed endpoint admits ownership. Verify Avahi publication and actual
DNS-SD resolution from another host. This is required to establish the accepted
Avahi linkage runs in the installed managed process.

Repeat the same supported precompiled installation on the now-installed card to
exercise upgrade/reinstallation and preservation of valid configuration. Verify
binary identity, config retention, service readiness and installed file state.
Reboot once and verify the installed service returns healthy, local Enable
remains off and remote admission/DNS-SD return after the interface is ready.

No broad RF campaign is needed for a dependency allowlist fix. If a focused job
is required by a concrete runtime finding, use a finite 20m job, capture the
corresponding SDR signal, confirm output-off and release ownership afterward.

Keep the new installation healthy with local Enable off. Do not automatically
restore old host-wide configuration onto the new OS. Report separately what old
state remains available and whether the original journal was deliberately not
imported; the clean card is a fresh host identity unless a later explicit
migration preserves the old identity and takeover history together.

## 7. Adversarial review, repair and reassessment

Review final production changes and tests as an adversary. In particular:

- No generic allow-all library acceptance, ABI relaxation, root execution of the
  supplied binary or bypass of runtime symbol validation.
- Package reporting and actual installer dependency selection work on a host
  without previously installed development packages.
- Unknown-library and version/runtime validation failures stop before publication.
- Failed installs do not create a stale completion marker or falsely report
  runtime readiness, and recovery retains the prior executable when required.
- Clean-install evidence is actually from the replacement card and is not
  contaminated by restoring previous software/configuration before baseline.
- Saved evidence distinguishes fresh install, repeat install, boot behavior,
  source tests, discovery observations and any optional RF observation.
- Private archives, credentials and raw large captures are not committed.

Repair every actionable finding within this scope, rerun affected checks, and
perform another assessment. If physical testing still awaits the user, report
that exact handoff; do not claim completion or push a final closure record early.

## 8. Documentation, commit and report

Save the execution prompt, source-test results, clean/repeat install evidence,
review/repair/reassessment record and exact remaining limitations in the parent
repository. Update the precompiled installation reference if behavior or
requirements change. Preserve historical failure evidence; add a dated closure
link rather than rewriting it as if the failure never occurred.

Review operator documentation impact in `../Wsprry_Pi_Docs`; follow that repo's
instructions and write there only if cross-repository authorization applies.
Avoid UI changes. If rendered UI documentation must change, use the mandatory
Impeccable workflow.

Review the complete staged diff. Commit only this authorized task on `devel`,
push `origin/devel`, and independently verify the remote commit. Report behavior
fixed, automated and live tests, adversarial findings/closure, documentation
impact, final wspr4 state, backup location and repository status. Keep other live
failure, fleet scale, browser and discovery-topology campaigns as separate gates.

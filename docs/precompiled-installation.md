# Installing a precompiled executable

The installer selects the newest published executable for the selected source branch and the Pi's OS/userspace architecture by default. Published prereleases are eligible when their executable branch matches. The release target identifies the branch when it names one; a published `publication-manifest.json` identifies the executable branch when the release target is a commit SHA. Supporting files still come from the selected application checkout or branch.

If no compatible published executable exists for that branch, the installer builds from source. A release lookup or validation failure stops installation rather than silently compiling. An explicit `--binary-source build`, `--release`, or debug build override requests compilation. Explicit local and tagged-release choices remain available through `--binary-source local` and `--binary-source release`.

To force a source build from a checkout:

```sh
sudo ./scripts/install.sh --binary-source build
```

To use an existing executable, select `--binary-source local`. Only the executable is required; no sidecar, source fingerprint, or matching Git commit is needed.

Run from the intended application checkout to select its supporting files.
Outside a checkout, an existing destination directory is refused; it is not
silently reused as the selected source.

```sh
rm -f ~/finished
sudo ./scripts/install.sh --binary-source local --binary-path /home/pi/wsprrypi && touch ~/finished
```

Without `--binary-path`, local mode reads `wsprrypi` from the invoking user's
home directory. The installer copies it into temporary staging and leaves the
original file untouched. Use trusted WsprryPi executables.

## Supported targets and checks

The binary matrix is Bookworm and Trixie, each with ARMv6 hard-float and AArch64
builds. The installer checks the host OS release and Debian userspace
architecture, then the executable's ELF architecture, floating-point ABI,
dynamic loader, and runtime libraries. The ARMv6 build serves supported 32-bit
Pis, including Pi 1A+ and the original Pi Zero. AArch64 serves supported 64-bit
installations. Kernel bitness alone does not select the binary.

After dependencies are installed, library and symbol resolution and the
executable's `--version` response are checked without root privileges, before
replacing the installed executable. These checks do not qualify RF operation.
The installer does not compare the executable with the checkout's commit.

The initial notices identify the selected release and make clear when the executable version is still pending validation. A source build from an existing checkout identifies its source version; a streamed source build waits for its checkout. After a successful installation, the summary reports the captured version used in the INI and service metadata. For precompiled installations, this is the executable's validated version, which can differ from its release tag or the supporting checkout. Dry runs do not report an installed version.

## Dependencies and resource use

Runtime packages are separate from application build packages. Precompiled mode installs runtime libraries and inspection tools, omits application compilers and development headers, and skips application compilation and its swap preparation. A compatible installed picotool is reused; otherwise the installer downloads the matching prebuilt picotool package, verifies its checksum and file inventory, and installs its executable and support files. Picotool installation never invokes a compiler or falls back to building from source. Existing development packages are not removed. Pi 5-family RP1 driver preparation retains its separate build requirements.

The normal APT handler installs Git, runtime packages and inspection tools before RP1 planning or artifact preparation. After artifact inspection identifies additional runtime packages, the same handler installs those packages without repeating the index refresh or dependency-repair transaction. Invalid artifacts can therefore be rejected after baseline dependencies have been installed; the installed executable remains unchanged.

Executables built with DNS-SD support use the Avahi runtime libraries
`libavahi-client3` and `libavahi-common3`. The installer includes
`libavahi-client3`; its package dependencies provide `libavahi-common3`.
Avahi development headers are needed only when building from source.

All four repository container recipes install `libavahi-client-dev`. Container
builds require `pkg-config` to find `avahi-client` before compilation and require
the executable to directly link both Avahi runtime libraries. The exported
`elf-dynamic.txt` records these dependencies. A missing dependency fails the
build; it does not silently produce a binary without DNS-SD discovery. Generic
source builds retain their optional Avahi support. These checks validate build
capability and library dependencies, not live discovery or RF operation.

Web packages are installed unless `--no-web` is selected. Optional RP1 GPCLK
DKMS installation still requires its own compiler, kernel headers, DKMS tools,
and build-resource checks, even with a precompiled application.

The executable is staged before service interruption and published by an
atomic rename. If a subsequent installer step fails, the installer attempts to
restore the prior executable and service state. This is executable recovery,
not a rollback of package or configuration changes.

An upgrade preserves current settings, including WTP connection settings,
WSPR frequency preferences and band-selector assignments. Retired `GPIO.Use NTP`
settings migrate to the clock-estimate policy and manual correction; existing
canonical values take precedence. A failed or empty
configuration merge stops installation and retains the existing configuration
and its recovery copy.

## Explicit Beta release downloads

For 4.0 Beta 1, clone the exact application tag first to select matching
supporting files. Follow the
[versioned Beta installation guide](https://wsprry-pi.readthedocs.io/en/v4.0.0-beta.1/Install/beta1.html)
for architecture/OS selection, independent checksums, dependencies and recovery.
Run from that tagged checkout:

```sh
rm -f ~/finished
sudo ./scripts/install.sh --binary-source release --release-tag v4.0.0-beta.1 && touch ~/finished
```

The installer downloads one asset named `wsprrypi-<cpu>-<release>` from the
selected GitHub release, where `<cpu>` is `armv6` or `aarch64` and `<release>` is
`bookworm` or `trixie`. This selects the executable only; normal installer
branch selection controls supporting files. A missing or incompatible asset
fails the installation without falling back to compilation. Release download
support performs a checksum comparison when automatic selection supplies the published manifest or GitHub asset checksum. An explicit tagged-release invocation without an expected checksum retains its existing validation behavior. To install independently checked bytes, use the guide's local-binary procedure. Beta public-download
verification is tracked in the [release evidence](../release_tools/4.0-Beta-1-Release-Evidence.md);
draft or dry-run checks alone do not establish public availability.

`--dry-run` performs no downloads, package installation, executable replacement,
or service changes. A local executable receives a basic ELF check; dependency
and version checks wait for a real installation.

Automatic selection resolves the release before build-resource estimates and package selection. A selected executable skips application compilation, its time/parallelism estimates, and application build dependencies. Optional RP1 provider preparation remains separate. Dry runs do not query release metadata or download executables; they report that release availability and any source-build fallback remain unresolved.

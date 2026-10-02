# Installing a precompiled executable

The installer builds from source by default. To use an existing executable,
select `--binary-source local`. Only the executable is required; no sidecar,
source fingerprint, or matching Git commit is needed. Supporting files use the
normal installer workflow.

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

## Dependencies and resource use

Runtime packages are separate from application build packages. Precompiled
mode installs runtime libraries and inspection tools, omits application
compilers and development headers, and skips application compilation and its
swap preparation. Existing development packages are not removed.

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

## Future release downloads

Once release assets are published, use:

```sh
sudo ./scripts/install.sh --binary-source release --release-tag v3.2.0
```

The installer downloads one asset named `wsprrypi-<cpu>-<release>` from the
selected GitHub release, where `<cpu>` is `armv6` or `aarch64` and `<release>` is
`bookworm` or `trixie`. This selects the executable only; normal installer
branch selection controls supporting files. A missing or incompatible asset
fails the installation without falling back to compilation. Release download
support is implemented; published assets are not yet available for end-to-end
validation.

`--dry-run` performs no downloads, package installation, executable replacement,
or service changes. A local executable receives a basic ELF check; dependency
and version checks wait for a real installation.

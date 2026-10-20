# Container builds

Build discrete, dynamically linked WsprryPi executables for this matrix:

| Target | Command | Docker platform | Package architecture |
| --- | --- | --- | --- |
| ARMv6 Bookworm | `./scripts/build-armv6-bookworm-container.sh` | `linux/arm/v6` | `armhf` |
| ARMv6 Trixie | `./scripts/build-armv6-trixie-container.sh` | `linux/arm/v6` | `armhf` |
| AArch64 Bookworm | `./scripts/build-aarch64-bookworm-container.sh` | `linux/arm64` | `arm64` |
| AArch64 Trixie | `./scripts/build-aarch64-trixie-container.sh` | `linux/arm64` | `arm64` |

ARMv6 uses the Raspbian ARMv6/VFPv2 hard-float baseline needed by Pi 1/A+,
Pi Zero, and Pi Zero W. It can also serve compatible newer Pis running 32-bit
Raspberry Pi OS. AArch64 is for compatible Pis running 64-bit Raspberry Pi OS.
Select by OS release and userspace architecture, not kernel architecture alone.
Debian ARMv7 `armhf` is not a substitute for the ARMv6 build environment.
Bullseye and separate ARMv7 builds are outside this maintained matrix.

## Prerequisites and commands

Use a POSIX shell, Python 3, and Docker with Buildx and a running engine.
Docker Desktop on macOS, Docker through WSL on Windows, or Docker on Linux,
can build when the engine provides the selected native architecture or CPU
emulation. ARMv6 execution support must be available for the ARMv6 recipes.
Image and package retrieval requires network access on an uncached build.
The recipes compile with four jobs inside Docker; the target Pi does not build.

Run a command from the table from the repository root. The default destination
is `dist/<target>`. An optional argument selects an absent or empty directory:

```sh
./scripts/build-aarch64-trixie-container.sh /path/to/empty-output
```

An unpublished candidate can select an explicit SemVer without creating a tag:

```sh
./scripts/build-aarch64-trixie-container.sh /path/to/empty-output --release-version 4.0.0-beta.1
```

All four wrappers support this option. The executable retains full source
commit, actual branch/detached state and dirty attribution; its version is
`4.0.0-beta.1+<short-commit>`. Only a SemVer without a `v` prefix or supplied build
metadata is accepted. Omitting the option preserves the normal Git-derived
version. The parent Make equivalent is `BUILD_METADATA_VERSION=4.0.0-beta.1`;
changing or removing it regenerates metadata and rebuilds the version object.
This is a build input, not a runtime setting or publication operation.

Non-empty directories, files, and destination symlinks are rejected. Existing
exports are never merged or overwritten. Failed exports remove temporary files
and containers; a completed artifact is published by a same-filesystem directory
rename after checksum and target verification. Empty parent directories can
remain after failure. Previously generated Bullseye artifacts and Docker images
are not deleted by these scripts.

Each build loads `wsprrypi-build:<target>` into the local Docker image store,
visible in Docker Desktop. Export uses the exact image ID returned by that
build, rather than resolving the mutable tag again. A temporary stopped
container supplies `/artifact`; export does not recompile or execute WsprryPi.
The image preserves the source snapshot and compiled workspace. Each invocation
builds the current checkout, including local changes, not necessarily a release.

## Build environments

All base images are pinned by digest. ARMv6 Bookworm uses the pinned Balena
Raspbian Bookworm build image. There is no published Balena ARMv6 Trixie build
image: that recipe upgrades the pinned Bookworm bootstrap to Trixie through the
signed Raspbian Trixie repository before installing build dependencies. It does
not use Debian ARMv7 packages or disable package signature checks.

AArch64 uses pinned official Debian Bookworm and Trixie ARM64 images as the
userspace build baselines. These are not complete Raspberry Pi OS images.
Compatibility with Raspberry Pi OS remains subject to runtime validation.

Package repositories are not snapshotted. Fresh dependency layers may obtain
newer package revisions, while Docker may reuse cached layers. These are pinned
bootstrap recipes, not a promise of bit-for-bit reproducibility. The exported
package inventory records the versions actually present in each build.

## Exported evidence

- `wsprrypi`: release executable with the default backend profile
- `SHA256SUMS`: executable checksum, rechecked after export
- `build-metadata.hpp`: exact compiled version, commit, branch and dirty state
- `file.txt`, `elf-header.txt`, `elf-attributes.txt`: executable format and ABI
- `elf-versions.txt`: required ELF symbol-version information
- `elf-dynamic.txt`: direct shared-library dependencies, including Avahi
- `shared-libraries.txt`: libraries resolved inside the build environment
- `build-packages.txt`: complete installed package/version inventory
- `os-release.txt`, `target.txt`: actual userspace release and requested target
- `build-image-id.txt`: exact local build-image identity

The context includes Git's index with its objects and refs so a clean source
checkout remains clean in the embedded metadata. Build from a clean frozen
checkout for a release candidate; the version override does not hide changes.

The build rejects a wrong userspace release/architecture, missing linked
libraries, or an unexpected ELF class/machine. ARMv6 additionally requires v6,
VFPv2, and hard-float argument attributes in the executable. These checks do not
exhaustively audit every instruction in every runtime dependency.

ARMv6 dependency resolution invokes the hard-float dynamic loader's `--list`
mode directly; the ARMv6 QEMU `ldd` verification subprocess crashed during
candidate preparation. AArch64 uses `ldd`. Both paths require successful
resolution and reject missing libraries. The dependency check does not execute
the application or qualify target runtime behavior. See the
[`ld.so --list` contract](https://man7.org/linux/man-pages/man8/ld.so.8.html).

Bookworm and Trixie have different library generations, including libgpiod.
Keep their artifacts separate; do not rename a Bookworm executable as Trixie.
The package inventory avoids hard-coding runtime names that change across
releases, such as OpenSSL's Trixie `t64` package naming.

## Validation and scope

Run the host-side export regression tests with:

```sh
python3 scripts/tests/container_build_test.py
```

Then build all four targets and inspect their exported evidence. The recipes
compile and inspect binaries without executing the application. A successful
build and resolved dependencies inside its container do not establish startup
or installation success on a Pi, nor qualify GPIO, timing, RF, services, or
RP1 kernel-provider compatibility. Those require separate validation on the
appropriate target and OS. ARMv6 compiler support does not prove that the full
application fits the Pi 1A+'s runtime memory budget.

These commands do not produce `.deb` packages, publish releases, install on a
Pi, or change the installer. RP1-GPCLK-DKMS remains a separately managed provider.

# Raspberry Pi ARM64 kernel build worker setup

This guide creates a Debian ARM64 worker that builds Raspberry Pi kernels
natively, keeps generated output outside the WsprryPi repositories, and stages
deployable artifacts without installing them on the worker or a Raspberry Pi.

The reference build uses the Raspberry Pi OS `6.18.34+rpt-rpi-2712` source and
configuration for a Pi 5. Substitute a different reviewed source pin and target
configuration for other releases or models; never assume that the reference
pin is current. This is the canonical guide for creating or using the kernel
compilation environment.

## Safety boundary

This procedure may install build packages and create files only beneath
`~/kernel-work` on the worker. It does not:

- write to the worker's `/boot` or `/lib/modules`;
- clone or modify WsprryPi application or documentation repositories;
- contact, copy files to, install files on, or reboot a Raspberry Pi;
- operate GPIO, a transmitter, or RF hardware.

Deployment is a separate, explicitly authorized procedure. Run this guide as a
normal user with `sudo` access only for Debian package installation. Do not run
kernel build or staging commands with `sudo`.

Run the command blocks in Bash. Some error-preserving logging steps use Bash's
`PIPESTATUS`; do not paste those blocks into a strictly POSIX shell.

## 1. Provision and verify the VM

The validated allocation is a profile, not a tested minimum:

| Resource | Validated value |
| --- | --- |
| Guest | Debian 13 (Trixie), ARM64 |
| Firmware | EFI |
| CPUs | 4 virtual CPUs |
| Memory | 8 GiB |
| Disk | 64 GiB |
| Network | Outbound HTTPS and SSH access from the controller |
| Runtime | Headless after initial installation |

Create the VM with Debian's ARM64 installer. Establish a normal user with
passwordless or interactive `sudo`, enable SSH, and confirm that the controller
can reconnect before continuing. Bridged networking, NAT with port forwarding,
mDNS, a fixed address, or an SSH alias are all acceptable; reachability is the
contract.

If the controller reaches the guest through NAT or a port forward, configure
SSH keepalives (for example, `ServerAliveInterval 10`) before long, quiet
source transfers and builds. A dropped controller connection must be treated
as an interrupted step and inspected before retrying.

On the guest, capture the baseline:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
mkdir -p "$WORK_ROOT/logs"

{
    date -u +CAPTURED_UTC=%Y-%m-%dT%H:%M:%SZ
    printf 'HOSTNAME='; hostname
    printf 'ARCHITECTURE='; dpkg --print-architecture
    printf 'KERNEL_ARCH='; uname -m
    printf 'CPUS='; nproc
    awk '/MemTotal/ { print "MEMTOTAL_KIB=" $2 }' /proc/meminfo
    findmnt -no SOURCE,FSTYPE,SIZE,AVAIL,TARGET /
    sed -n 's/^\(PRETTY_NAME\|VERSION_ID\|VERSION_CODENAME\)=/OS_\1=/p' \
        /etc/os-release
} | tee "$WORK_ROOT/logs/worker-profile.txt"
```

Stop unless the architecture is `arm64`/`aarch64`, Debian is version 13, at
least four CPUs and approximately 8 GiB RAM are visible, and the filesystem has
enough free space for source, build output, and staging. The validated 64 GiB
disk had more than 50 GiB free before installation.

## 2. Install the build prerequisites

Install the reviewed package set:

```sh
set -eu

sudo apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential bc bison flex libssl-dev make gcc libc6-dev \
    libncurses-dev pkg-config libelf-dev dwarves git rsync ccache kmod \
    device-tree-compiler fakeroot dpkg-dev debhelper xz-utils zstd \
    python3 perl gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf time
```

The ARM32 compiler and binutils are required by the reference configuration's
compatibility VDSO. They are conditional for other configurations. `ccache`,
`fakeroot`, `dpkg-dev`, and `debhelper` support later workflows but are not used
by the direct reference build.

Record resolved package and tool versions after installation:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
PACKAGES='build-essential bc bison flex libssl-dev make gcc libc6-dev libncurses-dev pkg-config libelf-dev dwarves git rsync ccache kmod device-tree-compiler fakeroot dpkg-dev debhelper xz-utils zstd python3 perl gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf time'

for package in $PACKAGES; do
    dpkg-query -W -f='${binary:Package}\t${Version}\n' "$package"
done | sort > "$WORK_ROOT/logs/worker-package-manifest.tsv"

{
    gcc --version | head -1
    arm-linux-gnueabihf-gcc --version | head -1
    ld --version | head -1
    make --version | head -1
    git --version
    dtc --version
    /usr/sbin/depmod --version
    zstd --version
    tar --version | head -1
    /usr/bin/time --version | head -1
} > "$WORK_ROOT/logs/worker-tool-versions.txt"

(
    cd "$WORK_ROOT/logs"
    sha256sum worker-profile.txt worker-package-manifest.tsv \
        worker-tool-versions.txt > bootstrap-files.sha256
)
```

All commands must exist and every requested package must have a recorded
version. Package versions are evidence for a particular run; do not permanently
pin them in the installation command unless Debian snapshot repositories are
also part of the maintenance policy.

## 3. Establish the workspace

Create stable directory roles:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
mkdir -p \
    "$WORK_ROOT/src" \
    "$WORK_ROOT/build" \
    "$WORK_ROOT/stage" \
    "$WORK_ROOT/logs" \
    "$WORK_ROOT/inputs"

chmod 0755 "$WORK_ROOT" "$WORK_ROOT"/{src,build,stage,logs,inputs}
```

- `src/` contains pinned source checkouts.
- `build/` contains out-of-tree compiler output, one intentional lane per
  target/configuration.
- `stage/` contains immutable bundles, manifests, archives, and checksums.
- `logs/` contains worker and build evidence.
- `inputs/` contains immutable target configurations and provenance records.

Repeating `mkdir -p` preserves existing contents. Later steps deliberately
refuse conflicting source, build, or stage paths. Generated files must not be
placed in WsprryPi repositories.

## 4. Acquire and pin the reference source

Define the reference input once in the current shell:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
SOURCE_URL=https://github.com/raspberrypi/linux.git
SOURCE_LINE=rpi-6.18.y
SOURCE_COMMIT=c8c7494100e99ee05b11aaa4f0588a223a63d1af
SOURCE="$WORK_ROOT/src/linux-rpi-6.18.34-rpt1"

test ! -e "$SOURCE" || {
    printf 'Refusing existing source path: %s\n' "$SOURCE" >&2
    exit 1
}

mkdir "$SOURCE"
git -C "$SOURCE" init
git -C "$SOURCE" remote add origin "$SOURCE_URL"
git -C "$SOURCE" fetch --depth=1 origin "$SOURCE_COMMIT"
git -C "$SOURCE" checkout --detach FETCH_HEAD

test "$(git -C "$SOURCE" rev-parse HEAD)" = "$SOURCE_COMMIT"
test -z "$(git -C "$SOURCE" status --porcelain)"
git -C "$SOURCE" cat-file -e "$SOURCE_COMMIT^{commit}"
```

Create the source record:

```sh
cat > "$WORK_ROOT/inputs/SOURCE_PIN.txt" <<EOF
binary_package=linux-image-6.18.34+rpt-rpi-2712
binary_version=1:6.18.34-1+rpt1
source_package=linux
source_version=1:6.18.34-1+rpt1
package_repository=http://archive.raspberrypi.com/debian trixie/main
package_changelog_linux_commit=$SOURCE_COMMIT
source_url=$SOURCE_URL
source_line=$SOURCE_LINE
pinned_commit=$SOURCE_COMMIT
authoritative_kernel_release=6.18.34+rpt-rpi-2712
configuration_sha256=d5ba966d17d456a6f29e53baf53464e1fd53f9f8e31481da18f2221f1da2593d
provenance=Installed signed Raspberry Pi OS binary package metadata and changelog map this package version to the official Git commit.
source_package_authentication_note=Source archives whose dsc signer is not authenticated by the available trusted keyrings must not be used.
update_policy=Detached exact-release pin; changes require a separately reviewed input lane.
EOF

sha256sum "$WORK_ROOT/inputs/SOURCE_PIN.txt" \
    > "$WORK_ROOT/inputs/SOURCE_PIN.txt.sha256"
```

For a new target release, derive the source commit from trusted Raspberry Pi OS
package metadata and its changelog, update all provenance fields, and review
the new record before building. A branch name alone is never a reproducible
pin. Fetching, switching, resetting, or cleaning source is not an implicit
build action.

## 5. Import and normalize the target configuration

Obtain the configuration from the actual target before starting this section.
Prefer its versioned `/boot/config-$(uname -r)`. If the target exposes
`/proc/config.gz`, decompress it to a regular file. Copy the file into
`~/kernel-work/inputs` without editing it and record its target identity and
origin. Target access and copying require separate authorization.

For the reference validation, the required input is named
`config-6.18.34+rpt-rpi-2712` and has SHA-256:

```text
d5ba966d17d456a6f29e53baf53464e1fd53f9f8e31481da18f2221f1da2593d
```

Normalize it only in a new out-of-tree build lane:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
SOURCE="$WORK_ROOT/src/linux-rpi-6.18.34-rpt1"
RAW_CONFIG="$WORK_ROOT/inputs/config-6.18.34+rpt-rpi-2712"
BUILD="$WORK_ROOT/build/pi5-6.18.34-stock"
EXPECTED_RAW_SHA=d5ba966d17d456a6f29e53baf53464e1fd53f9f8e31481da18f2221f1da2593d
EXPECTED_NORMALIZED_SHA=a5dde4cec5f4b9526d8c5e55a308d94fd49d2912145891f469212f650075ac6a

test "$(sha256sum "$RAW_CONFIG" | awk '{print $1}')" = "$EXPECTED_RAW_SHA"
test ! -e "$BUILD" || {
    printf 'Refusing existing build lane: %s\n' "$BUILD" >&2
    exit 1
}

mkdir -p "$BUILD"
cp --preserve=timestamps "$RAW_CONFIG" "$BUILD/.config.imported"
cp --preserve=timestamps "$RAW_CONFIG" "$BUILD/.config"

make -C "$SOURCE" O="$BUILD" ARCH=arm64 \
    CROSS_COMPILE_COMPAT=arm-linux-gnueabihf- olddefconfig

diff -u "$BUILD/.config.imported" "$BUILD/.config" \
    > "$BUILD/config-normalization.diff" || test $? -eq 1

NORMALIZED_SHA=$(sha256sum "$BUILD/.config" | awk '{print $1}')
test "$NORMALIZED_SHA" = "$EXPECTED_NORMALIZED_SHA"

cat > "$BUILD/IMPORT_RECORD.txt" <<EOF
target_model=Raspberry Pi 5
target_architecture=arm64
target_kernel_release=6.18.34+rpt-rpi-2712
configuration_origin=/boot/config-6.18.34+rpt-rpi-2712
raw_configuration_sha256=$EXPECTED_RAW_SHA
normalization_command=make -C $SOURCE O=$BUILD ARCH=arm64 CROSS_COMPILE_COMPAT=arm-linux-gnueabihf- olddefconfig
normalized_configuration_sha256=$NORMALIZED_SHA
compatibility_compiler=arm-linux-gnueabihf-
review_status=reference normalization matched validated hash
EOF
```

Review `config-normalization.diff`. For other target configurations, decide
whether `CROSS_COMPILE_COMPAT` is required from the imported settings and
normalization results. Do not enable it merely because this reference build
uses it, and do not accept unexpected normalized settings without review.

## 6. Build the unchanged reference kernel

Use a distinct local suffix so the worker result cannot be confused with the
packaged target kernel:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
SOURCE="$WORK_ROOT/src/linux-rpi-6.18.34-rpt1"
BUILD="$WORK_ROOT/build/pi5-6.18.34-stock"
SOURCE_COMMIT=c8c7494100e99ee05b11aaa4f0588a223a63d1af
LOCALVERSION=-kernel-worker-stock
JOBS=$(nproc)
BUILD_ID=$(date -u +%Y%m%dT%H%M%SZ)
BUILD_LOG="$WORK_ROOT/logs/stock-build-$BUILD_ID.log"

test "$(git -C "$SOURCE" rev-parse HEAD)" = "$SOURCE_COMMIT"
test -z "$(git -C "$SOURCE" status --porcelain)"
test -f "$BUILD/.config"
test ! -e "$BUILD_LOG"

set +e
{
    printf 'START_UTC=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'HOST=%s\nARCH=arm64\nJOBS=%s\n' "$(hostname)" "$JOBS"
    printf 'CROSS_COMPILE_COMPAT=arm-linux-gnueabihf-\n'
    printf 'LOCALVERSION=%s\nSOURCE_COMMIT=%s\n' "$LOCALVERSION" "$SOURCE_COMMIT"
    printf 'CONFIG_SHA256=%s\n' "$(sha256sum "$BUILD/.config" | awk '{print $1}')"
    gcc --version | head -1
    arm-linux-gnueabihf-gcc --version | head -1
    /usr/bin/time -v make -C "$SOURCE" O="$BUILD" \
        ARCH=arm64 \
        CROSS_COMPILE_COMPAT=arm-linux-gnueabihf- \
        LOCALVERSION="$LOCALVERSION" \
        -j"$JOBS" Image.gz modules dtbs
    status=$?
    printf 'END_UTC=%s\nEXIT_STATUS=%s\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$status"
    exit "$status"
} 2>&1 | tee "$BUILD_LOG"
status=${PIPESTATUS[0]}
set -e

(
    cd "$(dirname "$BUILD_LOG")"
    sha256sum "$(basename "$BUILD_LOG")" \
        > "$(basename "$BUILD_LOG").sha256"
)
test "$status" -eq 0
```

The external sidecar is created only after the log closes; a log must never
contain a checksum of itself.

Calculate and inspect the release and required outputs:

```sh
RELEASE=$(make -s -C "$SOURCE" O="$BUILD" ARCH=arm64 \
    LOCALVERSION="$LOCALVERSION" kernelrelease)
printf 'KERNEL_RELEASE=%s\n' "$RELEASE"

test "$RELEASE" = 6.18.34-v8-16k-kernel-worker-stock
test -s "$BUILD/arch/arm64/boot/Image.gz"
test -s "$BUILD/Module.symvers"
test -s "$BUILD/modules.order"
test -n "$(find "$BUILD" -type f -name '*.ko' -print -quit)"
test -n "$(find "$BUILD/arch/arm64/boot/dts/broadcom" -type f \
    -name 'bcm2712*.dtb' -print -quit)"
test -n "$(find "$BUILD/arch/arm64/boot/dts/overlays" -type f \
    -name '*.dtbo' -print -quit)"
```

A successful reference build proves the environment before kernel patches are
introduced. Its duration and binary hashes may vary with tool versions and
timestamps.

## 7. Stage and archive the result

Create a new immutable stage path. `modules_install` is permitted only with an
explicit staging root:

```sh
set -eu

WORK_ROOT="$HOME/kernel-work"
SOURCE="$WORK_ROOT/src/linux-rpi-6.18.34-rpt1"
BUILD="$WORK_ROOT/build/pi5-6.18.34-stock"
LOCALVERSION=-kernel-worker-stock
RELEASE=$(make -s -C "$SOURCE" O="$BUILD" ARCH=arm64 \
    LOCALVERSION="$LOCALVERSION" kernelrelease)
STAGE_ID="$RELEASE-$(date -u +%Y%m%dT%H%M%SZ)"
STAGE_ROOT="$WORK_ROOT/stage/$STAGE_ID"
BUNDLE="$STAGE_ROOT/bundle"
STAGE_LOG="$WORK_ROOT/logs/stage-$STAGE_ID.log"
BUILD_RESULT="$WORK_ROOT/logs/stock-build-current.result"
BUILD_LOG=$(sed -n 's/^BUILD_LOG=//p' "$BUILD_RESULT")

test ! -e "$STAGE_ROOT"
test ! -e "$STAGE_LOG"
test -f "$BUILD_LOG"
test -f "$BUILD_LOG.sha256"
mkdir -p "$BUNDLE/rootfs" "$BUNDLE/boot/overlays" "$BUNDLE/evidence"

set +e
{
    printf 'START_UTC=%s\nSTAGE_ROOT=%s\nRELEASE=%s\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$STAGE_ROOT" "$RELEASE"
    make -C "$SOURCE" O="$BUILD" ARCH=arm64 \
        CROSS_COMPILE_COMPAT=arm-linux-gnueabihf- \
        LOCALVERSION="$LOCALVERSION" \
        INSTALL_MOD_PATH="$BUNDLE/rootfs" modules_install
    status=$?
    printf 'END_UTC=%s\nEXIT_STATUS=%s\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$status"
    exit "$status"
} 2>&1 | tee "$STAGE_LOG"
status=${PIPESTATUS[0]}
set -e

(
    cd "$(dirname "$STAGE_LOG")"
    sha256sum "$(basename "$STAGE_LOG")" \
        > "$(basename "$STAGE_LOG").sha256"
)
test "$status" -eq 0
```

Remove only the generated module-tree links that point back into the worker,
then copy boot and evidence files:

```sh
MODULE_ROOT="$BUNDLE/rootfs/lib/modules/$RELEASE"
test -d "$MODULE_ROOT"

for link in build source; do
    if test -L "$MODULE_ROOT/$link"; then
        rm "$MODULE_ROOT/$link"
    fi
done

cp "$BUILD/arch/arm64/boot/Image.gz" "$BUNDLE/boot/Image.gz"
find "$BUILD/arch/arm64/boot/dts/broadcom" -maxdepth 1 -type f \
    -name '*.dtb' -exec cp -t "$BUNDLE/boot" {} +
find "$BUILD/arch/arm64/boot/dts/overlays" -maxdepth 1 -type f \
    -name '*.dtbo' -exec cp -t "$BUNDLE/boot/overlays" {} +
cp "$SOURCE/arch/arm64/boot/dts/overlays/README" \
    "$BUNDLE/boot/overlays/README"

cp "$WORK_ROOT/inputs/SOURCE_PIN.txt" "$BUNDLE/evidence/"
cp "$WORK_ROOT/inputs/config-6.18.34+rpt-rpi-2712" \
    "$BUNDLE/evidence/config.raw"
cp "$BUILD/.config" "$BUNDLE/evidence/config.normalized"
cp "$BUILD/config-normalization.diff" "$BUNDLE/evidence/"
cp "$BUILD/IMPORT_RECORD.txt" "$BUNDLE/evidence/"
cp "$WORK_ROOT/logs/worker-package-manifest.tsv" "$BUNDLE/evidence/"
cp "$WORK_ROOT/logs/worker-tool-versions.txt" "$BUNDLE/evidence/"
cp "$BUILD_LOG" "$BUILD_LOG.sha256" "$BUNDLE/evidence/"
cp "$STAGE_LOG" "$STAGE_LOG.sha256" "$BUNDLE/evidence/"

/usr/sbin/depmod -b "$BUNDLE/rootfs" "$RELEASE"

cat > "$BUNDLE/BUILD_RECORD.txt" <<EOF
build_id=$STAGE_ID
target_model=Raspberry Pi 5
architecture=arm64
source_url=https://github.com/raspberrypi/linux.git
source_commit=c8c7494100e99ee05b11aaa4f0588a223a63d1af
config_sha256=$(sha256sum "$BUILD/.config" | awk '{print $1}')
localversion=$LOCALVERSION
kernel_release=$RELEASE
module_install_root=rootfs/lib/modules/$RELEASE
kernel_image=boot/Image.gz
build_log=evidence/$(basename "$BUILD_LOG")
safety=staged only; not installed or deployed
EOF
```

Generate a relative manifest, verify it, close the archive, and create a
portable archive checksum:

```sh
(
    cd "$BUNDLE"
    find . -type f ! -name MANIFEST.sha256 -print0 \
        | sort -z \
        | xargs -0 sha256sum > MANIFEST.sha256
    sha256sum -c MANIFEST.sha256
)

test -z "$(find "$BUNDLE" -type l -print -quit)"

ARCHIVE="$STAGE_ROOT/$STAGE_ID.tar.zst"
tar --sort=name --numeric-owner --owner=0 --group=0 \
    -C "$STAGE_ROOT" -cf - bundle | zstd -T0 -19 -o "$ARCHIVE"

(
    cd "$STAGE_ROOT"
    sha256sum "$(basename "$ARCHIVE")" > SHA256SUMS
    sha256sum -c SHA256SUMS
)
```

The portable `SHA256SUMS` contains only the archive basename. Do not append a
checksum to a file and then claim that the appended value verifies that same
file.

## 8. Verify the acceptance criteria

Run these checks before calling the worker ready:

```sh
set -eu

test "$(dpkg --print-architecture)" = arm64
test "$(git -C "$SOURCE" rev-parse HEAD)" = \
    c8c7494100e99ee05b11aaa4f0588a223a63d1af
test -z "$(git -C "$SOURCE" status --porcelain)"
test "$(sha256sum "$BUILD/.config" | awk '{print $1}')" = \
    a5dde4cec5f4b9526d8c5e55a308d94fd49d2912145891f469212f650075ac6a
test "$RELEASE" = 6.18.34-v8-16k-kernel-worker-stock
test -d "$BUNDLE/rootfs/lib/modules/$RELEASE"
test -z "$(find "$BUNDLE" -type l -print -quit)"

MODULE_COUNT=$(find "$MODULE_ROOT" -type f \
    \( -name '*.ko' -o -name '*.ko.xz' -o -name '*.ko.zst' \) | wc -l)
BCM2712_DTB_COUNT=$(find "$BUNDLE/boot" -maxdepth 1 -type f \
    -name 'bcm2712*.dtb' | wc -l)
OVERLAY_COUNT=$(find "$BUNDLE/boot/overlays" -maxdepth 1 -type f \
    -name '*.dtbo' | wc -l)
MANIFEST_COUNT=$(wc -l < "$BUNDLE/MANIFEST.sha256")

printf 'modules=%s\nbcm2712_dtbs=%s\noverlays=%s\nmanifest_entries=%s\n' \
    "$MODULE_COUNT" "$BCM2712_DTB_COUNT" "$OVERLAY_COUNT" "$MANIFEST_COUNT"

test "$MODULE_COUNT" -eq 1895
test "$BCM2712_DTB_COUNT" -eq 8
test "$OVERLAY_COUNT" -eq 368

(
    cd "$BUNDLE"
    sha256sum -c MANIFEST.sha256
)
(
    cd "$STAGE_ROOT"
    sha256sum -c SHA256SUMS
)
```

For the exact reference inputs, require 1,895 modules, eight BCM2712 DTBs, and
368 overlays. These counts are strict structural checks for the pinned input.
The manifest count depends on the generated record set; inspect and record it.

Verify archive portability from a second directory:

```sh
VERIFY_DIR=$(mktemp -d)
tar --use-compress-program=unzstd -xf "$ARCHIVE" -C "$VERIFY_DIR"
(
    cd "$VERIFY_DIR/bundle"
    sha256sum -c MANIFEST.sha256
)
printf 'Temporary verification copy retained at %s\n' "$VERIFY_DIR"
```

Finally confirm the safety boundary:

```sh
test ! -d "/lib/modules/$RELEASE"
test ! -e "/boot/Image.gz"
find "$WORK_ROOT" -xdev -type l -print
```

The first two checks confirm that this build was not installed on the worker.
Review any links printed by the final command; the staged bundle itself must
contain none. This procedure never supplies a target hostname, so no Raspberry
Pi can be modified by following it.

## 9. Repeat runs and cleanup

Before every build, inspect the exact source commit and status. Never fetch,
pull, switch, reset, or clean source as an implicit build step.

- Reuse a `build/` lane only for an intentional incremental build from the same
  reviewed source and configuration.
- Use a new build lane when source, target configuration, compatibility-tool
  decision, or patches change.
- Give every stage result a unique release and UTC identifier; never overwrite
  a prior bundle.
- Retain logs and their external checksum sidecars while any bundle, audit, or
  qualification record refers to them.
- Delete a specific build or stage directory only after resolving its absolute
  path, confirming it is beneath `~/kernel-work`, inspecting it, and confirming
  its evidence is no longer needed.
- Never use the home directory, `~/kernel-work` itself, a WsprryPi repository,
  `/boot`, or `/lib/modules` as a cleanup target.

VirtualBox checkpoints are useful after package/toolchain validation and after
the full stock build/staging acceptance passes. Name checkpoints for capability,
for example `debian-arm64-rpi-kernel-toolchain-ready` and
`rpi-arm64-kernel-build-worker-validated`. Snapshots supplement this guide and
its checksums; they do not replace them.

## 10. Adapting the worker for kernel development

After the stock acceptance build passes, create a new immutable development
lane. Record the new source commit, raw and normalized configuration hashes,
ordered patch inputs and hashes, local version, build log, stage record,
manifest, and archive checksum. Do not modify the validated stock source or
reuse its stage directory.

Kernel deployment remains separate from worker setup. Before any live Pi
change, independently review the target boot layout, kernel filename, module
release, firmware expectations, rollback configuration, copy procedure, and
post-boot verification plan.

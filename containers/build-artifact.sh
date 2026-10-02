#!/bin/sh
# Runs only inside the selected build container. Never executes the application.
set -eu
export LC_ALL=C
cpu=$1
release=$2
. /etc/os-release
test "$VERSION_CODENAME" = "$release"
case "$cpu" in
    armv6) test "$ID" = raspbian; test "$(dpkg --print-architecture)" = armhf ;;
    aarch64) test "$(dpkg --print-architecture)" = arm64 ;;
    *) exit 1 ;;
esac
git config --global --add safe.directory /workspace
if ! pkg-config --exists avahi-client; then
    echo "container build requires Avahi development files (avahi-client)" >&2
    exit 1
fi
make -C src release SUDO= JOBS=4
mkdir -p /artifact
cp src/build/bin/wsprrypi /artifact/wsprrypi
file /artifact/wsprrypi > /artifact/file.txt
readelf --file-header /artifact/wsprrypi > /artifact/elf-header.txt
readelf --arch-specific /artifact/wsprrypi > /artifact/elf-attributes.txt
readelf --version-info /artifact/wsprrypi > /artifact/elf-versions.txt
readelf --dynamic /artifact/wsprrypi > /artifact/elf-dynamic.txt
for library in libavahi-client.so.3 libavahi-common.so.3; do
    if ! grep -E '\(NEEDED\)' /artifact/elf-dynamic.txt | grep -Fq "[$library]"; then
        echo "container executable must directly link $library" >&2
        exit 1
    fi
done
case "$cpu" in
    armv6)
        grep -Eq 'Class: +ELF32' /artifact/elf-header.txt
        grep -Eq 'Machine: +ARM$' /artifact/elf-header.txt
        grep -Eq 'Tag_CPU_arch: v6$' /artifact/elf-attributes.txt
        grep -Eq 'Tag_FP_arch: VFPv2$' /artifact/elf-attributes.txt
        grep -q 'Tag_ABI_VFP_args: VFP registers' /artifact/elf-attributes.txt
        ;;
    aarch64)
        grep -Eq 'Class: +ELF64' /artifact/elf-header.txt
        grep -Eq 'Machine: +AArch64$' /artifact/elf-header.txt
        ;;
esac
ldd /artifact/wsprrypi > /artifact/shared-libraries.txt
if grep -q 'not found' /artifact/shared-libraries.txt; then
    cat /artifact/shared-libraries.txt >&2
    exit 1
fi
# Record the complete package inventory, including transitive dependencies and
# release-specific names such as libssl3t64 and libgpiod3 on Trixie.
dpkg-query -W -f='${binary:Package}\t${Version}\n' > /artifact/build-packages.txt
cp /etc/os-release /artifact/os-release.txt
printf 'cpu=%s\nrelease=%s\npackage_architecture=%s\n' \
    "$cpu" "$release" "$(dpkg --print-architecture)" > /artifact/target.txt
cd /artifact
sha256sum wsprrypi > SHA256SUMS

#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repository_root=$(cd "${script_dir}/../.." && pwd)
source_dir="${repository_root}/src"
test_root=$(mktemp -d /tmp/wsprrypi-strict-install.XXXXXX)
trap 'rm -rf "$test_root"' EXIT

fake_bin="${test_root}/fake-bin"
install_prefix="${test_root}/prefix/bin"
systemctl_marker="${test_root}/systemctl-invoked"
mkdir -p "$fake_bin"

cat >"${fake_bin}/systemctl" <<EOF
#!/usr/bin/env sh
touch "$systemctl_marker"
exit 99
EOF
chmod 755 "${fake_bin}/systemctl"

PATH="${fake_bin}:${PATH}" make -C "$source_dir" install-binary \
    BACKENDS=si5351 ANCILLARY_GPIO=0 \
    PREFIX="$install_prefix" SUDO=

installed_binary="${install_prefix}/wsprrypi"
built_binary="${source_dir}/build/bin/wsprrypi"

test -x "$installed_binary"
cmp "$built_binary" "$installed_binary"
test "$(stat -c '%a' "$installed_binary")" = 755
test ! -e "$systemctl_marker"

"${repository_root}/scripts/tests/backend_capability_reporting_test.sh" \
    "$installed_binary" si5351 simulated disabled

if ldd "$installed_binary" | grep -i gpiod >/dev/null; then
    echo "installed strict I2C executable unexpectedly links libgpiod" >&2
    exit 1
fi

installed_entries=$(find "${test_root}/prefix" -mindepth 1 -type f -print)
if [ "$installed_entries" != "$installed_binary" ]; then
    echo "install-binary wrote an unexpected file:" >&2
    printf '%s\n' "$installed_entries" >&2
    exit 1
fi

echo "strict I2C install path passed"

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPOSITORY_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
# shellcheck source=scripts/support_bundle_age_dependency.sh
source "${REPOSITORY_ROOT}/scripts/support_bundle_age_dependency.sh"

fixture=$(mktemp -d "${TMPDIR:-/tmp}/wsprrypi-age-dependency.XXXXXX")
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/usr/bin"
uid=$(id -u)

write_tools() {
    printf '#!/bin/sh\nexit 0\n' >"$fixture/usr/bin/age"
    printf '#!/bin/sh\nexit 0\n' >"$fixture/usr/bin/age-keygen"
    chmod 0755 "$fixture/usr/bin/age" "$fixture/usr/bin/age-keygen"
}

assert_rejected() {
    local expected="$1"
    shift
    if support_bundle_validate_age_dependency_at_root "$@"; then
        echo "unsafe age dependency fixture was accepted" >&2
        exit 1
    fi
    [[ "$SUPPORT_BUNDLE_AGE_DEPENDENCY_ERROR" == "$expected" ]]
}

write_tools
support_bundle_validate_age_dependency_at_root "$fixture" "$uid"

saved_ifs="$IFS"
IFS=$'\n\t'
support_bundle_validate_age_dependency_at_root "$fixture" "$uid"
IFS="$saved_ifs"

chmod 0775 "$fixture/usr/bin/age"
assert_rejected unsafe_mode "$fixture" "$uid"
write_tools

chmod 0757 "$fixture/usr/bin/age"
assert_rejected unsafe_mode "$fixture" "$uid"
write_tools

chmod 0644 "$fixture/usr/bin/age-keygen"
assert_rejected unsafe_mode "$fixture" "$uid"
write_tools

rm "$fixture/usr/bin/age"
ln -s /bin/sh "$fixture/usr/bin/age"
assert_rejected unsafe_file "$fixture" "$uid"
rm "$fixture/usr/bin/age"
write_tools

rm "$fixture/usr/bin/age-keygen"
mkdir "$fixture/usr/bin/age-keygen"
assert_rejected unsafe_file "$fixture" "$uid"
rmdir "$fixture/usr/bin/age-keygen"
write_tools

rm "$fixture/usr/bin/age-keygen"
mkfifo "$fixture/usr/bin/age-keygen"
assert_rejected unsafe_file "$fixture" "$uid"
rm "$fixture/usr/bin/age-keygen"
write_tools

assert_rejected unsafe_metadata "$fixture" "$((uid + 1))"
assert_rejected invalid_arguments relative "$uid"

rm "$fixture/usr/bin/age"
failure_output=$(support_bundle_validate_age_dependency_at_root "$fixture" "$uid" 2>&1 || true)
[[ -z "$failure_output" ]]

echo "support-bundle age dependency tests: PASS"

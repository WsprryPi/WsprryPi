#!/usr/bin/env bash
# Installer globals and service mocks are consumed by the sourced script.
# shellcheck disable=SC2034,SC2329
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/../install.sh"
precompiled_original_loge=$(declare -f logE)
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
logI() { :; }; logE() { :; }; logW() { :; }
expect_failure() { if "$@"; then echo "Expected failure: $*" >&2; exit 1; fi; }
contains_pkg() { printf '%s\n' "${RESOLVED_APT_PACKAGES[@]}" | grep -Fxq -- "$1"; }
INSTALL_RP1_GPCLK_DKMS=false
NO_WEB=true
BINARY_SOURCE=local
BINARY_RUNTIME_PACKAGES=(libgpiod2 libssl3)
resolve_apt_package_list
for bad in build-essential libssl-dev libsystemd-dev libgpiod-dev apache2 php; do
    ! contains_pkg "$bad" || exit 1
done
for good in python3 age chrony binutils libgpiod2 libssl3; do
    contains_pkg "$good" || exit 1
done
BUILD_RESOURCE_MEMINFO_PATH=/does/not/exist
preflight_build_resources
BINARY_SOURCE=build
resolve_apt_package_list
contains_pkg build-essential
BINARY_SOURCE=local
INSTALL_RP1_GPCLK_DKMS=true
resolve_apt_package_list
contains_pkg build-essential
contains_pkg dkms
INSTALL_RP1_GPCLK_DKMS=false
BINARY_SOURCE=bad
expect_failure validate_binary_options
BINARY_SOURCE=local
BINARY_RELEASE_TAG=v1
expect_failure validate_binary_options
BINARY_SOURCE=release
BINARY_RELEASE_TAG='../../bad'
expect_failure validate_binary_options
BINARY_RELEASE_TAG=v1
BINARY_PATH=/local
expect_failure validate_binary_options
BINARY_PATH=""; BINARY_RELEASE_TAG=""; BINARY_SOURCE=local
validate_binary_options
ACTION=uninstall
expect_failure validate_binary_options
ACTION=install
# Real file replacement in a fixture, with only service and root-ownership calls mocked.
BINARY_INSTALL_DIR="$fixture/bin"; BINARY_STAGE="$fixture/stage"
LOCAL_REPO_DIR="$fixture/repo"
mkdir -p "$BINARY_INSTALL_DIR" "$BINARY_STAGE"
printf old > "$BINARY_INSTALL_DIR/wsprrypi"
printf new > "$BINARY_STAGE/wsprrypi"
systemctl() { printf '%s ' "$@" >> "$fixture/services"; printf '\n' >> "$fixture/services"; return 0; }
install() { local args=("$@"); command cp "${args[${#args[@]}-2]}" "${args[${#args[@]}-1]}"; }
exec_command() {
    shift
    local args=() arg
    for arg in "$@"; do
        [[ -z "$arg" || "$arg" == "debug" ]] || args+=("$arg")
    done
    "${args[@]}"
}
DRY_RUN=true
manage_exe wsprrypi
[[ $(cat "$BINARY_INSTALL_DIR/wsprrypi") == old ]]
[[ ! -e "$fixture/services" ]]
DRY_RUN=false
manage_exe wsprrypi
[[ $(cat "$BINARY_INSTALL_DIR/wsprrypi") == new ]]
[[ "$WAS_RUNNING" == true ]]
rollback_executable
[[ $(cat "$BINARY_INSTALL_DIR/wsprrypi") == old ]]
# Failed publication preserves the old file and restores prior service state.
mv() { return 1; }
expect_failure manage_exe wsprrypi
[[ $(cat "$BINARY_INSTALL_DIR/wsprrypi") == old ]]
grep -q '^start ' "$fixture/services"
unset -f mv
# A failed rollback must retain the only recovery copy.
BINARY_INSTALLED=true
systemctl() { [[ "$1" == is-active ]]; }
expect_failure rollback_executable
[[ "$BINARY_INSTALLED" == recovery-failed ]]
expect_failure restore_daemon_state
previous="$BINARY_PREVIOUS"
cleanup_precompiled_executable
[[ -f "$previous" ]]
BINARY_INSTALLED=false
cleanup_precompiled_executable
[[ ! -e "$BINARY_STAGE/wsprrypi" ]]
REBOOT=true
DRY_RUN=false
flag_need_reboot </dev/null >"$fixture/reboot-notice" 2>"$fixture/reboot-errors"
[[ ! -s "$fixture/reboot-errors" ]]
grep -q 'sudo reboot' "$fixture/reboot-notice"
# Exercise helper failures through the real logger, without running the
# installer, package manager, application, or system services.
(
    eval "$precompiled_original_loge"
    LOG_FILE="$fixture/precompiled.log"
    LOG_OUTPUT=both
    LOG_LEVEL=INFO
    USE_CONSOLE=true
    export TERM=xterm
    init_log() { :; }
    setup_log
    BINARY_SOURCE=local
    BINARY_STAGE="$fixture/stage"
    LOCAL_REPO_DIR="$fixture/repo"
    SUDO_USER=pi
    DRY_RUN=false
    python3() {
        printf 'partial result\n'
        printf 'Precompiled executable rejected: unsupported libraries for bookworm: [libunknown.so.1]\nsecond diagnostic\n' >&2
        return 23
    }
    BINARY_VERSION=stale
    if validate_precompiled_runtime >"$fixture/helper-console" 2>"$fixture/helper-stderr"; then
        echo 'Expected runtime validation failure' >&2
        exit 1
    fi
    [[ -z "$BINARY_VERSION" && ! -s "$fixture/helper-stderr" ]]
    for destination in "$fixture/helper-console" "$LOG_FILE"; do
        grep -Eq '\[ERROR\].*unsupported libraries for bookworm' "$destination"
        grep -Eq '\[ERROR\].*second diagnostic' "$destination"
    done
    # Dry-run basic checks use the same error route.
    mkdir -p "$LOCAL_REPO_DIR/scripts"
    touch "$LOCAL_REPO_DIR/scripts/precompiled_binary.py"
    DRY_RUN=true
    if prepare_precompiled_executable >"$fixture/dry-console" 2>"$fixture/dry-stderr"; then
        echo 'Expected dry-run validation failure' >&2
        exit 1
    fi
    [[ ! -s "$fixture/dry-stderr" ]]
    grep -Eq '\[ERROR\].*unsupported libraries' "$fixture/dry-console"
    # Successful multiline fields retain exact argv and contain no log text.
    python3() {
        [[ "$1" == 'helper path' && "$2" == 'argument with spaces' ]]
        printf 'libgpiod2\nlibssl3\n'
    }
    packages=''
    run_precompiled_helper packages 'helper path' 'argument with spaces' >"$fixture/success-console" 2>"$fixture/success-stderr"
    [[ "$packages" == $'libgpiod2\nlibssl3' ]]
    [[ ! -s "$fixture/success-console" && ! -s "$fixture/success-stderr" ]]
    # Silent helper failures still produce a logged explanation and preserve
    # their status while clearing any stale result.
    python3() { return 23; }
    helper_status=0
    run_precompiled_helper packages helper >"$fixture/silent-console" || helper_status=$?
    [[ "$helper_status" == 23 && -z "$packages" ]]
    grep -Eq '\[ERROR\].*helper failed \(status 23\)' "$fixture/silent-console"
)
printf 'precompiled installer tests: PASS\n'

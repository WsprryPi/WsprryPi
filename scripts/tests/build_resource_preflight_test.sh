#!/usr/bin/env bash
# shellcheck disable=SC2034
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
INSTALLER="${SCRIPT_DIR}/../install.sh"
FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/wsprrypi-build-resource.XXXXXX")
trap 'rm -rf -- "$FIXTURE"' EXIT

# shellcheck source=../install.sh
# shellcheck disable=SC1091
source "$INSTALLER"
FGGLD='' RESET='' FGGRN='' FGRED='' MOVE_UP='' CLEAR_LINE=''

TEST_LOG="$FIXTURE/log"
MEMINFO="$FIXTURE/meminfo"
SWAPS="$FIXTURE/swaps"
SWAP_ROOT="$FIXTURE/build-swap"

logI() { printf '[INFO] %s\n' "${1:-}" >>"$TEST_LOG"; }
logW() { printf '[WARN] %s\n' "${1:-}" >>"$TEST_LOG"; }
logE() { printf '[ERROR] %s\n' "${1:-}" >>"$TEST_LOG"; }

fail() {
    printf 'build resource preflight test failed: %s\n' "$1" >&2
    exit 1
}

assert_eq() {
    [[ "$1" == "$2" ]] || fail "expected '$2', got '$1'"
}

write_meminfo() {
    printf 'MemTotal:       %s kB\nMemAvailable:   %s kB\n' "$1" "$2" >"$MEMINFO"
}

write_swaps_header() {
    printf 'Filename\tType\tSize\tUsed\tPriority\n' >"$SWAPS"
}

reset_case() {
    : >"$TEST_LOG"
    rm -rf -- "$SWAP_ROOT"
    write_swaps_header
    BUILD_RESOURCE_MEMINFO_PATH="$MEMINFO"
    BUILD_RESOURCE_SWAPS_PATH="$SWAPS"
    BUILD_RESOURCE_SWAP_ROOT="$SWAP_ROOT"
    BUILD_RESOURCE_DISK_AVAILABLE_KB=$((4 * 1024 * 1024))
    ALLOW_TEMP_BUILD_SWAP=auto
    BINARY_SOURCE=build
    INSTALL_RP1_GPCLK_DKMS=false
    DRY_RUN=false
    JOBS=4
    TEMP_BUILD_SWAP_PATH=""
    TEMP_BUILD_SWAP_OWNED=false
    TEMP_BUILD_SWAP_ACTIVE=false
    TEMP_BUILD_SWAP_ROOT_OWNED=false
}

# Mock only the privileged swap operations. Real temporary paths and ownership
# state exercise attribution and cleanup without enabling host swap.
fallocate() {
    local path="${*: -1}"
    TEST_ALLOCATED_KB="${2%K}"
    : >"$path"
}
mkswap() { return "${TEST_MKSWAP_STATUS:-0}"; }
swapon() {
    [[ "${TEST_SWAPON_STATUS:-0}" -eq 0 ]] || return "$TEST_SWAPON_STATUS"
    local path="${*: -1}"
    printf '%s\tfile\t%s\t0\t10\n' "$path" "$((TEST_ALLOCATED_KB - 16))" >>"$SWAPS"
}
swapoff() {
    local path="$1"
    if [[ "${TEST_SWAPOFF_STATUS:-0}" -ne 0 ]]; then
        return "$TEST_SWAPOFF_STATUS"
    fi
    awk -v target="$path" 'NR == 1 || $1 != target' "$SWAPS" >"$SWAPS.next"
    mv "$SWAPS.next" "$SWAPS"
}
stat() {
    case "$1" in
        -c)
            case "$2" in
                %a) printf '700\n' ;;
                %u) printf '%s\n' "$EUID" ;;
                *) return 1 ;;
            esac
            ;;
        *) command stat "$@" ;;
    esac
}

# Source-time defaults and explicit overrides must survive initialization.
# Expand installer variables in a fresh shell, avoiding inherited readonly globals.
# shellcheck disable=SC2016
assert_eq "$(env -u ALLOW_TEMP_BUILD_SWAP bash -c 'source "$1"; printf "%s" "$ALLOW_TEMP_BUILD_SWAP"' bash "$INSTALLER")" auto
for policy in auto true false; do
    assert_eq "$(ALLOW_TEMP_BUILD_SWAP="$policy" bash -c 'source "$1"; printf "%s" "$ALLOW_TEMP_BUILD_SWAP"' bash "$INSTALLER")" "$policy"
done

# Exercise real preflight decisions at either side of both RAM boundaries.
# Every allocation here uses the privileged-operation mocks above.
for policy in auto true false; do
    for memory_kb in 262144 524288 786431 786432 1048576 1572863 1572864 2097152; do
        reset_case
        ALLOW_TEMP_BUILD_SWAP="$policy"
        write_meminfo "$memory_kb" "$((128 * 1024))"
        if ((memory_kb < 786432)); then
            expected_swap_kb=2097152
        elif ((memory_kb < 1572864)); then
            expected_swap_kb=1048576
        else
            expected_swap_kb=0
        fi
        if [[ "$policy" == false ]] && ((expected_swap_kb > 0)); then
            if preflight_build_resources; then
                fail "opt-out unexpectedly passed at ${memory_kb} KiB"
            fi
            [[ ! -e "$SWAP_ROOT" ]] || fail "opt-out allocated swap"
        else
            preflight_build_resources
            if ((expected_swap_kb > 0)); then
                [[ -f "$TEMP_BUILD_SWAP_PATH" ]] || fail "missing swap for $policy at ${memory_kb} KiB"
                assert_eq "$TEST_ALLOCATED_KB" "$((expected_swap_kb + 1024))"
                if [[ "$policy" == auto ]]; then
                    grep -q 'Automatic temporary build swap selected' "$TEST_LOG" || fail "automatic allocation was not explained"
                fi
                cleanup_temp_build_swap
            else
                [[ ! -e "$SWAP_ROOT" ]] || fail "swap allocated at or above 1.5 GiB"
            fi
        fi
        if ((expected_swap_kb > 0)); then
            assert_eq "$JOBS" 1
        else
            assert_eq "$JOBS" 4
        fi
    done
done

# A precompiled application bypasses resources unless provider compilation is selected.
for binary_source in local release; do
    reset_case
    BINARY_SOURCE="$binary_source"
    BUILD_RESOURCE_MEMINFO_PATH="$FIXTURE/absent-meminfo"
    preflight_build_resources
    [[ ! -e "$SWAP_ROOT" ]] || fail "precompiled-only install allocated swap"
    assert_eq "$JOBS" 4
    INSTALL_RP1_GPCLK_DKMS=true
    BUILD_RESOURCE_MEMINFO_PATH="$MEMINFO"
    write_meminfo 524288 262144
    preflight_build_resources
    [[ -f "$TEMP_BUILD_SWAP_PATH" ]] || fail "provider build skipped automatic swap"
    assert_eq "$JOBS" 1
    cleanup_temp_build_swap
done

assert_eq "$(required_non_zram_build_swap_kb $((256 * 1024)))" "$((2048 * 1024))"
assert_eq "$(required_non_zram_build_swap_kb $((512 * 1024)))" "$((2048 * 1024))"
assert_eq "$(required_non_zram_build_swap_kb $((768 * 1024)))" "$((1024 * 1024))"
assert_eq "$(required_non_zram_build_swap_kb $((1024 * 1024)))" "$((1024 * 1024))"
assert_eq "$(required_non_zram_build_swap_kb $((1536 * 1024)))" "0"
assert_eq "$(required_non_zram_build_swap_kb $((2048 * 1024)))" "0"

reset_case
{
    printf '/dev/zram0\tpartition\t524288\t131072\t100\n'
    printf '/var/swapfile\tfile\t1048576\t262144\t-2\n'
    printf '/dev/mmcblk0p3\tpartition\t262144\t0\t-3\n'
} >>"$SWAPS"
IFS=' ' read -r total_free zram_free file_free partition_free other_free \
    independent_free < <(build_resource_swap_free_kb "$SWAPS")
assert_eq "$total_free" "1441792"
assert_eq "$zram_free" "393216"
assert_eq "$file_free" "786432"
assert_eq "$partition_free" "262144"
assert_eq "$other_free" "0"
assert_eq "$independent_free" "1048576"

# Explicit opt-out on a 512 MiB-class zram-only host fails and forces -j1.
reset_case
ALLOW_TEMP_BUILD_SWAP=false
write_meminfo "$((512 * 1024))" "$((300 * 1024))"
printf '/dev/zram0\tpartition\t524288\t0\t100\n' >>"$SWAPS"
if preflight_build_resources; then
    fail "zram-only low-memory host unexpectedly passed"
fi
assert_eq "$JOBS" "1"
[[ ! -e "$SWAP_ROOT" ]] || fail "opt-out preflight mutated the filesystem"
grep -q 'zram swap is not counted' "$TEST_LOG" || fail "missing zram classification warning"

for policy in auto true false; do
    # Adequate independently backed swap passes without creating anything.
    reset_case
    ALLOW_TEMP_BUILD_SWAP="$policy"
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    printf '/var/existing-swap\tfile\t2097136\t0\t-2\n' >>"$SWAPS"
    preflight_build_resources
    assert_eq "$JOBS" "1"
    [[ ! -e "$SWAP_ROOT" ]] || fail "adequate existing swap was replaced"

done

# Larger systems retain their requested build parallelism.
reset_case
write_meminfo "$((4096 * 1024))" "$((2048 * 1024))"
preflight_build_resources
assert_eq "$JOBS" "4"

# Invalid policy values fail closed.
reset_case
write_meminfo "$((512 * 1024))" "$((300 * 1024))"
ALLOW_TEMP_BUILD_SWAP=yes
if preflight_build_resources; then
    fail "invalid swap policy value unexpectedly passed"
fi

for policy in auto true; do
    # Zram supplies no independently backed capacity, even in automatic mode.
    reset_case
    ALLOW_TEMP_BUILD_SWAP="$policy"
    write_meminfo 524288 262144
    printf '/dev/zram0\tpartition\t2097152\t0\t100\n' >>"$SWAPS"
    preflight_build_resources
    assert_eq "$TEST_ALLOCATED_KB" "$((2048 * 1024 + 1024))"
    cleanup_temp_build_swap
    grep -q '^/dev/zram0' "$SWAPS" || fail "existing zram was modified"

    # Dry-run reports creation and cleanup without creating a directory or file.
    reset_case
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    DRY_RUN=true
    preflight_build_resources
    [[ ! -e "$SWAP_ROOT" ]] || fail "dry-run created swap state"
    grep -q 'Dry run: would create' "$TEST_LOG" || fail "dry-run omitted creation plan"
    grep -q 'disable and remove only' "$TEST_LOG" || fail "dry-run omitted cleanup plan"

    # Insufficient disk fails without creating swap state.
    reset_case
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    BUILD_RESOURCE_DISK_AVAILABLE_KB=$((2300 * 1024))
    if preflight_build_resources; then
        fail "insufficient disk unexpectedly passed"
    fi
    [[ ! -e "$SWAP_ROOT" ]] || fail "disk-space failure created swap state"

    # A symlink is never accepted as the protected swap root.
    reset_case
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    mkdir -p "$FIXTURE/swap-target"
    ln -s "$FIXTURE/swap-target" "$SWAP_ROOT"
    if preflight_build_resources; then
        fail "symlink swap root unexpectedly passed"
    fi
    [[ -z "$(find "$FIXTURE/swap-target" -mindepth 1 -print -quit)" ]] ||
        fail "symlink swap root received an owned file"

    # Successful creation is attributable, idempotent within the invocation, and
    # preserves pre-existing swap while removing only the owned file.
    reset_case
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    printf '/var/existing-swap\tfile\t1048576\t0\t-2\n' >>"$SWAPS"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    preflight_build_resources
    owned_path="$TEMP_BUILD_SWAP_PATH"
    [[ -f "$owned_path" ]] || fail "temporary swap file was not created"
    assert_eq "$TEST_ALLOCATED_KB" "$((1024 * 1024 + 1024))"
    temp_build_swap_is_active "$owned_path" || fail "temporary swap was not activated"
    preflight_build_resources
    assert_eq "$TEMP_BUILD_SWAP_PATH" "$owned_path"
    assert_eq "$(awk 'NR > 1 && $1 ~ /wsprrypi-build/ { count++ } END { print count + 0 }' "$SWAPS")" "1"
    cleanup_temp_build_swap
    [[ ! -e "$owned_path" ]] || fail "owned swap file survived cleanup"
    grep -q '^/var/existing-swap' "$SWAPS" || fail "pre-existing swap was modified"

    # A pre-existing protected root is retained after the owned file is cleaned.
    reset_case
    mkdir -m 700 "$SWAP_ROOT"
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    preflight_build_resources
    cleanup_temp_build_swap
    [[ -d "$SWAP_ROOT" ]] || fail "pre-existing swap root was removed"

    # Creation failure removes the incomplete owned file and directory.
    reset_case
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    TEST_MKSWAP_STATUS=7
    if preflight_build_resources; then
        fail "mkswap failure unexpectedly passed"
    fi
    unset TEST_MKSWAP_STATUS
    [[ -z "$TEMP_BUILD_SWAP_PATH" ]] || fail "failed creation retained ownership state"
    [[ ! -e "$SWAP_ROOT" ]] || fail "failed creation retained its directory"

    # Activation failure also removes incomplete owned state.
    reset_case
    ALLOW_TEMP_BUILD_SWAP="$policy"
    write_meminfo 524288 262144
    TEST_SWAPON_STATUS=8
    if preflight_build_resources; then
        fail "swapon failure unexpectedly passed"
    fi
    unset TEST_SWAPON_STATUS
    [[ -z "$TEMP_BUILD_SWAP_PATH" ]] || fail "failed activation retained ownership state"
    [[ ! -e "$SWAP_ROOT" ]] || fail "failed activation retained its directory"

    # Failed swapoff preserves the active file and recovery state.
    reset_case
    write_meminfo "$((512 * 1024))" "$((300 * 1024))"
    ALLOW_TEMP_BUILD_SWAP="$policy"
    preflight_build_resources
    owned_path="$TEMP_BUILD_SWAP_PATH"
    TEST_SWAPOFF_STATUS=9
    if cleanup_temp_build_swap; then
        fail "swapoff failure unexpectedly passed"
    fi
    unset TEST_SWAPOFF_STATUS
    [[ -f "$owned_path" ]] || fail "active swap file was removed after swapoff failure"
    temp_build_swap_is_active "$owned_path" || fail "active swap inventory was lost after swapoff failure"
    cleanup_temp_build_swap

    # A handled termination runs EXIT cleanup, preserves pre-existing swap, and
    # keeps the signal-derived failure status.
    set +e
    (
        trap egress EXIT
        trap 'exit 143' TERM
        reset_case
        write_meminfo "$((512 * 1024))" "$((300 * 1024))"
        printf '/var/existing-swap\tfile\t1048576\t0\t-2\n' >>"$SWAPS"
        ALLOW_TEMP_BUILD_SWAP="$policy"
        preflight_build_resources
        kill -TERM "$BASHPID"
    )
    signal_status=$?
    set -e
    assert_eq "$signal_status" "143"
    [[ ! -e "$SWAP_ROOT" ]] || fail "handled termination retained owned swap state"
    grep -q '^/var/existing-swap' "$SWAPS" || fail "handled termination modified pre-existing swap"

done

# The installed signal traps must funnel handled signals through EXIT cleanup.
grep -Fq "trap 'exit 130' INT" "$INSTALLER" || fail "INT trap is missing"
grep -Fq "trap 'exit 143' TERM" "$INSTALLER" || fail "TERM trap is missing"
grep -Fq "trap 'exit 129' HUP" "$INSTALLER" || fail "HUP trap is missing"
awk '/egress\(\)/,/^}/' "$INSTALLER" | grep -q 'cleanup_temp_build_swap' ||
    fail "EXIT cleanup does not own temporary swap cleanup"
if bash -c '
    source "$1"
    report_ui_publication_result() { :; }
    cleanup_rp1_gpclk_dkms_state() { :; }
    cleanup_temp_build_swap() { return 1; }
    egress
' bash "$INSTALLER"; then
    fail "EXIT cleanup failure did not produce a failing process status"
fi
awk '
    /^_main\(\)/ { in_main = 1 }
    in_main && /start_script/ && !start { start = NR }
    in_main && /preflight_build_resources/ && !preflight { preflight = NR }
    in_main && /handle_apt_packages/ && !packages { packages = NR }
    END { exit !(start && preflight && packages && start < preflight && preflight < packages) }
' "$INSTALLER" || fail "resource preflight is not between confirmation and package mutation"

printf 'build resource preflight tests: PASS\n'

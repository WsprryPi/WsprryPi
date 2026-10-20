#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPOSITORY_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
TEST_ROOT=$(mktemp -d /tmp/wsprrypi-support-bundle-install-test.XXXXXX)
trap 'rm -rf "$TEST_ROOT"' EXIT

declare -a EXECUTED_OPERATIONS=()
declare -a EXECUTED_DEBUG_FLAGS=()
DEBUG_PRINTS=0
DRY_RUN=false

debug_print() {
    [[ "${2:-}" == "debug" ]] && DEBUG_PRINTS=$((DEBUG_PRINTS + 1))
}
logD() { :; }
logE() { :; }
exec_command() {
    local operation="$1"
    shift
    local debug="${!#}"
    set -- "${@:1:$(($# - 1))}"
    EXECUTED_OPERATIONS+=("$operation")
    EXECUTED_DEBUG_FLAGS+=("$debug")
    [[ "$DRY_RUN" == "true" ]] && return 0
    "$@"
}

# shellcheck source=scripts/support_bundle_runtime_install.sh
source "${REPOSITORY_ROOT}/scripts/support_bundle_runtime_install.sh"

collector_path() { printf '%s\n' "$1/usr/local/lib/wsprrypi/collect-support-bundle.sh"; }
storage_path() { printf '%s\n' "$1/var/lib/wsprrypi/support-bundles"; }
assert_mode() { [[ $(stat -c '%a' "$1") == "$2" ]]; }
assert_recorded() {
    local expected="$1"
    local operation
    for operation in "${EXECUTED_OPERATIONS[@]}"; do
        [[ "$operation" == "$expected" ]] && return 0
    done
    return 1
}
assert_debug_propagated() {
    local debug
    for debug in "${EXECUTED_DEBUG_FLAGS[@]}"; do
        [[ "$debug" == "debug" ]] || return 1
    done
}

stage="$TEST_ROOT/stage"
mkdir -p "$stage"
support_bundle_runtime_provision install "$REPOSITORY_ROOT" "$stage" debug
collector=$(collector_path "$stage")
storage=$(storage_path "$stage")

[[ -f "$collector" && -d "$storage" ]]
cmp -s "$REPOSITORY_ROOT/scripts/collect-support-bundle.sh" "$collector"
assert_mode "$collector" 755
assert_mode "$storage" 700
[[ $(stat -c '%u' "$collector") == $(id -u) ]]
[[ $(stat -c '%u' "$storage") == $(id -u) ]]
assert_recorded "Create support-bundle collector directory"
assert_recorded "Set support-bundle collector directory ownership"
assert_recorded "Set support-bundle collector directory permissions"
assert_recorded "Create support-bundle storage directory"
assert_recorded "Set support-bundle storage ownership"
assert_recorded "Set support-bundle storage permissions"
assert_recorded "Copy support-bundle collector"
assert_recorded "Set support-bundle collector ownership"
assert_recorded "Set support-bundle collector permissions"
assert_recorded "Publish support-bundle collector"
assert_debug_propagated
(( DEBUG_PRINTS > 0 ))

printf 'preserve\n' >"$storage/existing-bundle.tar.gz"
support_bundle_runtime_provision install "$REPOSITORY_ROOT" "$stage" debug
[[ -f "$storage/existing-bundle.tar.gz" ]]

fixture="$TEST_ROOT/fixture"
mkdir -p "$fixture/scripts"
printf '#!/usr/bin/env bash\necho upgraded\n' >"$fixture/scripts/collect-support-bundle.sh"
chmod 0755 "$fixture/scripts/collect-support-bundle.sh"
support_bundle_runtime_provision install "$fixture" "$stage" debug
cmp -s "$fixture/scripts/collect-support-bundle.sh" "$collector"
[[ -f "$storage/existing-bundle.tar.gz" ]]

support_bundle_runtime_provision uninstall "$REPOSITORY_ROOT" "$stage" debug
[[ ! -e "$collector" && -f "$storage/existing-bundle.tar.gz" ]]
assert_recorded "Remove support-bundle collector"
[[ ! -e "$stage/var/www/usr/local/lib/wsprrypi/collect-support-bundle.sh" ]]
[[ ! -e "$stage/var/www/var/lib/wsprrypi/support-bundles" ]]

dry_stage="$TEST_ROOT/dry-run"
mkdir -p "$dry_stage"
before_operations=${#EXECUTED_OPERATIONS[@]}
DRY_RUN=true
support_bundle_runtime_provision install "$REPOSITORY_ROOT" "$dry_stage" debug
DRY_RUN=false
(( ${#EXECUTED_OPERATIONS[@]} > before_operations ))
[[ ! -e "$dry_stage/usr" && ! -e "$dry_stage/var" ]]
assert_debug_propagated

for collision in collector-parent-file storage-file; do
    collision_stage="$TEST_ROOT/$collision"
    mkdir -p "$collision_stage"
    if [[ "$collision" == collector-parent-file ]]; then
        mkdir -p "$collision_stage/usr/local/lib"
        : >"$collision_stage/usr/local/lib/wsprrypi"
    else
        mkdir -p "$collision_stage/var/lib/wsprrypi"
        : >"$collision_stage/var/lib/wsprrypi/support-bundles"
    fi
    ! support_bundle_runtime_provision install "$REPOSITORY_ROOT" "$collision_stage" debug
done

collision_stage="$TEST_ROOT/collector-link"
mkdir -p "$collision_stage/usr/local/lib/wsprrypi"
ln -s "$REPOSITORY_ROOT/scripts/collect-support-bundle.sh" \
    "$collision_stage/usr/local/lib/wsprrypi/collect-support-bundle.sh"
! support_bundle_runtime_provision install "$REPOSITORY_ROOT" "$collision_stage" debug

for collision in collector-parent-link storage-link; do
    collision_stage="$TEST_ROOT/$collision"
    mkdir -p "$collision_stage"
    if [[ "$collision" == collector-parent-link ]]; then
        mkdir -p "$collision_stage/usr/local/lib" "$TEST_ROOT/target-parent"
        ln -s "$TEST_ROOT/target-parent" "$collision_stage/usr/local/lib/wsprrypi"
    else
        mkdir -p "$collision_stage/var/lib/wsprrypi" "$TEST_ROOT/target-storage"
        ln -s "$TEST_ROOT/target-storage" "$collision_stage/var/lib/wsprrypi/support-bundles"
    fi
    ! support_bundle_runtime_provision install "$REPOSITORY_ROOT" "$collision_stage" debug
done

echo "support-bundle-runtime-install tests: PASS"

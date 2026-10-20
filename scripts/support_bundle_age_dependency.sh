#!/usr/bin/env bash

# Validate the distribution-provided age runtime without PATH lookup. Production
# callers use support_bundle_validate_age_dependency; the rooted entry point is
# a test-only seam for deterministic filesystem fixtures.

SUPPORT_BUNDLE_AGE_DEPENDENCY_ERROR=""

support_bundle_age_dependency_fail() {
    SUPPORT_BUNDLE_AGE_DEPENDENCY_ERROR="$1"
    return 1
}

support_bundle_age_file_metadata() {
    local path="$1"
    if stat -c '%u %a' "$path" 2>/dev/null; then
        return 0
    fi
    stat -f '%u %Lp' "$path" 2>/dev/null
}

support_bundle_validate_age_dependency_at_root() {
    local root="$1"
    local expected_uid="$2"
    local executable metadata uid mode mode_value

    # Consumed by the installer after this function returns.
    # shellcheck disable=SC2034
    SUPPORT_BUNDLE_AGE_DEPENDENCY_ERROR=""
    [[ "$root" == /* && "$expected_uid" =~ ^[0-9]+$ ]] ||
        support_bundle_age_dependency_fail invalid_arguments || return 1

    for executable in age age-keygen; do
        local path="${root%/}/usr/bin/${executable}"
        if [[ -L "$path" || ! -f "$path" ]]; then
            support_bundle_age_dependency_fail unsafe_file
            return 1
        fi
        metadata=$(support_bundle_age_file_metadata "$path") || {
            support_bundle_age_dependency_fail metadata_unavailable
            return 1
        }
        IFS=' ' read -r uid mode <<<"$metadata"
        if [[ "$uid" != "$expected_uid" || ! "$mode" =~ ^[0-7]{3,4}$ ]]; then
            support_bundle_age_dependency_fail unsafe_metadata
            return 1
        fi
        mode_value=$((8#$mode))
        if (( (mode_value & 0022) != 0 || (mode_value & 0111) == 0 )); then
            support_bundle_age_dependency_fail unsafe_mode
            return 1
        fi
    done
    return 0
}

support_bundle_validate_age_dependency() {
    support_bundle_validate_age_dependency_at_root / 0
}

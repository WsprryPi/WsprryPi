#!/usr/bin/env bash
set -u
set -o pipefail

log() {
    printf "%s\n" "$*" >&2
}

repo_root() {
    git rev-parse --show-toplevel
}

sync_submodules() {
    log "[githooks] Syncing submodules"

    git submodule sync --recursive || return 1
    git submodule update --init --recursive || return 1
}

check_submodules_aligned() {
    local bad=0

    while IFS= read -r line; do
        case "$line" in
            -*)
                log "[githooks] ERROR: Uninitialized submodule: $line"
                bad=1
                ;;
            +*)
                log "[githooks] ERROR: Submodule not at recorded commit: $line"
                bad=1
                ;;
            U*)
                log "[githooks] ERROR: Submodule conflict: $line"
                bad=1
                ;;
        esac
    done < <(git submodule status --recursive)

    return $bad
}

check_submodules_clean() {
    if ! git submodule foreach --recursive \
        'git diff --quiet && git diff --cached --quiet' \
        >/dev/null 2>&1; then
        log "[githooks] ERROR: Submodules contain local changes"
        return 1
    fi

    return 0
}

validate_submodules() {
    check_submodules_aligned || return 1
    check_submodules_clean || return 1
}

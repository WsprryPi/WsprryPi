#!/usr/bin/env bash

# Support-bundle runtime provisioning is deliberately separate from the web
# installer path.  The optional staging root is a test-only seam; production
# callers pass an empty value and therefore use the fixed absolute locations.

support_bundle_runtime_provision() {
    local action="$1"
    local repository_root="$2"
    local staging_root="${3:-}"
    local debug="${4:-}"
    local owner="root:root"
    local collector_parent collector storage source temporary

    if [[ -n "$staging_root" ]]; then
        owner="$(id -u):$(id -g)"
    fi

    collector_parent="${staging_root}/usr/local/lib/wsprrypi"
    collector="${collector_parent}/collect-support-bundle.sh"
    storage="${staging_root}/var/lib/wsprrypi/support-bundles"
    source="${repository_root}/scripts/collect-support-bundle.sh"

    case "$action" in
    install)
        if [[ ! -f "$source" || -L "$source" ]]; then
            logE "Support-bundle collector source is unavailable."
            return 1
        fi

        if [[ -L "$collector_parent" ]] ||
            [[ -e "$collector_parent" && ! -d "$collector_parent" ]]; then
            logE "Support-bundle collector directory is unsafe."
            return 1
        fi
        debug_print "Provisioning support-bundle collector directory." "$debug"
        exec_command "Create support-bundle collector directory" \
            mkdir -p "$collector_parent" "$debug" || return 1
        if [[ "$DRY_RUN" != "true" ]] &&
            { [[ -L "$collector_parent" ]] || [[ ! -d "$collector_parent" ]]; }; then
            logE "Support-bundle collector directory is unsafe."
            return 1
        fi
        exec_command "Set support-bundle collector directory ownership" \
            chown "$owner" "$collector_parent" "$debug" || return 1
        exec_command "Set support-bundle collector directory permissions" \
            chmod 0755 "$collector_parent" "$debug" || return 1

        if [[ -L "$storage" ]] || [[ -e "$storage" && ! -d "$storage" ]]; then
            logE "Support-bundle storage directory is unsafe."
            return 1
        fi
        debug_print "Provisioning private support-bundle storage." "$debug"
        exec_command "Create support-bundle storage directory" \
            mkdir -p "$storage" "$debug" || return 1
        if [[ "$DRY_RUN" != "true" ]] &&
            { [[ -L "$storage" ]] || [[ ! -d "$storage" ]]; }; then
            logE "Support-bundle storage directory is unsafe."
            return 1
        fi
        exec_command "Set support-bundle storage ownership" \
            chown "$owner" "$storage" "$debug" || return 1
        exec_command "Set support-bundle storage permissions" \
            chmod 0700 "$storage" "$debug" || return 1

        if [[ -L "$collector" ]] || [[ -e "$collector" && ! -f "$collector" ]]; then
            logE "Support-bundle collector path is unsafe."
            return 1
        fi
        if [[ "$DRY_RUN" == "true" ]]; then
            temporary="${collector_parent}/.collect-support-bundle.sh.dry-run"
            logD "Exec (dry): mktemp ${collector_parent}/.collect-support-bundle.sh.XXXXXX"
        else
            debug_print "Allocating support-bundle collector temporary file." "$debug"
            temporary=$(mktemp "${collector_parent}/.collect-support-bundle.sh.XXXXXX") || {
                logE "Failed to allocate support-bundle collector temporary file."
                return 1
            }
        fi
        if ! exec_command "Copy support-bundle collector" \
            install -m 0755 "$source" "$temporary" "$debug" ||
            ! exec_command "Set support-bundle collector ownership" \
            chown "$owner" "$temporary" "$debug" ||
            ! exec_command "Set support-bundle collector permissions" \
            chmod 0755 "$temporary" "$debug" ||
            ! exec_command "Publish support-bundle collector" \
            mv -f "$temporary" "$collector" "$debug"; then
            exec_command "Remove incomplete support-bundle collector" \
                rm -f "$temporary" "$debug" || true
            logE "Failed to install support-bundle collector."
            return 1
        fi
        ;;
    uninstall)
        if [[ -L "$collector" ]]; then
            logE "Support-bundle collector path is unsafe."
            return 1
        fi
        if [[ -e "$collector" && ! -f "$collector" ]]; then
            logE "Support-bundle collector path is unsafe."
            return 1
        fi
        debug_print "Removing support-bundle collector." "$debug"
        exec_command "Remove support-bundle collector" rm -f "$collector" "$debug" || return 1
        # Preserve storage: it can contain administrator-collected diagnostics.
        ;;
    *)
        return 1
        ;;
    esac
}

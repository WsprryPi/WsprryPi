#!/usr/bin/env bash
# Only called by the established package lifecycle; respects its dry-run guard.
pico_usb_runtime_provision() {
    local action="$1" repository="$2" debug="${3:-}"
    local diagnostics="" output="" status=0
    local invoking_user="${SUDO_USER:-}"
    if [[ "$action" == "install" && ( -z "$invoking_user" || "$invoking_user" == "root" ) ]]; then
        logE "Pico USB access setup requires the ordinary user who invoked sudo."
        return 1
    fi
    if [[ "$DRY_RUN" != "true" ]]; then
        diagnostics=$(mktemp -d "${TMPDIR:-/tmp}/wsprrypi-pico-deploy.XXXXXX") || return 1
        output="$diagnostics/output.log"
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$output" \
        exec_command "Deploy identified shared Pico USB installer" \
        /usr/bin/python3 "${repository}/scripts/pico_usb_deploy.py" "$action" --repository "$repository" "$debug"; then
        status=1
    fi
    [[ -z "$diagnostics" ]] || rm -rf -- "$diagnostics"
    [[ "$status" -eq 0 ]] || return "$status"
    if [[ "$action" == "install" ]]; then
        exec_command "Add invoking user to Pico USB access group" \
            usermod -aG dialout -- "$invoking_user" "$debug" || return 1
        if [[ "$DRY_RUN" != "true" ]]; then
            logI 'Group membership in dialout was necessary for USB access; your user has been added to that group.'
        fi
    fi
    exec_command "Reload Pico USB permission rules" udevadm control --reload-rules "$debug"
}

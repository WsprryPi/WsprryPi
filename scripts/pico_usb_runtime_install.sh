#!/usr/bin/env bash
# Only called by the established package lifecycle; respects its dry-run guard.
pico_usb_runtime_provision() {
    local action="$1" repository="$2" debug="${3:-}"
    exec_command "Deploy identified shared Pico USB installer" \
        /usr/bin/python3 "${repository}/scripts/pico_usb_deploy.py" "$action" --repository "$repository" "$debug" || return 1
    exec_command "Reload Pico USB permission rules" udevadm control --reload-rules "$debug"
}

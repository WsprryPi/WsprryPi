#!/bin/bash
set -euo pipefail
# Invoke only through: sudo unshare --mount --net --fork --kill-child -- bash SCRIPT BUNDLE
bundle=$(realpath -e -- "$1")
[[ "$bundle" == /tmp/wsprrypi-controller-native.* ]]
[[ $EUID -eq 0 && ${SUDO_UID:-0} -ne 0 && ${SUDO_GID:-0} -ne 0 ]]
[[ $(readlink /proc/self/ns/mnt) != $(readlink /proc/1/ns/mnt) ]]
host_network_namespace=$(readlink /proc/1/ns/net)
[[ $(readlink /proc/self/ns/net) != "$host_network_namespace" ]]
[[ $(stat -c %u -- "$bundle") == "$SUDO_UID" ]]
user_home=$(getent passwd "$SUDO_UID" | cut -d: -f6)
[[ -d "$user_home" && "$user_home" == /home/* && "$user_home" != /home/ ]]
[[ -d "$bundle/home" && ! -L "$bundle/home" && -d "$bundle/empty-run" && ! -L "$bundle/empty-run" ]]
[[ $(cat "$bundle/home/.issue475-private-home") == 'controller qualification' ]]
# Do not retain a cwd descriptor into the account's original home after masking it.
cd "$bundle"
mount --make-rprivate /
mount --bind "$bundle/home" "$user_home"
mount --bind "$bundle/empty-run" /run
ip link set lo up
exec setpriv --reuid="$SUDO_UID" --regid="$SUDO_GID" --clear-groups --bounding-set=-all --inh-caps=-all --ambient-caps=-all --no-new-privs \
    env -i HOME="$user_home" PATH=/usr/bin:/bin LD_LIBRARY_PATH="$bundle/lib" \
    python3 "$bundle/controller_profile_production_test.py" --binary "$bundle/wsprrypi" --strace "$bundle/strace" --evidence "$bundle/evidence" --host-network-namespace "$host_network_namespace"

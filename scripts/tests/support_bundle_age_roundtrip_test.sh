#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /absolute/path/to/support_bundle_age_roundtrip_test" >&2
    exit 64
fi

fixture=$1
if [[ $fixture != /* || ! -x $fixture ]]; then
    echo "round-trip fixture must be an absolute executable path" >&2
    exit 64
fi

umask 077

age_bin=$(command -v age || true)
age_keygen_bin=$(command -v age-keygen || true)
if [[ -z $age_bin || -z $age_keygen_bin ]]; then
    echo "Debian packaged age and age-keygen are required" >&2
    exit 69
fi

private_root=$(mktemp -d "${TMPDIR:-/tmp}/wsprrypi-age-qualification.XXXXXX")
chmod 0700 "$private_root"
cleanup() {
    rm -rf -- "$private_root"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

identity=$private_root/identity.txt
"$age_keygen_bin" -o "$identity" >/dev/null 2>"$private_root/keygen.log"
chmod 0600 "$identity"
recipient=$("$age_keygen_bin" -y "$identity")
if [[ $recipient != age1* ]]; then
    echo "age-keygen did not return an X25519 recipient" >&2
    exit 70
fi

echo "age version: $("$age_bin" --version)"
"$fixture" "$age_bin" "$identity" "$recipient"
echo "ephemeral identity cleanup: armed"

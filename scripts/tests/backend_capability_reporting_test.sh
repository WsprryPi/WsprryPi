#!/usr/bin/env bash
set -euo pipefail

binary=${1:?binary path required}
expected=${2:?expected backend list required}
omitted=${3:-}
expected_ancillary=${4:-enabled}

actual=$($binary --list-backends)
test "$actual" = "$expected"

$binary --version 2>&1 | grep -F "Compiled backends: $expected" >/dev/null
$binary --help 2>&1 | grep -F "Compiled backends: $expected" >/dev/null
$binary --version 2>&1 | grep -F "Ancillary GPIO: $expected_ancillary" >/dev/null
$binary --help 2>&1 | grep -F "Ancillary GPIO: $expected_ancillary" >/dev/null

if [[ -n "$omitted" ]]; then
    output_file=$(mktemp)
    trap 'rm -f "$output_file"' EXIT
    if $binary --backend "$omitted" AA0NT EM18 20 20m >"$output_file" 2>&1; then
        echo "omitted backend unexpectedly succeeded: $omitted" >&2
        exit 1
    fi
    grep -F "Backend '$omitted' is valid but unavailable in this build." "$output_file" >/dev/null || {
        cat "$output_file" >&2
        exit 1
    }
    grep -F "Compiled backends: $expected." "$output_file" >/dev/null || {
        cat "$output_file" >&2
        exit 1
    }

    if $binary --backend definitely-invalid AA0NT EM18 20 20m >"$output_file" 2>&1; then
        echo "invalid backend unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "Invalid backend. Expected 'gpio', 'rp1-gpclk', 'si5351', or 'simulated'." "$output_file" >/dev/null || {
        cat "$output_file" >&2
        exit 1
    }
fi

echo "backend capability reporting passed: $expected"

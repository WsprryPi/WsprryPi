#!/usr/bin/env bash
set -euo pipefail

binary=${1:?binary path required}

if [ "$(id -u)" -ne 0 ]; then
    output_file=$(mktemp)
    trap 'rm -f "$output_file"' EXIT
    if "$binary" --backend si5351 --use-led AA0NT EM18 20 20m >"$output_file" 2>&1; then
        echo "ancillary GPIO request unexpectedly succeeded" >&2
        exit 1
    fi
    if grep -F "must be run as root" "$output_file" >/dev/null; then
        echo "GPIO-free Si5351 executable retained the unconditional root gate" >&2
        cat "$output_file" >&2
        exit 1
    fi
    grep -F "Ancillary GPIO is unavailable in this build" "$output_file" >/dev/null || {
        echo "non-root strict-profile request did not reach configuration validation" >&2
        cat "$output_file" >&2
        exit 1
    }
    rm -f "$output_file"
    trap - EXIT
fi

../scripts/tests/backend_capability_reporting_test.sh \
    "$binary" si5351 simulated disabled

if ldd "$binary" | grep -i gpiod >/dev/null; then
    echo "strict I2C executable unexpectedly links libgpiod" >&2
    exit 1
fi

if nm -C "$binary" | grep -E 'gpiod::|[[:space:]]gpiod_' >/dev/null; then
    echo "strict I2C executable unexpectedly references a gpiod symbol" >&2
    exit 1
fi

for arguments in \
    "--backend si5351 --use-led AA0NT EM18 20 20m" \
    "--backend si5351 --amp-pin 17 AA0NT EM18 20 20m" \
    "--backend si5351 --use-shutdown AA0NT EM18 20 20m" \
    "--backend si5351 --test-tone 14095600@22"
do
    output_file=$(mktemp)
    trap 'rm -f "$output_file"' EXIT
    if "$binary" $arguments >"$output_file" 2>&1; then
        echo "ancillary GPIO request unexpectedly succeeded: $arguments" >&2
        exit 1
    fi
    grep -F "Ancillary GPIO is unavailable in this build" "$output_file" >/dev/null || {
        echo "missing ancillary GPIO diagnostic for: $arguments" >&2
        cat "$output_file" >&2
        exit 1
    }
    if grep -E '/dev/i2c|Si5351 transmission is unavailable' "$output_file" >/dev/null; then
        echo "ancillary rejection reached Si5351 probing: $arguments" >&2
        cat "$output_file" >&2
        exit 1
    fi
    rm -f "$output_file"
    trap - EXIT
done

echo "strict I2C profile passed"

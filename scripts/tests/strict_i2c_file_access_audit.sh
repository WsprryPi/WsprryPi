#!/usr/bin/env bash
set -euo pipefail

binary=${1:?binary path required}
audit_bus=2147483646
expected_i2c_path="/dev/i2c-${audit_bus}"
trace_file=$(mktemp)
output_file=$(mktemp)
worker_trace_file=$(mktemp)
worker_output_file=$(mktemp)
trap 'rm -f "$trace_file" "$output_file" "$worker_trace_file" "$worker_output_file"' EXIT

if [ "$(id -u)" -eq 0 ]; then
    echo "strict I2C file-access audit must run as a non-root account" >&2
    exit 1
fi

if [ -e "$expected_i2c_path" ] || [ -L "$expected_i2c_path" ]; then
    echo "audit requires a nonexistent I2C path: $expected_i2c_path" >&2
    exit 1
fi

if strace -f -e trace=open,openat,ioctl,read,write \
    -o "$trace_file" \
    "$binary" --no-web --backend si5351 \
        --si5351-i2c-bus "$audit_bus" AA0NT EM18 20 20m \
        >"$output_file" 2>&1
then
    echo "strict I2C audit invocation unexpectedly succeeded" >&2
    exit 1
fi

# Application validation rejects an unavailable bus before opening any adapter.
grep -F "Si5351 transmission is unavailable" "$output_file" >/dev/null || {
    echo "missing selected-bus validation diagnostic" >&2
    cat "$output_file" >&2
    exit 1
}
if grep -E '/dev/i2c-[0-9]+' "$trace_file" >/dev/null; then
    echo "unavailable-bus validation unexpectedly reached an I2C open" >&2
    exit 1
fi
if grep -F "must be run as root" "$output_file" >/dev/null; then
    echo "strict I2C audit was rejected by the legacy root gate" >&2
    cat "$output_file" >&2
    exit 1
fi

# Exercise the actual isolated worker against only the verified nonexistent
# path. No adapter can be opened and no I2C ioctl can occur. Its result is JSON,
# not an application startup failure. The ordinary CI hardware guard stays set
# for every other invocation; only this negative-path worker bypasses it.
strace -f -e trace=open,openat,ioctl,read,write -o "$worker_trace_file" \
    env -u WSPRRYPI_DISABLE_HARDWARE_ACCESS \
    "$binary" --internal-si5351-inventory "$audit_bus" 27000000 96 \
    >"$worker_output_file" 2>&1
cat "$worker_trace_file" >>"$trace_file"
python3 - "$worker_output_file" "$audit_bus" "$expected_i2c_path" <<'PY'
import json, pathlib, sys
reply = json.loads(pathlib.Path(sys.argv[1]).read_text())
assert reply['bus'] == int(sys.argv[2])
assert reply['addresses'] == []
assert f"Open failed for {sys.argv[3]}" in reply['error'], reply
PY

if ! grep -E \
    "open(at)?\\(.*\"${expected_i2c_path}\", O_RDWR\\|O_CLOEXEC(\\) = -1 ENOENT| <unfinished \\.\\.\\.>)" \
    "$trace_file" >/dev/null
then
    echo "trace did not contain the expected I2C open attempt: $expected_i2c_path" >&2
    cat "$trace_file" >&2
    exit 1
fi

observed_i2c_paths=$(grep -Eo '/dev/i2c-[0-9]+' "$trace_file" | sort -u)
if [ "$observed_i2c_paths" != "$expected_i2c_path" ]; then
    echo "trace contained an unexpected I2C path" >&2
    printf '%s\n' "$observed_i2c_paths" >&2
    exit 1
fi

if grep -E \
    '/dev/(mem|gpiomem|vcio|gpiochip[0-9]*|rp1-gpclk[0-9]*)|/sys/bus/platform/devices/[^ ]*/resource[0-9]*' \
    "$trace_file" >/dev/null
then
    echo "trace contained a forbidden GPIO, mailbox, MMIO, or RP1 path" >&2
    grep -E \
        '/dev/(mem|gpiomem|vcio|gpiochip[0-9]*|rp1-gpclk[0-9]*)|/sys/bus/platform/devices/[^ ]*/resource[0-9]*' \
        "$trace_file" >&2
    exit 1
fi

if grep -E 'ioctl\([^,]+, (I2C_|0x070)' "$trace_file" >/dev/null; then
    echo "audit unexpectedly reached an I2C ioctl" >&2
    grep -E 'ioctl\([^,]+, (I2C_|0x070)' "$trace_file" >&2
    exit 1
fi

echo "strict I2C file-access audit passed: $expected_i2c_path only"

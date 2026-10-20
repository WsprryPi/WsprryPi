#!/bin/sh
set -eu

binary=./build/bin/wsprrypi_debug
trace=/tmp/wsprrypi-simulated-trace.json
first=/tmp/wsprrypi-simulated-trace-first.json

run_simulator() {
    "$binary" \
        --backend simulated \
        --no-web \
        --qrss-message E \
        --qrss-frequency 14097100 \
        --qrss-dot-seconds 0.01
}

run_simulator
cp "$trace" "$first"

strace -f -e trace=openat,open \
    -o /tmp/wsprrypi-simulator.strace \
    "$binary" \
        --backend simulated \
        --no-web \
        --qrss-message E \
        --qrss-frequency 14097100 \
        --qrss-dot-seconds 0.01

cmp "$first" "$trace"
grep -q '"backend":"simulated"' "$trace"
grep -q '"kind":"complete"' "$trace"
grep -q '"kind":"cleanup"' "$trace"
echo "Simulated backend trace is deterministic and complete."

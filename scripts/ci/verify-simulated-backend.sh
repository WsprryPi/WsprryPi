#!/bin/sh
set -eu

binary=./build/bin/wsprrypi_debug
trace=/tmp/wsprrypi-simulated-trace.json
first=$(mktemp /tmp/wsprrypi-simulated-first.XXXXXX)
lock=/tmp/wsprrypi-simulated-ci.lock
mkdir "$lock" || { rm -f "$first"; echo 'Another simulator check owns the fixed CLI trace path.' >&2; exit 1; }
trap 'rm -f "$first"; rmdir "$lock"' EXIT
trap 'exit 130' HUP INT TERM

run_simulator() {
    timeout --foreground --signal=TERM --kill-after=2s 10s "$binary" \
        --backend simulated \
        --no-web \
        --qrss-message E \
        --qrss-frequency 14097100 \
        --qrss-dot-seconds 0.01
}

rm -f "$trace"
run_simulator
[ -f "$trace" ] && [ ! -L "$trace" ] && [ -s "$trace" ]
cp "$trace" "$first"

rm -f "$trace" /tmp/wsprrypi-simulator.strace
strace -f -e trace=openat,open \
    -o /tmp/wsprrypi-simulator.strace \
    timeout --foreground --signal=TERM --kill-after=2s 10s "$binary" \
        --backend simulated \
        --no-web \
        --qrss-message E \
        --qrss-frequency 14097100 \
        --qrss-dot-seconds 0.01

[ -f "$trace" ] && [ ! -L "$trace" ] && [ -s "$trace" ]
cmp "$first" "$trace"
python3 - "$trace" <<'PY_TRACE'
import json, math, sys
with open(sys.argv[1], encoding="utf-8") as source:
    trace = json.load(source)
assert type(trace["schema_version"]) is int and trace["schema_version"] == 1
assert trace["backend"] == "simulated" and trace["mode"] == "QRSS"
assert type(trace["plan_id"]) is int and trace["plan_id"] == 1
assert type(trace["request_id"]) is int and trace["request_id"] == 1
events = trace["events"]
assert [e["kind"] for e in events] == ["configure", "configured", "execution_start", "event", "complete", "cleanup"]
assert [e["event_index"] for e in events] == [-1, -1, -1, 0, -1, -1]
assert [e["logical_ns"] for e in events] == [0, 0, 0, 0, 10_000_000, 0]
assert [e["rf_on"] for e in events] == [False, False, False, True, False, False]
assert all(type(e["rf_on"]) is bool and type(e["event_index"]) is int and type(e["logical_ns"]) is int for e in events)
assert all(e["detail"] == "" for e in events)
assert all(type(e["frequency_hz"]) in (int, float) and math.isfinite(e["frequency_hz"]) for e in events)
reference = 14_097_100 + 1.5 * 12_000 / 8192
assert all(math.isclose(e["frequency_hz"], reference, rel_tol=0, abs_tol=1e-6) for e in events[:3])
assert events[3]["frequency_hz"] == 14_097_100
assert events[4]["frequency_hz"] == events[5]["frequency_hz"] == 0
print("Fresh QRSS trace: exact E event, logical duration, identities, completion and cleanup passed.")
PY_TRACE

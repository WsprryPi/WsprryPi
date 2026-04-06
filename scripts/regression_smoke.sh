#!/usr/bin/env bash
set -euo pipefail

# Top-level regression smoke script for recent WsprryPi integration work.
#
# Default behavior:
# - Runs non-RF checks automatically
# - Prints manual RF checklist
#
# Optional:
#   RUN_RF=1 ./scripts/regression_smoke.sh
#
# When RUN_RF=1:
# - Runs real RF/manual-style checks that require sudo -n and time
# - Captures logs and verifies paired two-slot timing from output

SRC_ROOT="$(git rev-parse --show-toplevel)"
SRC_ROOT="${SRC_ROOT}/src"

REF_DIR="${SRC_ROOT}/WSPR-Reference"
REF_BUILD_DIR="${REF_DIR}/build"
BIN_DIR="${SRC_ROOT}/build/bin"
LOG_DIR="${SRC_ROOT}/build/regression_logs"

mkdir -p "${LOG_DIR}"

pass() { printf "[PASS] %s\n" "$1"; }
fail() { printf "[FAIL] %s\n" "$1" >&2; exit 1; }
step() { printf "\n==> %s\n" "$1"; }

require_file() {
    local path="$1"
    [[ -e "$path" ]] || fail "Missing required path: $path"
}

run_and_capture() {
    local outfile="$1"
    shift
    bash -lc "$*" >"${outfile}" 2>&1
}

run_and_check() {
    local desc="$1"
    local cmd="$2"
    shift 2

    local outfile
    outfile="$(mktemp "${LOG_DIR}/check.XXXXXX.log")"

    if ! run_and_capture "${outfile}" "$cmd"; then
        cat "${outfile}" >&2
        fail "$desc"
    fi

    local needle
    for needle in "$@"; do
        grep -Fq -- "$needle" "${outfile}" || {
            cat "${outfile}" >&2
            fail "$desc (missing: $needle)"
        }
    done

    pass "$desc"
}

check_timestamp_prefix_present() {
    local file="$1"
    grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} .* \[INFO \]' "$file" ||
        fail "Expected timestamp prefix in ${file}"
}

check_timestamp_prefix_absent() {
    local file="$1"
    grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} .* \[INFO \]' "$file" &&
        fail "Did not expect timestamp prefix in ${file}"
}

step "Sanity-check project layout"
require_file "${SRC_ROOT}/Makefile"
require_file "${BIN_DIR}/wsprrypi"
require_file "${REF_BUILD_DIR}/wspr-encode"
require_file "${REF_BUILD_DIR}/wspr-decode"
require_file "${REF_BUILD_DIR}/wspr-correlate"
pass "Project layout looks correct"

step "Build checks"
run_and_check \
    "Top-level release build" \
    "cd '${SRC_ROOT}' && make release" \
    "Release build completed successfully."

run_and_check \
    "Top-level debug build" \
    "cd '${SRC_ROOT}' && make debug" \
    "Debug build completed successfully."

step "Reference regression binaries"
run_and_check \
    "Golden vector regression" \
    "cd '${REF_BUILD_DIR}' && ./verify_vectors" \
    "Summary: PASS"

run_and_check \
    "Payload roundtrip regression" \
    "cd '${REF_BUILD_DIR}' && ./payload_compare_roundtrip" \
    "Summary: PASS"

run_and_check \
    "Type 1/2/3 unpack roundtrips" \
    "cd '${REF_BUILD_DIR}' && ./unpack_type1_roundtrip && ./unpack_type2_roundtrip && ./unpack_type3_roundtrip" \
    "Summary: PASS"

run_and_check \
    "Planning and paired encode smoke" \
    "cd '${REF_BUILD_DIR}' && ./plan_transmission_smoke && ./encode_paired_smoke" \
    "Summary: PASS"

run_and_check \
    "Correlator and Type 2 ambiguity/matrix regressions" \
    "cd '${REF_BUILD_DIR}' && ./correlator_smoke && ./unpack_type2_ambiguity && ./unpack_type2_matrix" \
    "Summary: PASS"

step "CLI/help behavior (startup only, non-RF validation)"
run_and_check \
    "Default help text" \
    "cd '${SRC_ROOT}' && ./build/bin/wsprrypi --help" \
    "Usage:" \
   -- "--test-tone"

default_log="${LOG_DIR}/default_startup.log"
run_and_capture "${default_log}" \
    "cd '${SRC_ROOT}' && timeout 2s sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m || true"
grep -Fq "Logging backend: streams" "${default_log}" || fail "Default logging backend was not streams"
check_timestamp_prefix_absent "${default_log}"
pass "Default logging backend is streams with no timestamps"

timestamp_log="${LOG_DIR}/timestamp_startup.log"
run_and_capture "${timestamp_log}" \
    "cd '${SRC_ROOT}' && timeout 2s sudo -n ./build/bin/wsprrypi -D AA0NT EM18 20 80m || true"
grep -Fq "Logging backend: streams" "${timestamp_log}" || fail "Timestamp mode did not stay on streams"
check_timestamp_prefix_present "${timestamp_log}"
pass "-D enables timestamps on streams"

journald_log="${LOG_DIR}/journald_startup.log"
run_and_capture "${journald_log}" \
    "cd '${SRC_ROOT}' && timeout 2s sudo -n ./build/bin/wsprrypi --journald AA0NT EM18 20 80m || true"
grep -Fq "Logging backend: journald" "${journald_log}" || fail "Explicit journald mode did not activate"
check_timestamp_prefix_absent "${journald_log}"
pass "--journald selects journald without timestamp prefixes"

journald_timestamp_log="${LOG_DIR}/journald_timestamp_startup.log"
run_and_capture "${journald_timestamp_log}" \
    "cd '${ROOT_DIR}' && timeout 2s sudo -n ./build/bin/wsprrypi --journald -D AA0NT EM18 20 80m || true"
grep -Fq "Logging backend: journald" "${journald_timestamp_log}" || fail "Combined journald + -D did not activate journald"
check_timestamp_prefix_absent "${journald_timestamp_log}"
pass "--journald overrides timestamp prefixes"

run_and_check \
    "Explicit Type 3 input reaches planner" \
    "cd '${SRC_ROOT}' && timeout 2s sudo -n ./build/bin/wsprrypi '<AA0NT>' EM18IG 20 80m || true" \
    "Prepared WSPR plan type: Type3Single, frames: 1."

run_and_check \
    "Require-paired reaches paired planner" \
    "cd '${SRC_ROOT}' && timeout 2s sudo -n ./build/bin/wsprrypi --require-paired W1/AA0NT EM18IG 20 80m || true" \
    "Prepared WSPR plan type: Type2Type3Paired, frames: 2"

if [[ "${RUN_RF:-0}" != "1" ]]; then
    step "Manual RF checklist"
    cat <<'EOF'

Set RUN_RF=1 to execute RF/integration validation automatically.

Suggested manual checks:

1. Type 1 one-shot
   sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m

2. Explicit Type 3 one-shot
   sudo -n ./build/bin/wsprrypi "<AA0NT>" EM18IG 20 80m

3. Forced paired two-slot run
   sudo -n ./build/bin/wsprrypi -D --require-paired W1/AA0NT EM18IG 20 80m

4. Test tone sanity
   sudo -n ./build/bin/wsprrypi --test-tone 3.5686MHz

EOF
    pass "Non-RF regression smoke completed"
    exit 0
fi

step "RF/integration checks"

type1_rf_log="${LOG_DIR}/rf_type1.log"
run_and_capture "${type1_rf_log}" \
    "cd '${SRC_ROOT}' && timeout 180s sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m || true"
grep -Fq "Prepared WSPR plan type: Type1Single, frames: 1." "${type1_rf_log}" || fail "Type 1 plan not detected"
grep -Fq "Started transmission:" "${type1_rf_log}" || fail "Type 1 transmission did not start"
grep -Fq "Completed transmission: 110.592" "${type1_rf_log}" || fail "Type 1 transmission duration incorrect"
grep -Fq "Shutdown requested: completed configured TX iterations" "${type1_rf_log}" || fail "Type 1 shutdown did not occur"
pass "Type 1 one-shot RF behavior looks correct"

type3_rf_log="${LOG_DIR}/rf_type3.log"
run_and_capture "${type3_rf_log}" \
    "cd '${SRC_ROOT}' && timeout 180s sudo -n ./build/bin/wsprrypi '<AA0NT>' EM18IG 20 80m || true"
grep -Fq "Prepared WSPR plan type: Type3Single, frames: 1." "${type3_rf_log}" || fail "Type 3 plan not detected"
grep -Fq "Completed transmission: 110.592" "${type3_rf_log}" || fail "Type 3 transmission duration incorrect"
pass "Explicit Type 3 one-shot RF behavior looks correct"

paired_rf_log="${LOG_DIR}/rf_paired.log"
run_and_capture "${paired_rf_log}" \
    "cd '${ROOT_DIR}' && timeout 360s sudo -n ./build/bin/wsprrypi -D --require-paired W1/AA0NT EM18IG 20 80m || true"

grep -Fq "Prepared WSPR plan type: Type2Type3Paired, frames: 2" "${paired_rf_log}" || fail "Paired plan not detected"
grep -Fq "Scheduling paired WSPR frame 2 of 2 for the next WSPR slot." "${paired_rf_log}" || fail "Frame 2 was not scheduled for the next slot"

grep -Fq "Completed transmission: 110.592" "${paired_rf_log}" || fail "Expected per-slot transmission duration not found"

python3 - "$paired_rf_log" <<'PY'
import re
import sys
from datetime import datetime

path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read().splitlines()

starts = []
for line in text:
    if "Started transmission:" in line:
        m = re.match(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})", line)
        if m:
            starts.append(datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S"))

if len(starts) != 2:
    print(f"Expected exactly 2 timestamped transmission starts, found {len(starts)}", file=sys.stderr)
    sys.exit(1)

delta = int((starts[1] - starts[0]).total_seconds())
if delta != 120:
    print(f"Expected paired frame start delta of 120 seconds, found {delta}", file=sys.stderr)
    sys.exit(1)
PY

grep -Fq "Shutdown requested: completed configured TX iterations" "${paired_rf_log}" || fail "Paired shutdown did not occur after plan completion"
pass "Paired two-slot RF behavior looks correct"

tone_rf_log="${LOG_DIR}/rf_tone.log"
run_and_capture "${tone_rf_log}" \
    "cd '${SRC_ROOT}' && timeout 5s sudo -n ./build/bin/wsprrypi --test-tone 3.5686MHz || true"
grep -Fq "Started transmission:" "${tone_rf_log}" || fail "Test tone did not start"
pass "Test tone startup looks correct"

pass "RF/integration regression smoke completed"

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

GIT_ROOT="$(git rev-parse --show-toplevel)"
SRC_ROOT="${GIT_ROOT}/src"

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

run_log_check() {
    local desc="$1"
    local outfile="$2"
    local cmd="$3"
    shift 3

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
    if ! grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} ' "$file"; then
        cat "$file" >&2
        fail "Expected timestamp prefix in ${file}"
    fi
}

check_timestamp_prefix_absent() {
    local file="$1"
    if grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} ' "$file"; then
        cat "$file" >&2
        fail "Did not expect timestamp prefix in ${file}"
    fi
}

check_log_contains() {
    local file="$1"
    local needle="$2"
    grep -Fq -- "$needle" "$file" || {
        cat "$file" >&2
        fail "Missing expected log line in ${file}: ${needle}"
    }
}

check_log_absent() {
    local file="$1"
    local needle="$2"
    if grep -Fq -- "$needle" "$file"; then
        cat "$file" >&2
        fail "Unexpected log line in ${file}: ${needle}"
    fi
}

check_command_fails() {
    local desc="$1"
    local cmd="$2"
    shift 2

    local outfile
    outfile="$(mktemp "${LOG_DIR}/check.XXXXXX.log")"

    if run_and_capture "${outfile}" "$cmd"; then
        cat "${outfile}" >&2
        fail "$desc (command unexpectedly succeeded)"
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

check_log_count() {
    local file="$1"
    local needle="$2"
    local expected="$3"
    local actual
    actual="$(grep -F -- "$needle" "$file" | wc -l | tr -d ' ')"
    [[ "${actual}" == "${expected}" ]] || {
        cat "$file" >&2
        fail "Expected ${expected} occurrences of '${needle}' in ${file}, found ${actual}"
    }
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
    "--test-tone"

default_log="${LOG_DIR}/default_startup.log"
run_log_check \
    "Default logging backend is streams with no timestamps" \
    "${default_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi AA0NT EM18 20 80m || true" \
    "Logging backend: streams"
check_log_contains "${default_log}" "Transmit GPIO: 4"
check_log_contains "${default_log}" "Frequency-entry control GPIO polarity: active low"
check_log_contains "${default_log}" "Selected frequency entry control GPIO: none for 80m."
check_timestamp_prefix_absent "${default_log}"
check_log_contains "${default_log}" \
    "Selected WSPR plan: Type1Single, frames: 1, paired requested: false, auto-upgraded: false."
check_log_absent "${default_log}" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters."
check_log_absent "${default_log}" \
    "Paired WSPR planning explicitly requested."

timestamp_log="${LOG_DIR}/timestamp_startup.log"
run_log_check \
    "-D enables timestamps on streams" \
    "${timestamp_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi -D AA0NT EM18 20 80m || true" \
    "Logging backend: streams"
check_timestamp_prefix_present "${timestamp_log}"

journald_log="${LOG_DIR}/journald_startup.log"
run_log_check \
    "--journald selects journald without timestamp prefixes" \
    "${journald_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --journald AA0NT EM18 20 80m || true" \
    "Logging backend: journald"
check_timestamp_prefix_absent "${journald_log}"

journald_timestamp_log="${LOG_DIR}/journald_timestamp_startup.log"
run_log_check \
    "--journald overrides timestamp prefixes" \
    "${journald_timestamp_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --journald -D AA0NT EM18 20 80m || true" \
    "Logging backend: journald"
check_timestamp_prefix_absent "${journald_timestamp_log}"

custom_tx_gpio_log="${LOG_DIR}/custom_tx_gpio_startup.log"
run_log_check \
    "--transmit-gpio overrides the default WSPR RF GPIO" \
    "${custom_tx_gpio_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --transmit-gpio 20 AA0NT EM18 20 80m || true" \
    "Transmit GPIO: 20" \
    "Selected WSPR plan: Type1Single, frames: 1, paired requested: false, auto-upgraded: false."

mixed_freq_gpio_log="${LOG_DIR}/mixed_frequency_gpio_startup.log"
run_log_check \
    "Mixed frequency list accepts optional @GPIO suffixes" \
    "${mixed_freq_gpio_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi AA0NT EM18 20 80m@17,40m@27,20m,14.097100MHz@22 || true" \
    "Selected frequency entry control GPIO: 17 (active low) for 80m." \
    "Selected WSPR plan: Type1Single, frames: 1, paired requested: false, auto-upgraded: false."

high_polarity_log="${LOG_DIR}/frequency_gpio_high_polarity.log"
run_log_check \
    "--tx-gpio-polarity high is accepted" \
    "${high_polarity_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --tx-gpio-polarity high AA0NT EM18 20 80m@17 || true" \
    "Frequency-entry control GPIO polarity: active high" \
    "Selected frequency entry control GPIO: 17 (active high) for 80m."

low_polarity_log="${LOG_DIR}/frequency_gpio_low_polarity.log"
run_log_check \
    "--tx-gpio-polarity low is accepted" \
    "${low_polarity_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --tx-gpio-polarity low AA0NT EM18 20 80m@17 || true" \
    "Frequency-entry control GPIO polarity: active low" \
    "Selected frequency entry control GPIO: 17 (active low) for 80m."

test_tone_gpio_log="${LOG_DIR}/test_tone_tx_gpio.log"
run_log_check \
    "--transmit-gpio also applies to test-tone mode" \
    "${test_tone_gpio_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --transmit-gpio 20 --test-tone 7040100 || true" \
    "Transmit GPIO: 20" \
    "A direct RF test tone will be generated at:"

check_command_fails \
    "Invalid transmit GPIO is rejected" \
    "cd '${SRC_ROOT}' && sudo -n ./build/bin/wsprrypi --transmit-gpio 17 AA0NT EM18 20 80m" \
    "Invalid transmit GPIO. Supported GPIO values: 4, 20."

check_command_fails \
    "Invalid @GPIO syntax is rejected" \
    "cd '${SRC_ROOT}' && sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m@,40m@27" \
    "Invalid frequency token '80m@': GPIO value is missing after @."

check_command_fails \
    "Invalid frequency-entry GPIO is rejected" \
    "cd '${SRC_ROOT}' && sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m@99,40m@27" \
    "Invalid frequency token '80m@99': GPIO suffix must be between 0 and 27."

check_command_fails \
    "Invalid frequency-entry GPIO polarity is rejected" \
    "cd '${SRC_ROOT}' && sudo -n ./build/bin/wsprrypi --tx-gpio-polarity sideways AA0NT EM18 20 80m@17" \
    "Invalid TX GPIO polarity. Expected 'high' or 'low'."

run_and_check \
    "Explicit Type 3 input reaches planner" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi '<AA0NT>' EM18IG 20 80m || true" \
    "Selected WSPR plan: Type3Single, frames: 1, paired requested: false, auto-upgraded: false."
type3_startup_log="${LOG_DIR}/type3_startup.log"
run_log_check \
    "Explicit Type 3 does not auto-upgrade to paired" \
    "${type3_startup_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi '<AA0NT>' EM18IG 20 80m || true" \
    "Selected WSPR plan: Type3Single, frames: 1, paired requested: false, auto-upgraded: false."
check_log_absent "${type3_startup_log}" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters."
check_log_absent "${type3_startup_log}" \
    "Paired WSPR planning explicitly requested."

forced_paired_startup_log="${LOG_DIR}/forced_paired_startup.log"
run_log_check \
    "Require-paired reaches paired planner" \
    "${forced_paired_startup_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --require-paired W1/AA0NT EM18IG 20 80m || true" \
    "Paired WSPR planning explicitly requested." \
    "Selected WSPR plan: Type2Type3Paired, frames: 2, paired requested: true, auto-upgraded: false."
check_log_absent "${forced_paired_startup_log}" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters."

auto_paired_log="${LOG_DIR}/auto_paired_startup.log"
run_log_check \
    "Auto-upgrade to paired WSPR plan works" \
    "${auto_paired_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi W1/AA0NT EM18IG 20 80m || true" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters." \
    "Selected WSPR plan: Type2Type3Paired, frames: 2, paired requested: false, auto-upgraded: true."
check_log_absent "${auto_paired_log}" "Type1Single"
check_log_absent "${auto_paired_log}" \
    "Paired WSPR planning explicitly requested."

if [[ "${RUN_RF:-0}" != "1" ]]; then
    step "Manual RF checklist"
    cat <<'EOF'

Set RUN_RF=1 to execute RF/integration validation automatically.

Before running the manual checks, refresh sudo credentials once:
  sudo -v

Suggested manual checks:

1. Type 1 one-shot
   sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m

   Expect log lines:
   - Selected WSPR plan: Type1Single, frames: 1, paired requested: false, auto-upgraded: false.
   - Waiting for next transmission window.
   - Started transmission:
   - Completed transmission: 110.592
   - Shutdown requested: completed configured TX iterations

2. Explicit Type 3 one-shot
   sudo -n ./build/bin/wsprrypi "<AA0NT>" EM18IG 20 80m

   Expect log lines:
   - Selected WSPR plan: Type3Single, frames: 1, paired requested: false, auto-upgraded: false.
   - Waiting for next transmission window.
   - Started transmission:
   - Completed transmission: 110.592
   - Shutdown requested: completed configured TX iterations

3. Auto-upgraded paired two-slot run
   sudo -n ./build/bin/wsprrypi -D W1/AA0NT EM18IG 20 80m

   Expect log lines:
   - Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters.
   - Selected WSPR plan: Type2Type3Paired, frames: 2, paired requested: false, auto-upgraded: true.
   - Scheduling paired WSPR frame 2 of 2 for the next WSPR slot.
   - Started transmission:
   - Completed transmission: 110.592
   - Shutdown requested: completed configured TX iterations

4. Forced paired two-slot run
   sudo -n ./build/bin/wsprrypi -D --require-paired W1/AA0NT EM18IG 20 80m

   Expect log lines:
   - Paired WSPR planning explicitly requested.
   - Selected WSPR plan: Type2Type3Paired, frames: 2, paired requested: true, auto-upgraded: false.
   - Scheduling paired WSPR frame 2 of 2 for the next WSPR slot.
   - Started transmission:
   - Completed transmission: 110.592
   - Shutdown requested: completed configured TX iterations

5. Test tone sanity
   sudo -n ./build/bin/wsprrypi --test-tone 3.5686MHz

   Expect log lines:
   - Started transmission:
   - no WSPR slot wait
   - clean Ctrl-C or timeout shutdown

EOF
    pass "Non-RF regression smoke completed"
    exit 0
fi

step "RF/integration checks"

type1_rf_log="${LOG_DIR}/rf_type1.log"
run_log_check \
    "Type 1 one-shot RF behavior looks correct" \
    "${type1_rf_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 180s sudo -n ./build/bin/wsprrypi AA0NT EM18 20 80m || true" \
    "Selected WSPR plan: Type1Single, frames: 1, paired requested: false, auto-upgraded: false." \
    "Started transmission:" \
    "Completed transmission: 110.592" \
    "Shutdown requested: completed configured TX iterations"

type3_rf_log="${LOG_DIR}/rf_type3.log"
run_log_check \
    "Explicit Type 3 one-shot RF behavior looks correct" \
    "${type3_rf_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 180s sudo -n ./build/bin/wsprrypi '<AA0NT>' EM18IG 20 80m || true" \
    "Selected WSPR plan: Type3Single, frames: 1, paired requested: false, auto-upgraded: false." \
    "Completed transmission: 110.592"

paired_rf_log="${LOG_DIR}/rf_paired.log"
run_log_check \
    "Forced paired two-slot RF behavior log checks" \
    "${paired_rf_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 360s sudo -n ./build/bin/wsprrypi -D --tx-gpio-polarity high --require-paired W1/AA0NT EM18IG 20 20m@17 || true" \
    "Selected frequency entry control GPIO: 17 (active high) for 20m." \
    "Paired WSPR planning explicitly requested." \
    "Selected WSPR plan: Type2Type3Paired, frames: 2, paired requested: true, auto-upgraded: false." \
    "Scheduling paired WSPR frame 2 of 2 for the next WSPR slot." \
    "Completed transmission: 110.592" \
    "Shutdown requested: completed configured TX iterations"
check_log_absent "${paired_rf_log}" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters."
check_log_count "${paired_rf_log}" \
    "Paired WSPR planning explicitly requested." 1
check_log_count "${paired_rf_log}" \
    "Scheduling paired WSPR frame 2 of 2 for the next WSPR slot." 1
check_log_count "${paired_rf_log}" \
    "Selected frequency entry control GPIO: 17 (active high) for 20m." 2

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
pass "Forced paired two-slot RF timing looks correct"

auto_paired_rf_log="${LOG_DIR}/rf_auto_paired.log"
run_log_check \
    "Auto-upgraded paired two-slot RF behavior log checks" \
    "${auto_paired_rf_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 360s sudo -n ./build/bin/wsprrypi -D W1/AA0NT EM18IG 20 80m || true" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters." \
    "Selected WSPR plan: Type2Type3Paired, frames: 2, paired requested: false, auto-upgraded: true." \
    "Scheduling paired WSPR frame 2 of 2 for the next WSPR slot." \
    "Completed transmission: 110.592" \
    "Shutdown requested: completed configured TX iterations"
check_log_absent "${auto_paired_rf_log}" \
    "Paired WSPR planning explicitly requested."
check_log_absent "${auto_paired_rf_log}" "Type1Single"
check_log_count "${auto_paired_rf_log}" \
    "Auto-upgrading to paired WSPR plan because callsign is compound and locator is 6 characters." 1
check_log_count "${auto_paired_rf_log}" \
    "Scheduling paired WSPR frame 2 of 2 for the next WSPR slot." 1

python3 - "$auto_paired_rf_log" <<'PY'
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
pass "Auto-upgraded paired two-slot RF timing looks correct"

tone_rf_log="${LOG_DIR}/rf_tone.log"
run_log_check \
    "Test tone startup looks correct" \
    "${tone_rf_log}" \
    "cd '${SRC_ROOT}' && timeout --foreground 5s sudo -n stdbuf -oL -eL ./build/bin/wsprrypi --test-tone 3.5686MHz || true" \
    "Started transmission:"

pass "RF/integration regression smoke completed"

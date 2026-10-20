#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${WSPR_REFERENCE_BUILD_DIR:-${REPO_ROOT}/build}"

TYPE1_SYMBOLS="132000023020111000302321113000022230012300222012112033210003103022213010303230030032332021321030223220203223221310310213010221110002210302110200222310303320011002"
TYPE2_SUFFIX_SYMBOLS="112000003022111202322101133202202230012102202010112033210203123022213032323030210230310223301012203220023221203112310033010221310002232300130200202310303322031200"
TYPE2_PREFIX_SYMBOLS="112002223220131200302101113000002230010300202210112233010021101022233030303232210232332223101232223022023023221110310213030023110000210102130220202310103320031200"
TYPE2_AMBIGUOUS_SYMBOLS="132002003022131200302103113000202032012302202010112033210003103020213230323230210032330223321012203020003223203112310233030021110000212302110000222110303120031200"
TYPE3_SYMBOLS="332020201202313022320301333200202232012102022030312013210201121200031232123032232210330021321030203220021223023130110233030221332200212120310022200330323102211000"
PAIRED_TYPE3_SYMBOLS="${TYPE3_SYMBOLS}"

TYPE1_QUIET_EXPECTED="TYPE1 AA0NT EM18 20"
TYPE2_SUFFIX_QUIET_EXPECTED="TYPE2 <hashed>/12 20"
TYPE2_PREFIX_QUIET_EXPECTED="TYPE2 W0/<hashed> 20"
TYPE2_AMBIGUOUS_QUIET_EXPECTED="TYPE2 <hashed>/Z 20 ALT /09"
TYPE3_QUIET_EXPECTED="TYPE3 <hashed> 19169 EM18IG 20"
CORRELATED_SUFFIX_QUIET_EXPECTED="CORRELATED TYPE2 <callsign>/12 19169 EM18IG 20"
CORRELATED_PREFIX_QUIET_EXPECTED="CORRELATED TYPE2 W0/<callsign> 19169 EM18IG 20"
CORRELATED_AMBIGUOUS_QUIET_EXPECTED="CORRELATED TYPE2 <callsign>/Z 19169 EM18IG 20"

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local label="$3"

    if [[ "${haystack}" != *"${needle}"* ]]; then
        printf "ASSERTION FAILED: %s\n" "${label}" >&2
        printf "Expected to find: %s\n" "${needle}" >&2
        exit 1
    fi
}

run_and_assert() {
    local label="$1"
    local cmd="$2"
    shift 2
    local output

    printf "== %s ==\n" "${label}"
    output="$(eval "${cmd}")"
    printf "%s\n\n" "${output}"

    while (( $# > 0 )); do
        assert_contains "${output}" "$1" "${label}"
        shift
    done
}

printf "Repository root: %s\n" "${REPO_ROOT}"
printf "Build directory: %s\n\n" "${BUILD_DIR}"

printf "== Building ==\n"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" --parallel "${WSPR_REFERENCE_BUILD_JOBS:-2}"
printf "\n"

cd "${BUILD_DIR}"

printf "== Running core regression executables ==\n"
./verify_vectors
./payload_compare_roundtrip
./unpack_type1_roundtrip
./unpack_type2_roundtrip
./unpack_type3_roundtrip
./correlator_smoke
printf "\n"

printf "== Running transmission planning smoke test ==\n"
./plan_transmission_smoke
printf "\n"

printf "== Running paired encoding smoke test ==\n"
./encode_paired_smoke
printf "\n"

printf "== Running Type 2 coverage observations (non-fatal) ==\n"
./unpack_type2_matrix || true
./unpack_type2_overlap_observation || true
printf "\n"

run_and_assert \
    "CLI encode sanity" \
    "./wspr-encode AA0NT EM18 20" \
    "Type: TYPE1" \
    "Callsign: AA0NT" \
    "Locator: EM18" \
    "Power: 20 dBm" \
    "Symbols:"

run_and_assert \
    "CLI decode Type 1" \
    "./wspr-decode ${TYPE1_SYMBOLS}" \
    "Decoded Type 1 message:" \
    "Callsign: AA0NT" \
    "Locator:  EM18" \
    "Power:    20"

run_and_assert \
    "CLI decode Type 2 suffix" \
    "./wspr-decode ${TYPE2_SUFFIX_SYMBOLS}" \
    "Decoded Type 2 partial message:" \
    "Callsign: <hashed>/12" \
    "Extra:    /12" \
    "Power:    20"

run_and_assert \
    "CLI decode Type 2 prefix" \
    "./wspr-decode ${TYPE2_PREFIX_SYMBOLS}" \
    "Decoded Type 2 partial message:" \
    "Callsign: W0/<hashed>" \
    "Extra:    W0/" \
    "Power:    20"

run_and_assert \
    "CLI decode Type 3" \
    "./wspr-decode ${TYPE3_SYMBOLS}" \
    "Decoded Type 3 partial message:" \
    "Callsign: <hashed>" \
    "Hash:     19169" \
    "Locator:  EM18IG" \
    "Power:    20"

run_and_assert \
    "CLI correlate suffix" \
    "./wspr-correlate ${TYPE2_SUFFIX_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    "Correlated result" \
    "Callsign: <callsign>/12" \
    "Locator:  EM18IG" \
    "Power:    20" \
    "Partial:  false"

run_and_assert \
    "CLI correlate prefix" \
    "./wspr-correlate ${TYPE2_PREFIX_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    "Correlated result" \
    "Callsign: W0/<callsign>" \
    "Locator:  EM18IG" \
    "Power:    20" \
    "Partial:  false"

run_and_assert \
    "CLI encode quiet" \
    "./wspr-encode --quiet AA0NT EM18 20" \
    "${TYPE1_SYMBOLS}"

run_and_assert \
    "CLI encode symbols-only" \
    "./wspr-encode --symbols-only AA0NT EM18 20" \
    "${TYPE1_SYMBOLS}"

run_and_assert \
    "CLI decode Type 2 ambiguous verbose" \
    "./wspr-decode ${TYPE2_AMBIGUOUS_SYMBOLS}" \
    "Decoded Type 2 partial message:" \
    "Callsign: <hashed>/Z" \
    "Extra:    /Z" \
    "Ambiguous with: /09"

run_and_assert \
    "CLI decode Type 1 quiet" \
    "./wspr-decode --quiet ${TYPE1_SYMBOLS}" \
    "${TYPE1_QUIET_EXPECTED}"

run_and_assert \
    "CLI decode Type 2 suffix quiet" \
    "./wspr-decode --quiet ${TYPE2_SUFFIX_SYMBOLS}" \
    "${TYPE2_SUFFIX_QUIET_EXPECTED}"

run_and_assert \
    "CLI decode Type 2 prefix quiet" \
    "./wspr-decode --quiet ${TYPE2_PREFIX_SYMBOLS}" \
    "${TYPE2_PREFIX_QUIET_EXPECTED}"

run_and_assert \
    "CLI decode Type 3 quiet" \
    "./wspr-decode --quiet ${TYPE3_SYMBOLS}" \
    "${TYPE3_QUIET_EXPECTED}"

run_and_assert \
    "CLI correlate suffix quiet" \
    "./wspr-correlate --quiet ${TYPE2_SUFFIX_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    "${CORRELATED_SUFFIX_QUIET_EXPECTED}"

run_and_assert \
    "CLI correlate prefix quiet" \
    "./wspr-correlate --quiet ${TYPE2_PREFIX_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    "${CORRELATED_PREFIX_QUIET_EXPECTED}"

run_and_assert \
    "CLI decode Type 2 ambiguous quiet" \
    "./wspr-decode --quiet ${TYPE2_AMBIGUOUS_SYMBOLS}" \
    "${TYPE2_AMBIGUOUS_QUIET_EXPECTED}"

run_and_assert \
    "CLI correlate ambiguous quiet" \
    "./wspr-correlate --quiet ${TYPE2_AMBIGUOUS_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    "${CORRELATED_AMBIGUOUS_QUIET_EXPECTED}"

printf "%s\n" ""
printf "%s\n" "== CLI JSON sanity =="

run_and_assert \
    "CLI encode JSON" \
    "${BUILD_DIR}/wspr-encode --json AA0NT EM18 20" \
    '"type": "TYPE1"' \
    '"callsign": "AA0NT"' \
    '"locator": "EM18"' \
    '"power_dbm": 20'

run_and_assert \
    "CLI decode Type 1 JSON" \
    "${BUILD_DIR}/wspr-decode --json ${TYPE1_SYMBOLS}" \
    '"type": "TYPE1"' \
    '"callsign": "AA0NT"' \
    '"locator": "EM18"' \
    '"power_dbm": 20'

run_and_assert \
    "CLI decode Type 2 ambiguous JSON" \
    "${BUILD_DIR}/wspr-decode --json ${TYPE2_AMBIGUOUS_SYMBOLS}" \
    '"type": "TYPE2"' \
    '"callsign": "<hashed>/Z"' \
    '"extra": "/Z"' \
    '"has_ambiguity": true' \
    '"alternate_extra": "/09"'

run_and_assert \
    "CLI correlate JSON" \
    "${BUILD_DIR}/wspr-correlate --json ${TYPE2_SUFFIX_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    '"status": "CORRELATED"' \
    '"type": "TYPE2"' \
    '"callsign": "<callsign>/12"' \
    '"locator": "EM18IG"' \
    '"is_partial": false'

run_and_assert \
    "CLI correlate ambiguous JSON" \
    "${BUILD_DIR}/wspr-correlate --json ${TYPE2_AMBIGUOUS_SYMBOLS} ${PAIRED_TYPE3_SYMBOLS}" \
    '"status": "CORRELATED"' \
    '"type": "TYPE2"' \
    '"callsign": "<callsign>/Z"' \
    '"has_ambiguity": false'

printf "All major regressions completed successfully.\n"

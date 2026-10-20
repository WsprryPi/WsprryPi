#!/usr/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
COLLECTOR="${SCRIPT_DIR}/../collect-support-bundle.sh"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { grep -Fq -- "$2" "$1" || fail "expected '$2' in $1"; }
assert_not_contains() { ! grep -Fq -- "$2" "$1" || fail "did not expect '$2' in $1"; }
assert_file() { [[ -f "$1" ]] || fail "expected file $1"; }
command -v python3 >/dev/null 2>&1 || fail "python3 is required for JSON result validation"

assert_valid_json() {
  python3 - "$1" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as source:
    json.load(source)
PY
}

assert_failure_result() {
  python3 - "$1" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as source:
    result = json.load(source)
assert result["status"] == "failure"
assert result["archive_filename"] is None
assert result["sha256_filename"] is None
assert result["sha256"] is None
PY
}

assert_no_workdirs() {
  local leaked
  leaked="$(find "$TEST_ROOT/tmp" -mindepth 1 -maxdepth 1 -type d -name 'WsprryPi-support-20260102T030405Z.*' -print -quit)"
  [[ -z "$leaked" ]] || fail "collector work directory was retained: $leaked"
}

# Directory prefixes are literal paths, not shell patterns.
literal_prefix='/tmp/project[*?]\path with spaces'
literal_file="${literal_prefix}/nested/file.ini"
literal_relative="${literal_file#"$literal_prefix"/}"
[[ "$literal_relative" == 'nested/file.ini' ]] || fail "literal path prefix was treated as a pattern"
assert_contains "$COLLECTOR" "rel=\"\${file#\"\$PROJECT_PATH\"/}\""
assert_contains "$COLLECTOR" "rel=\"\${file#\"\$LEGACY_LOG_DIR\"/}\""

make_mocks() {
  local directory="$1" probe_exit="$2"
  mkdir -p "$directory"
  cat > "$directory/date" <<'EOF'
#!/usr/bin/bash
if [[ "$*" == *'+%Y%m%dT%H%M%SZ'* ]]; then printf '20260102T030405Z\n'; else /usr/bin/date "$@"; fi
EOF
  cat > "$directory/hostname" <<'EOF'
#!/usr/bin/bash
printf 'test-host\n'
EOF
  cat > "$directory/i2cdetect" <<EOF
#!/usr/bin/bash
printf '%s\\n' "\$*" >> "\${I2C_CALL_LOG:?}"
if [[ "\$1" == '-l' ]]; then exit 0; fi
exit ${probe_exit}
EOF
  for command in raspi-config vcgencmd gpioinfo gpiodetect gpiofind pinout gpio curl journalctl dmesg sh; do
    cat > "$directory/$command" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
  done
  cat > "$directory/systemctl" <<'EOF'
#!/usr/bin/bash
if [[ "$*" == 'show wsprrypi --property=ActiveState --value' ]]; then
  printf '%s\n' "${MOCK_ACTIVE_STATE:-active}"
elif [[ "$*" == 'show wsprrypi --property=MainPID --value' ]]; then
  if [[ "${MOCK_SYSTEMCTL_MAINPID_FAILURE:-0}" == 1 ]]; then printf 'Failed to get properties: Permission denied\n' >&2; exit 1; fi
  printf '%s' "${MOCK_MAIN_PID-0}"
  [[ "${MOCK_MAIN_PID_NEWLINE:-1}" == 1 ]] && printf '\n'
fi
exit 0
EOF
  cat > "$directory/ps" <<'EOF'
#!/usr/bin/bash
printf '    PID    PPID USER     STAT     ELAPSED   RSS    VSZ NLWP COMMAND         COMMAND\n'
printf '   4242       1 wsprrypi Sl         01:23 12345  67890    4 wsprrypi       /usr/local/bin/wsprrypi --token process-secret\n'
printf '   5000    1000 www-data S          00:42  2048  12000    1 php             php /var/www/worker.php\n'
EOF
  cat > "$directory/pstree" <<'EOF'
#!/usr/bin/bash
printf 'systemd,1---apache2,1000---php,5000---journalctl,5001\n'
EOF
  cat > "$directory/systemd-cgls" <<'EOF'
#!/usr/bin/bash
printf 'Control group /: wsprrypi.service apache2.service\n'
EOF
  cat > "$directory/cat" <<'EOF'
#!/usr/bin/bash
source_path="${1:-}"
if [[ "$source_path" != "/proc/${MOCK_PROC_PID:-none}/"* ]]; then exec /usr/bin/cat "$@"; fi
name="${source_path##*/}"
case "$name" in
  stat)
    count_file="${MOCK_STATE_DIR:?}/stat-count"
    count=0; [[ -f "$count_file" ]] && count="$(/usr/bin/cat "$count_file")"
    count=$((count + 1)); printf '%s\n' "$count" > "$count_file"
    if [[ "${MOCK_PROC_SCENARIO:-success}" == disappear && "$count" -gt 1 ]]; then printf 'cat: %s: No such file or directory\n' "$source_path" >&2; exit 1; fi
    start=777; [[ "${MOCK_PROC_SCENARIO:-success}" == reused && "$count" -gt 1 ]] && start=888
    printf '%s (wsprrypi worker) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 %s 20 21\n' "${MOCK_PROC_PID}" "$start"
    ;;
  status) /usr/bin/cat "${MOCK_FIXTURE_DIR:?}/status" ;;
  smaps_rollup)
    if [[ "${MOCK_PROC_SCENARIO:-success}" == unreadable ]]; then printf 'cat: %s: Permission denied\n' "$source_path" >&2; exit 1; fi
    /usr/bin/cat "${MOCK_FIXTURE_DIR:?}/smaps_rollup"
    ;;
  statm|limits|cgroup) /usr/bin/cat "${MOCK_FIXTURE_DIR:?}/$name" ;;
  cmdline) printf '/usr/local/bin/wsprrypi\0--password\0cmdline-secret\0' ;;
  *) exec /usr/bin/cat "$@" ;;
esac
EOF
  cat > "$directory/find" <<'EOF'
#!/usr/bin/bash
if [[ "${1:-}" == "/proc/${MOCK_PROC_PID:-none}/task" ]]; then
  [[ "${MOCK_PROC_SCENARIO:-success}" == unreadable ]] && { printf 'find: Permission denied\n' >&2; exit 1; }
  printf '....'; exit 0
fi
if [[ "${1:-}" == "/proc/${MOCK_PROC_PID:-none}/fd" ]]; then
  [[ "${MOCK_PROC_SCENARIO:-success}" == unreadable ]] && { printf 'find: Permission denied\n' >&2; exit 1; }
  printf '.....'; exit 0
fi
exec /usr/bin/find "$@"
EOF
  chmod 700 "$directory"/*
}

make_no_i2c_path() {
  local directory="$1" command target
  mkdir -p "$directory"
  for command in awk basename chmod cp cut df dirname file free getconf getent grep gzip id ldd ln ls mkdir mktemp mount perl readelf rm sha256sum sh sort stat tail tar tr uname uptime wc; do
    target="$(command -v "$command" || true)"
    [[ -n "$target" ]] && ln -s "$target" "$directory/$command"
  done
  rm "$directory/sh"
  cp "$TEST_ROOT/mocks/date" "$directory/date"
  cp "$TEST_ROOT/mocks/hostname" "$directory/hostname"
  for command in raspi-config vcgencmd gpioinfo gpiodetect gpiofind pinout gpio curl systemctl journalctl dmesg sh cat find; do
    cp "$TEST_ROOT/mocks/$command" "$directory/$command"
  done
}

run_collector() {
  local output="$1" mocks="$2" i2c_log="$3"
  shift 3
  ( cd / && I2C_CALL_LOG="$i2c_log" TMPDIR="$TEST_ROOT/tmp" PATH="$mocks:$PATH" bash "$COLLECTOR" --output-dir "$output" "$@" )
}

run_collector_isolated() {
  local output="$1" mocks="$2" i2c_log="$3"
  shift 3
  ( cd / && I2C_CALL_LOG="$i2c_log" TMPDIR="$TEST_ROOT/tmp" PATH="$mocks" /usr/bin/bash "$COLLECTOR" --output-dir "$output" "$@" )
}

mkdir -p "$TEST_ROOT/tmp" "$TEST_ROOT/out" "$TEST_ROOT/mocks"
chmod 700 "$TEST_ROOT/out"
make_mocks "$TEST_ROOT/mocks" 0
mkdir -p "$TEST_ROOT/proc-fixture" "$TEST_ROOT/mock-state"
cat > "$TEST_ROOT/proc-fixture/status" <<'EOF'
Name:	wsprrypi
State:	S (sleeping)
Pid:	4242
PPid:	1
Uid:	1000	1000	1000	1000
Gid:	1000	1000	1000	1000
VmPeak:	  90000 kB
VmSize:	  67890 kB
VmHWM:	   23456 kB
VmRSS:	   12345 kB
RssAnon:	10000 kB
RssFile:	 2345 kB
RssShmem:	    0 kB
Threads:	4
voluntary_ctxt_switches:	99
nonvoluntary_ctxt_switches:	7
EOF
cat > "$TEST_ROOT/proc-fixture/smaps_rollup" <<'EOF'
00400000-7fffffff ---p 00000000 00:00 0 [rollup]
Rss:               12345 kB
Pss:               11223 kB
EOF
printf '100 20 10 5 0 3 0\n' > "$TEST_ROOT/proc-fixture/statm"
printf 'Max open files            1024                 4096                 files\n' > "$TEST_ROOT/proc-fixture/limits"
printf '0::/system.slice/wsprrypi.service\n' > "$TEST_ROOT/proc-fixture/cgroup"

extract_bundle() {
  local destination="$1"
  rm -rf "$destination"
  mkdir "$destination"
  tar -xzf "$archive" -C "$destination"
}

run_process_case() {
  local scenario="$1" main_pid="$2" active_state="$3" destination="$4"
  rm -f "$archive" "${archive}.sha256" "$result" "$TEST_ROOT/mock-state/stat-count"
  export MOCK_PROC_SCENARIO="$scenario" MOCK_MAIN_PID="$main_pid" MOCK_ACTIVE_STATE="$active_state"
  export MOCK_PROC_PID="$$" MOCK_FIXTURE_DIR="$TEST_ROOT/proc-fixture" MOCK_STATE_DIR="$TEST_ROOT/mock-state"
  run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/process-i2c.log" > "$TEST_ROOT/process-${scenario}.stdout"
  extract_bundle "$destination"
}

# Default: no active bus scan, stable artifacts, and no work tree remains.
: > "$TEST_ROOT/default-i2c.log"
run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/default-i2c.log" >"$TEST_ROOT/default.stdout"
archive="$TEST_ROOT/out/WsprryPi-support-test-host-20260102T030405Z.tar.gz"
result="${archive}.result.json"
[[ -f "$archive" && -f "${archive}.sha256" && -f "$result" ]] || fail "expected completed artifacts"
assert_valid_json "$result"
assert_contains "$result" '"status": "success"'
assert_contains "$result" '"configuration_files_included": true'
assert_contains "$result" '"full_logs_included": false'
assert_contains "$result" '"i2c_probe_status": "skipped_by_user"'
assert_contains "$result" '"case_id": null'
assert_contains "$result" '"manifest_included": false'
assert_not_contains "$TEST_ROOT/default-i2c.log" '-y 1'
assert_no_workdirs
[[ "$(stat -c %a "$archive")" == 600 ]] || fail "archive permissions are not restrictive"

# Private mode places validated support context and a complete file inventory in the archive.
rm -f "$archive" "${archive}.sha256" "$result"
printf 'Scheduling stops after saving "QRSS".\\path' > "$TEST_ROOT/description.txt"
printf 'operator@example.test' > "$TEST_ROOT/contact.txt"
chmod 600 "$TEST_ROOT/description.txt" "$TEST_ROOT/contact.txt"
run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/private-i2c.log" \
  --case-id 7K3M-9QFX-2DPA --context-kind no_github \
  --project-version 3.2.1-qualification.1 \
  --problem-description-file "$TEST_ROOT/description.txt" \
  --contact-file "$TEST_ROOT/contact.txt" > "$TEST_ROOT/private.stdout"
extract_bundle "$TEST_ROOT/extracted-private"
assert_file "$TEST_ROOT/extracted-private/bundle/manifest.json"
assert_file "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_dkms.txt"
assert_file "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_modinfo_rp1_gpclk_dkms_filename.txt"
assert_file "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_modinfo_rp1_route_controller_vermagic.txt"
assert_file "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_running_kernel_headers.txt"
assert_file "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_runtime_provider_inspect.txt"
assert_file "$TEST_ROOT/extracted-private/bundle/hardware/rp1-gpclk/wsprrypi-installation-record.txt"
assert_contains "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_dkms.txt" '/usr/sbin/dkms status -m rp1-gpclk-dkms'
assert_contains "$TEST_ROOT/extracted-private/bundle/commands/rp1_gpclk_runtime_provider_inspect.txt" 'runtime_provider.py inspect'
assert_contains "$TEST_ROOT/extracted-private/bundle/hardware/rp1-gpclk/evidence-summary.txt" 'Qualification: not established by this support bundle'
assert_contains "$TEST_ROOT/extracted-private/bundle/README.txt" 'This .tar.gz is the readable review copy.'
assert_contains "$TEST_ROOT/extracted-private/bundle/NEXT-STEPS.txt" 'Upload only the later encrypted .age artifact'
assert_not_contains "$TEST_ROOT/extracted-private/bundle/NEXT-STEPS.txt" 'Upload this .tar.gz archive'
assert_contains "$result" '"case_id": "7K3M-9QFX-2DPA"'
assert_contains "$result" '"manifest_included": true'
python3 - "$TEST_ROOT/extracted-private/bundle" <<'PY'
import hashlib
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
assert manifest["schema_version"] == 1
assert manifest["contract_version"] == 1
assert manifest["project_id"] == "wsprrypi"
assert manifest["project_version"] == "3.2.1-qualification.1"
assert manifest["case_id"] == "7K3M-9QFX-2DPA"
assert manifest["created_at_utc"] == "2026-01-02T03:04:05Z"
assert manifest["support_context"] == {
    "kind": "no_github", "issue_url": None,
    "problem_description": 'Scheduling stops after saving "QRSS".\\path',
    "contact": "operator@example.test",
}
entries = manifest["files"]
assert [entry["path"] for entry in entries] == sorted(entry["path"] for entry in entries)
actual = sorted(str(path.relative_to(root)) for path in root.rglob("*")
                if path.is_file() and path.name != "manifest.json")
assert [entry["path"] for entry in entries] == actual
for entry in entries:
    payload = (root / entry["path"]).read_bytes()
    assert entry["size"] == len(payload)
    assert entry["sha256"] == hashlib.sha256(payload).hexdigest()
PY
! tar -tzf "$archive" | grep -Fq 'description.txt' || fail 'description staging file leaked into archive'
! tar -tzf "$archive" | grep -Fq 'contact.txt' || fail 'contact staging file leaked into archive'

# Existing-issue context is normalized and does not add description/contact values.
rm -f "$archive" "${archive}.sha256" "$result"
run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/github-i2c.log" \
  --case-id ABCD-EFGH-JKMP \
  --project-version 3.2.1-qualification.1 \
  --github-issue https://github.com/WsprryPi/WsprryPi/issues/352 > "$TEST_ROOT/github.stdout"
extract_bundle "$TEST_ROOT/extracted-github"
python3 - "$TEST_ROOT/extracted-github/bundle/manifest.json" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as source:
    context = json.load(source)["support_context"]
assert context == {"kind": "existing_github_issue",
                   "issue_url": "https://github.com/WsprryPi/WsprryPi/issues/352",
                   "problem_description": None, "contact": None}
PY

# Invalid or conflicting private metadata fails closed without a successful archive.
rm -f "$archive" "${archive}.sha256" "$result"
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/missing-version.log" \
  --case-id ABCD-EFGH-JKMP --github-issue https://github.com/WsprryPi/WsprryPi/issues/352 >/dev/null 2>&1; then
  fail "missing private project version succeeded"
fi
assert_failure_result "$result"
rm -f "$result"
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/noncanonical-version.log" \
  --case-id ABCD-EFGH-JKMP --project-version 03.2.1 \
  --github-issue https://github.com/WsprryPi/WsprryPi/issues/352 >/dev/null 2>&1; then
  fail "noncanonical private project version succeeded"
fi
assert_failure_result "$result"
rm -f "$result"
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/invalid-private.log" \
  --case-id bad --github-issue https://github.com/WsprryPi/WsprryPi/issues/352 >/dev/null 2>&1; then
  fail "invalid private case ID succeeded"
fi
[[ ! -e "$archive" && ! -e "${archive}.sha256" ]] || fail "invalid private metadata published artifacts"
assert_failure_result "$result"
rm -f "$result"
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/conflicting-private.log" \
  --case-id ABCD-EFGH-JKMP --github-issue https://github.com/WsprryPi/WsprryPi/issues/352 \
  --context-kind no_github --problem-description-file "$TEST_ROOT/description.txt" \
  --contact-file "$TEST_ROOT/contact.txt" >/dev/null 2>&1; then
  fail "conflicting private context succeeded"
fi
assert_failure_result "$result"
rm -f "$result"
printf 'line one\nline two' > "$TEST_ROOT/multiline-description.txt"
chmod 600 "$TEST_ROOT/multiline-description.txt"
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/multiline-private.log" \
  --case-id ABCD-EFGH-JKMP --context-kind no_github \
  --problem-description-file "$TEST_ROOT/multiline-description.txt" \
  --contact-file "$TEST_ROOT/contact.txt" >/dev/null 2>&1; then
  fail "multiline private description succeeded"
fi
assert_failure_result "$result"
rm -f "$result"
ln -s "$TEST_ROOT/description.txt" "$TEST_ROOT/description-link.txt"
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/symlink-private.log" \
  --case-id ABCD-EFGH-JKMP --context-kind no_github \
  --problem-description-file "$TEST_ROOT/description-link.txt" \
  --contact-file "$TEST_ROOT/contact.txt" >/dev/null 2>&1; then
  fail "symlink private description succeeded"
fi
assert_failure_result "$result"

# Fixed systemd MainPID discovery and fixed /proc paths produce the requested point-in-time evidence.
run_process_case success "$$" active "$TEST_ROOT/extracted-success"
process_dir="$TEST_ROOT/extracted-success/bundle/processes"
for member in all-processes.txt process-tree.txt systemd-cgroups.txt wsprrypi-summary.txt wsprrypi-status.txt wsprrypi-smaps-rollup.txt wsprrypi-stat.txt wsprrypi-statm.txt wsprrypi-limits.txt wsprrypi-cgroup.txt wsprrypi-cmdline.txt; do
  assert_file "$process_dir/$member"
done
assert_contains "$process_dir/wsprrypi-summary.txt" 'Collection status: collected successfully'
assert_contains "$process_dir/wsprrypi-summary.txt" 'VmRSS: 12345 kB'
assert_contains "$process_dir/wsprrypi-summary.txt" 'VmSize: 67890 kB'
assert_contains "$process_dir/wsprrypi-summary.txt" 'PSS: 11223 kB'
assert_contains "$process_dir/wsprrypi-summary.txt" 'Threads (status): 4'
assert_contains "$process_dir/wsprrypi-summary.txt" 'Task directory count: 4'
assert_contains "$process_dir/wsprrypi-summary.txt" 'Open file descriptor count: 5'
assert_contains "$process_dir/wsprrypi-status.txt" $'voluntary_ctxt_switches:\t99'
assert_contains "$process_dir/all-processes.txt" 'php /var/www/worker.php'
assert_contains "$process_dir/process-tree.txt" 'journalctl'
assert_contains "$process_dir/systemd-cgroups.txt" 'apache2.service'
assert_not_contains "$process_dir/all-processes.txt" 'process-secret'
assert_contains "$process_dir/all-processes.txt" '[REDACTED]'
assert_not_contains "$process_dir/wsprrypi-cmdline.txt" 'cmdline-secret'
assert_contains "$process_dir/wsprrypi-cmdline.txt" '[REDACTED]'
! tar -tzf "$archive" | grep -Eq '/fd(/|$)' || fail 'FD inventory or targets were archived'

# Stopped, zero, malformed, missing, disappearing, reused, and unreadable states remain explicit.
run_process_case success 0 inactive "$TEST_ROOT/extracted-stopped"
assert_contains "$TEST_ROOT/extracted-stopped/bundle/processes/wsprrypi-summary.txt" 'Collection status: service not running'
assert_contains "$TEST_ROOT/extracted-stopped/bundle/processes/wsprrypi-summary.txt" 'PSS: unavailable'

run_process_case success 0 active "$TEST_ROOT/extracted-zero"
assert_contains "$TEST_ROOT/extracted-zero/bundle/processes/wsprrypi-summary.txt" 'Collection status: MainPID is zero; no active main process'

run_process_case success '' active "$TEST_ROOT/extracted-empty"
assert_contains "$TEST_ROOT/extracted-empty/bundle/processes/wsprrypi-summary.txt" 'Collection status: invalid or empty MainPID'

run_process_case success malformed active "$TEST_ROOT/extracted-malformed"
assert_contains "$TEST_ROOT/extracted-malformed/bundle/processes/wsprrypi-summary.txt" 'Collection status: invalid or empty MainPID'

export MOCK_SYSTEMCTL_MAINPID_FAILURE=1
run_process_case success 0 active "$TEST_ROOT/extracted-mainpid-denied"
assert_contains "$TEST_ROOT/extracted-mainpid-denied/bundle/processes/wsprrypi-summary.txt" 'Collection status: MainPID unavailable (permission denied)'
unset MOCK_SYSTEMCTL_MAINPID_FAILURE

run_process_case success 99999999 active "$TEST_ROOT/extracted-missing"
assert_contains "$TEST_ROOT/extracted-missing/bundle/processes/wsprrypi-summary.txt" 'Collection status: process missing before detailed collection'

run_process_case disappear "$$" active "$TEST_ROOT/extracted-disappear"
assert_contains "$TEST_ROOT/extracted-disappear/bundle/processes/wsprrypi-summary.txt" 'Collection status: process disappeared during collection'

run_process_case reused "$$" active "$TEST_ROOT/extracted-reused"
assert_contains "$TEST_ROOT/extracted-reused/bundle/processes/wsprrypi-summary.txt" 'Collection status: process identity changed; PID may have been reused'

run_process_case unreadable "$$" active "$TEST_ROOT/extracted-unreadable"
unreadable_dir="$TEST_ROOT/extracted-unreadable/bundle/processes"
assert_contains "$unreadable_dir/wsprrypi-summary.txt" 'PSS: unavailable'
assert_contains "$unreadable_dir/wsprrypi-summary.txt" 'Task directory count: unavailable (permission denied)'
assert_contains "$unreadable_dir/wsprrypi-summary.txt" 'Open file descriptor count: unavailable (permission denied)'
assert_not_contains "$unreadable_dir/wsprrypi-summary.txt" 'PSS: 0'
assert_contains "$unreadable_dir/wsprrypi-smaps-rollup.txt" 'Collection status: permission denied'

unset MOCK_PROC_SCENARIO MOCK_MAIN_PID MOCK_ACTIVE_STATE MOCK_PROC_PID MOCK_FIXTURE_DIR MOCK_STATE_DIR

# The fixed opt-in probe executes exactly one allowed command and records success.
rm -f "$archive" "${archive}.sha256" "$result"
: > "$TEST_ROOT/probe-i2c.log"
run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/probe-i2c.log" --probe-i2c >"$TEST_ROOT/probe.stdout"
[[ "$(cat "$TEST_ROOT/probe-i2c.log")" == $'-l\n-y 1' ]] || fail "unexpected I2C invocation"
assert_valid_json "$result"
assert_contains "$result" '"i2c_probe_status": "succeeded"'

# Failure and unavailable probe states remain machine-readable without a real bus.
rm -f "$archive" "${archive}.sha256" "$result"
make_mocks "$TEST_ROOT/mocks" 9
: > "$TEST_ROOT/failed-i2c.log"
run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/failed-i2c.log" --probe-i2c >"$TEST_ROOT/failed.stdout"
assert_valid_json "$result"
assert_contains "$result" '"i2c_probe_status": "failed"'

rm -f "$archive" "${archive}.sha256" "$result"
make_no_i2c_path "$TEST_ROOT/no-i2c-path"
: > "$TEST_ROOT/unavailable-i2c.log"
run_collector_isolated "$TEST_ROOT/out" "$TEST_ROOT/no-i2c-path" "$TEST_ROOT/unavailable-i2c.log" --probe-i2c >"$TEST_ROOT/unavailable.stdout"
assert_valid_json "$result"
assert_contains "$result" '"i2c_probe_status": "unavailable"'

# Unsafe/missing output paths and existing artifact collisions fail without overwriting output.
if run_collector "$TEST_ROOT/missing" "$TEST_ROOT/mocks" "$TEST_ROOT/missing.log" >/dev/null 2>&1; then fail "missing output directory succeeded"; fi
mkdir "$TEST_ROOT/world-writable" && chmod 777 "$TEST_ROOT/world-writable"
if run_collector "$TEST_ROOT/world-writable" "$TEST_ROOT/mocks" "$TEST_ROOT/unsafe.log" >/dev/null 2>&1; then fail "unsafe output directory succeeded"; fi
mkdir -p "$TEST_ROOT/collision" && chmod 700 "$TEST_ROOT/collision"
printf 'preserve me\n' > "$TEST_ROOT/collision/$(basename "$archive")"
if run_collector "$TEST_ROOT/collision" "$TEST_ROOT/mocks" "$TEST_ROOT/collision.log" >/dev/null 2>&1; then fail "archive collision succeeded"; fi
assert_contains "$TEST_ROOT/collision/$(basename "$archive")" 'preserve me'

# A failed archive build leaves only an explicit failure result, never partial success output.
mkdir "$TEST_ROOT/failure-out" "$TEST_ROOT/failure-mocks"
chmod 700 "$TEST_ROOT/failure-out"
make_mocks "$TEST_ROOT/failure-mocks" 0
cat > "$TEST_ROOT/failure-mocks/tar" <<'EOF'
#!/usr/bin/bash
exit 1
EOF
chmod 700 "$TEST_ROOT/failure-mocks/tar"
if run_collector "$TEST_ROOT/failure-out" "$TEST_ROOT/failure-mocks" "$TEST_ROOT/failure-i2c.log" >/dev/null 2>&1; then fail "archive failure succeeded"; fi
failure_archive="$TEST_ROOT/failure-out/$(basename "$archive")"
[[ ! -e "$failure_archive" && ! -e "${failure_archive}.sha256" ]] || fail "partial archive was retained"
assert_failure_result "${failure_archive}.result.json"
assert_no_workdirs

# A checksum failure occurs after temporary workspace creation and also cleans it up.
mkdir "$TEST_ROOT/checksum-failure-out" "$TEST_ROOT/checksum-failure-mocks"
chmod 700 "$TEST_ROOT/checksum-failure-out"
make_mocks "$TEST_ROOT/checksum-failure-mocks" 0
cat > "$TEST_ROOT/checksum-failure-mocks/sha256sum" <<'EOF'
#!/usr/bin/bash
printf 'not-a-sha256\n'
EOF
chmod 700 "$TEST_ROOT/checksum-failure-mocks/sha256sum"
if run_collector "$TEST_ROOT/checksum-failure-out" "$TEST_ROOT/checksum-failure-mocks" "$TEST_ROOT/checksum-failure-i2c.log" >/dev/null 2>&1; then fail "checksum failure succeeded"; fi
checksum_failure_archive="$TEST_ROOT/checksum-failure-out/$(basename "$archive")"
[[ ! -e "$checksum_failure_archive" && ! -e "${checksum_failure_archive}.sha256" ]] || fail "checksum failure retained partial artifacts"
assert_failure_result "${checksum_failure_archive}.result.json"
assert_no_workdirs

# Explicitly reject caller-controlled probe/bus arguments and retain legacy CWD output.
if run_collector "$TEST_ROOT/out" "$TEST_ROOT/mocks" "$TEST_ROOT/arguments.log" --probe-i2c=2 >/dev/null 2>&1; then fail "arbitrary probe argument accepted"; fi
legacy="$TEST_ROOT/legacy"
mkdir "$legacy"
( cd "$legacy" && I2C_CALL_LOG="$TEST_ROOT/legacy-i2c.log" TMPDIR="$TEST_ROOT/tmp" PATH="$TEST_ROOT/mocks:$PATH" bash "$COLLECTOR" >"$TEST_ROOT/legacy.stdout" )
[[ -f "$legacy/$(basename "$archive")" ]] || fail "legacy current-directory archive missing"
assert_valid_json "$legacy/$(basename "$archive").result.json"

echo "collect-support-bundle tests: PASS"

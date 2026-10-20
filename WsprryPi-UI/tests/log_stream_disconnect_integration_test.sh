#!/usr/bin/env bash

# Raspberry Pi integration regression for WsprryPi/WsprryPi#355.
#
# This intentionally exercises the installed Apache endpoint. If the endpoint
# abandons the request-specific shell or journalctl after curl disconnects,
# cleanup restarts only Apache so repeated runs do not accumulate process trees.

set -u
set -o pipefail

readonly ENDPOINT="${WSPRRYPI_LOG_STREAM_ENDPOINT:-http://127.0.0.1/wsprrypi/log_stream.php}"
readonly HEARTBEAT_SECONDS=5
# Four heartbeats gives the server a bounded 20 seconds to observe disconnect.
readonly CLEANUP_TIMEOUT_SECONDS=$((HEARTBEAT_SECONDS * 4))
readonly STABLE_ABSENCE_SECONDS=2
readonly PROCESS_PREFIX="wsprrypi-issue355-test-"

for tool in awk cat curl date grep mktemp ps sed sleep sudo systemctl tr; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'FAIL: required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

UNIT_FILTER="${PROCESS_PREFIX}$$-$(date +%s).service"
readonly UNIT_FILTER
DIAGNOSTICS_DIR="$(mktemp -d "/tmp/issue355-log-stream.XXXXXX")"
if [[ -z "$DIAGNOSTICS_DIR" || ! -d "$DIAGNOSTICS_DIR" ]]; then
    printf 'FAIL: could not create diagnostics directory\n' >&2
    exit 1
fi
readonly DIAGNOSTICS_DIR
readonly RESPONSE_FILE="${DIAGNOSTICS_DIR}/response.sse"
readonly HEADER_FILE="${DIAGNOSTICS_DIR}/response.headers"
readonly PRE_CLEANUP_PROCESS_FILE="${DIAGNOSTICS_DIR}/processes-before-disconnect.txt"
readonly POST_CLEANUP_PROCESS_FILE="${DIAGNOSTICS_DIR}/processes-after-disconnect.txt"

client_pid=""
client_started=0
journal_pid=""
journal_start=""
shell_pid=""
shell_start=""
request_topology=""
server_pid=""
server_start=""
response_server_pid=""
test_failed=0
cleanup_observation_complete=0
cleanup_running=0
apache_restarted=0
disconnect_started_ns=""
cleanup_duration_ms=""

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    test_failed=1
}

process_start() {
    awk '{print $22}' "/proc/$1/stat" 2>/dev/null
}

same_process() {
    local pid="$1"
    local expected_start="$2"
    [[ -n "$pid" && -n "$expected_start" && -r "/proc/${pid}/stat" && \
        "$(process_start "$pid")" == "$expected_start" ]]
}

process_parent() {
    awk '/^PPid:/ {print $2}' "/proc/$1/status" 2>/dev/null
}

process_args() {
    local pid="$1"
    if [[ -r "/proc/${pid}/cmdline" ]]; then
        tr '\0' ' ' < "/proc/${pid}/cmdline"
    fi
}

process_name() {
    local pid="$1"
    if [[ -r "/proc/${pid}/comm" ]]; then
        sed -n '1p' "/proc/${pid}/comm"
    fi
}

discover_request_tree() {
    local args=""
    local comm_file=""
    local found_pid=""
    local found_count=0
    local pid=""

    if same_process "$journal_pid" "$journal_start"; then
        return 0
    fi

    for comm_file in /proc/[0-9]*/comm; do
        [[ -r "$comm_file" ]] || continue
        [[ "$(sed -n '1p' "$comm_file" 2>/dev/null)" == "journalctl" ]] || continue
        pid="${comm_file#/proc/}"
        pid="${pid%/comm}"
        args="$(process_args "$pid")"
        if [[ " $args " == *" -u $UNIT_FILTER "* ]]; then
            found_pid="$pid"
            found_count=$((found_count + 1))
        fi
    done

    if (( found_count != 1 )); then
        return 1
    fi

    journal_pid="$found_pid"
    journal_start="$(process_start "$journal_pid")"
    local journal_parent=""
    local journal_parent_name=""
    journal_parent="$(process_parent "$journal_pid")"
    journal_parent_name="$(process_name "$journal_parent")"

    if [[ "$journal_parent_name" == "apache2" ]]; then
        request_topology="direct"
        shell_pid=""
        shell_start=""
        server_pid="$journal_parent"
    elif [[ "$journal_parent_name" == "sh" ]]; then
        request_topology="shell"
        shell_pid="$journal_parent"
        shell_start="$(process_start "$shell_pid")"
        server_pid="$(process_parent "$shell_pid")"
    else
        return 1
    fi
    server_start="$(process_start "$server_pid")"

    [[ -n "$journal_start" && -n "$server_pid" && -n "$server_start" &&
        ( "$request_topology" == "direct" || -n "$shell_start" ) ]]
}

request_descendants_alive() {
    same_process "$journal_pid" "$journal_start" || same_process "$shell_pid" "$shell_start"
}

apache_identity_unchanged() {
    systemctl is-active --quiet apache2 &&
        same_process "$server_pid" "$server_start" &&
        [[ "$(process_name "$server_pid")" == "apache2" ]]
}

record_diagnostics() {
    local output_file="$1"
    {
        printf 'endpoint=%s\nunit_filter=%s\n' "$ENDPOINT" "$UNIT_FILTER"
        printf 'topology=%s client_pid=%s response_server_pid=%s journal_pid=%s journal_start=%s shell_pid=%s shell_start=%s server_pid=%s server_start=%s\n' \
            "$request_topology" \
            "$client_pid" "$response_server_pid" "$journal_pid" "$journal_start" \
            "$shell_pid" "$shell_start" "$server_pid" "$server_start"
        printf 'disconnect_started_ns=%s cleanup_duration_ms=%s\n' \
            "$disconnect_started_ns" "$cleanup_duration_ms"
        printf '\nTracked process state:\n'
        for pid in "$server_pid" "$shell_pid" "$journal_pid"; do
            [[ -n "$pid" ]] || continue
            ps -o user=,pid=,ppid=,pgid=,stat=,lstart=,args= -p "$pid" 2>&1 || true
        done
        printf '\nMatching process state:\n'
        # Exact marker output is more useful diagnostically than PID-only pgrep output.
        # shellcheck disable=SC2009
        ps -eo user=,pid=,ppid=,pgid=,stat=,lstart=,args= | grep -F -- "$UNIT_FILTER" || true
    } > "$output_file"
}

stop_test_client() {
    if (( client_started )) && [[ -n "$client_pid" ]]; then
        if kill -0 "$client_pid" 2>/dev/null; then
            kill "$client_pid" 2>/dev/null || true
        fi
        wait "$client_pid" 2>/dev/null || true
        client_pid=""
    fi
}

poll_for_request_cleanup() {
    local deadline=$((SECONDS + CLEANUP_TIMEOUT_SECONDS))
    local absent_since=-1
    local now_ns=""

    while (( SECONDS < deadline )); do
        apache_identity_unchanged || return 2
        # This also catches a follow child that appeared just before disconnect
        # or was replaced under the same request-specific unit filter.
        discover_request_tree >/dev/null 2>&1 || true
        if [[ -n "$journal_pid" || -n "$shell_pid" ]]; then
            if request_descendants_alive; then
                absent_since=-1
            elif (( absent_since < 0 )); then
                absent_since=$SECONDS
            elif (( SECONDS - absent_since >= STABLE_ABSENCE_SECONDS )); then
                now_ns="$(date +%s%N)"
                cleanup_duration_ms=$(((now_ns - disconnect_started_ns) / 1000000))
                return 0
            fi
        fi
        sleep 1
    done

    apache_identity_unchanged || return 2
    discover_request_tree >/dev/null 2>&1 || true
    if ! request_descendants_alive && (( absent_since >= 0 )) &&
        (( SECONDS - absent_since >= STABLE_ABSENCE_SECONDS )); then
        now_ns="$(date +%s%N)"
        cleanup_duration_ms=$(((now_ns - disconnect_started_ns) / 1000000))
        return 0
    fi
    return 1
}

# Called from cleanup(), which ShellCheck sees only through the EXIT trap.
# shellcheck disable=SC2317
verify_descendants_removed_after_restart() {
    local deadline=$((SECONDS + CLEANUP_TIMEOUT_SECONDS))

    while (( SECONDS < deadline )); do
        request_descendants_alive || return 0
        sleep 1
    done

    ! request_descendants_alive
}

# Called indirectly by the EXIT trap. It disables all traps before doing work so
# cleanup cannot recurse. The original nonzero result is retained; cleanup
# failure becomes status 2 only when the original result was successful.
# shellcheck disable=SC2317
cleanup() {
    local original_status="$1"
    local final_status="$original_status"
    local cleanup_failed=0

    if (( cleanup_running )); then
        return
    fi
    cleanup_running=1
    trap - EXIT HUP INT TERM

    stop_test_client

    if (( client_started && ! cleanup_observation_complete )); then
        if ! poll_for_request_cleanup; then
            cleanup_observation_complete=1
        fi
    fi

    record_diagnostics "$POST_CLEANUP_PROCESS_FILE"

    if request_descendants_alive; then
        printf 'CLEANUP: restarting apache2 to remove the abandoned issue #355 test process tree.\n' >&2
        apache_restarted=1
        if ! sudo -n systemctl restart apache2; then
            printf 'CLEANUP ERROR: sudo denied or apache2 restart failed; diagnostics: %s\n' \
                "$DIAGNOSTICS_DIR" >&2
            cleanup_failed=1
        else
            if ! systemctl is-active --quiet apache2; then
                printf 'CLEANUP ERROR: apache2 did not return active; diagnostics: %s\n' \
                    "$DIAGNOSTICS_DIR" >&2
                cleanup_failed=1
            fi
            if ! verify_descendants_removed_after_restart; then
                printf 'CLEANUP ERROR: exact request descendants survived apache2 restart; diagnostics: %s\n' \
                    "$DIAGNOSTICS_DIR" >&2
                cleanup_failed=1
            fi
        fi
    fi

    record_diagnostics "$POST_CLEANUP_PROCESS_FILE"
    if (( apache_restarted )) && (( cleanup_failed == 0 )); then
        printf 'CLEANUP: apache2 is active and exact request descendants are gone.\n' >&2
    fi

    if (( cleanup_failed )); then
        printf 'CLEANUP ERROR: cleanup failed; original test status=%s; diagnostics: %s\n' \
            "$original_status" "$DIAGNOSTICS_DIR" >&2
        (( final_status == 0 )) && final_status=2
    elif (( test_failed == 0 && final_status == 0 )); then
        printf 'Diagnostics: %s\n' "$DIAGNOSTICS_DIR"
    else
        printf 'Diagnostics: %s\n' "$DIAGNOSTICS_DIR" >&2
    fi

    exit "$final_status"
}

trap 'cleanup "$?"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if ! sudo -n true; then
    printf 'NOTICE: noninteractive sudo was not generally available; if cleanup requires an apache2 restart, the exact command may fail.\n' >&2
fi

# Refuse to add another abandoned test tree if a prior interrupted run exists.
# shellcheck disable=SC2009
if ps -eo args= | grep -F -- "$PROCESS_PREFIX" | grep -v -F -- "$UNIT_FILTER" | grep -q journalctl; then
    fail "an earlier issue #355 integration-test journalctl is still running"
    exit 1
fi

url="${ENDPOINT}?playback=0&heartbeat=${HEARTBEAT_SECONDS}&unit=${UNIT_FILTER}"
curl --silent --show-error --fail --no-buffer --noproxy '*' \
    --dump-header "$HEADER_FILE" "$url" > "$RESPONSE_FILE" 2>&1 &
client_pid=$!
client_started=1

deadline=$((SECONDS + 10))
connection_line=""
while (( SECONDS < deadline )); do
    connection_line="$(grep -F -m1 '"MESSAGE":"SSE connected.' "$RESPONSE_FILE" 2>/dev/null || true)"
    [[ -n "$connection_line" ]] && break
    if ! kill -0 "$client_pid" 2>/dev/null; then
        wait "$client_pid" || true
        client_pid=""
        fail "client exited before establishing SSE connection; response: $RESPONSE_FILE"
        exit 1
    fi
    sleep 1
done

if [[ -z "$connection_line" ]]; then
    fail "no SSE connection event received within 10 seconds; response: $RESPONSE_FILE"
    exit 1
fi
if ! grep -Fqi 'Content-Type: text/event-stream' "$HEADER_FILE"; then
    fail "endpoint did not return the expected SSE content type; headers: $HEADER_FILE"
    exit 1
fi
if [[ "$connection_line" != *'"MESSAGE":"SSE connected. playback=0 '* ||
      "$connection_line" != *" unit=${UNIT_FILTER} heartbeat=${HEARTBEAT_SECONDS}s\""* ]]; then
    fail "SSE connection event did not confirm playback, unit, and heartbeat parameters"
    exit 1
fi
response_server_pid="$(printf '%s\n' "$connection_line" | sed -n 's/.*"PID":\([0-9][0-9]*\).*/\1/p')"
if [[ -z "$response_server_pid" ]]; then
    fail "SSE connection event did not include a usable PHP/Apache PID"
    exit 1
fi

deadline=$((SECONDS + 10))
while (( SECONDS < deadline )); do
    discover_request_tree && break
    sleep 1
done
if ! same_process "$journal_pid" "$journal_start"; then
    fail "expected exactly one live journalctl for the unique unit"
    exit 1
fi

if [[ "$(process_name "$server_pid")" != "apache2" ]]; then
    fail "journalctl request parent ${server_pid} is not the expected Apache request process"
    exit 1
fi
if [[ "$response_server_pid" != "$server_pid" ]]; then
    fail "SSE PHP PID ${response_server_pid} does not own the identified request tree under Apache PID ${server_pid}"
    exit 1
fi

printf 'Connected: topology=%s client=%s apache=%s shell=%s journalctl=%s unit=%s\n' \
    "$request_topology" "$client_pid" "$server_pid" "${shell_pid:-none}" \
    "$journal_pid" "$UNIT_FILTER"

record_diagnostics "$PRE_CLEANUP_PROCESS_FILE"

# Disconnect only this test's client. The server-side descendants are
# observation-only until the bounded cleanup poll ends.
disconnect_started_ns="$(date +%s%N)"
stop_test_client

poll_status=0
if poll_for_request_cleanup; then
    cleanup_observation_complete=1
    record_diagnostics "$POST_CLEANUP_PROCESS_FILE"
    if ! apache_identity_unchanged; then
        fail "Apache request worker identity changed before PASS"
        exit 1
    fi
    printf 'PASS: request-specific descendants remained absent for %ss after disconnect; cleanup=%sms\n' \
        "$STABLE_ABSENCE_SECONDS" "$cleanup_duration_ms"
    exit 0
else
    poll_status=$?
fi
cleanup_observation_complete=1

record_diagnostics "$POST_CLEANUP_PROCESS_FILE"
if (( poll_status == 2 )); then
    fail "Apache request worker identity changed while waiting for descendant cleanup"
    exit 1
fi
survivors=()
same_process "$shell_pid" "$shell_start" && survivors+=("sh:${shell_pid}")
same_process "$journal_pid" "$journal_start" && survivors+=("journalctl:${journal_pid}")
fail "request descendants survived ${CLEANUP_TIMEOUT_SECONDS}s after disconnect: ${survivors[*]}"
printf '%s\n' '--- surviving process evidence ---' >&2
cat "$POST_CLEANUP_PROCESS_FILE" >&2
exit 1

<?php
// log_stream.php
//
// Journald JSON SSE adapter with unified schema, replay + follow, cursor resume,
// consumer-controlled playback/backlog, server-side filtering on PRIORITY/UNIT,
// heartbeat events, and playback boolean.
//
// Output rule:
// - Every SSE event emitted by this script MUST contain JSON only in `data:`.
// - Optional `id:` is emitted ONLY for type="journal" events with a non-empty
//   __CURSOR value.
// - Optional `event:` is emitted as "journal" or "internal".
//
// Consumer rule:
// - Persist only the last cursor from events where type === "journal" and
//   __CURSOR is a non-empty string. Internal events do not carry cursors.
//
// Unified payload schema for ALL emitted events:
//
// {
//   "type": "journal" | "internal",
//   "playback": boolean,
//   "__CURSOR": string|null,
//   "__REALTIME_TIMESTAMP": string|null, // microseconds since epoch
//   "PRIORITY": string|null,       // journald 0..7 as strings
//   "SYSLOG_IDENTIFIER": string|null,
//   "MESSAGE": string,
//   "_SYSTEMD_UNIT": string|null,
//   "HOSTNAME": string|null,
//   "PID": int|null,
//   "UID": int|null,
//   "GID": int|null
// }
//
// Consumer controls:
// - playback=0|1 (default 1) controls whether replay/backlog happens at all
// - backlog=N (default 200, clamped) controls journalctl -n N for replay
// - priority_min=0..7 / priority_max=0..7
// - unit=a.service,b.service  (default wsprrypi.service) or unit=* to disable
// - heartbeat=seconds (default 15, clamped)

declare(strict_types=1);

header('Content-Type: text/event-stream; charset=utf-8');
header('Cache-Control: no-cache, no-transform');
header('Connection: keep-alive');
header('X-Accel-Buffering: no');

set_time_limit(0);
ignore_user_abort(true);

@ini_set('output_buffering', '0');
@ini_set('zlib.output_compression', '0');
@ini_set('implicit_flush', '1');
@ini_set('display_errors', '0');
@ini_set('log_errors', '0');

error_reporting(E_ALL);

if (function_exists('apache_setenv')) {
    @apache_setenv('no-gzip', '1');
}

while (ob_get_level() > 0) {
    @ob_end_flush();
}
@ob_implicit_flush(true);

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
function now_micro(): string
{
    $parts = gettimeofday();
    $sec = isset($parts['sec']) ? (int)$parts['sec'] : time();
    $usec = isset($parts['usec']) ? (int)$parts['usec'] : 0;

    return sprintf('%d%06d', $sec, $usec);
}

final class ClientDisconnected extends RuntimeException {}

function client_connection_open(): bool
{
    global $__clientDisconnected;

    if (!$__clientDisconnected && connection_aborted()) {
        $__clientDisconnected = true;
    }

    return !$__clientDisconnected;
}

function require_client_connection(): void
{
    if (!client_connection_open()) throw new ClientDisconnected('SSE client disconnected');
}

function flush_sse(): void
{
    if (ob_get_level() > 0) {
        @ob_flush();
    }
    @flush();

    require_client_connection();
}

/**
 * Emit an SSE event with JSON-only data payload.
 *
 * @param array       $payload   Unified schema payload (will be JSON encoded).
 * @param string|null $eventName Optional SSE event name (journal/internal).
 * @param string|null $sseCursor Optional cursor for SSE id line (journal only).
 *
 * @return void
 */
function emit_payload(array $payload, ?string $eventName = null, ?string $sseCursor = null): void
{
    if ($sseCursor !== null && $sseCursor !== '') {
        echo 'id: ' . rawurlencode($sseCursor) . "\n";
    }

    if ($eventName !== null && $eventName !== '') {
        echo 'event: ' . $eventName . "\n";
    }

    echo 'data: ' . json_encode(
        $payload,
        JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE
    ) . "\n\n";

    flush_sse();
}

function base_internal_payload(bool $isPlayback): array
{
    return [
        'type' => 'internal',
        'playback' => $isPlayback,
        '__CURSOR' => null,
        '__REALTIME_TIMESTAMP' => now_micro(),
        'PRIORITY' => '6',
        'SYSLOG_IDENTIFIER' => 'log_stream.php',
        'MESSAGE' => '',
        '_SYSTEMD_UNIT' => null,
        'HOSTNAME' => gethostname() ?: null,
        'PID' => getmypid(),
        'UID' => function_exists('posix_getuid') ? posix_getuid() : null,
        'GID' => function_exists('posix_getgid') ? posix_getgid() : null,
    ];
}

function emit_internal(string $message, string $priority, ?string $unit, bool $isPlayback): void
{
    $payload = base_internal_payload($isPlayback);
    $payload['MESSAGE'] = $message;
    $payload['PRIORITY'] = $priority;
    $payload['_SYSTEMD_UNIT'] = $unit;

    emit_payload($payload, 'internal', null);
}

/**
 * Emit a deterministic playback boundary event.
 *
 * The UI uses this to switch from "catch-up" (replay/backlog) to live follow
 * without relying on timing heuristics.
 *
 * @param string $eventName "playback_start" or "playback_end".
 * @param bool   $isPlayback True when replay/backlog is active.
 *
 * @return void
 */
function emit_playback_event(string $eventName, bool $isPlayback): void
{
    $payload = base_internal_payload($isPlayback);
    $payload['MESSAGE'] = $eventName;
    $payload['PRIORITY'] = '7';

    // Use a dedicated SSE event so the client can listen without parsing text.
    emit_payload($payload, $eventName, null);
}

// Global holders for error and shutdown handlers.
$__internalUnitForErrors = null;
$__activeProcess = null;
$__clientDisconnected = false;
$__cleanupActive = false;

// Convert PHP errors into internal events. (Avoid echoing raw PHP warnings.)
set_error_handler(function (int $errno, string $errstr, string $errfile, int $errline): bool {
    global $__internalUnitForErrors, $__cleanupActive, $__clientDisconnected;
    if (!(error_reporting() & $errno) || $__cleanupActive || $__clientDisconnected) {
        return true;
    }

    $msg = '[php error] ' . $errstr . ' at ' . $errfile . ':' . (string)$errline;
    emit_internal($msg, '3', $__internalUnitForErrors, false);
    // Returning true prevents default handler output.
    return true;
});

// Capture fatal errors on shutdown.
register_shutdown_function(function (): void {
    global $__internalUnitForErrors, $__activeProcess, $__cleanupActive, $__clientDisconnected;
    $cleanupSucceeded = true;
    if (is_array($__activeProcess)) {
        $cleanup = proc_cleanup($__activeProcess);
        $cleanupSucceeded = (bool)($cleanup['terminated'] ?? false);
    }

    $err = error_get_last();
    if ($err === null) {
        return;
    }

    $fatalTypes = [E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_USER_ERROR];
    if (!in_array($err['type'], $fatalTypes, true) || !$cleanupSucceeded ||
        $__cleanupActive || $__clientDisconnected || !client_connection_open()) {
        return;
    }

    $msg = '[php fatal] ' . ($err['message'] ?? 'unknown') .
        ' at ' . ($err['file'] ?? '?') . ':' . (string)($err['line'] ?? 0);

    try {
        emit_internal($msg, '3', $__internalUnitForErrors, false);
    } catch (ClientDisconnected $ignored) {
        // Client disconnected between the safety check and flush.
    }
});

// -----------------------------------------------------------------------------
// Defaults
// -----------------------------------------------------------------------------
$defaultUnitName  = 'wsprrypi.service';
$defaultBacklog   = 200;
$maxBacklog       = 2000;

$defaultHeartbeat = 15;
$minHeartbeat     = 5;
$maxHeartbeat     = 60;

// -----------------------------------------------------------------------------
// Consumer controls: playback/backlog/heartbeat
// -----------------------------------------------------------------------------
$playbackParam   = $_GET['playback'] ?? '1';
$playbackEnabled = !in_array($playbackParam, ['0', 0, false, 'false', 'off'], true);

$initialBacklog = $defaultBacklog;
if (isset($_GET['backlog'])) {
    $n = filter_var($_GET['backlog'], FILTER_VALIDATE_INT);
    if ($n !== false) {
        $n = max(0, min((int)$n, $maxBacklog));
        $initialBacklog = $n;
    }
}

$heartbeatSec = $defaultHeartbeat;
if (isset($_GET['heartbeat'])) {
    $h = filter_var($_GET['heartbeat'], FILTER_VALIDATE_INT);
    if ($h !== false) {
        $h = max($minHeartbeat, min((int)$h, $maxHeartbeat));
        $heartbeatSec = $h;
    }
}

// -----------------------------------------------------------------------------
// Consumer filters: PRIORITY and _SYSTEMD_UNIT only
// -----------------------------------------------------------------------------
function clamp_priority($v): ?int
{
    if ($v === null) {
        return null;
    }

    $i = filter_var($v, FILTER_VALIDATE_INT);
    if ($i === false) {
        return null;
    }

    $i = (int)$i;
    if ($i < 0) {
        $i = 0;
    }
    if ($i > 7) {
        $i = 7;
    }

    return $i;
}

$priorityMin = clamp_priority($_GET['priority_min'] ?? null);
$priorityMax = clamp_priority($_GET['priority_max'] ?? null);

if ($priorityMin !== null && $priorityMax !== null && $priorityMin > $priorityMax) {
    $tmp = $priorityMin;
    $priorityMin = $priorityMax;
    $priorityMax = $tmp;
}

// Unit filter: default to the project unit, unless unit=* is requested.
$unitParam = $_GET['unit'] ?? null;
$units = [];
$unitFilterDisabled = false;

if (is_string($unitParam) && $unitParam !== '') {
    if ($unitParam === '*') {
        $unitFilterDisabled = true;
    } else {
        $parts = array_filter(array_map('trim', explode(',', $unitParam)));
        foreach ($parts as $u) {
            if ($u !== '') {
                $units[] = $u;
            }
        }
    }
}

if (!$unitFilterDisabled && count($units) === 0) {
    $units = [$defaultUnitName];
}

// For internal events, attach a "best" unit name (first unit or null).
$internalUnit = (!$unitFilterDisabled && count($units) > 0) ? $units[0] : null;
$__internalUnitForErrors = $internalUnit;

// Resume cursor may arrive either from the SSE Last-Event-ID header
// or from the explicit ?cursor= query parameter used by the client when it
// creates a fresh EventSource instance.
$lastEventIdRaw = $_SERVER['HTTP_LAST_EVENT_ID'] ?? null;
$cursorParamRaw = $_GET['cursor'] ?? null;
$lastCursor = null;

if (is_string($lastEventIdRaw) && $lastEventIdRaw !== '') {
    $lastCursor = rawurldecode($lastEventIdRaw);
} elseif (is_string($cursorParamRaw) && $cursorParamRaw !== '') {
    $lastCursor = trim($cursorParamRaw);
    if ($lastCursor === '') {
        $lastCursor = null;
    }
}

function build_cmd(array $parts): string
{
    return implode(' ', array_map('escapeshellarg', $parts));
}

function process_start_identity(int $pid): ?string
{
    if ($pid <= 1) return null;
    $stat = @file_get_contents('/proc/' . (string)$pid . '/stat');
    if (!is_string($stat)) return null;
    $commEnd = strrpos($stat, ')');
    if ($commEnd === false) return null;
    $fields = preg_split('/\s+/', trim(substr($stat, $commEnd + 1)));
    $startTime = $fields[19] ?? null;
    return is_string($startTime) && preg_match('/^[0-9]+$/', $startTime)
        ? $startTime
        : null;
}

function owned_process_is_running(?array $identity): bool
{
    if (!is_array($identity)) return false;
    $pid = $identity['pid'] ?? null;
    $startTime = $identity['start'] ?? null;
    return is_int($pid) && is_string($startTime) &&
        process_start_identity($pid) === $startTime;
}

function proc_status(array &$started): array
{
    $proc = $started['proc'] ?? null;
    if (!is_resource($proc)) return ['running' => false, 'exitcode' => $started['exitcode'] ?? null];
    $status = proc_get_status($proc);
    $exitcode = (int)($status['exitcode'] ?? -1);
    if ($exitcode >= 0) $started['exitcode'] = $exitcode;
    return $status;
}

function wait_for_owned_process(array &$started, float $timeoutSec): bool
{
    $deadline = microtime(true) + $timeoutSec;
    do {
        $status = proc_status($started);
        if (!($status['running'] ?? false) &&
            !owned_process_is_running($started['identity'] ?? null)) return true;
        usleep(50000);
    } while (microtime(true) < $deadline);

    $status = proc_status($started);
    return !($status['running'] ?? false) &&
        !owned_process_is_running($started['identity'] ?? null);
}

function proc_start(array $parts): array
{
    global $__activeProcess;

    $desc = [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']];
    $proc = proc_open($parts, $desc, $pipes);
    if (!is_resource($proc)) return ['ok' => false, 'stderr' => 'proc_open failed'];
    stream_set_blocking($pipes[1], false);
    stream_set_blocking($pipes[2], false);

    $status = proc_get_status($proc);
    $pid = isset($status['pid']) ? (int)$status['pid'] : 0;
    $started = [
        'ok' => true,
        'proc' => $proc,
        'pipes' => $pipes,
        'running' => (bool)($status['running'] ?? false),
        'exitcode' => $status['exitcode'] ?? null,
        'cleanup_result' => null,
        'identity' => ['pid' => $pid, 'start' => process_start_identity($pid)],
    ];
    $__activeProcess = $started;

    usleep(150000);
    $status = proc_get_status($proc);
    $started['running'] = (bool)($status['running'] ?? false);
    $started['exitcode'] = $status['exitcode'] ?? null;
    $__activeProcess = $started;

    // Replay (`journalctl ... -n N`) can legitimately exit immediately after
    // emitting its output. Follow mode (`journalctl -f`) must remain running.
    // Return handles even when the process has already exited so callers can
    // drain stdout/stderr and close the process cleanly.
    if (!$status['running']) {
        $err = trim(stream_get_contents($pipes[2]));
        if ($err === '') $err = 'process exited immediately';
        $started['ok'] = false;
        $started['stderr'] = $err;
        $started['exited_immediately'] = true;
        return $started;
    }

    return $started;
}

function close_process_pipes(array &$started): void
{
    foreach (($started['pipes'] ?? []) as $pipe) {
        if (is_resource($pipe)) @fclose($pipe);
    }
    $started['pipes'] = [];
}

function cleanup_result(bool $terminated, string $method, ?int $exitcode, bool $alive, ?string $error): array
{
    return [
        'terminated' => $terminated, 'termination' => $method,
        'exitcode' => $exitcode, 'stillAlive' => $alive, 'error' => $error,
    ];
}

function proc_cleanup(array &$started): array
{
    global $__activeProcess, $__cleanupActive;

    $previous = $started['cleanup_result'] ?? null;
    if (is_array($previous) && !empty($previous['terminated'])) return $previous;
    if ($__cleanupActive) {
        $alive = owned_process_is_running($started['identity'] ?? null);
        return cleanup_result(false, 'none', $started['exitcode'] ?? null, $alive, 'cleanup is already active');
    }

    $__cleanupActive = true;
    $termination = 'none';
    $error = null;
    $proc = $started['proc'] ?? null;
    $status = proc_status($started);
    $running = (bool)($status['running'] ?? false);

    if ($running) {
        // The proc resource itself owns the direct child, so graceful
        // termination is safe even if /proc identity capture raced startup.
        $termination = 'graceful';
        @proc_terminate($proc, 15);
        if (!wait_for_owned_process($started, 1.0)) {
            if (!owned_process_is_running($started['identity'] ?? null)) {
                $error = 'owned process identity unavailable before forced termination';
            } else {
                $termination = 'forced';
                @proc_terminate($proc, 9);
                if (!wait_for_owned_process($started, 1.0))
                    $error = 'owned process remained alive after SIGKILL';
            }
        }
    }

    close_process_pipes($started);
    $status = proc_status($started);
    $stillAlive = (bool)($status['running'] ?? false) ||
        owned_process_is_running($started['identity'] ?? null);
    $terminated = !$stillAlive;
    if ($terminated) $error = null;

    if ($terminated && is_resource($proc)) {
        $statusExitcode = (int)($status['exitcode'] ?? -1);
        $closeExitcode = proc_close($proc);
        if ($statusExitcode >= 0) $started['exitcode'] = $statusExitcode;
        elseif ($closeExitcode >= 0) $started['exitcode'] = $closeExitcode;
        $started['proc'] = null;
        $__activeProcess = null;
    } elseif ($stillAlive) {
        $error = $error ?? 'owned process is still running after cleanup';
        // Keep the live resource and identity owned for a shutdown retry. PHP
        // cannot guarantee a bounded final resource destructor for a process
        // stuck in an uninterruptible kernel state.
        $__activeProcess = $started;
    }

    $result = cleanup_result(
        $terminated, $termination, $started['exitcode'] ?? null, $stillAlive, $error
    );
    $started['cleanup_result'] = $result;
    if ($stillAlive) $__activeProcess = $started;
    $__cleanupActive = false;

    return $result;
}

function normalize_realtime_timestamp($value): ?string
{
    if (is_int($value)) {
        $value = (string)$value;
    } elseif (is_float($value)) {
        $value = sprintf('%.0F', $value);
    } elseif (is_string($value)) {
        $value = trim($value);
    } else {
        return null;
    }

    if ($value === '' || !preg_match('/^[0-9]+$/', $value)) {
        return null;
    }

    if (ltrim($value, '0') === '') {
        return null;
    }

    return $value;
}

function send_entry(array $entry, bool $isPlayback): ?string
{
    $cursor = $entry['__CURSOR'] ?? null;
    $realtimeTimestamp = normalize_realtime_timestamp(
        $entry['__REALTIME_TIMESTAMP'] ?? ($entry['_SOURCE_REALTIME_TIMESTAMP'] ?? null)
    );

    $payload = [
        'type' => 'journal',
        'playback' => $isPlayback,
        '__CURSOR' => is_string($cursor) ? $cursor : null,
        '__REALTIME_TIMESTAMP' => $realtimeTimestamp,
        'PRIORITY' => $entry['PRIORITY'] ?? null,
        'SYSLOG_IDENTIFIER' => $entry['SYSLOG_IDENTIFIER'] ?? null,
        'MESSAGE' => (string)($entry['MESSAGE'] ?? ''),
        '_SYSTEMD_UNIT' => $entry['_SYSTEMD_UNIT'] ?? null,
        'HOSTNAME' => $entry['_HOSTNAME'] ?? null,
        'PID' => isset($entry['_PID']) ? (int)$entry['_PID'] : null,
        'UID' => isset($entry['_UID']) ? (int)$entry['_UID'] : null,
        'GID' => isset($entry['_GID']) ? (int)$entry['_GID'] : null,
    ];

    $sseCursor = is_string($cursor) && $cursor !== '' ? $cursor : null;
    emit_payload($payload, 'journal', $sseCursor);

    return $sseCursor;
}

function drain_process(
    array &$started,
    bool $followMode,
    ?string $internalUnit,
    int $heartbeatSec,
    bool $isPlayback
): array {
    $pipes = $started['pipes'];

    $stdoutBuf = '';
    $stderrBuf = '';
    $lastCursor = null;
    $lastHeartbeatAt = 0;
    $entryCount = 0;
    $stderrFull = '';

    // Track whether each pipe is still open. When a pipe reaches EOF, it remains
    // "readable" forever, which can cause stream_select() to wake continuously.
    $outOpen = is_resource($pipes[1]);
    $errOpen = is_resource($pipes[2]);

    while (true) {
        require_client_connection();

        $read = [];
        if ($outOpen && is_resource($pipes[1])) {
            $read[] = $pipes[1];
        }
        if ($errOpen && is_resource($pipes[2])) {
            $read[] = $pipes[2];
        }

        // If the process is not running and both pipes are closed/EOF, we're done.
        $status = proc_status($started);
        if (!$status['running'] && !$outOpen && !$errOpen) {
            break;
        }

        // If there is nothing to read, sleep/heartbeat or wait for exit.
        if (count($read) === 0) {
            if ($followMode) {
                $now = time();
                if (($now - $lastHeartbeatAt) >= $heartbeatSec) {
                    emit_internal('[HEARTBEAT]', '7', $internalUnit, false);
                    $lastHeartbeatAt = $now;
                }
                sleep(1);
                continue;
            }

            // Non-follow replay mode: wait briefly for process exit.
            sleep(1);
            continue;
        }

        $write = [];
        $except = [];
        $ready = @stream_select($read, $write, $except, 1);
        if ($ready === false) {
            $ready = 0;
        }

        if ($ready === 0) {
            if ($followMode) {
                $now = time();
                if (($now - $lastHeartbeatAt) >= $heartbeatSec) {
                    emit_internal('[HEARTBEAT]', '7', $internalUnit, false);
                    $lastHeartbeatAt = $now;
                }
                continue;
            }

            // Replay mode: if the process has exited, do a final drain and exit.
            $status = proc_status($started);
            if (!$status['running']) {
                if ($outOpen && is_resource($pipes[1])) {
                    $finalOut = stream_get_contents($pipes[1]);
                    if ($finalOut !== false && $finalOut !== '') {
                        $stdoutBuf .= $finalOut;
                    }
                    if (feof($pipes[1])) {
                        fclose($pipes[1]);
                        $outOpen = false;
                    }
                }
                if ($errOpen && is_resource($pipes[2])) {
                    $finalErr = stream_get_contents($pipes[2]);
                    if ($finalErr !== false && $finalErr !== '') {
                        $stderrBuf .= $finalErr;
                    }
                    if (feof($pipes[2])) {
                        fclose($pipes[2]);
                        $errOpen = false;
                    }
                }
                // Let the loop condition break when both pipes are closed.
            }

            continue;
        }

        foreach ($read as $r) {
            $chunk = stream_get_contents($r);

            if ($chunk !== false && $chunk !== '') {
                if ($r === $pipes[2]) {
                    $stderrBuf .= $chunk;
                } else {
                    $stdoutBuf .= $chunk;
                }
            }

            // If EOF is reached, close this pipe so we stop selecting on it.
            if (feof($r)) {
                fclose($r);
                if ($r === $pipes[2]) {
                    $errOpen = false;
                } else {
                    $outOpen = false;
                }
            }
        }

        // Process complete stderr lines.
        while (($pos = strpos($stderrBuf, "\n")) !== false) {
            $line = trim(substr($stderrBuf, 0, $pos));
            $stderrBuf = substr($stderrBuf, $pos + 1);
            if ($line !== '') {
                $stderrFull .= ($stderrFull === '' ? '' : "\n") . $line;
                emit_internal('[journalctl stderr] ' . $line, '4', $internalUnit, false);
            }
        }

        // Process complete stdout JSON lines.
        while (($pos = strpos($stdoutBuf, "\n")) !== false) {
            $line = trim(substr($stdoutBuf, 0, $pos));
            $stdoutBuf = substr($stdoutBuf, $pos + 1);
            if ($line === '') {
                continue;
            }

            $entry = json_decode($line, true);
            if (!is_array($entry)) {
                emit_internal('[journalctl non-json] ' . $line, '4', $internalUnit, false);
                continue;
            }

            $c = send_entry($entry, $isPlayback);
            $entryCount += 1;
            if ($c !== null) {
                $lastCursor = $c;
            }
        }
    }

    // Flush any remaining complete lines after exit.
    while (($pos = strpos($stderrBuf, "\n")) !== false) {
        $line = trim(substr($stderrBuf, 0, $pos));
        $stderrBuf = substr($stderrBuf, $pos + 1);
        if ($line !== '') {
            $stderrFull .= ($stderrFull === '' ? '' : "\n") . $line;
            emit_internal('[journalctl stderr] ' . $line, '4', $internalUnit, false);
        }
    }

    while (($pos = strpos($stdoutBuf, "\n")) !== false) {
        $line = trim(substr($stdoutBuf, 0, $pos));
        $stdoutBuf = substr($stdoutBuf, $pos + 1);
        if ($line === '') {
            continue;
        }

        $entry = json_decode($line, true);
        if (!is_array($entry)) {
            emit_internal('[journalctl non-json] ' . $line, '4', $internalUnit, false);
            continue;
        }

        $c = send_entry($entry, $isPlayback);
        $entryCount += 1;
        if ($c !== null) {
            $lastCursor = $c;
        }
    }

    $cleanup = proc_cleanup($started);

    return [
        'lastCursor' => $lastCursor,
        'exitcode' => $cleanup['exitcode'] ?? null,
        'entryCount' => $entryCount,
        'stderrText' => trim($stderrFull),
        'cleanup' => $cleanup,
    ];
}

// Discover journalctl path.
$journalctlPath = trim((string)shell_exec('command -v journalctl 2>/dev/null'));
if ($journalctlPath === '') {
    emit_internal('journalctl not found in PATH', '3', $internalUnit, false);
    exit;
}

// -----------------------------------------------------------------------------
// Build journalctl filter args (shared by replay + follow).
// -----------------------------------------------------------------------------
$journalFilters = [];

// Priority filter: journalctl -p "min..max" or -p "N"
if ($priorityMin !== null || $priorityMax !== null) {
    if ($priorityMin === null) {
        $priorityMin = 0;
    }
    if ($priorityMax === null) {
        $priorityMax = 7;
    }

    $journalFilters[] = '-p';
    $journalFilters[] = ($priorityMin === $priorityMax)
        ? (string)$priorityMin
        : ((string)$priorityMin . '..' . (string)$priorityMax);
}

// Unit filter: add one -u per unit unless disabled
if (!$unitFilterDisabled) {
    foreach ($units as $u) {
        $journalFilters[] = '-u';
        $journalFilters[] = $u;
    }
}

emit_internal(
    'SSE connected. playback=' . ($playbackEnabled ? '1' : '0') .
    ' backlog=' . (string)$initialBacklog .
    ' priority=' . (($priorityMin === null && $priorityMax === null) ? 'any' :
        ((string)($priorityMin ?? 0) . '..' . (string)($priorityMax ?? 7))) .
    ' unit=' . ($unitFilterDisabled ? '*' : implode(',', $units)) .
    ' heartbeat=' . (string)$heartbeatSec . 's',
    '6',
    $internalUnit,
    false
);


function run_replay_command(
    array $parts,
    ?string $internalUnit,
    int $heartbeatSec,
    string $label
): array {
    emit_playback_event('playback_start', true);
    emit_internal($label . ' starting', '7', $internalUnit, true);
    emit_internal($label . ' cmd: ' . build_cmd($parts), '7', $internalUnit, true);

    $started = proc_start($parts);
    require_client_connection();

    if ($started['ok'] || !empty($started['exited_immediately'])) {
        if (!$started['ok'] && !empty($started['exited_immediately'])) {
            emit_internal($label . ' exited quickly; draining output', '7', $internalUnit, true);
        }

        $res = drain_process($started, false, $internalUnit, $heartbeatSec, true);

        if (empty($res['cleanup']['terminated'])) {
            emit_internal(
                $label . ' cleanup failed: ' . (string)($res['cleanup']['error'] ?? 'unknown error'),
                '3',
                $internalUnit,
                false
            );
            return [
                'ok' => false,
                'result' => $res,
            ];
        }

        if (!$started['ok'] && (($res['exitcode'] ?? 0) !== 0)) {
            $stderrText = (string)($res['stderrText'] ?? ($started['stderr'] ?? ''));
            emit_internal(
                $label . ' failed: exitcode=' . (string)($res['exitcode'] ?? '') .
                    ($stderrText !== '' ? ' stderr=' . $stderrText : ''),
                '3',
                $internalUnit,
                false
            );
        } else {
            emit_internal(
                $label . ' complete: entries=' . (string)($res['entryCount'] ?? 0),
                '7',
                $internalUnit,
                true
            );
        }

        emit_playback_event('playback_end', false);

        return [
            'ok' => (($res['exitcode'] ?? 0) === 0),
            'result' => $res,
        ];
    }

    emit_internal(
        $label . ' failed: ' . (string)($started['stderr'] ?? ''),
        '3',
        $internalUnit,
        false
    );

    $cleanup = proc_cleanup($started);

    if (empty($cleanup['terminated'])) {
        emit_internal(
            $label . ' cleanup failed: ' . (string)($cleanup['error'] ?? 'unknown error'),
            '3',
            $internalUnit,
            false
        );
    } else {
        emit_playback_event('playback_end', false);
    }

    return [
        'ok' => false,
        'result' => [
            'lastCursor' => null,
            'exitcode' => $started['exitcode'] ?? 1,
            'entryCount' => 0,
            'stderrText' => (string)($started['stderr'] ?? ''),
            'cleanup' => $cleanup,
        ],
    ];
}

// -----------------------------------------------------------------------------
// Phase 1: Replay (optional)
// -----------------------------------------------------------------------------
$cursorForFollow = null;
$usedCursorReplay = false;

if ($lastCursor !== null) {
    $cursorForFollow = $lastCursor;
    emit_internal('Resume cursor accepted from ' .
        ((is_string($lastEventIdRaw) && $lastEventIdRaw !== '') ? 'Last-Event-ID' : 'query'),
        '7',
        $internalUnit,
        false
    );
}

if ($playbackEnabled && $initialBacklog > 0) {
    $replayBaseParts = array_merge(
        [$journalctlPath, '--no-pager', '-o', 'json'],
        $journalFilters
    );

    $replayOutcome = null;

    if ($lastCursor !== null) {
        $usedCursorReplay = true;
        $cursorReplayParts = $replayBaseParts;
        $cursorReplayParts[] = '--after-cursor';
        $cursorReplayParts[] = $lastCursor;
        $cursorReplayParts[] = '-n';
        $cursorReplayParts[] = (string)$initialBacklog;

        $replayOutcome = run_replay_command(
            $cursorReplayParts,
            $internalUnit,
            $heartbeatSec,
            'journalctl replay from cursor'
        );
        if (empty($replayOutcome['result']['cleanup']['terminated'])) {
            exit;
        }

        $cursorReplayEntries = (int)($replayOutcome['result']['entryCount'] ?? 0);
        $cursorReplayLastCursor = $replayOutcome['result']['lastCursor'] ?? null;
        if (is_string($cursorReplayLastCursor) && $cursorReplayLastCursor !== '') {
            $cursorForFollow = $cursorReplayLastCursor;
        }

        $cursorReplayFailed = !$replayOutcome['ok'];
        $cursorReplayEmpty = ($cursorReplayEntries === 0);

        if ($cursorReplayFailed || $cursorReplayEmpty) {
            $reason = $cursorReplayFailed ? 'cursor replay failed' : 'cursor replay returned 0 entries';
            emit_internal(
                'Falling back to current-boot backlog because ' . $reason,
                '4',
                $internalUnit,
                false
            );

            $bootReplayParts = $replayBaseParts;
            $bootReplayParts[] = '-b';
            $bootReplayParts[] = '0';
            $bootReplayParts[] = '-n';
            $bootReplayParts[] = (string)$initialBacklog;

            $replayOutcome = run_replay_command(
                $bootReplayParts,
                $internalUnit,
                $heartbeatSec,
                'journalctl replay current boot'
            );
            if (empty($replayOutcome['result']['cleanup']['terminated'])) {
                exit;
            }

            $cursorForFollow = $replayOutcome['result']['lastCursor'] ?? null;
            if (!is_string($cursorForFollow) || $cursorForFollow === '') {
                $cursorForFollow = null;
            }
        }
    } else {
        $tailReplayParts = $replayBaseParts;
        $tailReplayParts[] = '-n';
        $tailReplayParts[] = (string)$initialBacklog;

        $replayOutcome = run_replay_command(
            $tailReplayParts,
            $internalUnit,
            $heartbeatSec,
            'journalctl replay backlog'
        );
        if (empty($replayOutcome['result']['cleanup']['terminated'])) {
            exit;
        }

        $cursorForFollow = $replayOutcome['result']['lastCursor'] ?? $cursorForFollow;
    }
}
// -----------------------------------------------------------------------------
// Phase 2: Follow (always)
// -----------------------------------------------------------------------------
emit_internal('journalctl follow loop entering', '7', $internalUnit, false);

while (true) {
    require_client_connection();

    $followParts = array_merge(
        [$journalctlPath, '--no-pager', '-o', 'json', '-f'],
        $journalFilters
    );

    if ($cursorForFollow !== null) {
        array_splice($followParts, 4, 0, ['--after-cursor', $cursorForFollow]);
    }

    emit_internal('journalctl follow starting', '7', $internalUnit, false);
    emit_internal('journalctl follow cmd: ' . build_cmd($followParts), '7', $internalUnit, false);

    $started = proc_start($followParts);
    require_client_connection();

    if (!$started['ok']) {
        $followErr = (string)($started['stderr'] ?? '');
        emit_internal('journalctl follow failed: ' . $followErr, '3', $internalUnit, false);

        $cleanup = proc_cleanup($started);

        if (empty($cleanup['terminated'])) {
            emit_internal(
                'journalctl follow cleanup failed: ' .
                    (string)($cleanup['error'] ?? 'unknown error'),
                '3',
                $internalUnit,
                false
            );
            break;
        }

        if ($cursorForFollow !== null) {
            emit_internal(
                'Clearing follow cursor and retrying live follow without cursor',
                '4',
                $internalUnit,
                false
            );
            $cursorForFollow = null;
        }

        sleep(1);
        continue;
    }

    $res = drain_process($started, true, $internalUnit, $heartbeatSec, false);
    if (empty($res['cleanup']['terminated'])) {
        emit_internal(
            'journalctl follow cleanup failed: ' .
                (string)($res['cleanup']['error'] ?? 'unknown error'),
            '3',
            $internalUnit,
            false
        );
        break;
    }
    $cursorForFollow = $res['lastCursor'] ?? $cursorForFollow;

    emit_internal('journalctl follow restarted', '4', $internalUnit, false);

    if (!$playbackEnabled) {
        $cursorForFollow = null;
    }
}

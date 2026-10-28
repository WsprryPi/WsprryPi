<?php
// SPDX-License-Identifier: MIT
// Pure bounded normalization shared by the SSE adapter and tests.
declare(strict_types=1);
require_once __DIR__ . "/log_origin_v2.php";
function log_source($value, int $version = 1): string {
    if ($version === 2) return log_v2_source($value);
    return is_string($value) && ($value === 'all' || $value === 'local' || preg_match('/^pico:[0-9a-f]{32}$/D', $value))
        ? $value : 'local';
}
function log_scope_matches(array $entry, int $version = 1): bool {
    if ($version === 2) return log_v2_scope_matches($entry);
    if (($entry['_SYSTEMD_UNIT'] ?? null) === 'wsprrypi.service') return true;
    $pid = $entry['_PID'] ?? null;
    return ($pid === '1' || $pid === 1) &&
        (($entry['UNIT'] ?? null) === 'wsprrypi.service' || ($entry['OBJECT_SYSTEMD_UNIT'] ?? null) === 'wsprrypi.service');
}
function log_source_matches(array $entry, string $source, int $version = 1): bool {
    if ($version === 2) return log_v2_source_matches($entry,$source);
    $id = $entry['WSPRRY_ORIGIN_ID'] ?? null;
    $pico = ($entry['WSPRRY_ORIGIN_KIND'] ?? null) === 'pico' && is_string($id) && preg_match('/^[0-9a-f]{32}$/D', $id);
    return $source === 'all' || ($source === 'local' ? !$pico : $pico && substr($source, 5) === $id);
}
function log_cursor_matches(array $entry, string $cursor, string $source, int $version = 1): bool {
    return ($entry['__CURSOR'] ?? null) === $cursor && log_scope_matches($entry,$version) && log_source_matches($entry, $source,$version);
}
function log_origin(array $entry, int $version = 1): array {
    if ($version === 2) return log_v2_origin($entry);
    $field = static function (string $key, int $limit = 128) use ($entry): ?string {
        $value = $entry[$key] ?? null;
        return is_string($value) && strlen($value) <= $limit && !preg_match('/[\x00-\x1f\x7f]/', $value) ? $value : null;
    };
    $decimal = static function (string $key) use ($field): ?string {
        $value = $field($key, 20);
        return $value !== null && preg_match('/^(0|[1-9][0-9]{0,19})$/D', $value) ? $value : null;
    };
    $kind = $field('WSPRRY_ORIGIN_KIND') === 'pico' ? 'pico' : 'pi';
    static $hostId = null;
    if ($hostId === null) {
        $machine = trim((string)@file_get_contents('/etc/machine-id'));
        $hostId = preg_match('/^[0-9a-f]{32}$/D', $machine) ? substr(hash('sha256', 'wsprrypi-wtp-device-v1:' . $machine), 0, 32) : '';
    }
    return [
        'log_schema' => 'wsprry-log/1', 'source' => $kind === 'pico' ? 'pico:' . ($field('WSPRRY_ORIGIN_ID', 32) ?? '') : 'local',
        'origin_kind' => $kind, 'origin_id' => $field('WSPRRY_ORIGIN_ID', 32) ?? ($hostId ?: null),
        'origin_name' => $field('WSPRRY_ORIGIN_NAME', 63) ?? $field('_HOSTNAME'),
        'origin_boot' => $field('WSPRRY_ORIGIN_BOOT', 32) ?? $field('_BOOT_ID', 32),
        'origin_sequence' => $decimal('WSPRRY_ORIGIN_SEQUENCE'), 'origin_utc_ns' => $decimal('WSPRRY_ORIGIN_UTC_NS'),
        'origin_monotonic_ns' => $decimal('WSPRRY_ORIGIN_MONOTONIC_NS'), 'event_code' => $field('WSPRRY_EVENT', 32),
        'facility' => $field('SYSLOG_FACILITY', 2), 'dropped' => $decimal('WSPRRY_DROPPED'),
        'collector_id' => $field('WSPRRY_COLLECTOR_ID', 32), 'receipt_us' => $decimal('WSPRRY_RECEIPT_US'),
        'ingress' => $field('WSPRRY_INGRESS', 16), 'forwarders' => $field('WSPRRY_FORWARDERS', 256),
    ];
}

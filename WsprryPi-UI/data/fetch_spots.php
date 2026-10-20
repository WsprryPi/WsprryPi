<?php

declare(strict_types=1);

@ini_set('display_errors', '0');
@ini_set('log_errors', '1');
error_reporting(E_ALL);

header('Content-Type: application/json; charset=UTF-8');

const SOURCE_AUTO = 'auto';
const SOURCE_WSPR_LIVE_DOWNLOADER = 'wspr_live_downloader';
const SOURCE_WSPR_LIVE_CLICKHOUSE = 'wspr_live_clickhouse';
const DEFAULT_TTL = 120;
const MIN_TTL = 30;
const MAX_TTL = 900;
const CLICKHOUSE_URL = 'https://db1.wspr.live/';
const CLICKHOUSE_LIMIT = 250;

final class UpstreamException extends RuntimeException
{
    public function __construct(
        public readonly string $source,
        public readonly string $type,
        string $message,
        public readonly int $statusCode = 0
    ) {
        parent::__construct($message);
    }
}

function respond(int $statusCode, array $payload): never
{
    http_response_code($statusCode);
    echo json_encode($payload, JSON_UNESCAPED_SLASHES);
    exit;
}

function normalizeEnvelope(mixed $decoded): array
{
    if (is_array($decoded) && array_is_list($decoded)) {
        return ['data' => $decoded];
    }

    if (!is_array($decoded)) {
        return ['data' => []];
    }

    if (!isset($decoded['data']) || !is_array($decoded['data'])) {
        $decoded['data'] = [];
    }

    return $decoded;
}

function parseHttpStatus(array $headers): int
{
    foreach ($headers as $header) {
        if (preg_match('#^HTTP/\S+\s+(\d+)#', $header, $matches)) {
            return (int)$matches[1];
        }
    }

    return 0;
}

function httpRequest(string $source, string $url, int $timeoutSeconds): array
{
    $warning = null;
    set_error_handler(static function (int $severity, string $message) use (&$warning): bool {
        $warning = $message;
        return true;
    });

    $body = file_get_contents(
        $url,
        false,
        stream_context_create([
            'http' => [
                'method' => 'GET',
                'timeout' => $timeoutSeconds,
                'ignore_errors' => true,
                'header' => "User-Agent: WsprryPi-UI/1.0\r\nAccept: application/json",
            ],
            'ssl' => [
                'verify_peer' => true,
                'verify_peer_name' => true,
            ],
        ])
    );

    restore_error_handler();

    $headers = $http_response_header ?? [];
    $status = parseHttpStatus($headers);

    if ($body === false) {
        $messageText = strtolower($warning ?? '');
        $type = str_contains($messageText, 'timed out') ? 'timeout' : 'network';
        $message = $type === 'timeout'
            ? sprintf('%s timed out before returning spot data.', $source)
            : sprintf('%s could not be reached: %s', $source, $warning !== null && $warning !== '' ? $warning : 'unknown network error');
        throw new UpstreamException($source, $type, $message, $status);
    }

    if ($status < 200 || $status >= 300) {
        throw new UpstreamException($source, 'http_status', sprintf('%s returned HTTP %d.', $source, $status), $status);
    }

    return ['body' => $body];
}

function isDownloaderRateLimitHtml(string $body): bool
{
    return str_contains($body, 'Please let the server cool down between requests');
}

function normalizeSource(string $value): string
{
    $value = strtolower(trim($value));
    $allowed = [SOURCE_AUTO, SOURCE_WSPR_LIVE_DOWNLOADER, SOURCE_WSPR_LIVE_CLICKHOUSE];
    if (!in_array($value, $allowed, true)) {
        respond(400, ['error' => 'Invalid source. Use auto, wspr_live_downloader, or wspr_live_clickhouse.']);
    }

    return $value;
}

function sanitizeDownloaderCallsign(string $value, string $fieldName): string
{
    $value = strtoupper(trim($value));
    if ($value === '') {
        respond(400, ['error' => sprintf('%s is required.', $fieldName)]);
    }

    $value = preg_replace('/[^A-Z0-9\/<>\*\%]/', '', $value) ?? '';
    if ($value === '') {
        respond(400, ['error' => sprintf('A valid %s is required.', strtolower($fieldName))]);
    }

    return $value;
}

function chooseLookupBaseCallsignSegment(array $segments): string
{
    $best = '';
    $bestScore = [-1, -1];

    foreach ($segments as $segment) {
        $normalized = strtoupper(trim((string)$segment));
        if ($normalized === '') {
            continue;
        }

        $score = 0;
        if (preg_match('/[A-Z]/', $normalized)) {
            $score += 1;
        }
        if (preg_match('/[0-9]/', $normalized)) {
            $score += 1;
        }

        $candidateScore = [$score, strlen($normalized)];
        if ($candidateScore > $bestScore) {
            $best = $normalized;
            $bestScore = $candidateScore;
        }
    }

    return $best;
}

function normalizeLookupBaseCallsign(string $value): string
{
    $value = strtoupper(trim($value));
    if ($value === '') {
        return '';
    }

    if (preg_match('/^<([A-Z0-9\/]+)>$/', $value, $matches)) {
        $value = $matches[1];
    }

    $segments = array_values(array_filter(
        explode('/', $value),
        static fn(string $segment): bool => $segment !== ''
    ));

    if ($segments === []) {
        return $value;
    }

    $best = chooseLookupBaseCallsignSegment($segments);
    return $best !== '' ? $best : $value;
}

function buildLookupCallsignCandidates(string $value): array
{
    $baseCallsign = normalizeLookupBaseCallsign($value);
    if ($baseCallsign === '') {
        respond(400, ['error' => 'A valid transmitter callsign is required.']);
    }

    return [
        'base' => $baseCallsign,
        'downloader' => array_values(array_unique([
            $baseCallsign,
            '%/' . $baseCallsign,
            $baseCallsign . '/%',
            '<' . $baseCallsign . '>',
        ])),
        'clickhouse_regexes' => array_values(array_unique([
            '^' . preg_quote($baseCallsign, '/') . '$',
            '^<'. preg_quote($baseCallsign, '/') . '>$',
            '^[A-Z0-9]+/' . preg_quote($baseCallsign, '/') . '$',
            '^' . preg_quote($baseCallsign, '/') . '/[A-Z0-9]+$',
        ])),
    ];
}

function sanitizeClickhouseCallsign(string $value, string $fieldName): string
{
    $value = strtoupper(trim($value));
    if ($value === '') {
        respond(400, ['error' => sprintf('%s is required.', $fieldName)]);
    }

    if (!preg_match('/^[A-Z0-9]+(?:\/[A-Z0-9]+)*$/', $value)) {
        respond(400, ['error' => sprintf('Invalid %s. Use only callsign characters and slash modifiers.', strtolower($fieldName))]);
    }

    return $value;
}

function sanitizeOptionalDownloaderRxSign(string $value): string
{
    $value = strtoupper(trim($value));
    if ($value === '') {
        return '%';
    }

    $value = preg_replace('/[^A-Z0-9\/\*\%]/', '', $value) ?? '%';
    return $value !== '' ? $value : '%';
}

function sanitizeOptionalClickhouseRxSign(string $value): ?string
{
    $value = trim($value);
    if ($value === '' || $value === '%' || $value === '*') {
        return null;
    }

    return sanitizeClickhouseCallsign($value, 'Receiver callsign');
}

function parseUtcTimestamp(string $value): DateTimeImmutable
{
    $tsRegex = '/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/';
    if (!preg_match($tsRegex, $value)) {
        respond(400, ['error' => 'Invalid timestamp format. Use YYYY-MM-DD HH:MM:SS.']);
    }

    $dt = DateTimeImmutable::createFromFormat('Y-m-d H:i:s', $value, new DateTimeZone('UTC'));
    if (!$dt) {
        respond(400, ['error' => 'Invalid timestamp value.']);
    }

    return $dt;
}

function buildCachePath(string $source, string $txSign, string $start, string $end, string $rxSign): string
{
    $cacheDir = __DIR__ . '/cache';
    if (!is_dir($cacheDir) && !mkdir($cacheDir, 0755, true)) {
        respond(500, ['error' => 'Cannot create cache directory.']);
    }

    $cacheKey = md5($source . '|' . $txSign . '|' . $start . '|' . $end . '|' . $rxSign);
    return sprintf('%s/wspr_spots_%s_%s.json', $cacheDir, $source, $cacheKey);
}

function readCache(string $cacheFile, int $ttl): ?array
{
    if (!is_file($cacheFile) || (time() - filemtime($cacheFile) >= $ttl)) {
        return null;
    }

    $cached = @file_get_contents($cacheFile);
    if ($cached === false) {
        return null;
    }

    $decoded = json_decode($cached, true);
    if (json_last_error() !== JSON_ERROR_NONE) {
        return null;
    }

    return normalizeEnvelope($decoded);
}

function writeCache(string $cacheFile, array $payload): void
{
    $encoded = json_encode($payload, JSON_UNESCAPED_SLASHES);
    if ($encoded !== false) {
        @file_put_contents($cacheFile, $encoded, LOCK_EX);
    }
}

function buildCallsignRegex(string $callsign): string
{
    return '(^|/)' . preg_quote($callsign, '/') . '($|/)';
}

function normalizeSpotRow(array $row): array
{
    return [
        'time' => (string)($row['time'] ?? ''),
        'tx_sign' => (string)($row['tx_sign'] ?? ''),
        'frequency' => $row['frequency'] ?? '',
        'snr' => $row['snr'] ?? '',
        'drift' => $row['drift'] ?? '',
        'tx_loc' => (string)($row['tx_loc'] ?? ''),
        'power' => $row['power'] ?? '',
        'rx_sign' => (string)($row['rx_sign'] ?? ''),
        'rx_loc' => (string)($row['rx_loc'] ?? ''),
        'distance' => $row['distance'] ?? '',
        'code' => $row['code'] ?? '',
        'version' => (string)($row['version'] ?? ''),
    ];
}

function normalizeClickhouseRows(array $rows): array
{
    $normalized = [];
    foreach ($rows as $row) {
        $normalized[] = normalizeSpotRow([
            'time' => (string)($row['time'] ?? ''),
            'tx_sign' => strtoupper((string)($row['tx_sign'] ?? '')),
            'frequency' => isset($row['frequency']) ? (int)$row['frequency'] : '',
            'snr' => isset($row['snr']) ? (int)$row['snr'] : '',
            'drift' => isset($row['drift']) ? (int)$row['drift'] : '',
            'tx_loc' => strtoupper((string)($row['tx_loc'] ?? '')),
            'power' => isset($row['power']) ? (int)$row['power'] : '',
            'rx_sign' => strtoupper((string)($row['rx_sign'] ?? '')),
            'rx_loc' => strtoupper((string)($row['rx_loc'] ?? '')),
            'distance' => isset($row['distance']) ? (int)$row['distance'] : '',
            'code' => isset($row['code']) ? (int)$row['code'] : '',
            'version' => (string)($row['version'] ?? ''),
        ]);
    }

    return $normalized;
}

function deduplicateSpotRows(array $rows): array
{
    $unique = [];
    foreach ($rows as $row) {
        $normalized = normalizeSpotRow($row);
        $key = json_encode($normalized, JSON_UNESCAPED_SLASHES);
        if ($key === false) {
            $key = md5(serialize($normalized));
        }
        $unique[$key] = $normalized;
    }

    $deduplicated = array_values($unique);
    usort(
        $deduplicated,
        static fn(array $lhs, array $rhs): int => strcmp(
            (string)($rhs['time'] ?? ''),
            (string)($lhs['time'] ?? '')
        )
    );

    return $deduplicated;
}

function fetchDownloaderDataForCandidate(string $txSign, string $rxSign, string $start, string $end, string $format): array
{
    $query = http_build_query([
        'start' => $start,
        'end' => $end,
        'tx_sign' => $txSign,
        'rx_sign' => $rxSign,
        'format' => $format,
    ]);

    $response = httpRequest(SOURCE_WSPR_LIVE_DOWNLOADER, 'https://wspr.live/wspr_downloader.php?' . $query, 6);
    if (isDownloaderRateLimitHtml($response['body'])) {
        throw new UpstreamException(
            SOURCE_WSPR_LIVE_DOWNLOADER,
            'rate_limit',
            'wspr_live_downloader rate limit reached. Please wait and try again.'
        );
    }

    $decoded = json_decode($response['body'], true);
    if (json_last_error() !== JSON_ERROR_NONE) {
        throw new UpstreamException(SOURCE_WSPR_LIVE_DOWNLOADER, 'malformed_json', 'wspr_live_downloader returned invalid JSON.');
    }

    $payload = normalizeEnvelope($decoded);
    $payload['source_used'] = SOURCE_WSPR_LIVE_DOWNLOADER;
    $payload['fallback_used'] = false;
    $payload['warning'] = '';

    return $payload;
}

function fetchDownloaderData(array $txCandidates, string $rxSign, string $start, string $end, string $format): array
{
    $mergedRows = [];
    foreach ($txCandidates as $txCandidate) {
        $payload = fetchDownloaderDataForCandidate($txCandidate, $rxSign, $start, $end, $format);
        $mergedRows = array_merge($mergedRows, $payload['data'] ?? []);
    }

    return [
        'data' => deduplicateSpotRows($mergedRows),
        'source_used' => SOURCE_WSPR_LIVE_DOWNLOADER,
        'fallback_used' => false,
        'warning' => '',
    ];
}

function fetchClickhouseData(array $txRegexes, ?string $rxSign, DateTimeImmutable $start, DateTimeImmutable $end): array
{
    $txFilters = array_map(
        static fn(string $regex): string => sprintf("match(upper(tx_sign), '%s')", $regex),
        $txRegexes
    );
    $filters = [
        sprintf("time >= toDateTime('%s', 'UTC')", $start->format('Y-m-d H:i:s')),
        sprintf("time <= toDateTime('%s', 'UTC')", $end->format('Y-m-d H:i:s')),
        '(' . implode(' OR ', $txFilters) . ')',
    ];

    if ($rxSign !== null) {
        $filters[] = sprintf("match(upper(rx_sign), '%s')", buildCallsignRegex($rxSign));
    }

    $sql = implode("\n", [
        'SELECT time, tx_sign, frequency, snr, drift, tx_loc, power, rx_sign, rx_loc, distance, code, version',
        'FROM wspr.rx',
        'WHERE ' . implode(' AND ', $filters),
        'ORDER BY time DESC',
        'LIMIT ' . CLICKHOUSE_LIMIT,
        'FORMAT JSON',
    ]);

    $response = httpRequest(
        SOURCE_WSPR_LIVE_CLICKHOUSE,
        CLICKHOUSE_URL . '?' . http_build_query(['query' => $sql]),
        6
    );

    $decoded = json_decode($response['body'], true);
    if (json_last_error() !== JSON_ERROR_NONE || !is_array($decoded)) {
        throw new UpstreamException(SOURCE_WSPR_LIVE_CLICKHOUSE, 'malformed_json', 'wspr_live_clickhouse returned invalid JSON.');
    }

    if (!isset($decoded['data']) || !is_array($decoded['data'])) {
        throw new UpstreamException(SOURCE_WSPR_LIVE_CLICKHOUSE, 'invalid_response', 'wspr_live_clickhouse returned an unexpected JSON payload.');
    }

    return [
        'data' => normalizeClickhouseRows($decoded['data']),
        'source_used' => SOURCE_WSPR_LIVE_CLICKHOUSE,
        'fallback_used' => false,
        'warning' => '',
    ];
}

$startText = trim((string)($_GET['start'] ?? ''));
$endText = trim((string)($_GET['end'] ?? ''));
$txSignRaw = (string)($_GET['tx_sign'] ?? '');
$rxSignRaw = (string)($_GET['rx_sign'] ?? '%');

if ($txSignRaw === '' || $startText === '' || $endText === '') {
    respond(400, ['error' => 'Missing required query parameters.']);
}

$start = parseUtcTimestamp($startText);
$end = parseUtcTimestamp($endText);
if ($end < $start) {
    respond(400, ['error' => 'End time must be later than start time.']);
}

$source = normalizeSource((string)($_GET['source'] ?? SOURCE_AUTO));
$ttl = isset($_GET['ttl']) ? max(MIN_TTL, min(MAX_TTL, (int)$_GET['ttl'])) : DEFAULT_TTL;
$format = strtoupper(trim((string)($_GET['format'] ?? 'JSON')));
if ($format !== 'JSON') {
    $format = 'JSON';
}

$txLookup = buildLookupCallsignCandidates($txSignRaw);
$txLookupBase = sanitizeClickhouseCallsign($txLookup['base'], 'Transmitter callsign');
$txSignDownloaderCandidates = array_map(
    static fn(string $candidate): string => sanitizeDownloaderCallsign($candidate, 'Transmitter callsign'),
    $txLookup['downloader']
);
$txSignClickhouseRegexes = $txLookup['clickhouse_regexes'];
$rxSignDownloader = sanitizeOptionalDownloaderRxSign($rxSignRaw);
$rxSignClickhouse = sanitizeOptionalClickhouseRxSign($rxSignRaw);

$cacheFile = buildCachePath(
    $source,
    $txLookupBase,
    $start->format('Y-m-d H:i:s'),
    $end->format('Y-m-d H:i:s'),
    $rxSignClickhouse ?? $rxSignDownloader
);

$cached = readCache($cacheFile, $ttl);
if ($cached !== null) {
    echo json_encode($cached, JSON_UNESCAPED_SLASHES);
    exit;
}

if ($source === SOURCE_WSPR_LIVE_DOWNLOADER) {
    try {
        $payload = fetchDownloaderData($txSignDownloaderCandidates, $rxSignDownloader, $startText, $endText, $format);
        writeCache($cacheFile, $payload);
        echo json_encode($payload, JSON_UNESCAPED_SLASHES);
        exit;
    } catch (UpstreamException $e) {
        respond(502, ['error' => $e->getMessage(), 'source_used' => SOURCE_WSPR_LIVE_DOWNLOADER, 'fallback_used' => false]);
    }
}

if ($source === SOURCE_WSPR_LIVE_CLICKHOUSE) {
    try {
        $payload = fetchClickhouseData($txSignClickhouseRegexes, $rxSignClickhouse, $start, $end);
        writeCache($cacheFile, $payload);
        echo json_encode($payload, JSON_UNESCAPED_SLASHES);
        exit;
    } catch (UpstreamException $e) {
        respond(502, ['error' => $e->getMessage(), 'source_used' => SOURCE_WSPR_LIVE_CLICKHOUSE, 'fallback_used' => false]);
    }
}

try {
    $payload = fetchDownloaderData($txSignDownloaderCandidates, $rxSignDownloader, $startText, $endText, $format);
    writeCache($cacheFile, $payload);
    echo json_encode($payload, JSON_UNESCAPED_SLASHES);
    exit;
} catch (UpstreamException $downloaderError) {
    try {
        $payload = fetchClickhouseData($txSignClickhouseRegexes, $rxSignClickhouse, $start, $end);
        $payload['fallback_used'] = true;
        $payload['warning'] = 'wspr.live downloader failed, using direct wspr.live data.';
        $payload['fallback_reason'] = $downloaderError->getMessage();
        writeCache($cacheFile, $payload);
        echo json_encode($payload, JSON_UNESCAPED_SLASHES);
        exit;
    } catch (UpstreamException $clickhouseError) {
        respond(502, [
            'error' => sprintf(
                'Automatic failover failed. wspr.live downloader: %s wspr.live direct: %s',
                $downloaderError->getMessage(),
                $clickhouseError->getMessage()
            ),
            'source_used' => null,
            'fallback_used' => true,
        ]);
    }
}

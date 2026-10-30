<?php
// Hardware-free browser fixture only. Never execute application, privileged,
// spot-proxy or journal handlers, and never allow direct-port fallback.
$root = realpath(__DIR__ . '/../data');
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
header("Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self'; form-action 'none'; frame-src 'none'");
// Supply only synthetic read-only status so the production maintenance guard
// can settle. No request is forwarded to an application or attached device.
if ($path === '/api/control/v1/status' && $_SERVER['REQUEST_METHOD'] === 'GET') {
    header('Content-Type: application/json');
    echo json_encode(['contract' => 'Wsprry-Control/1', 'host' => ['selected' => false, 'firmware_maintenance' => false,
        'ready' => false, 'phase' => 'idle', 'session_phase' => 'disconnected']]);
    return true;
}
$file = realpath($root . '/' . rawurldecode($path));
$pages = ['index.php', 'maintenance.php', 'view_logs.php', 'view_spots.php',
    'ui-version.php', 'ui-manifest.php', 'version.php'];
$static = ['css', 'js', 'ttf', 'woff2', 'png', 'ico', 'webmanifest'];
if ($path === '/' || ($file && str_starts_with($file, $root . '/') &&
    (in_array(ltrim($path, '/'), $pages, true) ||
     in_array(pathinfo($file, PATHINFO_EXTENSION), $static, true)))) {
    return false;
}
http_response_code(503);
header('Content-Type: application/json');
echo json_encode(['error' => ['code' => 'fixture_endpoint_unavailable'],
    'message' => 'Browser fixture: no application or external endpoint is available.']);

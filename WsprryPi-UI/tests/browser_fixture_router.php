<?php
// Hardware-free browser fixture only. Never execute application, privileged,
// spot-proxy or journal handlers, and never allow direct-port fallback.
$root = realpath(getenv('WSPRRYPI_TEST_UI_DOCUMENT_ROOT') ?: __DIR__ . '/../data');
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
$prefixed = getenv('WSPRRYPI_UI_FIXTURE_PREFIX') === '/wsprrypi' && str_starts_with($path, '/wsprrypi/');
if ($prefixed) {
    $path = substr($path, strlen('/wsprrypi'));
    $_SERVER['SCRIPT_NAME'] = '/wsprrypi' . $path;
    $_SERVER['PHP_SELF'] = $_SERVER['SCRIPT_NAME'];
}
header("Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self'; form-action 'none'; frame-src 'none'");
// Optional native-host fixture: production templates/scripts, synthetic host data.
if (getenv('WSPRRYPI_NATIVE_HOST_FIXTURE') === '1') {
    if ($path === '/__native_host_fixture__.js') {
        header('Content-Type: application/javascript');
        readfile(__DIR__ . '/native_host_fixture.js'); return true;
    }
    if ($path === '/index.php') {
        chdir($root); require $root . '/index.php';
        echo '<script src="/__native_host_fixture__.js"></script>'; return true;
    }
}
// Static tests exercise the ordinary shell unless they supply their own host.
if ($path === '/version' && $_SERVER['REQUEST_METHOD'] === 'GET') {
    header('Content-Type: application/json');
    echo json_encode(['host_status' => ['role' => 'transmitter']]);
    return true;
}
// Supply only synthetic read-only status so the production maintenance guard
// can settle. No request is forwarded to an application or attached device.
if ($path === '/api/control/v1/status' && $_SERVER['REQUEST_METHOD'] === 'GET') {
    header('Content-Type: application/json');
    echo json_encode(['contract' => 'Wsprry-Control/1', 'host' => ['selected' => false, 'firmware_maintenance' => false,
        'ready' => false, 'phase' => 'idle', 'session_phase' => 'disconnected']]);
    return true;
}
// Read-only dependency regression responses; never proxy or persist anything.
if ($_SERVER['REQUEST_METHOD'] === 'GET' && str_starts_with($path, '/__dependency_fixture__/')) {
    header('Content-Type: application/json');
    switch ($path) {
        case '/__dependency_fixture__/ok': echo '{"fixture":true}'; return true;
        case '/__dependency_fixture__/invalid': echo '{broken'; return true;
        case '/__dependency_fixture__/slow': usleep(300000); echo '{}'; return true;
    }
}
$file = realpath($root . '/' . rawurldecode($path));
$pages = ['index.php', 'maintenance.php', 'view_logs.php', 'view_spots.php',
    'ui-version.php', 'ui-manifest.php', 'version.php', 'view_diag_logs.php'];
$static = ['css', 'js', 'ttf', 'woff2', 'png', 'ico', 'webmanifest'];
if ($path === '/' || ($file && str_starts_with($file, $root . '/') &&
    (in_array(ltrim($path, '/'), $pages, true) ||
     in_array(pathinfo($file, PATHINFO_EXTENSION), $static, true)))) {
    if ($prefixed && $file) {
        if (in_array(ltrim($path, '/'), $pages, true)) {
            chdir($root);
            require $file;
        } else {
            $types = ['css' => 'text/css', 'js' => 'application/javascript', 'woff2' => 'font/woff2', 'ttf' => 'font/ttf', 'png' => 'image/png', 'ico' => 'image/x-icon', 'webmanifest' => 'application/manifest+json'];
            header('Content-Type: ' . $types[pathinfo($file, PATHINFO_EXTENSION)]);
            readfile($file);
        }
        return true;
    }
    return false;
}
http_response_code(503);
header('Content-Type: application/json');
echo json_encode(['error' => ['code' => 'fixture_endpoint_unavailable'],
    'message' => 'Browser fixture: no application or external endpoint is available.']);

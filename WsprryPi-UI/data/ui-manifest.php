<?php
require_once __DIR__ . '/ui_version.php';

header('Content-Type: application/json; charset=UTF-8');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');
header('Expires: 0');

$manifestPath = __DIR__ . DIRECTORY_SEPARATOR . WSPRRYPI_UI_MANIFEST_FILENAME;
try {
    wsprrypiUiLoadManifest($manifestPath);
    $content = @file_get_contents($manifestPath);
    if (!is_string($content)) {
        throw new WsprryPiUiIdentityError('Packaged UI manifest could not be read.');
    }
    echo $content;
} catch (WsprryPiUiIdentityError $error) {
    http_response_code(404);
    echo json_encode([
        'schema_version' => WSPRRYPI_UI_MANIFEST_SCHEMA_VERSION,
        'error' => 'packaged_ui_manifest_unavailable',
    ]);
}

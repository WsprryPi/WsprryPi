<?php
require_once __DIR__ . '/ui_version.php';

header('Content-Type: application/json');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');
header('Expires: 0');

$output = getWsprryPiUiVersion();
$uiBuildId = getWsprryPiUiBuildId();

echo json_encode([
    'wspr_version' => $output,
    'ui_version' => $output,
    'ui_build_id' => $uiBuildId,
]);

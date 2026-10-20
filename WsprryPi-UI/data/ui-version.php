<?php
require_once __DIR__ . '/ui_version.php';

header('Content-Type: application/json; charset=UTF-8');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');
header('Expires: 0');

echo json_encode(
    wsprrypiUiIdentityStatus(),
    JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE
);

<?php
require_once __DIR__ . '/app_state.php';

$current = $legacyCurrentPage;

$pageMetadata = [];
foreach ($viewMetadata as $viewKey => $metadata) {
    $pageMetadata[$metadata['legacyScript']] = [
        'title' => $metadata['title'],
        'view' => $viewKey,
        'navLabel' => $metadata['navLabel'],
        'navIcon' => $metadata['navIcon'],
    ];
}

$defaultPageMetadata = $pageMetadata['index.php'];
$currentPageMetadata = $pageMetadata[$current] ?? $defaultPageMetadata;

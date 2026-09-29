<?php

$viewMetadata = [
    'operation' => [
        'title' => 'Wsprry Pi Operation',
        'navLabel' => 'Operation',
        'navIcon' => 'fa-tower-broadcast',
        'legacyScript' => 'index.php',
        'cardClass' => 'operation-card',
        'bodyClass' => '',
        'htmlTheme' => 'light',
        'css' => ['index.css', 'operation.css'],
        'js' => ['local-wtp.js', 'operation.js'],
        'partial' => __DIR__ . '/views/operation.php',
    ],
    'config' => [
        'title' => 'Wsprry Pi Setup',
        'navLabel' => 'Setup',
        'navIcon' => 'fa-sliders',
        'legacyScript' => 'config.php',
        'cardClass' => 'template-card',
        'bodyClass' => '',
        'htmlTheme' => 'light',
        'css' => ['index.css'],
        'js' => ['cw_timing_state.js', 'local-wtp.js', 'wtp.js', 'wtp-management.js', 'fleet-schedules.js', 'index.js'],
        'partial' => __DIR__ . '/views/config.php',
    ],
    'logs' => [
        'title' => 'Wsprry Pi Log',
        'navLabel' => 'Logs',
        'navIcon' => 'fa-file-lines',
        'legacyScript' => 'view_logs.php',
        'cardClass' => 'logs-card',
        'bodyClass' => 'bg-body-tertiary',
        'htmlTheme' => 'light',
        'css' => ['view_logs.css'],
        'js' => ['view_logs.js'],
        'partial' => __DIR__ . '/views/logs.php',
    ],
    'spots' => [
        'title' => 'Wsprry Pi Spots',
        'navLabel' => 'Spots',
        'navIcon' => 'fa-satellite-dish',
        'legacyScript' => 'view_spots.php',
        'cardClass' => 'spots-card',
        'bodyClass' => '',
        'htmlTheme' => 'light',
        'css' => ['view_spots.css'],
        'js' => ['view_spots.js'],
        'partial' => __DIR__ . '/views/spots.php',
    ],
    'maintenance' => [
        'title' => 'Wsprry Pi Maintenance',
        'navLabel' => 'Maintenance',
        'navIcon' => 'fa-screwdriver-wrench',
        'legacyScript' => 'maintenance.php',
        'cardClass' => 'template-card',
        'bodyClass' => '',
        'htmlTheme' => 'light',
        'css' => ['maintenance.css'],
        'js' => ['maintenance.js'],
        'partial' => __DIR__ . '/views/maintenance.php',
    ],
];

$logPaneViews = ['journal', 'internal', 'both'];
$requestedPage = $_GET['page'] ?? null;

if ($requestedPage === null && isset($_GET['view']) && is_string($_GET['view']) && in_array($_GET['view'], $logPaneViews, true)) {
    $requestedPage = 'logs';
}

if (!is_string($requestedPage) || !array_key_exists($requestedPage, $viewMetadata)) {
    $requestedPage = 'operation';
}

$activeView = $requestedPage;
$activeViewMetadata = $viewMetadata[$activeView];
$legacyCurrentPage = $activeViewMetadata['legacyScript'];

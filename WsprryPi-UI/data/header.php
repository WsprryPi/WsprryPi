<?php
require_once 'page_metadata.php';
require_once __DIR__ . '/ui_version.php';

$scriptName = $_SERVER['SCRIPT_NAME'] ?? '/';
$basePath = rtrim(str_replace('\\', '/', dirname($scriptName)), '/');
if ($basePath === '/' || $basePath === '.') {
    $basePath = '';
}

$pathConfig = [
    'basePath' => $basePath,
    'sharedApiPath' => $basePath . '/api/v1',
    'configPath' => $basePath . '/config',
    'versionPath' => $basePath . '/version',
    'uiIdentityPath' => $basePath . '/ui-version.php',
    'uiManifestPath' => $basePath . '/ui-manifest.php',
    'repairPath' => $basePath . '/config/repair',
    'supportBundlesPath' => $basePath . '/api/support-bundles',
    'supportIntakePath' => $basePath . '/api/support-intake',
    'networkSafetyPath' => $basePath . '/api/network-safety',
    'wtpPath' => $basePath . '/api/wtp',
    'rp1RoutePath' => $basePath . '/api/rp1-gpclk-route',
    'socketPath' => $basePath . '/socket',
    'logStreamPath' => $basePath . '/log_stream.php',
];
?>

<script>
    window.currentPage = <?= json_encode($legacyCurrentPage) ?>;
    window.WSPRRYPI_VIEW = <?= json_encode($activeView) ?>;
    window.WSPRRYPI_PATHS = <?= json_encode($pathConfig, JSON_UNESCAPED_SLASHES) ?>;
    window.WSPRRYPI_INSTALLED_UI_BUILD_ID = <?= json_encode(getWsprryPiInstalledUiBuildId()) ?>;
</script>
<script>
    (function () {
        try {
            var storedTheme = localStorage.getItem("theme");
            if (storedTheme === "light" || storedTheme === "dark") {
                document.documentElement.setAttribute("data-bs-theme", storedTheme);
            } else if (storedTheme !== null) {
                localStorage.removeItem("theme");
            }
        } catch (error) {
            // Keep the server-rendered theme if storage is unavailable.
        }
    })();
</script>

<meta charset="UTF-8" />

<title><?= htmlspecialchars($currentPageMetadata['title']) ?></title>

<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="<?= htmlspecialchars(wsprrypiAssetUrl('vendor/fonts/google/fonts.css')) ?>">
<link rel="apple-touch-icon" sizes="180x180" href="<?= htmlspecialchars(wsprrypiAssetUrl('apple-touch-icon.png')) ?>">
<link rel="icon" type="image/png" sizes="32x32" href="<?= htmlspecialchars(wsprrypiAssetUrl('favicon-32x32.png')) ?>">
<link rel="icon" type="image/png" sizes="16x16" href="<?= htmlspecialchars(wsprrypiAssetUrl('favicon-16x16.png')) ?>">
<link rel="manifest" href="<?= htmlspecialchars(wsprrypiAssetUrl('site.webmanifest')) ?>">
<link rel="icon" type="image/x-icon" href="<?= htmlspecialchars(wsprrypiAssetUrl('favicon.ico')) ?>">

<!-- Bootswatch Zephyr CSS -->
<link
    rel="stylesheet"
    href="<?= htmlspecialchars(wsprrypiAssetUrl('vendor/css/bootswatch-zephyr-5.3.8.min.css')) ?>"
>

<!-- Bootstrap Icons -->
<link
    rel="stylesheet"
    href="<?= htmlspecialchars(wsprrypiAssetUrl('vendor/fonts/bootstrap-icons/bootstrap-icons.css')) ?>">

<!-- FontAwesome Icons -->
<link
    rel="stylesheet"
    href="<?= htmlspecialchars(wsprrypiAssetUrl('vendor/fonts/fontawesome/all.min.css')) ?>">

<!-- Local Stylesheet -->
<link rel="stylesheet" href="<?= htmlspecialchars(wsprrypiAssetUrl('site.css')) ?>" />

<?php require_once __DIR__ . '/html_cache_headers.php'; ?>
<?php require_once 'app_state.php'; ?>
<?php require_once __DIR__ . '/ui_version.php'; ?>
<!DOCTYPE html>
<html lang="en"<?= $activeViewMetadata['htmlTheme'] !== null ? ' data-bs-theme="' . htmlspecialchars($activeViewMetadata['htmlTheme']) . '"' : '' ?>>

<head>
    <?php require_once 'header.php'; ?>

    <?php foreach ($activeViewMetadata['css'] as $stylesheet): ?>
        <link rel="stylesheet" href="<?= htmlspecialchars(wsprrypiAssetUrl($stylesheet)) ?>" />
    <?php endforeach; ?>
</head>

<?php
$bodyClass = $activeViewMetadata['bodyClass'];
$cardClass = $activeViewMetadata['cardClass'];
require_once 'page_shell_start.php';
require $activeViewMetadata['partial'];
require_once 'page_shell_end.php';
?>

    <?php require_once 'footer.php'; ?>

    <?php foreach ($activeViewMetadata['js'] as $script): ?>
        <script src="<?= htmlspecialchars(wsprrypiAssetUrl($script)) ?>"></script>
    <?php endforeach; ?>
</body>

</html>

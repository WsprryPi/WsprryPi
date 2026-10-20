<?php
if (!isset($bodyClass)) {
    $bodyClass = '';
}

if (!isset($cardClass)) {
    $cardClass = 'template-card';
}
?>
<body<?= $bodyClass !== '' ? ' class="' . htmlspecialchars($bodyClass) . '"' : '' ?>>
    <a class="skip-link" href="#main-content">Skip to main content</a>
    <?php require_once 'connection_alert.php'; ?>

    <!-- Fixed Navbar -->
    <?php require_once 'navbar.php'; ?>

    <!-- Main Content -->
    <main id="main-content" class="page-shell" tabindex="-1">
        <div class="container page-shell__inner">
            <p id="pico-firmware-safety" class="alert alert-warning mb-3" role="status" aria-live="polite" hidden></p>
            <div
                id="uiConsistencyDiagnostic"
                class="alert alert-warning ui-consistency-diagnostic d-none"
                role="status"
                aria-live="polite"
                aria-atomic="true">
                <div class="ui-consistency-diagnostic__title">
                    <i class="bi bi-exclamation-triangle-fill" aria-hidden="true"></i>
                    UI consistency could not be confirmed
                </div>
                <p class="ui-consistency-diagnostic__message mb-0"></p>
            </div>
            <div class="card shadow-sm page-card <?= htmlspecialchars($cardClass) ?>">

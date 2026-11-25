<?php
require_once __DIR__ . '/page_warning.php';
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
            <?php
            renderPageWarning('pico-firmware-safety');
            renderPageWarning('uiConsistencyDiagnostic', 'ui-consistency-diagnostic', 'UI consistency could not be confirmed', true);
            ?>
            <section id="controllerOnlyNotice" class="alert alert-info page-warning" aria-labelledby="hostRoleTitle">
                <div role="status" aria-atomic="true">
                    <h2 id="hostRoleTitle" class="h6">Connecting to Wsprry Pi</h2>
                    <p class="page-warning__message mb-0">Checking the host before enabling controls.</p>
                    <p id="controllerHostIdentity" class="small text-break mb-0 mt-1" hidden></p>
                </div>
                <button type="button" id="hostRoleRetry" class="btn btn-sm page-warning__retry mt-2" hidden>Retry connection</button>
            </section>
            <section id="configurationRecoveryNotice" class="alert alert-warning page-warning" role="status" aria-live="polite" hidden>
                <h2 class="h6">Transmitter startup is blocked</h2>
                <p class="page-warning__message mb-0">Transmission controls are unavailable until startup is verified. <a href="index.php?page=config">Review saved settings in Setup.</a></p>
            </section>
            <div class="card shadow-sm page-card <?= htmlspecialchars($cardClass) ?>">

    <!-- System action modal -->
    <?php require_once 'system_action_modal.php'; ?>

    <!-- Fixed Footer -->
    <footer class="fixed-bottom bg-primary text-white">
        <div class="container small footer-content">
            <div class="footer-line footer-version-line">
                <span class="footer-version-label">Build</span>
                <span id="versionText" class="footer-version-value">---</span>
                <a
                    id="versionUpdateLink"
                    class="footer-version-update-link d-none"
                    href="https://github.com/WsprryPi/WsprryPi/releases"
                    target="_blank"
                    rel="noopener"
                    title="An update is available"
                    aria-label="An update is available">
                    <i class="bi bi-exclamation-triangle-fill" aria-hidden="true"></i>
                </a>
            </div>
            <details class="footer-meta">
                <summary>About</summary>
                <div class="footer-meta__panel">
                    <div>Copyright © 2023 - 2026 Lee Bussy [AA0NT].</div>
                    <div>Licensed under the MIT License.</div>
                    <button type="button" id="updateCheckToggle" class="btn btn-link btn-sm footer-meta__action px-0">
                        Disable update checks
                    </button>
                </div>
            </details>
        </div>
    </footer>

    <!-- jQuery and Bootswatch -->
    <?php require_once 'site.js.includes.php'; ?>

    <!-- Main JavaScript -->
    <script src="<?= htmlspecialchars(wsprrypiAssetUrl('pico-firmware-safety.js')) ?>"></script>
    <script src="<?= htmlspecialchars(wsprrypiAssetUrl('site.js')) ?>"></script>

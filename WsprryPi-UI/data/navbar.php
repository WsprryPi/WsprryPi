<?php require_once 'page_metadata.php'; ?>
<?php $primaryNavViews = ['operation', 'config', 'logs', 'spots', 'maintenance']; ?>

<!-- Fixed Navbar -->
<nav id="mainNavbar" class="navbar navbar-expand-lg navbar-dark bg-primary fixed-top">
    <div class="container">
        <span class="navbar-brand navbar-brand-app">
            <span class="navbar-brand-copy">
                <span class="navbar-kicker">Wsprry Pi Console</span>
                <span class="navbar-title"><?= htmlspecialchars($currentPageMetadata['navLabel']) ?></span>
            </span>
            <span class="navbar-signal-status" aria-label="Controller link status">
                <i
                    id="connIcon"
                    data-bs-toggle="tooltip"
                    data-bs-original-title="Disconnected."
                    class="fa-solid fa-tower-broadcast"
                    aria-hidden="true"></i>
                <span
                    id="connStatusText"
                    class="navbar-signal-status__value"
                    role="status"
                    aria-live="polite"
                    aria-atomic="true">Disconnected</span>
            </span>
        </span>
        <button
            class="navbar-toggler ms-auto"
            type="button"
            data-bs-toggle="collapse"
            data-bs-target="#mainNav"
            aria-controls="mainNav"
            aria-expanded="false"
            aria-label="Toggle navigation">
            <span class="navbar-toggler-icon"></span>
        </button>

        <div class="collapse navbar-collapse align-items-center" id="mainNav">
            <ul class="navbar-nav navbar-nav-primary flex-wrap align-items-center">
                <?php foreach ($primaryNavViews as $viewKey): ?>
                    <?php
                    $navMetadata = $viewMetadata[$viewKey];
                    $isActive = $activeView === $viewKey;
                    $href = $viewKey === 'operation'
                        ? 'index.php'
                        : 'index.php?page=' . rawurlencode($viewKey);
                    ?>
                    <?php if ($viewKey === 'spots'): ?>
                        <li class="nav-item dropdown">
                            <button
                                class="nav-link nav-link-primary dropdown-toggle<?= $isActive ? ' active' : '' ?>"
                                id="spotsDropdown"
                                type="button"
                                data-bs-toggle="dropdown"
                                aria-expanded="false">
                                <i class="fa-solid <?= htmlspecialchars($navMetadata['navIcon']) ?>" aria-hidden="true"></i>
                                <span class="ms-2"><?= htmlspecialchars($navMetadata['navLabel']) ?></span>
                            </button>
                            <ul class="dropdown-menu dropdown-menu-start" aria-labelledby="spotsDropdown">
                                <li>
                                    <a
                                        class="dropdown-item<?= $isActive ? ' active' : '' ?>"
                                        href="<?= htmlspecialchars($href) ?>"
                                        <?= $isActive ? 'aria-current="page"' : '' ?>>
                                        WSPR spots
                                    </a>
                                </li>
                                <li>
                                    <a
                                        class="dropdown-item"
                                        href="https://swharden.com/qrss/plus/"
                                        target="_blank"
                                        rel="noopener noreferrer"
                                        aria-label="QRSS Plus (opens in a new tab)">
                                        QRSS Plus
                                        <i class="fa-solid fa-arrow-up-right-from-square ms-2" aria-hidden="true"></i>
                                    </a>
                                </li>
                            </ul>
                        </li>
                    <?php else: ?>
                        <li class="nav-item">
                            <a
                                class="nav-link nav-link-primary<?= $isActive ? ' active' : '' ?>"
                                href="<?= htmlspecialchars($href) ?>"
                                <?= $isActive ? 'aria-current="page"' : '' ?>>
                                <i class="fa-solid <?= htmlspecialchars($navMetadata['navIcon']) ?>"></i>
                                <span class="ms-2"><?= htmlspecialchars($navMetadata['navLabel']) ?></span>
                            </a>
                        </li>
                    <?php endif; ?>
                <?php endforeach; ?>
            </ul>

            <div class="navbar-utility-group ms-lg-auto">
                <ul class="navbar-nav navbar-nav-secondary">
                    <li class="nav-item dropdown align-items-center">
                        <a
                            class="nav-link dropdown-toggle"
                            href="#"
                            id="wsprlinksDropdown"
                            role="button"
                            data-bs-toggle="dropdown"
                            aria-expanded="false">
                            <i class="fa-solid fa-link me-2"></i>
                            Wsprry Pi Links
                        </a>
                        <ul class="dropdown-menu dropdown-menu-start" aria-labelledby="wsprlinksDropdown">
                            <li>
                                <a
                                    class="dropdown-item"
                                    href="https://wsprry-pi.readthedocs.io/en/stable/"
                                    target="_blank"
                                    rel="noopener">
                                    Documentation
                                </a>
                            </li>
                            <li>
                                <a
                                    class="dropdown-item"
                                    href="https://github.com/WsprryPi/"
                                    target="_blank"
                                    rel="noopener">
                                    GitHub
                                </a>
                            </li>
                            <li>
                                <a
                                    class="dropdown-item"
                                    href="https://tapr.org/"
                                    target="_blank"
                                    rel="noopener">
                                    TAPR
                                </a>
                            </li>
                            <li>
                                <a
                                    class="dropdown-item"
                                    href="https://www.wsprnet.org/olddb?mode=html&band=all&limit=50&findreporter=&sort=date&findcall="
                                    target="_blank"
                                    rel="noopener">
                                    WSPRNet Database
                                </a>
                            </li>
                        </ul>
                    </li>
                </ul>

                <div class="nav-item d-flex align-items-center">
                    <label for="themeToggle"
                        class="form-check form-switch d-inline-flex align-items-center mb-0 text-white">
                        <span
                            class="form-check-label mb-0 toggle-text"
                            id="themeToggleLabel"
                            data-bs-toggle="tooltip"
                            title="Change Theme">Dark</span>
                        <input
                            class="form-check-input"
                            type="checkbox"
                            id="themeToggle"
                            data-bs-toggle="tooltip"
                            title="Change Theme">
                    </label>
                </div>
            </div>
        </div>

    </div>
</nav>

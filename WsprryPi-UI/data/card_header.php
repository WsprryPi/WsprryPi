<div class="card-header d-flex flex-wrap justify-content-between align-items-center">
    <!-- Card Title -->
    <h1 id="<?= htmlspecialchars($cardTitleId) ?>" class="card-title mb-0"><?= htmlspecialchars($cardTitleText) ?></h1>

    <!-- Reboot, Shutdown and Clocks -->
    <?php require_once 'clock_and_reboot.php'; ?>
</div>

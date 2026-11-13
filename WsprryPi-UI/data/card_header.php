<div class="card-header d-flex flex-wrap justify-content-between align-items-center">
    <!-- Card Title -->
    <h1 id="<?= htmlspecialchars($cardTitleId) ?>" class="card-title mb-0"><?= htmlspecialchars($cardTitleText) ?></h1>

    <?php if (!empty($cardFleetSelector)): ?>
    <div class="operation-device-selector" id="operationDeviceField" hidden>
        <label for="operationDevice" class="form-label mb-0">Device</label>
        <select id="operationDevice" class="form-select form-select-sm" aria-describedby="operationDeviceHint"><option value="">This Pi (controller)</option></select>
        <span id="operationDeviceHint" class="visually-hidden">Select one output to inspect its status and controls.</span>
    </div>
    <?php endif; ?>
    <!-- Reboot, Shutdown and Clocks -->
    <?php require_once 'clock_and_reboot.php'; ?>
</div>

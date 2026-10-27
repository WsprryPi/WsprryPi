<?php
/** Render a persistent warning above the page card; callers own its lifecycle. */
function renderPageWarning(string $id, string $class = '', string $title = '', bool $atomic = false): void
{
    $escape = static fn(string $value): string => htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
    ?>
    <div id="<?= $escape($id) ?>"
        class="alert alert-warning page-warning <?= $escape($class) ?>"
        role="status" aria-live="polite"<?= $atomic ? ' aria-atomic="true"' : '' ?> hidden>
        <?php if ($title !== ''): ?>
            <div class="page-warning__title">
                <i class="bi bi-exclamation-triangle-fill" aria-hidden="true"></i>
                <?= $escape($title) ?>
            </div>
        <?php endif; ?>
        <p class="page-warning__message mb-0"></p>
    </div>
    <?php
}

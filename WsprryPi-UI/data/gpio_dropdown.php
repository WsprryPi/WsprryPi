<?php

/**
 * gpio_dropdown.php
 *
 * Single source of truth for curated safe GPIO selections.
 *
 * Supported render modes:
 * - dropdown: Bootstrap dropdown menu for button-based pickers
 * - select:   Native <select> for compact form/table usage
 */

if (!function_exists('wsprrypi_safe_gpio_pins')) {
    function wsprrypi_safe_gpio_pins(): array
    {
        return [
            4 => 'Pin 7',
            5 => 'Pin 29',
            6 => 'Pin 31',
            7 => 'Pin 26',
            8 => 'Pin 24',
            9 => 'Pin 21',
            10 => 'Pin 19',
            11 => 'Pin 23',
            12 => 'Pin 32',
            13 => 'Pin 33',
            14 => 'Pin 8',
            15 => 'Pin 10',
            16 => 'Pin 36',
            17 => 'Pin 11',
            18 => 'Pin 12 - TAPR LED',
            19 => 'Pin 35 - TAPR Shutdown',
            20 => 'Pin 38',
            21 => 'Pin 40',
            22 => 'Pin 15',
            23 => 'Pin 16',
            24 => 'Pin 18',
            25 => 'Pin 22',
            26 => 'Pin 37',
            27 => 'Pin 13',
        ];
    }
}

if (!function_exists('wsprrypi_gpio_code')) {
    function wsprrypi_gpio_code(int $gpio): string
    {
        return 'GPIO' . $gpio;
    }
}

$gpioRenderMode = isset($gpioRenderMode) ? (string)$gpioRenderMode : 'dropdown';
$gpioPins = wsprrypi_safe_gpio_pins();

if ($gpioRenderMode === 'select') {
    $selectId = isset($selectId) ? (string)$selectId : 'gpioSelect';
    $selectName = isset($selectName) ? (string)$selectName : $selectId;
    $selectClass = isset($selectClass) ? (string)$selectClass : 'form-select';
    $selectDataBand = isset($selectDataBand) ? (string)$selectDataBand : '';
    $selectAttributes = isset($selectAttributes) ? trim((string)$selectAttributes) : '';
    $defaultGpio = isset($defaultGpio) ? (string)$defaultGpio : '';
    $selectPlaceholder = isset($selectPlaceholder) ? (string)$selectPlaceholder : 'Select GPIO';
    ?>
    <select
        id="<?= htmlspecialchars($selectId) ?>"
        name="<?= htmlspecialchars($selectName) ?>"
        class="<?= htmlspecialchars($selectClass) ?>"
        <?php if ($selectDataBand !== ''): ?>data-band="<?= htmlspecialchars($selectDataBand) ?>"<?php endif; ?>
        <?= $selectAttributes !== '' ? $selectAttributes : '' ?>>
        <option value=""<?= $defaultGpio === '' ? ' selected' : '' ?> disabled><?= htmlspecialchars($selectPlaceholder) ?></option>
        <?php foreach ($gpioPins as $gpio => $pinText): ?>
            <option value="<?= htmlspecialchars((string)$gpio) ?>"<?= ($defaultGpio !== '' && (int)$defaultGpio === $gpio) ? ' selected' : '' ?>>
                <?= htmlspecialchars(wsprrypi_gpio_code($gpio) . ' (' . $pinText . ')') ?>
            </option>
        <?php endforeach; ?>
    </select>
    <?php
    return;
}

$dropdownId = isset($dropdownId) ? (string)$dropdownId : 'gpioDropdownButton';
$defaultGpio = isset($defaultGpio) ? (string)$defaultGpio : '';
$includeBlankGpio = isset($includeBlankGpio) ? (bool)$includeBlankGpio : false;
$blankGpioLabel = isset($blankGpioLabel) ? (string)$blankGpioLabel : 'Disabled';
?>

<ul class="dropdown-menu bg-body text-body" aria-labelledby="<?= htmlspecialchars($dropdownId) ?>">
    <?php if ($includeBlankGpio): ?>
        <li>
            <button
                type="button"
                class="dropdown-item<?= ($defaultGpio === '' ? ' active' : '') ?>"
                data-val="">
                <?= htmlspecialchars($blankGpioLabel) ?>
            </button>
        </li>
    <?php endif; ?>
    <?php foreach ($gpioPins as $gpio => $pinText): ?>
        <?php $gpioCode = wsprrypi_gpio_code($gpio); ?>
        <li>
            <button
                type="button"
                class="dropdown-item<?= ($defaultGpio === $gpioCode ? ' active' : '') ?>"
                data-val="<?= htmlspecialchars($gpioCode) ?>">
                <?= htmlspecialchars($gpioCode . ' (' . $pinText . ')') ?>
            </button>
        </li>
    <?php endforeach; ?>
</ul>

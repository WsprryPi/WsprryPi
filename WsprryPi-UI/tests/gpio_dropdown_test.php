<?php

declare(strict_types=1);

function require_true(bool $condition, string $message): void
{
    if (!$condition) {
        fwrite(STDERR, "FAIL: {$message}\n");
        exit(1);
    }
}

function render_gpio_dropdown(array $variables): string
{
    extract($variables, EXTR_SKIP);
    ob_start();
    include __DIR__ . '/../data/gpio_dropdown.php';
    return (string)ob_get_clean();
}

ob_start();
include __DIR__ . '/../data/gpio_dropdown.php';
ob_end_clean();

$pins = wsprrypi_safe_gpio_pins();
require_true($pins[4] === 'Pin 7', 'GPIO 4 must map to Pin 7');
require_true($pins[26] === 'Pin 37', 'GPIO 26 must map to Pin 37');
require_true($pins[27] === 'Pin 13', 'GPIO 27 must map to Pin 13');
require_true(array_keys($pins) === range(4, 27), 'safe GPIO pins must remain in ascending BCM order');

foreach ([0, 1, 2, 3] as $excludedGpio) {
    require_true(!array_key_exists($excludedGpio, $pins), "GPIO {$excludedGpio} must remain excluded");
}

$selectOutput = render_gpio_dropdown([
    'gpioRenderMode' => 'select',
    'selectId' => 'test-gpio-select',
]);
require_true(
    str_contains($selectOutput, 'value="4"') && str_contains($selectOutput, 'GPIO4 (Pin 7)'),
    'select mode must expose GPIO4 with Pin 7'
);
require_true(
    strpos($selectOutput, 'value="26"') < strpos($selectOutput, 'value="27"'),
    'select mode must retain GPIO 26 before GPIO 27'
);
require_true(
    str_contains($selectOutput, 'value="26"') && str_contains($selectOutput, 'GPIO26 (Pin 37)'),
    'select mode must expose GPIO 26 with Pin 37'
);
require_true(
    str_contains($selectOutput, 'value="27"') && str_contains($selectOutput, 'GPIO27 (Pin 13)'),
    'select mode must expose GPIO 27 with Pin 13'
);

$dropdownOutput = render_gpio_dropdown([
    'gpioRenderMode' => 'dropdown',
    'dropdownId' => 'test-gpio-dropdown',
]);
require_true(
    str_contains($dropdownOutput, 'data-val="GPIO4"') && str_contains($dropdownOutput, 'GPIO4 (Pin 7)'),
    'dropdown mode must expose GPIO4 with Pin 7'
);
require_true(
    str_contains($dropdownOutput, 'data-val="GPIO26"') && str_contains($dropdownOutput, 'GPIO26 (Pin 37)'),
    'dropdown mode must expose GPIO26 with Pin 37'
);
require_true(
    str_contains($dropdownOutput, 'data-val="GPIO27"') && str_contains($dropdownOutput, 'GPIO27 (Pin 13)'),
    'dropdown mode must expose GPIO27 with Pin 13'
);

echo "gpio_dropdown_test passed\n";

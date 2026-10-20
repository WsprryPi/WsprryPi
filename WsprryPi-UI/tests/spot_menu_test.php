<?php

declare(strict_types=1);

function require_true(bool $condition, string $message): void
{
    if (!$condition) {
        fwrite(STDERR, "FAIL: {$message}\n");
        exit(1);
    }
}

$_GET = ['page' => 'spots'];
ob_start();
include __DIR__ . '/../data/navbar.php';
$spotsNavbar = (string) ob_get_clean();

require_true(
    str_contains($spotsNavbar, 'id="spotsDropdown"')
        && str_contains($spotsNavbar, '<button')
        && str_contains($spotsNavbar, 'data-bs-toggle="dropdown"')
        && str_contains($spotsNavbar, 'aria-expanded="false"'),
    'Spots must render as a native button controlling an explicit dropdown'
);
require_true(
    str_contains($spotsNavbar, '<ul class="dropdown-menu dropdown-menu-start" aria-labelledby="spotsDropdown">'),
    'the Spot choices must be labelled by their visible trigger'
);
require_true(
    str_contains($spotsNavbar, 'href="index.php?page=spots"')
        && str_contains($spotsNavbar, 'WSPR spots'),
    'WSPR spots must preserve the existing destination'
);
require_true(
    str_contains($spotsNavbar, 'href="https://swharden.com/qrss/plus/"')
        && str_contains($spotsNavbar, 'QRSS Plus'),
    'QRSS Plus must use the requested exact destination'
);
require_true(
    str_contains($spotsNavbar, 'target="_blank"')
        && str_contains($spotsNavbar, 'rel="noopener noreferrer"')
        && str_contains($spotsNavbar, 'aria-label="QRSS Plus (opens in a new tab)"'),
    'QRSS Plus must open in a protected new browsing context'
);
$spotMenuMatched = preg_match(
    '/<ul class="dropdown-menu dropdown-menu-start" aria-labelledby="spotsDropdown">(.*?)<\/ul>/s',
    $spotsNavbar,
    $spotMenu
) === 1;
require_true(
    $spotMenuMatched
        && substr_count($spotMenu[1], 'class="dropdown-item') === 2
        && strpos($spotMenu[1], 'WSPR spots') < strpos($spotMenu[1], 'QRSS Plus'),
    'the Spot submenu must list WSPR spots before QRSS Plus'
);
require_true(
    str_contains($spotsNavbar, 'class="nav-link nav-link-primary dropdown-toggle active"')
        && str_contains($spotsNavbar, 'class="dropdown-item active"')
        && str_contains($spotsNavbar, 'aria-current="page"'),
    'the Spot trigger and WSPR choice must expose the active page'
);

echo "spot_menu_test passed\n";

<?php
// SPDX-License-Identifier: MIT
// Read-only render fixture: uses the real partial and existing theme assets.
chdir(__DIR__ . '/../data');
?><!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="vendor/css/bootswatch-zephyr-5.3.8.min.css"><link rel="stylesheet" href="vendor/fonts/google/fonts.css"><link rel="stylesheet" href="site.css"></head><body><main class="container py-4"><h1>Fleet USB controls</h1><p>Synthetic settings for visual validation.</p><?php include 'views/wtp-controls.php'; ?></main></body></html>

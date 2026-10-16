"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const siteCss = fs.readFileSync(path.join(root, "data/site.css"), "utf8");
const logsCss = fs.readFileSync(path.join(root, "data/view_logs.css"), "utf8");
const logsJs = fs.readFileSync(path.join(root, "data/view_logs.js"), "utf8");
const indexCss = fs.readFileSync(path.join(root, "data/index.css"), "utf8");
const configView = fs.readFileSync(path.join(root, "data/views/config.php"), "utf8");
const pageShell = fs.readFileSync(path.join(root, "data/page_shell_start.php"), "utf8");

assert.match(
    siteCss,
    /@media \(min-width: 992px\) and \(max-width: 1399\.98px\)[\s\S]*?#mainNav[\s\S]*?flex: 1 1 100%/,
    "compact desktop navigation must use an intentional second row"
);
assert.match(
    siteCss,
    /@media \(max-width: 767\.98px\)[\s\S]*?grid-template-areas:[\s\S]*?"copy toggle"[\s\S]*?"status status"[\s\S]*?\.navbar-title[\s\S]*?white-space: normal/,
    "mobile masthead must retain readable title and controller status rows"
);
assert.match(
    siteCss,
    /@media \(max-width: 575\.98px\)[\s\S]*?footer\.fixed-bottom\s*{\s*position: static;/,
    "phone footer must participate in document flow"
);
assert.doesNotMatch(
    siteCss.match(/@media \(max-width: 575\.98px\)[\s\S]*?@media \(prefers-reduced-motion/)?.[0] || "",
    /text-overflow:\s*ellipsis/,
    "phone footer must not ellipsize the build identifier"
);
assert.match(
    pageShell,
    /<a class="skip-link" href="#main-content">Skip to main content<\/a>/,
    "the shared shell must provide a keyboard bypass for repeated navigation"
);
assert.match(
    pageShell,
    /<main id="main-content" class="page-shell" tabindex="-1">/,
    "the keyboard bypass target must accept programmatic focus"
);
assert.match(
    pageShell,
    /id="uiConsistencyDiagnostic"[\s\S]*?role="status"[\s\S]*?aria-live="polite"[\s\S]*?UI consistency could not be confirmed/,
    "the shared shell must provide a persistent, assistive-technology-readable UI consistency diagnostic"
);
assert.match(
    siteCss,
    /\.ui-consistency-diagnostic\s*{[\s\S]*?overflow-wrap:\s*anywhere;/,
    "long UI identities must wrap without overflowing the diagnostic"
);
assert.match(
    logsJs,
    /isConnecting[\s\S]*?"Connecting…"[\s\S]*?btn\.disabled = isConnecting;[\s\S]*?aria-disabled/,
    "log reconnect must reject concurrent restart actions while connecting"
);
assert.match(
    logsJs,
    /const retry = document\.getElementById\("logsRetryButton"\);[\s\S]*?retry\.disabled = isConnecting;[\s\S]*?aria-disabled/,
    "the log empty-state retry must not permit a second connection while reconnecting"
);
assert.match(
    logsJs,
    /button\.dataset\.idleLabel = action\.label;/,
    "log retry actions must retain their context-specific idle label after reconnecting"
);
assert.match(
    logsJs,
    /scrollRegion\.setAttribute\("aria-busy", state === "reconnecting" \? "true" : "false"\)/,
    "log loading state must be exposed to assistive technology"
);
assert.match(
    logsCss,
    /\.logs-line\s*{[\s\S]*?display:\s*grid;/,
    "log entries must expose structured columns"
);
assert.match(
    logsCss,
    /@media \(max-width: 767\.98px\)[\s\S]*?\.logs-ts\s*{[\s\S]*?grid-column:\s*1 \/ -1/,
    "mobile timestamps must remain on their own complete row"
);
assert.match(
    logsJs,
    /function formatUnitFixed16\(unit\)[\s\S]*?return s;\s*}/,
    "log service names must remain complete"
);
assert.doesNotMatch(
    logsJs.match(/function formatUnitFixed16\(unit\)[\s\S]*?\n\s*}/)?.[0] || "",
    /slice\(|padEnd\(/,
    "log service names must not be truncated or padded into fragments"
);
assert.match(
    indexCss,
    /\.transmitter-backend-fields\s*{[\s\S]*?display:\s*grid;[\s\S]*?border-top:/,
    "each transmitter backend must retain a full-width grouped settings region"
);
assert.doesNotMatch(
    indexCss,
    /\.transmitter-backend-fields[^{}]*\{[^}]*display:\s*contents;/,
    "transmitter backend wrappers must not flatten into the RF Output panel grid"
);
assert.match(
    configView,
    /id="gpio-backend-panel"[\s\S]*?id="gpio-output-heading"[\s\S]*?col-12 col-lg-8 config-stacked-field transmitter-pin-field/,
    "GPIO route controls must lead a backend-specific row with useful desktop width"
);
assert.match(
    configView,
    /id="si5351-backend-panel"[\s\S]*?id="si5351-output-heading"[\s\S]*?id="si5351-calibration-heading"/,
    "Si5351 device controls and calibration must expose distinct visual groups"
);

console.log("responsive_shell_logs_test passed");

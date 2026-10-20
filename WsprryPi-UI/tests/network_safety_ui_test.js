"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const view = fs.readFileSync(path.join(root, "data/views/maintenance.php"), "utf8");
const script = fs.readFileSync(path.join(root, "data/maintenance.js"), "utf8");
const styles = fs.readFileSync(path.join(root, "data/maintenance.css"), "utf8");
const header = fs.readFileSync(path.join(root, "data/header.php"), "utf8");
const site = fs.readFileSync(path.join(root, "data/site.js"), "utf8");

assert.match(view, /id="networkSafetyPanel"/, "Maintenance must expose network safety");
assert.match(view, /Requested[\s\S]*Configured[\s\S]*Active/, "requested, configured, and active states must be distinct");
assert.match(view, /DISABLE LOCAL-LAN SAFETY/, "disable confirmation must show the exact phrase");
assert.match(view, /id="networkSafetyDisableConfirmation"[\s\S]*d-none/, "disable confirmation must be progressive and inline");
assert.ok(!view.includes("networkSafetyModal"), "network safety must not use a modal");
assert.match(view, /Host, Origin, CORS, malformed-request, and forwarded-header protections remain active/, "override boundaries must remain visible");
assert.match(header, /'networkSafetyPath'/, "network safety path must be centrally configured");
assert.match(site, /const NETWORK_SAFETY_ENDPOINT/, "network safety endpoint must be centralized");
assert.match(site, /NETWORK_SAFETY_PATH,\n\s+NETWORK_SAFETY_PATH/, "browser network safety must not fall back to a direct backend port");
assert.match(script, /const NETWORK_SAFETY_DISABLE_PHRASE = "DISABLE LOCAL-LAN SAFETY"/, "typed phrase must be exact");
assert.match(script, /networkSafetyDisablePhrase\.value !== NETWORK_SAFETY_DISABLE_PHRASE/, "disable apply must be phrase gated");
assert.match(script, /networkSafetyEnforced\.disabled = networkSafetyInFlight/, "mode choices must lock during apply");
assert.match(script, /networkSafetyDisablePhrase\.disabled = networkSafetyInFlight/, "confirmation phrase must lock during apply");
assert.match(script, /cancelNetworkSafetyButton\.disabled = networkSafetyInFlight/, "cancel must lock during apply");
assert.match(view, /id="networkSafetyPhraseStatus"[\s\S]*role="status"[\s\S]*aria-live="polite"/, "phrase readiness must be announced");
assert.match(script, /Confirmation phrase matches\. Ready to apply\./, "exact phrase must announce readiness");
assert.match(script, /body: JSON\.stringify\(\{ mode: requestedMode \}\)/, "apply must send only the requested mode");
assert.match(script, /fetch\(NETWORK_SAFETY_ENDPOINT\.proxyUrl/, "browser operation must use the Apache path");
assert.ok(!script.includes("fetchWithEndpointFallback(NETWORK_SAFETY_ENDPOINT"), "browser operation must not use direct backend fallback");
assert.match(script, /Your selection is preserved/, "failed apply must preserve the operator draft");
assert.match(script, /networkSafetyDisablePhrase\.value = ""/, "phrase must be clearable after success or explicit cancellation");
assert.match(script, /NETWORK SAFETY OFF/, "active insecure state must use the exact conspicuous warning");
assert.match(styles, /\.maintenance-network-safety__states/, "state comparison must have responsive layout");
assert.match(styles, /@media \(max-width: 767\.98px\)[\s\S]*\.maintenance-network-safety__states/, "state comparison must stack on mobile");

console.log("network_safety_ui_test passed");

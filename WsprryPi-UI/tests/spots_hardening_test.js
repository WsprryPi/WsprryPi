"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const source = fs.readFileSync(path.resolve(__dirname, "../data/view_spots.js"), "utf8");
const styles = fs.readFileSync(path.resolve(__dirname, "../data/view_spots.css"), "utf8");

assert.match(
    source,
    /function setSpotsBusy\(isBusy\)[\s\S]*?setAttribute\("aria-busy", isBusy \? "true" : "false"\)/,
    "spots loading state must be exposed to assistive technology"
);
assert.match(
    source,
    /if \(requestId === _requestSequence\) \{[\s\S]*?_activeRequest = null;[\s\S]*?setSpotsBusy\(false\);[\s\S]*?scheduleNext\(\);/,
    "a superseded request must not settle or reschedule the active request"
);
assert.match(
    source,
    /const requestId = \+\+_requestSequence;[\s\S]*?clearRefreshTimer\(\);[\s\S]*?clearActiveRequest\(\);[\s\S]*?const selectedSource/,
    "every new lookup must cancel its prior timer and request before taking an early-return path"
);
assert.match(
    source,
    /window\.addEventListener\("offline", \(\) => \{[\s\S]*?_requestSequence\+\+;[\s\S]*?clearRefreshTimer\(\);[\s\S]*?clearActiveRequest\(\);[\s\S]*?renderError/,
    "offline transition must invalidate active work and prevent a timed retry"
);
assert.match(
    styles,
    /\.spots-card \.table-responsive\s*\{[\s\S]*?overflow-x:\s*auto;/,
    "the Spots table must remain horizontally navigable on narrow screens"
);
assert.match(
    styles,
    /\.spots-card \.table\s*\{[\s\S]*?min-width:\s*68rem;/,
    "the twelve-column Spots table must retain readable columns instead of wrapping by character"
);

console.log("spots_hardening_test passed");

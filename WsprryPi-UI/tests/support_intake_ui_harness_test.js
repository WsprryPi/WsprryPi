"use strict";

const assert = require("node:assert/strict");
const {
    preferredHarnessError,
    removeProfileDirectory,
} = require("./support_intake_ui_integration_test.js");

async function main() {
    const primary = new Error("browser assertion failed");
    const cleanup = new Error("profile cleanup failed");
    assert.equal(preferredHarnessError(primary, cleanup), primary,
        "cleanup failures must not mask the browser assertion");
    assert.equal(preferredHarnessError(undefined, cleanup), cleanup,
        "cleanup failures must remain visible when the browser test passed");

    let attempts = 0;
    await removeProfileDirectory("/tmp/test-profile", {
        attempts: 3,
        wait: async () => {},
        remove: () => {
            attempts++;
            if (attempts < 3) throw new Error("directory still settling");
        },
    });
    assert.equal(attempts, 3, "profile cleanup must retry transient failures");

    const persistent = new Error("persistent cleanup failure");
    await assert.rejects(removeProfileDirectory("/tmp/test-profile", {
        attempts: 2,
        wait: async () => {},
        remove: () => { throw persistent; },
    }), (error) => error === persistent,
    "profile cleanup must report a persistent failure after its retry bound");

    console.log("support_intake_ui_harness_test passed");
}

main().catch((error) => {
    console.error(error.stack || error.message);
    process.exitCode = 1;
});

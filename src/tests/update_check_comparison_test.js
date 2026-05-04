const assert = require("assert");
const fs = require("fs");
const vm = require("vm");

const siteSource = fs.readFileSync("/home/pi/WsprryPi/WsprryPi-UI/data/site.js", "utf8");

function jqueryStub() {
    const chain = {};
    for (const method of [
        "addClass",
        "attr",
        "off",
        "on",
        "one",
        "prop",
        "removeClass",
        "text",
        "toggleClass",
    ]) {
        chain[method] = () => chain;
    }
    chain.is = () => false;
    chain.length = 0;
    return chain;
}

const context = {
    URL,
    console,
    setInterval: () => 0,
    setTimeout: () => 0,
    clearInterval: () => {},
    clearTimeout: () => {},
    window: {
        WSPRRYPI_PATHS: {},
        location: {
            href: "http://localhost/",
            origin: "http://localhost",
            protocol: "http:",
            hostname: "localhost",
        },
        addEventListener: () => {},
        localStorage: {
            getItem: () => null,
            setItem: () => {},
            removeItem: () => {},
        },
        open: () => {},
    },
    document: {
        addEventListener: () => {},
        querySelector: () => null,
        querySelectorAll: () => [],
        getElementById: () => null,
        createElement: () => ({
            appendChild: () => {},
            classList: {
                add: () => {},
                remove: () => {},
                toggle: () => {},
            },
            dataset: {},
            setAttribute: () => {},
        }),
        createTextNode: () => ({}),
        documentElement: {
            classList: {
                add: () => {},
                remove: () => {},
                toggle: () => {},
            },
            style: {
                setProperty: () => {},
            },
        },
    },
    bootstrap: {
        Modal: {
            getOrCreateInstance: () => ({
                hide: () => {},
                show: () => {},
            }),
        },
    },
    $: jqueryStub,
};
context.window.document = context.document;

vm.createContext(context);
vm.runInContext(siteSource, context);

const currentSha = "a8079bce00106957556c549ee827d2213483fea7";
const targetSha = "10a9bafef7e486c389aa2026c46d1d9cd2b87b2b";

let comparison = context.updateCheckCommitComparisonResult(currentSha, targetSha, "ahead");
assert.strictEqual(
    comparison.updateAvailable,
    true,
    "target branch containing current SHA at a newer head must report update available"
);
assert.strictEqual(comparison.versionComparisonStatus, "update_available");

comparison = context.updateCheckCommitComparisonResult(currentSha, currentSha, "identical");
assert.strictEqual(
    comparison.updateAvailable,
    false,
    "matching current and target SHAs must report no update"
);
assert.strictEqual(comparison.versionComparisonStatus, "equal");

comparison = context.updateCheckCommitComparisonResult(currentSha, targetSha, "behind");
assert.strictEqual(
    comparison.updateAvailable,
    false,
    "local commit ahead of target branch must not report update available"
);
assert.strictEqual(comparison.versionComparisonStatus, "local_ahead");

vm.runInContext(`
    __semanticUpdateAvailable = false;
    __semanticStatus = "equal";
    __semanticRemoteVersion = "3.0.0";
    __selectedBranch = "gpio_for_amp";
    buildSemanticVersionUpdateResult = async () => ({
        useCommitFallback: false,
        checkedAt: 1,
        currentSha: "${currentSha}",
        currentBranch: __selectedBranch,
        targetBranch: "release",
        targetHeadSha: "",
        updateAvailable: __semanticUpdateAvailable,
        releaseUrl: UPDATE_CHECK_RELEASES_URL,
        releaseTitle: __semanticUpdateAvailable ? "WsprryPi 3.0.1" : "",
        fallbackUsed: false,
        selectionReason: "semantic prerelease version compared against same-channel GitHub prerelease",
        versionComparisonUsed: "semver",
        versionComparisonStatus: __semanticStatus,
        localVersionParsed: "3.0.0",
        remoteVersionSelected: __semanticRemoteVersion
    });
    selectGithubUpdateBranch = async () => ({
        branch: __selectedBranch,
        headSha: __targetSha,
        fallbackUsed: false,
        selectionReason: __selectedBranch === "main"
            ? "local main targets upstream main"
            : "local branch targets same-name upstream branch"
    });
    fetchGithubJson = async () => ({ status: __compareStatus });
    findReleaseForHead = async () => null;
`, context);

async function runBuildPriorityCase({
    branch = "gpio_for_amp",
    targetHeadSha,
    compareStatus,
    semanticUpdateAvailable = false,
    semanticStatus = "equal",
    semanticRemoteVersion = "3.0.0",
}) {
    context.__selectedBranch = branch;
    context.__targetSha = targetHeadSha;
    context.__compareStatus = compareStatus;
    context.__semanticUpdateAvailable = semanticUpdateAvailable;
    context.__semanticStatus = semanticStatus;
    context.__semanticRemoteVersion = semanticRemoteVersion;
    return context.buildWsprryPiUpdateResult({
        currentSha,
        currentBranch: branch,
        branchState: "branch",
        buildDirtyKnown: false,
        buildDirty: false,
    });
}

(async () => {
    assert.strictEqual(context.branchAllowsCommitUpdate("main"), false);
    assert.strictEqual(context.branchAllowsCommitUpdate("gpio_for_amp"), true);

    let result = await runBuildPriorityCase({
        branch: "main",
        targetHeadSha: targetSha,
        compareStatus: "ahead",
        semanticUpdateAvailable: false,
        semanticStatus: "equal",
        semanticRemoteVersion: "3.0.0",
    });
    assert.strictEqual(result.versionComparisonUsed, "commit");
    assert.strictEqual(result.versionComparisonStatus, "main_commit_diff_without_release");
    assert.strictEqual(result.updateAvailable, false);
    assert.strictEqual(result.targetBranch, "main");
    assert.strictEqual(result.targetHeadSha, targetSha);

    result = await runBuildPriorityCase({
        branch: "main",
        targetHeadSha: targetSha,
        compareStatus: "ahead",
        semanticUpdateAvailable: true,
        semanticStatus: "update_available",
        semanticRemoteVersion: "3.0.1",
    });
    assert.strictEqual(result.versionComparisonUsed, "commit");
    assert.strictEqual(result.versionComparisonStatus, "update_available");
    assert.strictEqual(result.updateAvailable, true);
    assert.strictEqual(result.targetBranch, "main");
    assert.strictEqual(result.targetHeadSha, targetSha);
    assert.strictEqual(result.remoteVersionSelected, "3.0.1");

    result = await runBuildPriorityCase({
        branch: "gpio_for_amp",
        targetHeadSha: targetSha,
        compareStatus: "ahead",
    });
    assert.strictEqual(result.versionComparisonUsed, "commit");
    assert.strictEqual(result.versionComparisonStatus, "update_available");
    assert.strictEqual(result.updateAvailable, true);
    assert.strictEqual(result.targetHeadSha, targetSha);

    result = await runBuildPriorityCase({
        branch: "gpio_for_amp",
        targetHeadSha: currentSha,
        compareStatus: "identical",
    });
    assert.strictEqual(result.versionComparisonUsed, "commit");
    assert.strictEqual(result.versionComparisonStatus, "equal");
    assert.strictEqual(result.updateAvailable, false);

    result = await runBuildPriorityCase({
        branch: "gpio_for_amp",
        targetHeadSha: targetSha,
        compareStatus: "behind",
    });
    assert.strictEqual(result.versionComparisonUsed, "commit");
    assert.strictEqual(result.versionComparisonStatus, "local_ahead");
    assert.strictEqual(result.updateAvailable, false);

    const originalGetElementById = context.document.getElementById;
    let promptAttempts = 0;
    let promptReturnValue = false;
    context.window.WSPRRYPI_UI_BUILD_ID = "loaded-build";
    context.window.WSPRRYPI_UI_VERSION = "3.0.0";
    context.document.getElementById = (id) => {
        if (id === "confirmModal") {
            return {
                classList: {
                    contains: () => false,
                },
            };
        }
        return originalGetElementById(id);
    };
    context.showConfirmationDialog = (options) => {
        promptAttempts += 1;
        assert.strictEqual(options.title, "UI refresh required");
        assert.strictEqual(options.confirmLabel, "Refresh");
        promptReturnValue = promptAttempts > 1;
        return promptReturnValue;
    };

    context.maybePromptForUiRefresh({
        ui_build_id: "server-build",
        ui_version: "3.0.1",
    });
    assert.strictEqual(promptAttempts, 1);

    context.maybePromptForUiRefresh({
        ui_build_id: "server-build",
        ui_version: "3.0.1",
    });
    assert.strictEqual(
        promptAttempts,
        2,
        "polling must retry the UI refresh prompt when the first modal show attempt does not become active"
    );

    context.document.getElementById = originalGetElementById;

    console.log("update_check_comparison_test passed");
})().catch((error) => {
    console.error(error);
    process.exit(1);
});

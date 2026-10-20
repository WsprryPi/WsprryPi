const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const sitePath = path.resolve(__dirname, "..", "..", "WsprryPi-UI", "data", "site.js");
const siteSource = fs.readFileSync(sitePath, "utf8");

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
        setInterval: () => 0,
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

comparison = context.updateCheckCommitComparisonResult(currentSha, targetSha, "diverged");
assert.strictEqual(
    comparison.updateAvailable,
    false,
    "diverged histories must not report an update"
);
assert.strictEqual(comparison.versionComparisonStatus, "diverged");

const compareGithubCommitsUnderTest = context.compareGithubCommits;
const selectGithubUpdateBranchUnderTest = context.selectGithubUpdateBranch;

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

    const buildHttp404 = () => {
        const error = new Error("not found");
        error.status = 404;
        return error;
    };
    const normalFetchGithubJson = context.fetchGithubJson;
    context.fetchGithubJson = async () => {
        throw buildHttp404();
    };
    await assert.rejects(
        compareGithubCommitsUnderTest(currentSha, targetSha),
        (error) => error?.code === "comparison_unavailable" && error?.updateCheckFailed === true,
        "GitHub compare 404 must be a failed/unknown comparison, not an update"
    );
    context.fetchGithubJson = normalFetchGithubJson;

    const normalLookupGithubBranch = context.lookupGithubBranch;
    const normalReachabilityCheck = context.isCurrentShaReachableFromBranchHead;
    context.lookupGithubBranch = async (branch) => {
        if (branch === "deleted_feature") {
            throw buildHttp404();
        }
        return { branch, headSha: targetSha };
    };
    context.isCurrentShaReachableFromBranchHead = async () => ({
        contained: false,
        status: "diverged",
        uncertain: false,
    });
    await assert.rejects(
        selectGithubUpdateBranchUnderTest({
            currentSha,
            currentBranch: "deleted_feature",
            branchState: "branch",
        }),
        (error) => error?.code === "unsafe_target" && error?.updateCheckFailed === true,
        "missing same-name branch must not fall back to devel without proven containment"
    );
    context.isCurrentShaReachableFromBranchHead = async () => ({
        contained: true,
        status: "ahead",
        uncertain: false,
    });
    const containedFallback = await selectGithubUpdateBranchUnderTest({
        currentSha,
        currentBranch: "deleted_feature",
        branchState: "branch",
    });
    assert.strictEqual(containedFallback.branch, "devel");
    assert.strictEqual(containedFallback.fallbackUsed, true);
    assert.ok(containedFallback.selectionReason.includes("containment-gated fallback"));

    context.lookupGithubBranch = async (branch) => {
        if (branch === "devel") {
            throw buildHttp404();
        }
        return { branch, headSha: targetSha };
    };
    context.isCurrentShaReachableFromBranchHead = async () => ({
        contained: false,
        status: "behind",
        uncertain: false,
    });
    await assert.rejects(
        selectGithubUpdateBranchUnderTest({
            currentSha,
            currentBranch: "devel",
            branchState: "branch",
        }),
        (error) => error?.code === "unsafe_target" && error?.updateCheckFailed === true,
        "missing upstream devel must not fall back to main without proven containment"
    );
    context.isCurrentShaReachableFromBranchHead = async () => ({
        contained: true,
        status: "ahead",
        uncertain: false,
    });
    const containedMainFallback = await selectGithubUpdateBranchUnderTest({
        currentSha,
        currentBranch: "devel",
        branchState: "branch",
    });
    assert.strictEqual(containedMainFallback.branch, "main");
    assert.strictEqual(containedMainFallback.fallbackUsed, true);
    assert.ok(containedMainFallback.selectionReason.includes("containment-gated fallback"));
    context.lookupGithubBranch = normalLookupGithubBranch;
    context.isCurrentShaReachableFromBranchHead = normalReachabilityCheck;

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
    let promptOptions = null;
    let refreshModalVisible = false;
    context.window.WSPRRYPI_INSTALLED_UI_BUILD_ID = "loaded-build";
    context.document.getElementById = (id) => {
        if (id === "confirmModal") {
            return {
                classList: {
                    contains: () => refreshModalVisible,
                },
            };
        }
        return originalGetElementById(id);
    };
    context.showConfirmationDialog = (options) => {
        promptAttempts += 1;
        promptOptions = options;
        assert.strictEqual(options.title, "UI refresh required");
        assert.strictEqual(options.confirmLabel, "Refresh");
        refreshModalVisible = true;
        return true;
    };

    context.maybePromptForUiRefresh({
        installed_ui_build_id: "loaded-build",
    });
    assert.strictEqual(promptAttempts, 0, "a stable installed identity must not prompt");

    context.maybePromptForUiRefresh({
        ui_build_id: "service-build",
        ui_version: "9.9.9",
    });
    assert.strictEqual(promptAttempts, 0, "service version metadata must not prompt");

    context.maybePromptForUiRefresh({
        installed_ui_build_id: "changed-build",
    });
    assert.strictEqual(promptAttempts, 1, "an installed-file change must prompt once");
    context.maybePromptForUiRefresh({ installed_ui_build_id: "changed-build" });
    assert.strictEqual(promptAttempts, 1, "an active prompt must not repeat");

    let replacedUrl = "";
    context.window.location.replace = (url) => { replacedUrl = url; };
    promptOptions.onConfirm();
    assert.ok(replacedUrl.includes("ui_refresh=changed-build"), "refresh must cache-bust with the new installed identity");

    refreshModalVisible = false;
    promptOptions.onCancel();
    context.maybePromptForUiRefresh({ installed_ui_build_id: "changed-build" });
    assert.strictEqual(promptAttempts, 1, "a dismissed identity must stay suppressed while stable");

    let historyUrl = "";
    context.window.history = { replaceState: (_state, _title, url) => { historyUrl = url; } };
    context.window.location.href = "http://localhost/?ui_refresh=loaded-build";
    assert.strictEqual(context.checkUiRefreshConvergence(), true);
    assert.ok(!historyUrl.includes("ui_refresh"), "successful convergence must remove the refresh token");

    let diagnosticMessage = "";
    let diagnosticShown = false;
    const diagnostic = {
        classList: { remove: (name) => { diagnosticShown = name === "d-none"; } },
        querySelector: () => ({
            set textContent(value) { diagnosticMessage = value; },
        }),
    };
    context.document.getElementById = (id) => id === "uiConsistencyDiagnostic"
        ? diagnostic
        : originalGetElementById(id);
    context.window.location.href = "http://localhost/?ui_refresh=requested-build";
    assert.strictEqual(context.checkUiRefreshConvergence(), false);
    assert.strictEqual(diagnosticShown, true);
    assert.ok(diagnosticMessage.includes("requested-build"));
    assert.ok(diagnosticMessage.includes("loaded-build"));
    context.maybePromptForUiRefresh({ installed_ui_build_id: "another-build" });
    assert.strictEqual(promptAttempts, 1, "a convergence diagnostic must replace further prompts");

    context.document.getElementById = originalGetElementById;

    const versionInfo = {
        currentSha,
        currentModalVersion: "3.0.0-gpio_for_amp+a8079bc",
    };
    const updateResult = {
        currentSha,
        targetBranch: "gpio_for_amp",
        targetHeadSha: targetSha,
        updateAvailable: true,
        releaseUrl: "https://github.com/WsprryPi/WsprryPi/releases",
        releaseTitle: "",
        fallbackUsed: false,
        versionComparisonUsed: "commit",
        versionComparisonStatus: "update_available",
    };
    let modalCalls = 0;
    let footerCalls = 0;
    const originalShowWsprryPiUpdateModal = context.showWsprryPiUpdateModal;
    const originalMarkWsprryPiUpdateFooter = context.markWsprryPiUpdateFooter;
    context.showWsprryPiUpdateModal = (modalVersionInfo, modalResult) => {
        modalCalls += 1;
        assert.strictEqual(modalVersionInfo, versionInfo);
        assert.strictEqual(modalResult, updateResult);
    };
    context.markWsprryPiUpdateFooter = (footerResult) => {
        footerCalls += 1;
        assert.strictEqual(footerResult, updateResult);
    };
    context.applyWsprryPiUpdateResult(versionInfo, updateResult);
    assert.strictEqual(footerCalls, 1);
    assert.strictEqual(
        modalCalls,
        1,
        "fresh updateAvailable=true results must show the app update modal from the same path that updates the footer"
    );

    modalCalls = 0;
    footerCalls = 0;
    context.applyWsprryPiUpdateResult(versionInfo, updateResult, { suppressModal: true });
    assert.strictEqual(footerCalls, 1);
    assert.strictEqual(modalCalls, 0);

    const originalDateNow = Date.now;
    let now = 1000000;
    Date.now = () => now;
    context.writeUpdateModalState(versionInfo, updateResult, "dismissed");
    assert.strictEqual(
        context.shouldShowUpdateModal(versionInfo, updateResult),
        false,
        "dismissed update modal state must suppress repeats for the same target"
    );

    const nextUpdateResult = Object.assign({}, updateResult, {
        targetHeadSha: "20a9bafef7e486c389aa2026c46d1d9cd2b87b2b",
    });
    assert.strictEqual(
        context.shouldShowUpdateModal(versionInfo, nextUpdateResult),
        true,
        "a new target SHA must be eligible to show the update modal again"
    );
    Date.now = originalDateNow;
    context.showWsprryPiUpdateModal = originalShowWsprryPiUpdateModal;
    context.markWsprryPiUpdateFooter = originalMarkWsprryPiUpdateFooter;

    function createFakeElement(tagName = "div") {
        const classes = new Set();
        return {
            tagName,
            children: [],
            dataset: {},
            textContent: "",
            href: "",
            target: "",
            rel: "",
            type: "",
            className: "",
            classList: {
                add: (...names) => names.forEach((name) => classes.add(name)),
                remove: (...names) => names.forEach((name) => classes.delete(name)),
                contains: (name) => classes.has(name),
                toggle: (name, force) => {
                    if (force === false) {
                        classes.delete(name);
                        return false;
                    }
                    classes.add(name);
                    return true;
                },
            },
            appendChild(child) {
                this.children.push(child);
                return child;
            },
            setAttribute(name, value) {
                this[name] = value;
            },
            addEventListener() {},
        };
    }

    const originalCreateElement = context.document.createElement;
    const originalCreateTextNode = context.document.createTextNode;
    const originalBootstrap = context.bootstrap;
    const originalJquery = context.$;
    let confirmButtonHidden = null;
    let confirmButtonText = "";
    let modalShows = 0;
    const modalEl = createFakeElement("div");
    const labelEl = createFakeElement("h3");
    const bodyEl = createFakeElement("p");
    context.document.getElementById = (id) => {
        if (id === "confirmModal") return modalEl;
        if (id === "confirmModalLabel") return labelEl;
        if (id === "confirmModalBody") return bodyEl;
        return originalGetElementById(id);
    };
    context.document.createElement = (tagName) => createFakeElement(tagName);
    context.document.createTextNode = (text) => ({
        tagName: "#text",
        textContent: text,
    });
    context.bootstrap = {
        Modal: {
            getOrCreateInstance: () => ({
                hide: () => {},
                show: () => {
                    modalShows += 1;
                },
            }),
        },
    };
    context.$ = (selector) => {
        const chain = jqueryStub();
        if (selector === "#confirmActionBtn") {
            chain.toggleClass = (className, force) => {
                if (className === "d-none") {
                    confirmButtonHidden = force === true;
                }
                return chain;
            };
            chain.text = (value) => {
                confirmButtonText = value;
                return chain;
            };
        }
        return chain;
    };

    context.showWsprryPiUpdateModal(versionInfo, Object.assign({}, updateResult, {
        targetHeadSha: "30a9bafef7e486c389aa2026c46d1d9cd2b87b2b",
        remoteVersionSelected: "",
        releaseTitle: "",
        versionComparisonUsed: "commit",
    }));
    const commitModalText = bodyEl.children.map((child) => child.textContent || "").join("");
    assert.ok(commitModalText.includes("gpio_for_amp+30a9baf"));
    assert.ok(commitModalText.includes("Review the update channel given to you for this pre-release version."));
    assert.strictEqual(labelEl.textContent, "Newer branch build available");
    assert.ok(!commitModalText.includes("GitHub releases"));
    assert.strictEqual(
        bodyEl.children.some((child) => child.tagName === "a"),
        false,
        "commit/branch update modal must not append a GitHub releases link"
    );
    assert.strictEqual(confirmButtonHidden, true);

    bodyEl.children = [];
    bodyEl.textContent = "";
    confirmButtonHidden = null;
    confirmButtonText = "";
    context.showWsprryPiUpdateModal(versionInfo, Object.assign({}, updateResult, {
        targetHeadSha: "40a9bafef7e486c389aa2026c46d1d9cd2b87b2b",
        releaseTitle: "WsprryPi 3.0.1",
        remoteVersionSelected: "3.0.1",
        versionComparisonUsed: "semver",
    }));
    const taggedModalText = bodyEl.children.map((child) => child.textContent || "").join("");
    assert.strictEqual(labelEl.textContent, "Update available");
    assert.ok(taggedModalText.includes("A release is available for this update: "));
    assert.strictEqual(
        bodyEl.children.some((child) => child.tagName === "a" && child.textContent === "WsprryPi 3.0.1"),
        true,
        "tagged release update modal must keep the release link"
    );
    assert.strictEqual(confirmButtonHidden, false);
    assert.strictEqual(confirmButtonText, "View release");
    assert.strictEqual(modalShows >= 2, true);

    context.document.getElementById = originalGetElementById;
    context.document.createElement = originalCreateElement;
    context.document.createTextNode = originalCreateTextNode;
    context.bootstrap = originalBootstrap;
    context.$ = originalJquery;

    console.log("update_check_comparison_test passed");
})().catch((error) => {
    console.error(error);
    process.exit(1);
});

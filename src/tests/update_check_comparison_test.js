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

// Exercise the real parsers, cache policy, and modal identity before replacing network collaborators.
const fetchGithubJsonUnderTest = context.fetchGithubJson;
const findReleaseForHeadUnderTest = context.findReleaseForHead;
const semanticUpdateUnderTest = context.buildSemanticVersionUpdateResult;
const semanticOrder = ["1.0.0-alpha.1", "1.0.0-alpha.2", "1.0.0-alpha.10", "1.0.0-beta.1", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"];
for (let i = 0; i < semanticOrder.length; ++i) {
    const left = context.parseSemanticVersion(semanticOrder[i]);
    assert(left, semanticOrder[i]);
    assert.equal(context.compareSemanticVersions(left, context.parseSemanticVersion(semanticOrder[i] + "+build.009")), 0);
    for (let j = 0; j < semanticOrder.length; ++j)
        assert.equal(Math.sign(context.compareSemanticVersions(left, context.parseSemanticVersion(semanticOrder[j]))), Math.sign(i - j));
}
for (const invalid of ["unknown", "1.0", "v1.0.0-beta.01", "1.0.0-alpha..1"])
    assert.equal(context.parseSemanticVersion(invalid), null, invalid);
const releases = context.summarizeSemanticReleases(["v1.0.0", "v1.1.0-beta.1", "v1.1.0-beta.2", "v2.0.0-alpha.1", "junk"]
    .map(tag_name => ({tag_name, html_url: "https://example.test/" + tag_name})));
assert.equal(releases.latestStable.normalized, "1.0.0");
assert.equal(releases.prereleasesByChannel.get("beta").normalized, "1.1.0-beta.2");
assert.equal(releases.latestPrerelease.normalized, "2.0.0-alpha.1");
const structuredVersion = {wspr_version: "4.0.0 (devel)", wspr_version_raw: "4.0.0", wspr_branch: "devel", wspr_commit: "a".repeat(40), wspr_branch_state: "branch"};
assert.equal(context.parseWsprryPiVersionResponse(structuredVersion).currentSha, "a".repeat(40));
for (const [field, value, code] of [["wspr_branch", "", "missing_branch"], ["wspr_branch_state", "bogus", "missing_branch"], ["wspr_commit", "invalid", "missing_commit"],
    ["wspr_version_parsed", {valid: true, major: "bad", minor: 0, patch: 0}, "missing_version_data"]]) {
    const result = context.parseWsprryPiVersionResponse({...structuredVersion, [field]: value});
    assert.equal(result.updateCheckFailed, true); assert.equal(result.code, code);
}
const storage = new Map();
context.window.localStorage = {getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value), removeItem: key => storage.delete(key)};
const cacheVersion = {currentSha: "a".repeat(40), currentBranch: "devel", branchState: "branch", buildDirtyKnown: true, buildDirty: false};
const goodCache = {...cacheVersion, checkedAt: Date.now(), updateAvailable: false};
context.writeUpdateCheckCache(cacheVersion, goodCache);
assert.equal(context.readUpdateCheckCache(cacheVersion).updateAvailable, false);
const cacheKey = context.updateCheckCacheKey(cacheVersion);
const serializedValidCache = JSON.parse(storage.get(cacheKey));
context.window.location.href = "http://localhost/another-page";
assert.equal(context.updateCheckCacheKey(cacheVersion), cacheKey, "cache identity is site-global");
assert.notEqual(context.updateCheckCacheKey({...cacheVersion, buildDirty: true}), cacheKey);
assert.notEqual(context.updateCheckCacheKey({...cacheVersion, buildDirtyKnown: false}), cacheKey);
for (const bad of ["{", "null", JSON.stringify({...goodCache, schemaVersion: -1}), JSON.stringify({...serializedValidCache, checkedAt: 1}), JSON.stringify({...serializedValidCache, currentSha: "b".repeat(40)})]) {
    storage.set(cacheKey, bad); assert.equal(context.readUpdateCheckCache(cacheVersion), null);
}
context.writeUpdateCheckFailureCache(cacheVersion, {code: "network"});
assert.equal(context.readUpdateCheckFailureCache(cacheVersion).updateCheckFailed, true);
const failureCacheKey = context.updateCheckFailureCacheKey(cacheVersion);
const validFailureCache = JSON.parse(storage.get(failureCacheKey));
storage.set(failureCacheKey, JSON.stringify({...validFailureCache, checkedAt: 1}));
assert.equal(context.readUpdateCheckFailureCache(cacheVersion), null, "expired failure cache is not reused");
storage.set(failureCacheKey, JSON.stringify(validFailureCache));
assert.notEqual(context.updateCheckFailureCacheKey(cacheVersion), cacheKey);
context.writeUpdateCheckCache(cacheVersion, goodCache);
assert.equal(context.readUpdateCheckFailureCache(cacheVersion), null, "success clears failure only after valid outcome");
const updateResult = {targetBranch: "devel", targetHeadSha: "b".repeat(40), releaseUrl: "https://example.test/release"};
context.writeUpdateModalState(cacheVersion, updateResult, "dismissed");
assert.equal(context.shouldShowUpdateModal(cacheVersion, updateResult), false);
assert.equal(context.shouldShowUpdateModal(cacheVersion, {...updateResult, targetHeadSha: "c".repeat(40)}), true);
const modalKey = vm.runInContext("UPDATE_MODAL_STATE_KEY", context);
const modalState = JSON.parse(storage.get(modalKey));
for (const lastSeenAt of [Date.now() + 100000, 1]) {
    storage.set(modalKey, JSON.stringify({...modalState, lastSeenAt}));
    assert.equal(context.shouldShowUpdateModal(cacheVersion, updateResult), true);
}
context.window.localStorage = {getItem() {throw Error("denied");}, setItem() {throw Error("denied");}, removeItem() {throw Error("denied");}};
assert.equal(context.readUpdateCheckCache(cacheVersion), null);
context.writeUpdateModalState(cacheVersion, updateResult, "dismissed");
assert.equal(context.shouldShowUpdateModal(cacheVersion, updateResult), false, "denied storage retains in-tab dismissal");
context.window.localStorage = {getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value), removeItem: key => storage.delete(key)};
let modalHides = 0;
const originalGetElement = context.document.getElementById;
const originalModal = context.bootstrap.Modal.getOrCreateInstance;
context.document.getElementById = id => id === "confirmModal" ? {dataset: {updateCheckActive: "true"}} : null;
context.bootstrap.Modal.getOrCreateInstance = () => ({hide() {modalHides++;}});
context.__cacheVersion = cacheVersion; context.__updateResult = updateResult;
vm.runInContext("activeUpdateModalIdentity = updateModalIdentity(__cacheVersion, __updateResult)", context);
context.handleUpdateCheckStorageEvent({key: modalKey, newValue: JSON.stringify({...modalState, reason: "dismissed"})});
assert.equal(modalHides, 1);
context.handleUpdateCheckStorageEvent({key: modalKey, newValue: JSON.stringify({...modalState, targetSha: "foreign", reason: "dismissed"})});
assert.equal(modalHides, 1, "foreign identity cannot dismiss this update");
context.document.getElementById = originalGetElement; context.bootstrap.Modal.getOrCreateInstance = originalModal;
vm.runInContext("activeUpdateModalIdentity = null", context);
console.log("Real SemVer ordering, metadata refusal, site-global success/failure caches and cross-tab modal identity passed");

const dirtyInputs = [
    [{wspr_build_dirty_state: {known: true, dirty: false}}, "version-dirty", true, false, "structured"],
    [{wspr_build_dirty_state: {known: false, dirty: true}}, "version-dirty", false, false, "structured"],
    [{wspr_build_dirty: true}, "clean", true, true, "structured"],
    [{}, "4.0.0+dirty", true, true, "version_text"], [{}, "4.0.0", false, false, "unavailable"]];
for (const [metadata, display, known, dirty, source] of dirtyInputs) {
    const actual = context.parseBuildDirtyState(metadata, display, "", "");
    assert.equal(actual.known, known); assert.equal(actual.dirty, dirty); assert.equal(actual.source, source);
}
assert.equal(context.applyDirtyBuildMetadata({...cacheVersion, buildDirty: true}, {updateAvailable: false}).versionComparisonStatus, "local_modified");
assert.equal(context.applyDirtyBuildMetadata({...cacheVersion, buildDirty: true}, {updateAvailable: true, versionComparisonStatus: "update_available"}).updateAvailable, true);
const contradictory = context.parseWsprryPiVersionResponse({...structuredVersion, wspr_version: "3.0.0 (wrong-branch)+bbbbbbb"});
assert.equal(contradictory.currentBranch, "devel"); assert.equal(contradictory.currentSha, "a".repeat(40));
assert.equal(contradictory.currentModalVersion, "4.0.0");
assert.equal(contradictory.currentDisplayVersion, "3.0.0 (wrong-branch)+bbbbbbb");
const legacy = context.parseWsprryPiVersionResponse({wspr_version: "3.0.0 (feature)+abcdef1"});
assert.equal(legacy.currentBranch, "feature"); assert.equal(legacy.currentSha, "abcdef1");
context.writeUpdateCheckCache(cacheVersion, goodCache);
storage.set(cacheKey, JSON.stringify({...serializedValidCache, branchState: "detached"}));
assert.equal(context.readUpdateCheckCache(cacheVersion), null, "branch-state mismatch cannot reuse cache");
const normalVmNow = vm.runInContext("Date.now", context);
context.__testNow = 1700000000000;
vm.runInContext("Date.now = () => __testNow", context);
context.writeUpdateModalState(cacheVersion, updateResult, "dismissed");
context.__testNow += 2 * 60 * 60 * 1000 - 1;
assert.equal(context.shouldShowUpdateModal(cacheVersion, updateResult), false);
context.__testNow++;
assert.equal(context.shouldShowUpdateModal(cacheVersion, updateResult), true, "modal unlocks at exact two-hour boundary");
context.writeUpdateModalState(cacheVersion, updateResult, "dismissed");
assert.equal(context.shouldShowUpdateModal(cacheVersion, {...updateResult, releaseUrl: "https://example.test/other"}), true);
context.window.location.href = "http://localhost/different-page";
assert.equal(context.shouldShowUpdateModal(cacheVersion, updateResult), false, "modal identity spans pages");
context.__realNow = normalVmNow; vm.runInContext("Date.now = __realNow", context);
storage.clear();
const savedLocation = {...context.window.location};
for (const [protocol, http, wsProtocol] of [["http:", "http:", "ws:"], ["https:", "https:", "wss:"]]) {
    Object.assign(context.window.location, {protocol, hostname: "radio.test", origin: `${protocol}//radio.test`, href: `${protocol}//radio.test/wsprrypi/`});
    assert.equal(context.buildDirectRestFallbackUrl("/config"), `${http}//radio.test:31415/config`);
    assert.equal(context.buildDirectWebSocketFallbackUrl("/ws"), `${wsProtocol}//radio.test:31416/ws`);
    assert.equal(context.buildWebSocketUrl("/wsprrypi/ws"), `${wsProtocol}//radio.test/wsprrypi/ws`);
}
Object.assign(context.window.location, savedLocation);
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
    buildDirtyKnown = false, buildDirty = false,
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
        buildDirtyKnown,
        buildDirty,
    });
}

let verificationPhase = "update behavioral matrix";
const verificationWatchdog = setTimeout(() => {
    console.error(`Update test deadline: phase=${verificationPhase}, budget=10000ms; awaited completion was not observed`);
    process.exitCode = 1;
}, 10000);
(async () => {
    const dirtyEqual = await runBuildPriorityCase({targetHeadSha: currentSha, compareStatus: "identical", buildDirtyKnown: true, buildDirty: true});
    assert.equal(dirtyEqual.updateAvailable, false);
    assert.equal(dirtyEqual.localBuildState, "dirty_build", "actual update orchestration propagates dirty state");
    assert.equal(dirtyEqual.versionComparisonStatus, "local_modified");
    const dirtyOlder = await runBuildPriorityCase({targetHeadSha: "b".repeat(40), compareStatus: "ahead", buildDirtyKnown: true, buildDirty: true});
    assert.equal(dirtyOlder.updateAvailable, true, "dirty builds retain a genuine newer commit");
    assert.equal(dirtyOlder.localBuildState, "dirty_build");
    const footerOriginalGet = context.document.getElementById;
    const makeFooterNode = () => {
        const classes = new Set();
        return {classes, title: "", href: "", attributes: {}, classList: {add: name => classes.add(name), remove: name => classes.delete(name)},
                setAttribute(name, value) {this.attributes[name] = value;}, removeAttribute(name) {delete this.attributes[name];}};
    };
    const versionFooter = makeFooterNode(), linkFooter = makeFooterNode();
    context.document.getElementById = id => id === "versionText" ? versionFooter : id === "versionUpdateLink" ? linkFooter : null;
    for (const [status, word] of [["local_modified", "modifications"], ["local_ahead", "newer"], ["diverged", "diverged"]]) {
        context.markWsprryPiLocalUpdateState({versionComparisonStatus: status});
        assert(versionFooter.title.includes(word));
        assert(linkFooter.classes.has("d-none"));
        assert.equal(linkFooter.attributes["aria-label"], versionFooter.title);
        assert(!versionFooter.classes.has("update-available") && !versionFooter.classes.has("update-check-failed"));
    }
    context.markWsprryPiUpdateFooter({updateAvailable: true, releaseUrl: "https://example.test/release", versionComparisonUsed: "commit"});
    assert(versionFooter.classes.has("update-available") && !linkFooter.classes.has("d-none"));
    assert.equal(linkFooter.href, "https://example.test/release");
    context.markWsprryPiUpdateCheckFailed({code: "malformed_response"});
    assert(versionFooter.classes.has("update-check-failed") && !versionFooter.classes.has("update-available"));
    assert(linkFooter.attributes["aria-label"].includes("failed"));
    context.clearWsprryPiUpdateFooter();
    assert(!versionFooter.classes.has("update-check-failed") && linkFooter.classes.has("d-none"));
    const failureBuild = context.buildWsprryPiUpdateResult, failureApply = context.applyWsprryPiUpdateResult, failureWrite = context.writeUpdateCheckFailureCache;
    let failureApplied = 0, failureObserved;
    const failureCompleted = new Promise(resolve => {failureObserved = resolve;});
    context.__selectedBranch = "devel"; context.__targetSha = "b".repeat(40);
    context.__semanticUpdateAvailable = false; context.__semanticStatus = "equal"; context.__semanticRemoteVersion = "4.0.0";
    const failureGithub = context.fetchGithubJson;
    context.fetchGithubJson = async () => ({status: "not-a-relationship"});
    verificationPhase = "malformed comparison failure pipeline";
    context.applyWsprryPiUpdateResult = () => {failureApplied++; failureObserved();};
    context.writeUpdateCheckFailureCache = (...args) => {const result = failureWrite(...args); failureObserved(); return result;};
    storage.clear();
    context.checkForWsprryPiUpdate(structuredVersion, {bypassCache: true});
    await failureCompleted;
    assert.equal(failureApplied, 0, "failed orchestration never applies a successful no-update result");
    const parsedFailureVersion = context.parseWsprryPiVersionResponse(structuredVersion);
    assert.equal(context.readUpdateCheckCache(parsedFailureVersion), null);
    assert.equal(context.readUpdateCheckFailureCache(parsedFailureVersion).code, "malformed_response");
    assert(versionFooter.classes.has("update-check-failed"));
    context.buildWsprryPiUpdateResult = failureBuild; context.applyWsprryPiUpdateResult = failureApply; context.writeUpdateCheckFailureCache = failureWrite;
    context.fetchGithubJson = failureGithub;
    context.document.getElementById = footerOriginalGet; storage.clear();
    verificationPhase = "real GitHub branch and comparison matrices";
    const savedGithub = context.fetchGithubJson;
    let remoteStatus = "ahead", apiCalls = [];
    context.fetchGithubJson = async url => {
        apiCalls.push(url);
        if (url.includes("/branches/")) return {commit: {sha: "b".repeat(40)}};
        return {status: remoteStatus};
    };
    for (const sha of ["b".repeat(40), "b".repeat(7)]) {
        apiCalls.length = 0;
        assert.equal((await compareGithubCommitsUnderTest(sha, "b".repeat(40))).updateAvailable, false);
        assert.equal(apiCalls.length, 0, "matching full/short SHA never calls compare");
    }
    for (const [status, contained] of [["ahead", true], ["identical", true], ["behind", false], ["diverged", false]]) {
        remoteStatus = status;
        const result = await context.isCurrentShaReachableFromBranchHead("a".repeat(40), {headSha: "b".repeat(40)});
        assert.equal(result.contained, contained, status); assert.equal(result.uncertain, false);
    }
    assert.equal((await context.isCurrentShaReachableFromBranchHead("b".repeat(7), {headSha: "b".repeat(40)})).uncertain, true);
    remoteStatus = "ahead";
    for (const [branch, state, target] of [["main", "branch", "main"], ["feature", "branch", "feature"], ["devel", "branch", "main"], ["HEAD", "detached", "main"], ["unknown", "unknown", "main"]]) {
        const selected = await selectGithubUpdateBranchUnderTest({currentSha: "a".repeat(40), currentBranch: branch, branchState: state});
        assert.equal(selected.branch, target, `${branch}/${state}`);
    }
    remoteStatus = "behind";
    assert.equal((await selectGithubUpdateBranchUnderTest({currentSha: "a".repeat(40), currentBranch: "devel", branchState: "branch"})).branch, "devel");
    await assert.rejects(selectGithubUpdateBranchUnderTest({currentSha: "a".repeat(40), currentBranch: "HEAD", branchState: "detached"}), error => error.code === "detached_target_unknown");
    context.fetchGithubJson = async () => {throw Object.assign(Error("service failed"), {status: 500});};
    await assert.rejects(selectGithubUpdateBranchUnderTest({currentSha, currentBranch: "feature", branchState: "branch"}), error => error.status === 500);
    assert.equal((await context.isCurrentShaReachableFromBranchHead(currentSha, {headSha: targetSha})).contained, false);
    context.fetchGithubJson = async () => ({});
    await assert.rejects(context.lookupGithubBranch("feature"), error => error.code === "malformed_response");
    for (const bad of [{}, {status: "not-a-relationship"}]) {
        context.fetchGithubJson = async () => bad;
        await assert.rejects(compareGithubCommitsUnderTest(currentSha, targetSha), error => error.code === "malformed_response");
    }
    const lookupOriginal = context.lookupGithubBranch;
    context.lookupGithubBranch = async () => {throw Object.assign(Error("not a branch"), {status: 404});};
    let tagCalls = 0;
    context.fetchGithubJson = async url => {
        tagCalls++;
        if (url.includes("/git/ref/tags/")) return {object: {type: "tag", url: "https://example.test/tag-object"}};
        return {object: {type: "commit", sha: targetSha}};
    };
    const memo = vm.runInContext("new Map()", context);
    assert.equal(await context.resolveReleaseTargetSha("v4.0.0", memo), targetSha);
    assert.equal(await context.resolveReleaseTargetSha("v4.0.0", memo), targetSha);
    assert.equal(tagCalls, 2, "annotated tag dereference is memoized per scan");
    assert.equal(await context.resolveReleaseTargetSha(targetSha), targetSha);
    assert.equal(tagCalls, 2, "full SHA resolution does not hit transport");
    const releasesOriginal = context.fetchGithubReleases;
    context.fetchGithubReleases = async () => [
        {target_commitish: "v4.0.0", html_url: "https://example.test/release", name: "Candidate release"}
    ];
    assert.deepEqual(JSON.parse(JSON.stringify(await findReleaseForHeadUnderTest(targetSha))), {
        releaseUrl: "https://example.test/release", releaseTitle: "Candidate release"
    });
    assert.equal(await findReleaseForHeadUnderTest("c".repeat(40)), null);
    context.fetchGithubReleases = releasesOriginal;
    context.lookupGithubBranch = lookupOriginal; context.fetchGithubJson = savedGithub;
    const savedReleaseFetch = context.fetchGithubReleases;
    context.fetchGithubReleases = async () => ["v1.0.0", "v1.1.0-beta.2", "v2.0.0-alpha.1"].map(tag_name =>
        ({tag_name, target_commitish: "d".repeat(40), html_url: "https://example.test/" + tag_name}));
    for (const [version, selected, available] of [["1.0.0", "1.0.0", false], ["1.1.0-beta.1", "1.1.0-beta.2", true],
        ["1.1.0-rc.1", null, false], ["0.9.0-beta.1", "1.0.0", true]]) {
        const result = await semanticUpdateUnderTest({...cacheVersion, currentModalVersion: version});
        if (selected !== null) assert.equal(result.remoteVersionSelected, selected, version);
        assert.equal(result.updateAvailable, available, version);
    }
    assert.equal((await semanticUpdateUnderTest({...cacheVersion, currentModalVersion: "1.0.0+dirty"})).useCommitFallback, true);
    assert.equal((await semanticUpdateUnderTest({...cacheVersion, currentModalVersion: "9.0.0"})).versionComparisonStatus, "local_ahead");
    assert.equal((await semanticUpdateUnderTest({...cacheVersion, currentModalVersion: "not-a-version"})).useCommitFallback, true);
    context.fetchGithubReleases = async () => { throw {code: "network"}; };
    assert.equal((await semanticUpdateUnderTest({...cacheVersion, currentModalVersion: "1.0.0"})).useCommitFallback, true);
    context.fetchGithubReleases = savedReleaseFetch;
    let built = 0, applied = 0;
    const normalBuild = context.buildWsprryPiUpdateResult, normalApply = context.applyWsprryPiUpdateResult;
    context.buildWsprryPiUpdateResult = async () => { built++; return goodCache; };
    context.applyWsprryPiUpdateResult = () => { applied++; };
    storage.clear();
    context.setUpdateCheckDisabled(true);
    context.checkForWsprryPiUpdate(structuredVersion, {bypassCache: true});
    assert.equal(built, 0, "Never check returns before starting update work");
    assert.equal(applied, 0, "Never check cannot apply or prompt an update");
    context.setUpdateCheckDisabled(false);
    context.checkForWsprryPiUpdate(structuredVersion, {bypassCache: true});
    assert.equal(built, 1, "enabled positive control reaches the observed build boundary");
    await Promise.resolve();
    assert.equal(applied, 1, "enabled positive control applies its completed result");
    context.buildWsprryPiUpdateResult = normalBuild; context.applyWsprryPiUpdateResult = normalApply;
    storage.clear();
    for (const [mode, code] of [["network", "network"], ["rate", "rate_limited"], ["json", "malformed_response"]]) {
        context.fetch = async () => {
            if (mode === "network") throw Error("offline");
            return {ok: mode === "json", status: 403,
                    headers: {get: key => key === "x-ratelimit-remaining" ? "0" : null},
                    json: async () => {throw Error("invalid JSON");}};
        };
        await assert.rejects(fetchGithubJsonUnderTest("https://example.test/api"), error => error.code === code);
    }
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
    const diagnostic = {
        hidden: true,
        querySelector: (selector) => {
            assert.strictEqual(selector, ".page-warning__message", "the diagnostic must populate the shared warning message");
            return { set textContent(value) { diagnosticMessage = value; } };
        },
    };
    context.document.getElementById = (id) => id === "uiConsistencyDiagnostic"
        ? diagnostic
        : originalGetElementById(id);
    context.window.location.href = "http://localhost/?ui_refresh=requested-build";
    assert.strictEqual(context.checkUiRefreshConvergence(), false);
    assert.strictEqual(diagnostic.hidden, false, "failed convergence must reveal the page warning");
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
    assert.ok(commitModalText.includes(versionInfo.currentModalVersion), "branch summary names the installed local build");
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
        localVersionParsed: "3.0.0",
        versionComparisonUsed: "semver",
    }));
    const taggedModalText = bodyEl.children.map((child) => child.textContent || "").join("");
    assert.ok(taggedModalText.includes("3.0.0 is behind release 3.0.1."));
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
}).finally(() => clearTimeout(verificationWatchdog));

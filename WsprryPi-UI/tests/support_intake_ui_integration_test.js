"use strict";

const { waitForPhp, createBrowserProfile, freePort, waitFor: sharedWaitFor, launchBrowser, finishFixture, spawnFixtureChild, waitForSpawn } = require("./browser_harness.js");
const waitFor = (check, description, timeoutMs = 12000) => sharedWaitFor(check, description, timeoutMs);

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

const UI_ROOT = path.resolve(__dirname, "..");


async function removeProfileDirectory(profileDir, options = {}) {
    const remove = options.remove || fs.rmSync;
    const wait = options.wait || ((milliseconds) =>
        new Promise((resolve) => setTimeout(resolve, milliseconds)));
    const attempts = options.attempts || 10;
    let lastError;
    for (let attempt = 1; attempt <= attempts; attempt++) {
        try {
            remove(profileDir, { recursive: true, force: true });
            return;
        } catch (error) {
            lastError = error;
            if (attempt < attempts) await wait(100);
        }
    }
    throw lastError;
}

function preferredHarnessError(primaryError, cleanupError) {
    return primaryError || cleanupError;
}

function chromiumBinary() {
    if (process.env.CHROMIUM_BIN) return process.env.CHROMIUM_BIN;
    for (const candidate of ["chromium", "chromium-browser", "google-chrome"]) {
        const found = spawnSync("sh", ["-c", `command -v ${candidate}`], { encoding: "utf8" });
        if (found.status === 0 && found.stdout.trim()) return found.stdout.trim();
    }
    const macChrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    if (fs.existsSync(macChrome)) return macChrome;
    throw new Error("Chromium-compatible browser not found");
}


async function networkSafetyBehavior() {
    const ok = (v, label) => { if (!v) throw Error(label); };
    const field = id => document.getElementById(id);
    const tone = field("test_tone"), toneModal = field("testToneModal");
    tone.disabled = false;
    const shown = new Promise(resolve => toneModal.addEventListener("shown.bs.modal", resolve, {once: true}));
    tone.click(); await shown;
    const hidden = new Promise(resolve => toneModal.addEventListener("hidden.bs.modal", resolve, {once: true}));
    bootstrap.Modal.getOrCreateInstance(toneModal).hide(); await hidden;
    ok(new URL(NETWORK_SAFETY_ENDPOINT.proxyUrl, location.href).origin === location.origin && NETWORK_SAFETY_ENDPOINT.directUrl === NETWORK_SAFETY_ENDPOINT.proxyUrl, "network safety has no direct-port fallback");
    const original = window.fetch; const calls = []; let release; let observed;
    const entered = new Promise(resolve => { observed = resolve; });
    const held = new Promise(resolve => { release = resolve; });
    let fail = true;
    const state = {configured_known: true, active_known: true, configured: "enforced", active: "enforced", status: "NETWORK SAFETY ACTIVE"};
    window.fetch = async (url, options = {}) => {
        if (url !== NETWORK_SAFETY_ENDPOINT.proxyUrl) return original(url, options);
        calls.push({url, method: options.method, body: options.body});
        if (options.method === "GET") return {ok: true, json: async () => state};
        ok(options.method === "POST" && options.cache === "no-store", "closed same-origin apply transport");
        if (fail) { observed(); await held; return {ok: false, json: async () => ({message: "fixture apply refusal"})}; }
        return {ok: true, json: async () => ({...state, applied: true, configured: "insecure-disabled", active: "insecure-disabled", status: "NETWORK SAFETY OFF"})};
    };
    const settled = async check => {
        const deadline = performance.now() + 5000;
        while (!check()) {
            if (performance.now() >= deadline) throw Error("network safety render deadline");
            await new Promise(resolve => requestAnimationFrame(resolve));
        }
    };
    try {
        field("networkSafetyDisabled").checked = true;
        field("networkSafetyDisabled").dispatchEvent(new Event("change", {bubbles: true}));
        for (const value of ["", "disable local-lan safety", "DISABLE LOCAL-LAN SAFETY "]) {
            field("networkSafetyDisablePhrase").value = value;
            field("networkSafetyDisablePhrase").dispatchEvent(new Event("input", {bubbles: true}));
            ok(field("applyNetworkSafetyButton").disabled, "exact phrase required");
            field("applyNetworkSafetyButton").dispatchEvent(new Event("click"));
            ok(calls.length === 0, "wrong phrase cannot reach mutation even with synthetic click");
        }
        field("networkSafetyDisablePhrase").value = "DISABLE LOCAL-LAN SAFETY";
        field("networkSafetyDisablePhrase").dispatchEvent(new Event("input", {bubbles: true}));
        ok(!field("applyNetworkSafetyButton").disabled && field("networkSafetyPhraseStatus").textContent.includes("Ready to apply"), "phrase announces enabled action");
        field("applyNetworkSafetyButton").click(); await entered;
        for (const id of ["networkSafetyEnforced", "networkSafetyDisabled", "networkSafetyDisablePhrase", "cancelNetworkSafetyButton"])
            ok(field(id).disabled, "pending apply locks " + id);
        ok(calls.length === 1 && calls[0].body === JSON.stringify({mode: "insecure-disabled"}), "one exact mode-only mutation");
        field("applyNetworkSafetyButton").click(); ok(calls.length === 1, "pending apply cannot duplicate");
        release(); await settled(() => !field("networkSafetyDisablePhrase").disabled);
        ok(field("networkSafetyResult").dataset.state === "danger", "apply failure is truthful");
        ok(field("networkSafetyDisabled").checked && field("networkSafetyDisablePhrase").value === "DISABLE LOCAL-LAN SAFETY", "failure retains the operator draft");
        ok(field("networkSafetyConfigured").textContent === "Enforced" && field("networkSafetyActive").textContent === "Enforced", "failed draft does not become configured or active");
        fail = false; field("applyNetworkSafetyButton").click();
        await settled(() => field("networkSafetyResult").dataset.state === "success");
        ok(field("networkSafetyBanner").dataset.state === "off" && field("networkSafetyBanner").textContent === "NETWORK SAFETY OFF", "active override has exact conspicuous warning");
        ok(!field("networkSafetyDisabled").checked && field("networkSafetyDisablePhrase").value === "", "successful apply clears confirmation draft");
        field("networkSafetyDisabled").checked = true; field("networkSafetyDisabled").dispatchEvent(new Event("change", {bubbles: true}));
        field("networkSafetyDisablePhrase").value = "discard"; field("cancelNetworkSafetyButton").click();
        ok(!field("networkSafetyDisabled").checked && field("networkSafetyDisablePhrase").value === "", "explicit cancel clears draft without mutation");
        ok(calls.filter(call => call.method === "POST").length === 2, "only deliberate valid applies mutate");
        ok(field("networkSafetyPhraseStatus").getAttribute("aria-live") === "polite", "phrase feedback remains accessible");
    } finally { window.fetch = original; }
    return "Rendered network safety phrase, single-flight, failure preservation and active-state distinction passed";
}

async function browserTest() {
    const fail = (message) => { throw new Error(message); };
    const ok = (condition, message) => { if (!condition) fail(message); };
    const equal = (actual, expected, message) => {
        if (actual !== expected) fail(`${message}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    };
    const field = (id) => document.getElementById(id);
    const wait = async (check, message, timeout = 5000) => {
        const deadline = Date.now() + timeout;
        while (Date.now() < deadline) {
            if (check()) return;
            await new Promise((resolve) => setTimeout(resolve, 25));
        }
        fail(`Timed out: ${message}`);
    };
    const response = (body, status = 200) => ({
        ok: status >= 200 && status < 300,
        status,
        json: async () => body,
        blob: async () => new Blob(["readable candidate"]),
        headers: { get: () => 'attachment; filename="wsprrypi-support-test.tar.gz"' },
    });

    const calls = [];
    let deleteResponse = response({});
    let intakeResponse = response({
        status: "active",
        generation: 32,
        expires_at: "2026-10-01T00:00:00Z",
        minimum_upload_version: "1.3.0",
        signing_key_id: "wsprrypi-intake-2026-01",
        bundle_key_id: "wsprrypi-bundle-2026-01",
        request_url: "https://www.dropbox.com/request/secret-capability",
        user_message: "Private intake is operating normally."
    });
    let readableDownload = response({});
    let uploadReportResponse = response({ workflow_state: "upload_reported_complete" });
    window.fetchWithEndpointFallback = async (endpoint, options = {}) => {
        const method = options.method || "GET";
        const route = `${method} ${endpoint.proxyUrl}`;
        const allowed = new Set(["POST /api/support-bundles", "GET /api/support-bundles/job-32", "DELETE /api/support-bundles/job-32",
            "GET /api/support-bundles/job-32/download", "POST /api/support-bundles/job-32/finalize", "POST /api/support-bundles/job-32/encrypt",
            "GET /api/support-bundles/job-32/encrypted", "GET /api/support-bundles/job-32/receipt", "POST /api/support-bundles/job-32/upload-reported-complete",
            "GET /api/support-intake"]);
        ok(allowed.has(route), "strict support route: " + route);
        calls.push({ name: endpoint.name, method, url: endpoint.proxyUrl, body: options.body });
        if (endpoint.name === "support intake") return intakeResponse;
        if (endpoint.name === "support bundles" && options.method === "POST" &&
            endpoint.proxyUrl.endsWith("/api/support-bundles")) {
            const payload = JSON.parse(options.body);
            equal(Object.keys(payload).sort().join(","), "probe_i2c,support_context", "closed create request");
            equal(typeof payload.probe_i2c, "boolean", "probe policy is an explicit boolean");
            return response({ id: "job-32", case_id: "A7K3-M9QF-X2DP" });
        }
        if (endpoint.name === "support bundles" && options.method === "DELETE") return deleteResponse;
        if (endpoint.name === "support bundles" && endpoint.proxyUrl.endsWith("/download")) return readableDownload;
        if (endpoint.name === "support bundles" && options.method === "POST" && endpoint.proxyUrl === "/api/support-bundles/job-32/finalize") {
            return response({ workflow_state: "finalized" });
        }
        if (endpoint.name === "support bundles" && endpoint.proxyUrl.endsWith("/encrypt")) {
            return response({ workflow_state: "encrypted" });
        }
        if (endpoint.name === "support bundles" && endpoint.proxyUrl.endsWith("/encrypted")) {
            return response({});
        }
        if (endpoint.name === "support bundles" && endpoint.proxyUrl.endsWith("/receipt")) {
            return response({});
        }
        if (endpoint.name === "support bundles" &&
            endpoint.proxyUrl.endsWith("/upload-reported-complete")) {
            return uploadReportResponse;
        }
        if (endpoint.name === "support bundles" && (options.method || "GET") === "GET" && endpoint.proxyUrl === "/api/support-bundles/job-32") {
            return response({ state: "succeeded", download_available: true, case_id: "A7K3-M9QF-X2DP" });
        }
        throw Error(`Unexpected support fixture request: ${options.method || "GET"} ${endpoint.proxyUrl}`);
    };
    URL.createObjectURL = () => "blob:test";
    URL.revokeObjectURL = () => {};
    const downloadNames = [];
    HTMLAnchorElement.prototype.click = function () { downloadNames.push(this.download); };
    let copiedText = "";
    Object.defineProperty(navigator, "clipboard", {
        configurable: true,
        value: { writeText: async (value) => { copiedText = value; } }
    });

    equal(calls.length, 0, "page load must not resolve intake");
    ok(field("supportIntakePanel").classList.contains("d-none"), "intake action starts hidden");
    ok(!field("supportBundleProbeI2c").checked, "active I2C probing is opt-in in the actual template");
    field("createSupportBundleButton").click();
    field("supportBundleIssueNumber").value = "414";
    field("confirmCreateSupportBundleButton").click();
    await wait(() => calls.some((call) => call.name === "support bundles" && call.method === "POST"), "create call");
    equal(JSON.parse(calls.find(call => call.url === "/api/support-bundles" && call.method === "POST").body).probe_i2c, false, "unchecked active probe is not requested");
    equal(calls.filter((call) => call.name === "support intake").length, 0,
        "collection must not resolve intake");
    await wait(() => field("downloadSupportBundleButton").dataset.available === "true", "candidate ready", 6000);
    let releaseBlob, observedBlob;
    const blobRequested = new Promise(resolve => { observedBlob = resolve; });
    const heldBlob = new Promise(resolve => { releaseBlob = resolve; });
    readableDownload = {...response({}), blob: () => { observedBlob(); return heldBlob; }};
    field("downloadSupportBundleButton").click();
    await blobRequested;
    const downloadCalls = calls.filter(call => call.url.endsWith("/download")).length;
    field("downloadSupportBundleButton").click();
    equal(calls.filter(call => call.url.endsWith("/download")).length, downloadCalls, "pending download is single-flight");
    ok(field("supportBundleReviewConsent").classList.contains("d-none"), "partial download cannot enable review");
    field("finalizeSupportBundleButton").click();
    equal(calls.filter(call => call.url.endsWith("/finalize")).length, 0, "partial bytes cannot be finalized");
    releaseBlob(new Blob([]));
    await wait(() => field("supportBundleStatus").textContent.includes("did not complete"), "empty Blob refusal");
    equal(downloadNames.length, 0, "empty Blob never reaches browser download");
    let releaseStale, observedStale;
    const staleStarted = new Promise(resolve => { observedStale = resolve; });
    const staleBody = new Promise(resolve => { releaseStale = resolve; });
    readableDownload = {...response({}), blob: () => { observedStale(); return staleBody; }};
    field("downloadSupportBundleButton").click(); await staleStarted;
    // Model a workflow reset while an uncooperative response still owns a body promise.
    field("deleteSupportBundleButton").dispatchEvent(new Event("click"));
    await wait(() => field("supportBundleStatus").dataset.state === "deleted", "old job reset");
    releaseStale(new Blob(["stale complete bytes"]));
    await wait(() => field("supportBundleStatus").textContent.includes("did not complete"), "stale download refusal");
    equal(downloadNames.length, 0, "old job bytes never reach browser download");
    ok(field("supportBundleReviewConsent").classList.contains("d-none"), "stale body cannot approve a replacement workflow");
    field("createSupportBundleButton").click();
    field("supportBundleIssueNumber").value = "414";
    field("confirmCreateSupportBundleButton").click();
    await wait(() => field("downloadSupportBundleButton").dataset.available === "true", "replacement candidate ready", 6000);
    readableDownload = {...response({}), headers: {get: () => 'attachment; filename="../../private.tar.gz"'}};
    field("downloadSupportBundleButton").click();
    await wait(() => !field("supportBundleReviewConsent").classList.contains("d-none"), "review consent");
    equal(downloadNames.at(-1), "wsprrypi-support-bundle.tar.gz", "unsafe server filename uses exact safe fallback");
    equal(calls.filter((call) => call.name === "support intake").length, 0,
        "download must not resolve intake");
    readableDownload = response({});
    field("supportBundleReviewed").checked = true;
    field("supportBundleReviewed").dispatchEvent(new Event("change", { bubbles: true }));
    await wait(() => field("finalizeSupportBundleButton").disabled === false, "finalize action enabled");
    field("finalizeSupportBundleButton").click();
    equal(calls.at(-1).url, "/api/support-bundles/job-32/finalize", "review uses the exact bound finalize endpoint");
    equal(calls.at(-1).method, "POST", "finalize is an explicit mutation");
    equal(calls.at(-1).body, undefined, "finalize adds no alternate payload");
    await wait(() => !field("supportIntakePanel").classList.contains("d-none"), "intake action after finalization");
    equal(calls.filter((call) => call.name === "support intake").length, 0,
        "finalization must not resolve intake without explicit check");

    field("checkSupportIntakeButton").click();
    field("checkSupportIntakeButton").click();
    await wait(() => field("supportIntakeMessage").textContent.includes("Private upload is available"), "active state");
    equal(calls.filter((call) => call.name === "support intake").length, 1,
        "duplicate clicks must produce one request");
    ok(!document.body.textContent.includes("secret-capability"), "Dropbox capability must not enter rendered text");
    ok(![...document.querySelectorAll("a")].some((link) => link.href.includes("secret-capability")),
        "Dropbox capability must not enter a link");
    ok(field("supportIntakeSignedMessage").textContent.includes("operating normally"),
        "active signed message must render as text");
    ok(!field("supportEncryptionPanel").classList.contains("d-none"),
        "active intake must reveal local encryption consent");
    field("supportEncryptionConsent").checked = true;
    field("supportEncryptionConsent").dispatchEvent(new Event("change", { bubbles: true }));
    field("encryptSupportBundleButton").click();
    await wait(() => !field("downloadEncryptedSupportBundleButton").classList.contains("d-none"),
        "encrypted download action");
    field("downloadEncryptedSupportBundleButton").click();
    await wait(() => !field("downloadSupportReceiptButton").classList.contains("d-none"),
        "receipt after encrypted download");
    ok(!field("supportDropboxHandoffPanel").classList.contains("d-none"),
        "completed encrypted download reveals Dropbox disclosure");
    equal(field("openSupportDropboxButton").getAttribute("href"),
        "/api/support-bundles/job-32/handoff",
        "handoff link exposes only the local fresh resolver");
    ok(field("openSupportDropboxButton").classList.contains("disabled"),
        "handoff starts disabled without consent");
    const blockedHandoff = new MouseEvent("click", { bubbles: true, cancelable: true });
    equal(field("openSupportDropboxButton").dispatchEvent(blockedHandoff), false,
        "handoff click is blocked before consent");
    field("supportDropboxHandoffConsent").checked = true;
    field("supportDropboxHandoffConsent").dispatchEvent(new Event("change", { bubbles: true }));
    ok(!field("openSupportDropboxButton").classList.contains("disabled"),
        "explicit disclosure consent enables handoff");
    field("openSupportDropboxButton").addEventListener("click", (event) => event.preventDefault(),
        { once: true });
    field("openSupportDropboxButton").dispatchEvent(
        new MouseEvent("click", { bubbles: true, cancelable: true }));
    ok(!field("supportUploadReportPanel").classList.contains("d-none"),
        "handoff request reveals a distinct completion-report step");
    ok(field("reportSupportUploadButton").disabled,
        "upload report requires a separate explicit assertion");
    field("supportUploadReportedComplete").checked = true;
    field("supportUploadReportedComplete").dispatchEvent(new Event("change", { bubbles: true }));
    uploadReportResponse = response({ error: "temporary" }, 503);
    field("reportSupportUploadButton").click();
    await wait(() => field("supportUploadReportMessage").textContent.includes("not recorded"),
        "upload report failure recovery");
    ok(field("supportUploadReportedComplete").checked &&
        !field("reportSupportUploadButton").disabled,
        "failed report preserves the assertion and permits retry");
    uploadReportResponse = response({ workflow_state: "upload_reported_complete" });
    field("reportSupportUploadButton").click();
    await wait(() => field("supportUploadReportMessage").textContent.includes("You reported"),
        "truthful upload reported state");
    ok(field("supportUploadReportMessage").textContent.includes("not maintainer confirmation"),
        "user report must remain distinct from maintainer confirmation");
    ok(field("supportUploadReportedComplete").disabled &&
        field("reportSupportUploadButton").classList.contains("d-none"),
        "completed report is idempotently settled in the UI");
    ok(!field("supportGithubContinuationPanel").classList.contains("d-none"),
        "accepted upload report reveals the GitHub continuation");
    equal(field("openSupportGithubIssueButton").getAttribute("href"),
        "https://github.com/WsprryPi/WsprryPi/issues/414",
        "existing issue link is fixed to the validated WsprryPi issue");
    ok(field("supportGithubComment").value.includes("Case ID: A7K3-M9QF-X2DP") &&
        !field("supportGithubComment").value.includes("secret-capability"),
        "prepared public comment contains correlation but no transfer capability");
    const refreshModal = field("confirmModal");
    if (refreshModal.classList.contains("show") &&
        field("confirmModalLabel").textContent.trim() === "UI refresh required") {
        field("confirmCancelBtn").click();
        await wait(() => !refreshModal.classList.contains("show") &&
            !document.body.classList.contains("modal-open") &&
            !document.querySelector(".modal-backdrop"),
        "UI refresh prompt cleanup before focus recovery");
    }
    field("supportGithubComment").focus();
    equal(document.activeElement && document.activeElement.id, "supportGithubComment",
        "visible public comment can receive keyboard focus");
    field("copySupportGithubCommentButton").click();
    await wait(() => field("supportGithubCopyStatus").textContent.includes("copied"),
        "clipboard success status");
    equal(copiedText, field("supportGithubComment").value,
        "clipboard receives exactly the reviewed public comment");
    navigator.clipboard.writeText = async () => { throw new Error("denied"); };
    field("copySupportGithubCommentButton").click();
    await wait(() => field("supportGithubCopyStatus").textContent.includes("selected"),
        "clipboard failure recovery");
    equal(document.activeElement && document.activeElement.id, "supportGithubComment",
        "clipboard failure focuses the selectable public comment");
    equal(field("supportGithubComment").selectionStart, 0,
        "clipboard failure selects the public comment from its first character");
    equal(field("supportGithubComment").selectionEnd, field("supportGithubComment").value.length,
        "clipboard failure selects the complete public comment");
    field("downloadSupportReceiptButton").click();
    await wait(() => field("supportEncryptionMessage").textContent.includes("Receipt downloaded"),
        "receipt download state");

    intakeResponse = response({
        status: "disabled",
        generation: 33,
        expires_at: "2026-10-02T00:00:00Z",
        signing_key_id: "wsprrypi-intake-2026-01",
        bundle_key_id: "wsprrypi-bundle-2026-01",
        user_message: "Maintenance window."
    });
    field("checkSupportIntakeButton").click();
    await wait(() => field("supportIntakeMessage").textContent.includes("temporarily disabled"), "disabled state");
    ok(field("supportEncryptionPanel").classList.contains("d-none"),
        "non-active intake must revoke encryption authorization");
    ok(field("supportDropboxHandoffPanel").classList.contains("d-none") &&
        !field("supportDropboxHandoffConsent").checked,
        "non-active intake must revoke handoff authorization");
    ok(!field("openSupportDropboxButton").hasAttribute("href"),
        "non-active intake must remove the local handoff target");
    ok(!field("supportUploadReportPanel").classList.contains("d-none"),
        "later intake disablement must not erase an already recorded user report");

    intakeResponse = response({
        status: "upgrade_required",
        minimum_upload_version: "1.4.0",
        release_url: "https://github.com/WsprryPi/WsprryPi/releases/latest",
        user_message: "A newer upload protocol is required."
    });
    field("checkSupportIntakeButton").click();
    await wait(() => field("supportIntakeMessage").textContent.includes("1.4.0"), "upgrade state");
    equal(field("supportIntakeUpgradeLink").getAttribute("href"),
        "https://github.com/WsprryPi/WsprryPi/releases/latest", "authenticated release link");

    intakeResponse = response({ status: "active", request_url: "https://evil.invalid/request/x" });
    field("checkSupportIntakeButton").click();
    await wait(() => field("checkSupportIntakeButton").textContent === "Try again", "malformed fallback");
    ok(field("supportIntakeUpgradeLink").classList.contains("d-none"), "failure clears prior release link");
    ok(field("supportIntakeSignedMessage").classList.contains("d-none"), "failure clears prior signed message");

    intakeResponse = response({
        status: "upgrade_required",
        minimum_upload_version: "01.4.0",
        release_url: "https://github.com/WsprryPi/WsprryPi/releases.evil/latest",
        unexpected: "must fail closed"
    });
    field("checkSupportIntakeButton").click();
    await wait(() => field("checkSupportIntakeButton").textContent === "Try again", "unsafe upgrade fallback");
    ok(!field("supportIntakeUpgradeLink").hasAttribute("href"), "unsafe release URL must be erased");

    intakeResponse = response({ status: "unavailable" }, 503);
    field("checkSupportIntakeButton").click();
    await wait(() => field("supportIntakeMessage").textContent.includes("could not be checked"), "503 unavailable state");
    deleteResponse = response({}, 503);
    field("deleteSupportBundleButton").click();
    await wait(() => field("supportBundleStatus").dataset.state === "cleanup-pending", "cleanup failure is not deletion");
    ok(!field("supportIntakePanel").classList.contains("d-none"), "failed cleanup retains workflow state");
    ok(field("supportBundleStatus").textContent.includes("24 hours"), "cleanup failure states bounded retention");
    deleteResponse = response({});
    field("deleteSupportBundleButton").click();
    await wait(() => field("supportIntakePanel").classList.contains("d-none"), "delete reset");
    equal(field("checkSupportIntakeButton").textContent, "Check private upload availability",
        "reset restores initial action");
    ok(field("supportUploadReportPanel").classList.contains("d-none") &&
        !field("supportUploadReportedComplete").checked,
        "delete resets the upload-report workflow");
    ok(field("supportGithubContinuationPanel").classList.contains("d-none") &&
        !field("openSupportGithubIssueButton").hasAttribute("href") &&
        !field("createSupportGithubIssueButton").hasAttribute("href"),
        "delete clears every GitHub continuation target");

    async function completeAdditionalWorkflow(kind, description, contact) {
        intakeResponse = response({
            status: "active",
            generation: 34,
            expires_at: "2026-10-03T00:00:00Z",
            minimum_upload_version: "1.3.0",
            signing_key_id: "wsprrypi-intake-2026-01",
            bundle_key_id: "wsprrypi-bundle-2026-01",
            request_url: "https://www.dropbox.com/request/secret-capability"
        });
        field("createSupportBundleButton").click();
        const selector = document.querySelector(`input[name="supportBundleContextKind"][value="${kind}"]`);
        selector.checked = true;
        selector.dispatchEvent(new Event("change", { bubbles: true }));
        field("supportBundleProblemDescription").value = description;
        field("supportBundleContact").value = contact;
        field("supportBundleProbeI2c").checked = true;
        field("confirmCreateSupportBundleButton").click();
        equal(JSON.parse(calls.at(-1).body).probe_i2c, true, "explicit active-probe consent is sent");
        await wait(() => field("downloadSupportBundleButton").dataset.available === "true",
            `${kind} candidate ready`, 6000);
        field("downloadSupportBundleButton").click();
        await wait(() => !field("supportBundleReviewConsent").classList.contains("d-none"),
            `${kind} review consent`);
        field("supportBundleReviewed").checked = true;
        field("supportBundleReviewed").dispatchEvent(new Event("change", { bubbles: true }));
        field("finalizeSupportBundleButton").click();
        await wait(() => !field("supportIntakePanel").classList.contains("d-none"),
            `${kind} finalized`);
        field("checkSupportIntakeButton").click();
        await wait(() => !field("supportEncryptionPanel").classList.contains("d-none"),
            `${kind} intake active`);
        field("supportEncryptionConsent").checked = true;
        field("supportEncryptionConsent").dispatchEvent(new Event("change", { bubbles: true }));
        field("encryptSupportBundleButton").click();
        await wait(() => !field("downloadEncryptedSupportBundleButton").classList.contains("d-none"),
            `${kind} encrypted`);
        field("downloadEncryptedSupportBundleButton").click();
        await wait(() => !field("supportDropboxHandoffPanel").classList.contains("d-none"),
            `${kind} handoff`);
        field("supportDropboxHandoffConsent").checked = true;
        field("supportDropboxHandoffConsent").dispatchEvent(new Event("change", { bubbles: true }));
        field("openSupportDropboxButton").addEventListener("click", (event) => event.preventDefault(),
            { once: true });
        field("openSupportDropboxButton").dispatchEvent(
            new MouseEvent("click", { bubbles: true, cancelable: true }));
        field("supportUploadReportedComplete").checked = true;
        field("supportUploadReportedComplete").dispatchEvent(new Event("change", { bubbles: true }));
        field("reportSupportUploadButton").click();
        await wait(() => !field("supportGithubContinuationPanel").classList.contains("d-none"),
            `${kind} continuation`);
    }

    const privateDescription = "Private callsign K0ABC at EM10 with 192.168.1.5";
    const privateContact = "private.operator@example.invalid";
    await completeAdditionalWorkflow("new_github_issue", privateDescription, privateContact);
    const newIssueHref = field("createSupportGithubIssueButton").href;
    ok(newIssueHref.startsWith("https://github.com/WsprryPi/WsprryPi/issues/new?"),
        "new issue uses the fixed WsprryPi issue composer");
    ok(newIssueHref.includes("A7K3-M9QF-X2DP") &&
        !newIssueHref.includes("K0ABC") && !newIssueHref.includes("EM10") &&
        !newIssueHref.includes("192.168.1.5") && !newIssueHref.includes("private.operator"),
        "new issue URL includes correlation but excludes private context");
    field("deleteSupportBundleButton").click();
    await wait(() => field("supportGithubContinuationPanel").classList.contains("d-none"),
        "new issue workflow reset");

    await completeAdditionalWorkflow("no_github", privateDescription, privateContact);
    ok(field("supportGithubContinuationMessage").textContent.includes("No public GitHub issue is required") &&
        field("supportGithubExistingIssueActions").classList.contains("d-none") &&
        field("supportGithubNewIssueActions").classList.contains("d-none"),
        "non-GitHub path stays private and offers no public action");
    field("deleteSupportBundleButton").click();
    await wait(() => field("supportGithubContinuationPanel").classList.contains("d-none"),
        "non-GitHub workflow reset");

    return { scenarios: 21, intakeCalls: 8, assertions: "passed" };
}

async function dismissUiRefreshPrompt(client) {
    const dismissed = await client.send("Runtime.evaluate", {
        expression: `(() => {
            const modal = document.getElementById("confirmModal");
            const heading = document.getElementById("confirmModalLabel");
            if (modal && modal.classList.contains("show") && heading &&
                heading.textContent.trim() === "UI refresh required") {
                document.getElementById("confirmCancelBtn").click();
            }
        })()`,
    });
    if (dismissed.exceptionDetails) {
        const detail = dismissed.exceptionDetails.exception && dismissed.exceptionDetails.exception.description;
        throw new Error(detail || dismissed.exceptionDetails.text || "UI refresh prompt dismissal failed");
    }
    await waitFor(async () => {
        const result = await client.send("Runtime.evaluate", {
            expression: `(() => {
                const modal = document.getElementById("confirmModal");
                return (!modal || !modal.classList.contains("show")) &&
                    !document.body.classList.contains("modal-open") &&
                    !document.querySelector(".modal-backdrop");
            })()`,
            returnByValue: true,
        });
        return result.result.value === true;
    }, "UI refresh prompt to close");
}

async function capture(client, outputPath, width, height, state, theme = "light") {
    await dismissUiRefreshPrompt(client);
    await client.send("Emulation.setDeviceMetricsOverride", {
        width, height, deviceScaleFactor: 1, mobile: width < 600,
    });
    const rendered = await client.send("Runtime.evaluate", {
        expression: `(() => {
            document.documentElement.setAttribute("data-bs-theme", ${JSON.stringify(theme)});
            document.getElementById("supportBundleSetup").classList.add("d-none");
            document.getElementById("supportBundleReview").classList.remove("d-none");
            document.getElementById("supportBundleCaseId").textContent = "A7K3-M9QF-X2DP";
            document.getElementById("supportIntakePanel").classList.remove("d-none");
            document.getElementById("createSupportBundleButton").classList.add("d-none");
            document.getElementById("supportBundleStatus").dataset.state = "finalized";
            document.getElementById("supportBundleStatus").textContent = "Reviewed candidate finalized. No file has been uploaded.";
            document.getElementById("supportEncryptionPanel").classList.add("d-none");
            document.getElementById("supportDropboxHandoffPanel").classList.add("d-none");
            document.getElementById("supportUploadReportPanel").classList.add("d-none");
            document.getElementById("supportGithubContinuationPanel").classList.add("d-none");
            const state = ${JSON.stringify(state)};
            const message = document.getElementById("supportIntakeMessage");
            const button = document.getElementById("checkSupportIntakeButton");
            const link = document.getElementById("supportIntakeUpgradeLink");
            button.disabled = false;
            link.classList.add("d-none");
            if (state === "active" || state === "reported") {
                message.textContent = "Private upload is available until Oct 1, 2026, 12:00 AM UTC. No file has been uploaded.";
                button.textContent = "Check again";
                document.getElementById("supportEncryptionPanel").classList.remove("d-none");
                document.getElementById("supportEncryptionConsent").checked = true;
                document.getElementById("supportEncryptionConsent").disabled = false;
                document.getElementById("encryptSupportBundleButton").classList.remove("d-none");
                document.getElementById("encryptSupportBundleButton").disabled = false;
                document.getElementById("supportDropboxHandoffPanel").classList.remove("d-none");
                document.getElementById("supportDropboxHandoffConsent").checked = true;
                const handoff = document.getElementById("openSupportDropboxButton");
                handoff.classList.remove("disabled");
                handoff.setAttribute("aria-disabled", "false");
                handoff.tabIndex = 0;
                if (state === "reported") {
                    document.getElementById("supportEncryptionPanel").classList.add("d-none");
                    document.getElementById("supportDropboxHandoffPanel").classList.add("d-none");
                    const report = document.getElementById("supportUploadReportPanel");
                    report.classList.remove("d-none");
                    document.getElementById("supportUploadReportMessage").textContent =
                        "You reported that Dropbox displayed upload success for case A7K3-M9QF-X2DP. This is not maintainer confirmation. The readable archive remains on this Pi until you delete it or it expires.";
                    document.getElementById("supportUploadReportedComplete").checked = true;
                    document.getElementById("supportUploadReportedComplete").disabled = true;
                    document.getElementById("reportSupportUploadButton").classList.add("d-none");
                    const github = document.getElementById("supportGithubContinuationPanel");
                    github.classList.remove("d-none");
                    document.getElementById("supportGithubContinuationMessage").textContent =
                        "GitHub cannot be updated automatically. Sign in, then post the prepared case note yourself.";
                    document.getElementById("supportGithubExistingIssueActions").classList.remove("d-none");
                    document.getElementById("supportGithubComment").value =
                        "A support bundle was generated and uploaded through the private support channel.\\n\\nCase ID: A7K3-M9QF-X2DP\\n\\nNo diagnostic bundle or transfer link is attached to this public comment.";
                    document.getElementById("openSupportGithubIssueButton").href =
                        "https://github.com/WsprryPi/WsprryPi/issues/414";
                }
            } else if (state === "upgrade") {
                document.getElementById("supportEncryptionPanel").classList.add("d-none");
                message.textContent = "Upgrade to WsprryPi 1.4.0 or later before uploading. Your local bundle is unchanged.";
                button.textContent = "Check again";
                link.href = "https://github.com/WsprryPi/WsprryPi/releases/latest";
                link.classList.remove("d-none");
            } else if (state === "loading") {
                document.getElementById("supportBundleReview").classList.add("d-none");
                message.textContent = "Checking private upload availability…";
                button.textContent = "Check private upload availability";
                button.disabled = true;
            } else {
                message.textContent = "Private upload availability could not be checked. Your local bundle is unchanged. Try again.";
                button.textContent = "Try again";
            }
            document.getElementById(state === "reported" ? "supportGithubContinuationPanel" :
                state === "active" ? "supportDropboxHandoffPanel" :
                    "supportIntakePanel").scrollIntoView({ block: "center", behavior: "instant" });
        })()`,
    });
    if (rendered.exceptionDetails) {
        const detail = rendered.exceptionDetails.exception && rendered.exceptionDetails.exception.description;
        throw new Error(detail || rendered.exceptionDetails.text || "Screenshot state rendering failed");
    }
    await client.settleRendering();
    const screenshot = await client.send("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    fs.writeFileSync(outputPath, screenshot.data, "base64");
}

async function main() {
    let page;
    const phpPort = await freePort();
    const profileDir = createBrowserProfile();
    let chromium;
    let client, php;
    let primaryError;
    try {
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${phpPort}`, "-t", "data", "tests/browser_fixture_router.php"], {
            cwd: UI_ROOT,
        });
        await waitForSpawn(php);
        await waitForPhp(php, `http://127.0.0.1:${phpPort}/index.php?page=maintenance`, "Maintenance fixture", 12000);
        ({browser: chromium, client, page} = await launchBrowser({executable: chromiumBinary(), args: [
            "--headless", "--no-sandbox", "--disable-gpu",
            ], profile: profileDir, timeoutMs: 12000}));
        await client.send("Page.navigate", {url: `http://127.0.0.1:${phpPort}/index.php?page=maintenance`});
        await client.send("Page.bringToFront");
        await waitFor(async () => {
            const result = await client.send("Runtime.evaluate", {
                expression: "document.hasFocus()",
                returnByValue: true,
            });
            return result.result.value === true;
        }, "Maintenance page to receive browser focus");
        await waitFor(async () => {
            const result = await client.send("Runtime.evaluate", {
                expression: "document.readyState === 'complete' && typeof SUPPORT_INTAKE_ENDPOINT === 'object' && window.PicoFirmwareSafety?.locked === false",
                returnByValue: true,
            });
            return result.result.value === true;
        }, "Maintenance scripts and idle host firmware status");
        await dismissUiRefreshPrompt(client);
        console.log(await client.evaluate(`(${networkSafetyBehavior.toString()})()`));
        const result = await client.send("Runtime.evaluate", {
            expression: `(${browserTest.toString()})()`, awaitPromise: true, returnByValue: true,
        });
        if (result.exceptionDetails) {
            const detail = result.exceptionDetails.exception && result.exceptionDetails.exception.description;
            throw new Error(detail || result.exceptionDetails.text || "Browser test failed");
        }
        assert.deepEqual(result.result.value, { scenarios: 21, intakeCalls: 8, assertions: "passed" });
        if (process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR) {
            fs.mkdirSync(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR, { recursive: true });
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-intake-active-desktop.png"), 1440, 1100, "active");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-intake-handoff-mobile.png"), 390, 844, "active");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-upload-reported-desktop.png"), 1440, 1100, "reported");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-upload-reported-mobile.png"), 390, 844, "reported");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-upload-reported-dark-desktop.png"), 1440, 1100, "reported", "dark");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-intake-upgrade-desktop.png"), 1440, 1100, "upgrade");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-intake-loading-mobile.png"), 390, 844, "loading");
            await capture(client, path.join(process.env.WSPRRYPI_SUPPORT_INTAKE_SCREENSHOT_DIR,
                "support-intake-unavailable-mobile.png"), 390, 844, "unavailable");
        }
        console.log("support_intake_ui_integration_test passed");
    } catch (error) {
        primaryError = error;
    } finally {
        await finishFixture({failure: primaryError, client, browser: chromium, php, profile: profileDir});
    }
}

if (require.main === module) {
    main().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}

module.exports = { preferredHarnessError, removeProfileDirectory };

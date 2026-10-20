const overlay = document.getElementById("maintenanceOverlay");

function lockUI() {
    overlay.classList.remove("d-none");
}

function unlockUI() {
    overlay.classList.add("d-none");
}

document.addEventListener("DOMContentLoaded", () => {
    bindTestToneControls();

    const repairButton = document.getElementById("repairConfigButton");
    const restoreButton = document.getElementById("restoreConfigButton");
    const globalToastContainer =
        document.getElementById("globalToastContainer");
    const resultPanel = document.getElementById("maintenanceResult");
    const resultTitle = document.getElementById("maintenanceResultTitle");
    const resultBody = document.getElementById("maintenanceResultBody");
    const networkSafetyBanner = document.getElementById("networkSafetyBanner");
    const networkSafetyRequested = document.getElementById("networkSafetyRequested");
    const networkSafetyConfigured = document.getElementById("networkSafetyConfigured");
    const networkSafetyActive = document.getElementById("networkSafetyActive");
    const networkSafetyEnforced = document.getElementById("networkSafetyEnforced");
    const networkSafetyDisabled = document.getElementById("networkSafetyDisabled");
    const networkSafetyDisableConfirmation = document.getElementById("networkSafetyDisableConfirmation");
    const networkSafetyDisablePhrase = document.getElementById("networkSafetyDisablePhrase");
    const networkSafetyPhraseStatus = document.getElementById("networkSafetyPhraseStatus");
    const networkSafetyResult = document.getElementById("networkSafetyResult");
    const applyNetworkSafetyButton = document.getElementById("applyNetworkSafetyButton");
    const cancelNetworkSafetyButton = document.getElementById("cancelNetworkSafetyButton");
    const NETWORK_SAFETY_DISABLE_PHRASE = "DISABLE LOCAL-LAN SAFETY";
    let networkSafetyInFlight = false;

    function selectedNetworkSafetyMode() {
        return document.querySelector('input[name="networkSafetyMode"]:checked')?.value || "";
    }

    function networkSafetyModeLabel(mode) {
        if (mode === "enforced") return "Enforced";
        if (mode === "insecure-disabled") return "Insecure disabled";
        return "Unknown";
    }

    function setNetworkSafetyResult(state, message) {
        networkSafetyResult.dataset.state = state;
        networkSafetyResult.textContent = message;
        networkSafetyResult.classList.remove("d-none");
    }

    function updateNetworkSafetyDraft() {
        const requestedMode = selectedNetworkSafetyMode();
        const disabling = requestedMode === "insecure-disabled";
        const phraseMatches = networkSafetyDisablePhrase.value === NETWORK_SAFETY_DISABLE_PHRASE;
        networkSafetyRequested.textContent = requestedMode
            ? networkSafetyModeLabel(requestedMode)
            : "No change selected";
        networkSafetyDisableConfirmation.classList.toggle("d-none", !disabling);
        cancelNetworkSafetyButton.classList.toggle("d-none", !requestedMode);
        applyNetworkSafetyButton.disabled = networkSafetyInFlight || !requestedMode ||
            (disabling && !phraseMatches);
        networkSafetyEnforced.disabled = networkSafetyInFlight;
        networkSafetyDisabled.disabled = networkSafetyInFlight;
        networkSafetyDisablePhrase.disabled = networkSafetyInFlight;
        cancelNetworkSafetyButton.disabled = networkSafetyInFlight;
        networkSafetyDisablePhrase.setAttribute(
            "aria-invalid",
            disabling && networkSafetyDisablePhrase.value !== "" && !phraseMatches ? "true" : "false"
        );
        networkSafetyPhraseStatus.textContent = networkSafetyInFlight
            ? "Applying the requested state. Controls are locked until validation finishes."
            : phraseMatches
                ? "Confirmation phrase matches. Ready to apply."
                : "Enter the exact phrase to unlock Apply.";
        applyNetworkSafetyButton.classList.toggle("btn-danger", disabling);
        applyNetworkSafetyButton.classList.toggle("btn-primary", !disabling);
    }

    function renderNetworkSafetyState(data) {
        const configured = data.configured_known ? networkSafetyModeLabel(data.configured) : "Unknown";
        const active = data.active_known ? networkSafetyModeLabel(data.active) : "Unknown";
        networkSafetyConfigured.textContent = configured;
        networkSafetyActive.textContent = active;
        networkSafetyBanner.textContent = typeof data.status === "string"
            ? data.status
            : "NETWORK SAFETY STATE UNKNOWN";
        networkSafetyBanner.dataset.state = data.active === "insecure-disabled" && data.active_known
            ? "off"
            : data.active === "enforced" && data.active_known
                ? "enforced"
                : "unknown";
    }

    async function fetchNetworkSafetyStatus(options = {}) {
        try {
            const response = await fetch(NETWORK_SAFETY_ENDPOINT.proxyUrl, {
                method: "GET",
                cache: "no-store"
            });
            const data = await response.json();
            if (!response.ok) throw new Error(data.message || "The status request failed.");
            renderNetworkSafetyState(data);
        } catch {
            renderNetworkSafetyState({
                configured_known: false,
                active_known: false,
                status: "NETWORK SAFETY STATE UNKNOWN"
            });
            if (options.reportFailure !== false) {
                setNetworkSafetyResult(
                    "danger",
                    "Network safety status could not be read through Apache. Verify the browser is using the normal port-80 site, then reload this page."
                );
            }
        }
    }

    async function applyNetworkSafety() {
        const requestedMode = selectedNetworkSafetyMode();
        if (!requestedMode || networkSafetyInFlight) return;
        if (requestedMode === "insecure-disabled" &&
            networkSafetyDisablePhrase.value !== NETWORK_SAFETY_DISABLE_PHRASE) return;

        networkSafetyInFlight = true;
        updateNetworkSafetyDraft();
        applyNetworkSafetyButton.dataset.originalText = applyNetworkSafetyButton.textContent;
        applyNetworkSafetyButton.textContent = "Applying and validating…";
        networkSafetyResult.classList.add("d-none");

        try {
            const response = await fetch(NETWORK_SAFETY_ENDPOINT.proxyUrl, {
                method: "POST",
                cache: "no-store",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ mode: requestedMode })
            });
            let data = null;
            try {
                data = await response.json();
            } catch {
                data = null;
            }
            if (!response.ok || !data || data.applied !== true) {
                const detail = data && typeof data.message === "string"
                    ? data.message
                    : "The requested state was not applied.";
                throw new Error(detail);
            }

            renderNetworkSafetyState(data);
            setNetworkSafetyResult(
                "success",
                requestedMode === "enforced"
                    ? "Network safety is active. Privileged browser operation is restricted to the directly connected LAN."
                    : "NETWORK SAFETY OFF. The insecure override is active."
            );
            networkSafetyEnforced.checked = false;
            networkSafetyDisabled.checked = false;
            networkSafetyDisablePhrase.value = "";
        } catch (error) {
            const detail = error && typeof error.message === "string" && error.message.trim()
                ? error.message.trim()
                : "The request could not be completed.";
            setNetworkSafetyResult(
                "danger",
                `${detail} The requested, configured, and active states may differ. Your selection is preserved; verify the current state before trying again.`
            );
            await fetchNetworkSafetyStatus({ reportFailure: false });
        } finally {
            networkSafetyInFlight = false;
            applyNetworkSafetyButton.textContent = applyNetworkSafetyButton.dataset.originalText || "Apply requested state";
            delete applyNetworkSafetyButton.dataset.originalText;
            updateNetworkSafetyDraft();
        }
    }

    document.querySelectorAll('input[name="networkSafetyMode"]').forEach((control) => {
        control.addEventListener("change", updateNetworkSafetyDraft);
    });
    networkSafetyDisablePhrase.addEventListener("input", updateNetworkSafetyDraft);
    applyNetworkSafetyButton.addEventListener("click", applyNetworkSafety);
    cancelNetworkSafetyButton.addEventListener("click", () => {
        networkSafetyEnforced.checked = false;
        networkSafetyDisabled.checked = false;
        networkSafetyDisablePhrase.value = "";
        networkSafetyResult.classList.add("d-none");
        updateNetworkSafetyDraft();
    });
    updateNetworkSafetyDraft();
    fetchNetworkSafetyStatus();

    /**
     * Removes any existing toasts from the global container.
     *
     * @returns {void}
     */
    function clearToasts() {
        globalToastContainer.replaceChildren();
    }

    /**
     * Builds and shows a Bootstrap toast in the global container.
     *
     * The toast includes a centered OK button and auto-hides after
     * 10 seconds.
     *
     * @param {string} message Toast message text
     * @param {string} type Bootstrap semantic type
     * @returns {void}
     */
    function showToast(message, type) {
        clearToasts();

        const typeMap = {
            success: "border-success text-success",
            danger: "border-danger text-danger",
            warning: "border-warning text-warning",
            info: "border-info text-info"
        };

        const classes = typeMap[type] || typeMap.info;

        const toastElement = document.createElement("div");
        toastElement.className = `toast ${classes} border shadow-sm bg-body text-body`;
        toastElement.setAttribute("role", "alert");
        toastElement.setAttribute("aria-live", "assertive");
        toastElement.setAttribute("aria-atomic", "true");

        const toastBody = document.createElement("div");
        toastBody.className = "toast-body text-center";

        const messageBlock = document.createElement("div");
        messageBlock.className = "mb-3";
        messageBlock.textContent = message;

        const actionRow = document.createElement("div");
        actionRow.className = "d-flex justify-content-center";

        const okButton = document.createElement("button");
        okButton.type = "button";
        okButton.className = "btn btn-outline-secondary maintenance-toast-ok";
        okButton.textContent = "OK";

        actionRow.appendChild(okButton);
        toastBody.append(messageBlock, actionRow);
        toastElement.appendChild(toastBody);
        globalToastContainer.appendChild(toastElement);

        const toast = new bootstrap.Toast(toastElement, {
            autohide: true,
            delay: 10000
        });

        okButton.addEventListener("click", () => {
            toast.hide();
        });

        toastElement.addEventListener("hidden.bs.toast", () => {
            toastElement.remove();
            unlockUI();
        });

        lockUI();
        toast.show();
    }

    /**
     * Sets both maintenance buttons enabled or disabled.
     *
     * @param {boolean} disabled True to disable buttons
     * @returns {void}
     */
    function setButtonsDisabled(disabled) {
        repairButton.disabled = disabled;
        restoreButton.disabled = disabled;
    }

    function showResult(state, title, body) {
        resultPanel.dataset.state = state;
        resultTitle.textContent = title;
        resultBody.textContent = body;
        resultPanel.classList.remove("d-none");
    }

    /**
     * Sets a temporary working label on the active button.
     *
     * @param {HTMLButtonElement} button Button being acted on
     * @param {string} text Temporary text
     * @returns {void}
     */
    function setButtonWorkingState(button, text) {
        button.dataset.originalText = button.textContent;
        button.textContent = text;
    }

    /**
     * Restores the original label on the provided button.
     *
     * @param {HTMLButtonElement} button Button to restore
     * @returns {void}
     */
    function restoreButtonText(button) {
        if (button.dataset.originalText) {
            button.textContent = button.dataset.originalText;
            delete button.dataset.originalText;
        }
    }

    /**
     * Attempts to reload the current configuration from the backend.
     *
     * @returns {Promise<boolean>} True if reload succeeded
     */
    async function refreshConfig() {
        try {
            const response = await fetchWithEndpointFallback(SETTINGS_ENDPOINT, {
                method: "GET",
                cache: "no-store"
            });

            if (!response.ok) {
                return false;
            }

            await response.json();
            return true;
        } catch {
            return false;
        }
    }

    /**
     * Sends a repair or restore request to the backend.
     *
     * @param {string} verb Requested backend verb
     * @param {HTMLButtonElement} button Button that initiated the request
     * @returns {Promise<void>}
     */
    async function postRepairVerb(verb, button) {
        const isRepair = verb === "repair";

        const workingText = isRepair
            ? "Repairing Configuration..."
            : "Restoring to Stock...";
        const successTitle = isRepair
            ? "Configuration repaired"
            : "Configuration reset to stock defaults";
        const successText = isRepair
            ? "Review the Setup page now. Confirm the repaired settings, then save any adjustments you still need."
            : "Review station, transmit mode, and hardware settings now. The stock baseline is loaded, but you still need to save your intended operating values.";
        const warningText = isRepair
            ? "The repair completed, but the updated configuration could not be reloaded automatically. Reload the page and verify the repaired values before transmitting."
            : "The reset completed, but the stock configuration could not be reloaded automatically. Reload the page before making further changes.";
        const fallbackFailureText = isRepair
            ? "Configuration repair failed."
            : "Configuration reset failed.";

        showToast("Processing request...", "info");
        showResult(
            "warning",
            isRepair ? "Repair in progress" : "Reset in progress",
            isRepair
                ? "The UI is checking the configuration and writing repaired values where possible."
                : "The UI is replacing the current configuration with the stock baseline."
        );
        setButtonWorkingState(button, workingText);
        setButtonsDisabled(true);

        try {
            const response = await fetchWithEndpointFallback(REPAIR_ENDPOINT, {
                method: "POST",
                headers: {
                    "Content-Type": "application/json"
                },
                body: JSON.stringify({ verb })
            });

            let data = null;

            try {
                data = await response.json();
            } catch {
                data = null;
            }

            if (!response.ok) {
                const message =
                    data && data.message
                        ? data.message
                        : fallbackFailureText;

                showToast(message, "danger");
                showResult(
                    "danger",
                    isRepair ? "Repair did not complete" : "Reset did not complete",
                    `${message} No further changes were applied. Review the current configuration before trying again.`
                );
                return;
            }

            const configReloaded = await refreshConfig();

            if (configReloaded) {
                showToast(successTitle, "success");
                showResult("success", successTitle, successText);
            } else {
                showToast(
                    `${successTitle}. The updated configuration could not be reloaded.`,
                    "warning"
                );
                showResult("warning", successTitle, warningText);
            }
        } catch {
            showToast(
                "Unable to contact the server for the configuration operation.",
                "danger"
            );
            showResult(
                "danger",
                isRepair ? "Repair could not reach the server" : "Reset could not reach the server",
                "The request did not complete. Leave the current configuration unchanged, verify controller connectivity, then try again."
            );
        } finally {
            restoreButtonText(button);
            setButtonsDisabled(false);
        }
    }

    repairButton.addEventListener("click", () => {
        postRepairVerb("repair", repairButton);
    });

    restoreButton.addEventListener("click", () => {
        if (typeof showConfirmationDialog === "function") {
            showConfirmationDialog({
                title: "Reset configuration",
                message:
                    "Restore configuration to stock defaults? This will replace the current configuration.",
                confirmLabel: "Reset to defaults",
                confirmClass: "btn-danger",
                onConfirm: () => {
                    postRepairVerb("restore", restoreButton);
                }
            });
        } else {
            postRepairVerb("restore", restoreButton);
        }
    });

    const createSupportBundleButton = document.getElementById("createSupportBundleButton");
    const confirmCreateSupportBundleButton = document.getElementById("confirmCreateSupportBundleButton");
    const cancelCreateSupportBundleButton = document.getElementById("cancelCreateSupportBundleButton");
    const downloadSupportBundleButton = document.getElementById("downloadSupportBundleButton");
    const finalizeSupportBundleButton = document.getElementById("finalizeSupportBundleButton");
    const deleteSupportBundleButton = document.getElementById("deleteSupportBundleButton");
    const supportBundleProbeI2c = document.getElementById("supportBundleProbeI2c");
    const supportBundleStatus = document.getElementById("supportBundleStatus");
    const supportBundleAlert = document.getElementById("supportBundleAlert");
    const supportBundleSetup = document.getElementById("supportBundleSetup");
    const supportBundleReview = document.getElementById("supportBundleReview");
    const supportBundleCaseId = document.getElementById("supportBundleCaseId");
    const supportBundleReviewConsent = document.getElementById("supportBundleReviewConsent");
    const supportBundleReviewed = document.getElementById("supportBundleReviewed");
    const supportBundleExistingIssueFields = document.getElementById("supportBundleExistingIssueFields");
    const supportBundleDescriptionFields = document.getElementById("supportBundleDescriptionFields");
    const supportBundleIssueNumber = document.getElementById("supportBundleIssueNumber");
    const supportBundleProblemDescription = document.getElementById("supportBundleProblemDescription");
    const supportBundleContact = document.getElementById("supportBundleContact");
    const supportBundleIssueNumberError = document.getElementById("supportBundleIssueNumberError");
    const supportBundleProblemDescriptionError = document.getElementById("supportBundleProblemDescriptionError");
    const supportBundleContactError = document.getElementById("supportBundleContactError");
    const supportIntakePanel = document.getElementById("supportIntakePanel");
    const supportIntakeMessage = document.getElementById("supportIntakeMessage");
    const supportIntakeSignedMessage = document.getElementById("supportIntakeSignedMessage");
    const checkSupportIntakeButton = document.getElementById("checkSupportIntakeButton");
    const supportIntakeUpgradeLink = document.getElementById("supportIntakeUpgradeLink");
    const supportEncryptionPanel = document.getElementById("supportEncryptionPanel");
    const supportEncryptionMessage = document.getElementById("supportEncryptionMessage");
    const supportEncryptionConsent = document.getElementById("supportEncryptionConsent");
    const encryptSupportBundleButton = document.getElementById("encryptSupportBundleButton");
    const downloadEncryptedSupportBundleButton = document.getElementById("downloadEncryptedSupportBundleButton");
    const downloadSupportReceiptButton = document.getElementById("downloadSupportReceiptButton");
    const supportDropboxHandoffPanel = document.getElementById("supportDropboxHandoffPanel");
    const supportDropboxHandoffConsent = document.getElementById("supportDropboxHandoffConsent");
    const openSupportDropboxButton = document.getElementById("openSupportDropboxButton");
    const supportUploadReportPanel = document.getElementById("supportUploadReportPanel");
    const supportUploadReportMessage = document.getElementById("supportUploadReportMessage");
    const supportUploadReportedComplete = document.getElementById("supportUploadReportedComplete");
    const reportSupportUploadButton = document.getElementById("reportSupportUploadButton");
    const supportGithubContinuationPanel = document.getElementById("supportGithubContinuationPanel");
    const supportGithubContinuationMessage = document.getElementById("supportGithubContinuationMessage");
    const supportGithubExistingIssueActions = document.getElementById("supportGithubExistingIssueActions");
    const supportGithubNewIssueActions = document.getElementById("supportGithubNewIssueActions");
    const supportGithubComment = document.getElementById("supportGithubComment");
    const supportGithubCopyStatus = document.getElementById("supportGithubCopyStatus");
    const copySupportGithubCommentButton = document.getElementById("copySupportGithubCommentButton");
    const openSupportGithubIssueButton = document.getElementById("openSupportGithubIssueButton");
    const createSupportGithubIssueButton = document.getElementById("createSupportGithubIssueButton");
    const SUPPORT_BUNDLE_POLL_INTERVAL_MS = 2000;
    const SUPPORT_BUNDLE_FILENAME_FALLBACK = "wsprrypi-support-bundle.tar.gz";
    let supportBundleJobId = "";
    let supportBundlePollTimer = null;
    let supportBundleCreateInFlight = false;
    let supportBundleDownloadInFlight = false;
    let supportBundleDeleteInFlight = false;
    let supportBundleFinalizeInFlight = false;
    let supportBundlePageUnloading = false;
    let supportBundleDownloaded = false;
    let supportBundleFinalized = false;
    let supportIntakeInFlight = false;
    let supportEncryptionInFlight = false;
    let supportIntakeActive = false;
    let supportEncryptedDownloaded = false;
    let supportUploadReportInFlight = false;
    let supportUploadReportComplete = false;
    let supportContextKind = "";
    let supportExistingIssueNumber = "";

    function supportBundleEndpoint(suffix = "") {
        return createEndpointDefinition(
            "support bundles",
            `${SUPPORT_BUNDLES_ENDPOINT.proxyUrl}${suffix}`,
            `${SUPPORT_BUNDLES_ENDPOINT.directUrl}${suffix}`
        );
    }

    function stopSupportBundlePolling() {
        if (supportBundlePollTimer !== null) {
            window.clearTimeout(supportBundlePollTimer);
            supportBundlePollTimer = null;
        }
    }

    function setSupportBundleStatus(state, message) {
        supportBundleStatus.dataset.state = state;
        supportBundleStatus.textContent = message;
        supportBundleStatus.classList.remove("visually-hidden");
    }

    function clearSupportBundleAlert() {
        supportBundleAlert.textContent = "";
        supportBundleAlert.classList.add("d-none");
    }

    function showSupportBundleAlert(message) {
        supportBundleAlert.textContent = message;
        supportBundleAlert.classList.remove("d-none");
    }

    function selectedSupportContextKind() {
        return document.querySelector('input[name="supportBundleContextKind"]:checked')?.value ||
            "existing_github_issue";
    }

    function updateSupportContextFields() {
        const existing = selectedSupportContextKind() === "existing_github_issue";
        supportBundleExistingIssueFields.classList.toggle("d-none", !existing);
        supportBundleDescriptionFields.classList.toggle("d-none", existing);
        supportBundleIssueNumber.disabled = !existing;
        supportBundleProblemDescription.disabled = existing;
        supportBundleContact.disabled = existing;
    }

    function clearSupportContextErrors() {
        supportBundleIssueNumberError.textContent = "";
        supportBundleProblemDescriptionError.textContent = "";
        supportBundleContactError.textContent = "";
    }

    function normalizedOneLine(value) {
        return value.replace(/\s+/gu, " ").trim();
    }

    function supportContextPayload() {
        clearSupportContextErrors();
        const kind = selectedSupportContextKind();
        if (kind === "existing_github_issue") {
            const issueNumber = supportBundleIssueNumber.value.trim();
            if (!/^[1-9][0-9]{0,9}$/.test(issueNumber)) {
                supportBundleIssueNumberError.textContent = "Enter a valid WsprryPi issue number.";
                supportBundleIssueNumber.focus();
                return null;
            }
            return {
                kind,
                issue_url: `https://github.com/WsprryPi/WsprryPi/issues/${issueNumber}`
            };
        }
        const problemDescription = normalizedOneLine(supportBundleProblemDescription.value);
        const contact = normalizedOneLine(supportBundleContact.value);
        if (!problemDescription) {
            supportBundleProblemDescriptionError.textContent = "Describe the problem before collecting diagnostics.";
            supportBundleProblemDescription.focus();
            return null;
        }
        if (!contact) {
            supportBundleContactError.textContent = "Provide contact information for maintainer follow-up.";
            supportBundleContact.focus();
            return null;
        }
        return { kind, problem_description: problemDescription, contact };
    }

    function setSupportBundleActions() {
        const hasReadyDownload = supportBundleJobId !== "" &&
            downloadSupportBundleButton.dataset.available === "true";
        createSupportBundleButton.disabled = supportBundleCreateInFlight ||
            supportBundleDownloadInFlight || supportBundleDeleteInFlight ||
            supportBundleFinalizeInFlight ||
            (supportBundleJobId !== "" && supportBundlePollTimer !== null);
        downloadSupportBundleButton.disabled = !hasReadyDownload || supportBundleDownloadInFlight;
        deleteSupportBundleButton.disabled = supportBundleJobId === "" || supportBundleDeleteInFlight;
        finalizeSupportBundleButton.disabled = !supportBundleDownloaded ||
            !supportBundleReviewed.checked || supportBundleFinalizeInFlight || supportBundleFinalized;
        checkSupportIntakeButton.disabled = !supportBundleFinalized || supportIntakeInFlight;
        encryptSupportBundleButton.disabled = !supportEncryptionConsent.checked || supportEncryptionInFlight;
        const handoffEnabled = supportIntakeActive && supportEncryptedDownloaded &&
            supportDropboxHandoffConsent.checked;
        openSupportDropboxButton.classList.toggle("disabled", !handoffEnabled);
        openSupportDropboxButton.setAttribute("aria-disabled", handoffEnabled ? "false" : "true");
        openSupportDropboxButton.tabIndex = handoffEnabled ? 0 : -1;
        reportSupportUploadButton.disabled = !supportUploadReportedComplete.checked ||
            supportUploadReportInFlight || supportUploadReportComplete;
    }

    function clearSupportIntakeState() {
        supportIntakeInFlight = false;
        supportIntakePanel.classList.add("d-none");
        supportIntakeMessage.textContent = "Check whether this version can use the current private support channel.";
        supportIntakeSignedMessage.textContent = "";
        supportIntakeSignedMessage.classList.add("d-none");
        supportIntakeUpgradeLink.removeAttribute("href");
        supportIntakeUpgradeLink.classList.add("d-none");
        checkSupportIntakeButton.textContent = "Check private upload availability";
        supportEncryptionPanel.classList.add("d-none");
        supportEncryptionConsent.checked = false;
        supportEncryptionConsent.disabled = false;
        encryptSupportBundleButton.classList.remove("d-none");
        supportEncryptionMessage.textContent = "Encryption runs locally on this Pi. The readable archive remains available and no file is uploaded.";
        downloadEncryptedSupportBundleButton.classList.add("d-none");
        downloadSupportReceiptButton.classList.add("d-none");
        supportIntakeActive = false;
        supportEncryptedDownloaded = false;
        supportDropboxHandoffPanel.classList.add("d-none");
        supportDropboxHandoffConsent.checked = false;
        openSupportDropboxButton.removeAttribute("href");
        supportUploadReportInFlight = false;
        supportUploadReportComplete = false;
        supportUploadReportPanel.classList.add("d-none");
        supportUploadReportedComplete.checked = false;
        supportUploadReportedComplete.disabled = false;
        reportSupportUploadButton.classList.remove("d-none");
        supportUploadReportMessage.textContent = "The Dropbox page was requested. Opening it is not upload success or maintainer confirmation.";
        supportGithubContinuationPanel.classList.add("d-none");
        supportGithubExistingIssueActions.classList.add("d-none");
        supportGithubNewIssueActions.classList.add("d-none");
        supportGithubContinuationMessage.textContent = "";
        supportGithubComment.value = "";
        supportGithubCopyStatus.textContent = "";
        openSupportGithubIssueButton.removeAttribute("href");
        createSupportGithubIssueButton.removeAttribute("href");
    }

    function githubPublicComment(caseId) {
        return `A support bundle was generated and uploaded through the private support channel.\n\nCase ID: ${caseId}\n\nNo diagnostic bundle or transfer link is attached to this public comment.`;
    }

    function showSupportGithubContinuation() {
        const caseId = supportBundleCaseId.textContent;
        supportGithubContinuationPanel.classList.remove("d-none");
        supportGithubCopyStatus.textContent = "";
        if (supportContextKind === "existing_github_issue" &&
            /^[1-9][0-9]{0,9}$/.test(supportExistingIssueNumber)) {
            supportGithubContinuationMessage.textContent = "GitHub cannot be updated automatically. Sign in, then post the prepared case note yourself.";
            supportGithubComment.value = githubPublicComment(caseId);
            openSupportGithubIssueButton.href = `https://github.com/WsprryPi/WsprryPi/issues/${supportExistingIssueNumber}`;
            supportGithubExistingIssueActions.classList.remove("d-none");
            supportGithubNewIssueActions.classList.add("d-none");
            return;
        }
        if (supportContextKind === "new_github_issue") {
            const title = `Support request — case ${caseId}`;
            const body = `${githubPublicComment(caseId)}\n\nDescribe the problem here without including private diagnostic or contact information.`;
            const query = new URLSearchParams({ title, body });
            createSupportGithubIssueButton.href = `https://github.com/WsprryPi/WsprryPi/issues/new?${query.toString()}`;
            supportGithubContinuationMessage.textContent = "GitHub sign-in is required. The application will open a prefilled issue for you to review and submit.";
            supportGithubExistingIssueActions.classList.add("d-none");
            supportGithubNewIssueActions.classList.remove("d-none");
            return;
        }
        supportGithubContinuationMessage.textContent =
            "No public GitHub issue is required. Your encrypted bundle contains the private problem description and contact information for maintainer follow-up.";
        supportGithubExistingIssueActions.classList.add("d-none");
        supportGithubNewIssueActions.classList.add("d-none");
    }

    async function copySupportGithubComment() {
        try {
            if (!navigator.clipboard || typeof navigator.clipboard.writeText !== "function") {
                throw new Error("clipboard unavailable");
            }
            await navigator.clipboard.writeText(supportGithubComment.value);
            supportGithubCopyStatus.textContent = "Public comment copied. Open the issue and post it yourself.";
        } catch {
            window.setTimeout(() => {
                supportGithubComment.focus({ preventScroll: true });
                supportGithubComment.select();
                supportGithubComment.setSelectionRange(0, supportGithubComment.value.length);
                supportGithubCopyStatus.textContent = "Copy was not available. The comment is selected so you can copy it manually.";
            }, 0);
        }
    }

    function hasExactKeys(value, required, optional = []) {
        if (!value || typeof value !== "object" || Array.isArray(value)) return false;
        const keys = Object.keys(value);
        const allowed = new Set([...required, ...optional]);
        return required.every((key) => Object.hasOwn(value, key)) &&
            keys.every((key) => allowed.has(key));
    }

    function validIntakeText(value, maximum) {
        return typeof value === "string" && value.length > 0 && value.length <= maximum;
    }

    function validIntakeKeyId(value, kind) {
        return typeof value === "string" &&
            new RegExp(`^wsprrypi-${kind}-[0-9]{4}-[0-9]{2}$`).test(value);
    }

    function validIntakeVersion(value) {
        if (typeof value !== "string" || value.length === 0 || value.length > 64) return false;
        const buildSplit = value.split("+");
        if (buildSplit.length > 2) return false;
        const prereleaseAt = buildSplit[0].indexOf("-");
        const core = prereleaseAt === -1 ? buildSplit[0] : buildSplit[0].slice(0, prereleaseAt);
        const prerelease = prereleaseAt === -1 ? undefined : buildSplit[0].slice(prereleaseAt + 1);
        const numeric = core.split(".");
        if (numeric.length !== 3 || !numeric.every((part) => /^(0|[1-9][0-9]*)$/.test(part))) {
            return false;
        }
        const identifiersValid = (section, rejectNumericLeadingZero) => section !== undefined &&
            section.split(".").every((part) => /^[0-9A-Za-z-]+$/.test(part) &&
                (!rejectNumericLeadingZero || !/^[0-9]+$/.test(part) || /^(0|[1-9][0-9]*)$/.test(part)));
        if (prerelease !== undefined && !identifiersValid(prerelease, true)) return false;
        return buildSplit.length !== 2 || identifiersValid(buildSplit[1], false);
    }

    function validIntakeUrl(value, kind) {
        if (typeof value !== "string" || value.length > 2048) return false;
        try {
            const url = new URL(value);
            if (url.protocol !== "https:" || url.username || url.password || url.hash) return false;
            if (kind === "request") {
                return url.hostname === "www.dropbox.com" &&
                    /^\/request\/[A-Za-z0-9_-]+$/.test(url.pathname) && !url.search;
            }
            return url.hostname === "github.com" &&
                (url.pathname === "/WsprryPi/WsprryPi/releases" ||
                    url.pathname.startsWith("/WsprryPi/WsprryPi/releases/")) && !url.search;
        } catch {
            return false;
        }
    }

    function validIntakeExpiry(value) {
        if (typeof value !== "string" ||
            !/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$/.test(value)) return false;
        const parsed = new Date(value);
        return Number.isFinite(parsed.getTime()) && parsed.toISOString().replace(".000Z", "Z") === value;
    }

    function formatIntakeExpiry(value) {
        return new Intl.DateTimeFormat(undefined, {
            year: "numeric",
            month: "short",
            day: "numeric",
            hour: "numeric",
            minute: "2-digit",
            timeZone: "UTC",
            timeZoneName: "short"
        }).format(new Date(value));
    }

    function parseSupportIntakeResponse(value) {
        if (!value || typeof value.status !== "string") return null;
        const optional = ["user_message"];
        const messageValid = !Object.hasOwn(value, "user_message") ||
            validIntakeText(value.user_message, 2048);
        if (!messageValid) return null;
        if (value.status === "active") {
            const required = ["status", "generation", "expires_at", "minimum_upload_version",
                "signing_key_id", "bundle_key_id", "request_url"];
            if (!hasExactKeys(value, required, optional) ||
                !Number.isSafeInteger(value.generation) || value.generation < 1 ||
                !validIntakeExpiry(value.expires_at) ||
                !validIntakeVersion(value.minimum_upload_version) ||
                !validIntakeKeyId(value.signing_key_id, "intake") ||
                !validIntakeKeyId(value.bundle_key_id, "bundle") ||
                !validIntakeUrl(value.request_url, "request")) return null;
            return { status: value.status, expiresAt: value.expires_at, userMessage: value.user_message || "" };
        }
        if (value.status === "disabled") {
            const required = ["status", "generation", "expires_at", "signing_key_id", "bundle_key_id"];
            if (!hasExactKeys(value, required, optional) ||
                !Number.isSafeInteger(value.generation) || value.generation < 1 ||
                !validIntakeExpiry(value.expires_at) ||
                !validIntakeKeyId(value.signing_key_id, "intake") ||
                !validIntakeKeyId(value.bundle_key_id, "bundle")) return null;
            return { status: value.status, expiresAt: value.expires_at, userMessage: value.user_message || "" };
        }
        if (value.status === "upgrade_required") {
            const required = ["status", "minimum_upload_version", "release_url"];
            if (!hasExactKeys(value, required, optional) ||
                !validIntakeVersion(value.minimum_upload_version) ||
                !validIntakeUrl(value.release_url, "release")) return null;
            return { status: value.status, minimumVersion: value.minimum_upload_version,
                releaseUrl: value.release_url, userMessage: value.user_message || "" };
        }
        if (value.status === "unavailable" && hasExactKeys(value, ["status"])) {
            return { status: value.status };
        }
        return null;
    }

    function showSupportIntakeResult(result) {
        supportIntakeSignedMessage.textContent = result.userMessage || "";
        supportIntakeSignedMessage.classList.toggle("d-none", !result.userMessage);
        supportIntakeUpgradeLink.removeAttribute("href");
        supportIntakeUpgradeLink.classList.add("d-none");
        checkSupportIntakeButton.textContent = "Check again";
        if (result.status === "active") {
            supportIntakeActive = true;
            if (supportEncryptedDownloaded) openSupportDropboxButton.href =
                `${SUPPORT_BUNDLES_ENDPOINT.proxyUrl}/${encodeURIComponent(supportBundleJobId)}/handoff`;
            supportIntakeMessage.textContent = `Private upload is available until ${formatIntakeExpiry(result.expiresAt)}. No file has been uploaded.`;
            supportEncryptionPanel.classList.remove("d-none");
            supportDropboxHandoffPanel.classList.toggle("d-none", !supportEncryptedDownloaded);
        } else if (result.status === "disabled") {
            supportIntakeActive = false;
            supportEncryptionPanel.classList.add("d-none");
            supportDropboxHandoffPanel.classList.add("d-none");
            supportDropboxHandoffConsent.checked = false;
            openSupportDropboxButton.removeAttribute("href");
            supportEncryptionConsent.checked = false;
            setSupportBundleActions();
            supportIntakeMessage.textContent = `Private upload is temporarily disabled. This configuration expires ${formatIntakeExpiry(result.expiresAt)}. Your local bundle is unchanged.`;
        } else if (result.status === "upgrade_required") {
            supportIntakeActive = false;
            supportEncryptionPanel.classList.add("d-none");
            supportDropboxHandoffPanel.classList.add("d-none");
            supportDropboxHandoffConsent.checked = false;
            openSupportDropboxButton.removeAttribute("href");
            supportEncryptionConsent.checked = false;
            setSupportBundleActions();
            supportIntakeMessage.textContent = `Upgrade to WsprryPi ${result.minimumVersion} or later before uploading. Your local bundle is unchanged.`;
            supportIntakeUpgradeLink.href = result.releaseUrl;
            supportIntakeUpgradeLink.classList.remove("d-none");
        } else {
            supportIntakeActive = false;
            supportEncryptionPanel.classList.add("d-none");
            supportDropboxHandoffPanel.classList.add("d-none");
            supportDropboxHandoffConsent.checked = false;
            openSupportDropboxButton.removeAttribute("href");
            supportEncryptionConsent.checked = false;
            setSupportBundleActions();
            supportIntakeMessage.textContent = "Private upload availability could not be checked. Your local bundle is unchanged. Try again.";
            checkSupportIntakeButton.textContent = "Try again";
        }
    }

    async function encryptSupportBundle() {
        if (!supportBundleFinalized || !supportEncryptionConsent.checked || supportEncryptionInFlight) return;
        supportEncryptionInFlight = true;
        supportEncryptionMessage.textContent = "Encrypting the reviewed candidate on this Pi…";
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(supportBundleJobId)}/encrypt`), { method: "POST" });
            const value = await response.json();
            if (!response.ok || value.workflow_state !== "encrypted") throw new Error("encryption failed");
            supportEncryptionMessage.textContent = "Encrypted bundle ready. Download it before downloading the receipt. No file has been uploaded.";
            supportEncryptionConsent.disabled = true;
            encryptSupportBundleButton.classList.add("d-none");
            downloadEncryptedSupportBundleButton.classList.remove("d-none");
        } catch {
            supportEncryptionMessage.textContent = "Encryption did not complete. The readable archive is unchanged. Check availability and try again.";
        } finally {
            supportEncryptionInFlight = false;
            setSupportBundleActions();
        }
    }

    async function downloadPrivateArtifact(kind, button, filename) {
        button.disabled = true;
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(supportBundleJobId)}/${kind}`),
                { method: "GET", cache: "no-store" });
            if (!response.ok) throw new Error("download failed");
            const blob = await response.blob();
            if (!blob.size) throw new Error("empty download");
            invokeBrowserDownload(blob, safePrivateArtifactFilename(response.headers.get("Content-Disposition"), filename));
            if (kind === "encrypted") {
                supportEncryptedDownloaded = true;
                openSupportDropboxButton.href =
                    `${SUPPORT_BUNDLES_ENDPOINT.proxyUrl}/${encodeURIComponent(supportBundleJobId)}/handoff`;
                supportEncryptionMessage.textContent = "Encrypted bundle downloaded. Download its receipt for your records. No file has been uploaded.";
                downloadSupportReceiptButton.classList.remove("d-none");
                supportDropboxHandoffPanel.classList.toggle("d-none", !supportIntakeActive);
            } else {
                supportEncryptionMessage.textContent = "Receipt downloaded. Keep both files together. No file has been uploaded.";
            }
        } catch {
            supportEncryptionMessage.textContent = "Download did not complete. Your files remain on the Pi; try again.";
        } finally { button.disabled = false; }
    }

    function safePrivateArtifactFilename(disposition, fallback) {
        if (typeof disposition !== "string") return fallback;
        const match = disposition.match(/(?:^|;)\s*filename="?([^";]+)"?/i);
        const value = match ? match[1].trim() : "";
        return /^[A-Za-z0-9][A-Za-z0-9._-]*\.(?:age|json)$/.test(value) ? value : fallback;
    }

    async function checkSupportIntake() {
        if (!supportBundleFinalized || supportIntakeInFlight) return;
        supportIntakeInFlight = true;
        supportIntakeMessage.textContent = "Checking private upload availability…";
        supportIntakeSignedMessage.classList.add("d-none");
        supportIntakeUpgradeLink.classList.add("d-none");
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(SUPPORT_INTAKE_ENDPOINT, {
                method: "GET",
                cache: "no-store"
            });
            const value = await response.json();
            const result = parseSupportIntakeResponse(value);
            if ((!response.ok && !(response.status === 503 && result?.status === "unavailable")) || !result) {
                throw new Error("invalid intake response");
            }
            showSupportIntakeResult(result);
        } catch {
            showSupportIntakeResult({ status: "unavailable" });
        } finally {
            supportIntakeInFlight = false;
            setSupportBundleActions();
        }
    }

    async function reportSupportUploadComplete() {
        if (!supportUploadReportedComplete.checked || supportUploadReportInFlight ||
            supportUploadReportComplete || supportBundleJobId === "") return;
        supportUploadReportInFlight = true;
        supportUploadReportMessage.textContent = "Recording your report…";
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(supportBundleJobId)}/upload-reported-complete`),
                { method: "POST" });
            const value = await response.json();
            if (!response.ok || value.workflow_state !== "upload_reported_complete")
                throw new Error("upload report failed");
            supportUploadReportComplete = true;
            supportUploadReportedComplete.disabled = true;
            reportSupportUploadButton.classList.add("d-none");
            supportUploadReportMessage.textContent =
                `You reported that Dropbox displayed upload success for case ${supportBundleCaseId.textContent}. This is not maintainer confirmation. The readable archive remains on this Pi until you delete it or it expires.`;
            showSupportGithubContinuation();
        } catch {
            supportUploadReportMessage.textContent =
                "Your report was not recorded. Confirm that Dropbox displayed “Finished uploading,” then reopen the private upload page if needed and try again.";
        } finally {
            supportUploadReportInFlight = false;
            setSupportBundleActions();
        }
    }

    function setDownloadAvailability(available) {
        downloadSupportBundleButton.dataset.available = available ? "true" : "false";
        downloadSupportBundleButton.classList.toggle("d-none", !available);
        deleteSupportBundleButton.classList.toggle("d-none", supportBundleJobId === "");
        setSupportBundleActions();
    }

    function safeSupportBundleFilename(contentDisposition) {
        if (typeof contentDisposition !== "string") {
            return SUPPORT_BUNDLE_FILENAME_FALLBACK;
        }
        const match = contentDisposition.match(/(?:^|;)\s*filename="?([^";]+)"?/i);
        const filename = match ? match[1].trim() : "";
        if (!/^[A-Za-z0-9][A-Za-z0-9._-]*\.tar\.gz$/i.test(filename)) {
            return SUPPORT_BUNDLE_FILENAME_FALLBACK;
        }
        return filename;
    }

    function genericSupportBundleFailure(action) {
        const messages = {
            create: "Support bundle creation could not start. Check the controller connection and try again.",
            collect: "Support bundle collection did not complete. Try again when the controller is available.",
            status: "Support bundle status could not be checked. Try again shortly.",
            download: "The support bundle could not be downloaded. Try the download again.",
            delete: "The Pi-side support bundle could not be removed. Your downloaded file is safe; the Pi-side copy will expire automatically within 24 hours.",
            finalize: "The reviewed candidate could not be finalized. The readable archive remains available; verify it has not changed and try again."
        };
        return messages[action] || "The support bundle operation did not complete. Try again.";
    }

    function scheduleSupportBundlePoll(jobId) {
        stopSupportBundlePolling();
        if (supportBundlePageUnloading || jobId !== supportBundleJobId) {
            return;
        }
        supportBundlePollTimer = window.setTimeout(() => {
            supportBundlePollTimer = null;
            pollSupportBundle(jobId);
        }, SUPPORT_BUNDLE_POLL_INTERVAL_MS);
        setSupportBundleActions();
    }

    async function pollSupportBundle(jobId) {
        if (supportBundlePageUnloading || jobId !== supportBundleJobId) {
            return;
        }
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(jobId)}`),
                { method: "GET", cache: "no-store" }
            );
            if (!response.ok) {
                throw new Error("status request failed");
            }
            const snapshot = await response.json();
            if (!snapshot || jobId !== supportBundleJobId) {
                return;
            }
            if (snapshot.state === "queued") {
                setSupportBundleStatus("queued", "Support bundle queued.");
                scheduleSupportBundlePoll(jobId);
                return;
            }
            if (snapshot.state === "running") {
                setSupportBundleStatus("running", "Collecting diagnostic information.");
                scheduleSupportBundlePoll(jobId);
                return;
            }
            stopSupportBundlePolling();
            if (snapshot.state === "succeeded" && snapshot.download_available === true) {
                if (typeof snapshot.case_id === "string") {
                    supportBundleCaseId.textContent = snapshot.case_id;
                }
                supportBundleReview.classList.remove("d-none");
                setSupportBundleStatus("ready", "Readable candidate ready. Download and inspect it locally.");
                setDownloadAvailability(true);
                return;
            }
            setDownloadAvailability(false);
            setSupportBundleStatus("failed", "Support bundle collection did not complete.");
            showSupportBundleAlert(genericSupportBundleFailure("collect"));
            supportBundleJobId = "";
            setSupportBundleActions();
        } catch {
            stopSupportBundlePolling();
            setDownloadAvailability(false);
            setSupportBundleStatus("failed", "Support bundle status unavailable.");
            showSupportBundleAlert(genericSupportBundleFailure("status"));
            supportBundleJobId = "";
            setSupportBundleActions();
        }
    }

    async function createSupportBundle() {
        if (supportBundleCreateInFlight || supportBundleJobId !== "") {
            return;
        }
        supportBundleCreateInFlight = true;
        clearSupportBundleAlert();
        const supportContext = supportContextPayload();
        if (!supportContext) {
            supportBundleCreateInFlight = false;
            setSupportBundleActions();
            return;
        }
        supportContextKind = supportContext.kind;
        supportExistingIssueNumber = supportContext.kind === "existing_github_issue"
            ? supportBundleIssueNumber.value.trim() : "";
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(SUPPORT_BUNDLES_ENDPOINT, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    probe_i2c: supportBundleProbeI2c.checked,
                    support_context: supportContext
                })
            });
            if (!response.ok) {
                throw new Error("create request failed");
            }
            const snapshot = await response.json();
            if (!snapshot || typeof snapshot.id !== "string" || snapshot.id === "") {
                throw new Error("missing job id");
            }
            supportBundleJobId = snapshot.id;
            supportBundleCaseId.textContent = typeof snapshot.case_id === "string"
                ? snapshot.case_id : "Pending";
            supportBundleSetup.classList.add("d-none");
            supportBundleReview.classList.remove("d-none");
            createSupportBundleButton.classList.add("d-none");
            deleteSupportBundleButton.classList.remove("d-none");
            setDownloadAvailability(false);
            setSupportBundleStatus("queued", "Private support candidate queued.");
            scheduleSupportBundlePoll(supportBundleJobId);
        } catch {
            setSupportBundleStatus("failed", "Support bundle was not created.");
            showSupportBundleAlert(genericSupportBundleFailure("create"));
        } finally {
            supportBundleCreateInFlight = false;
            setSupportBundleActions();
        }
    }

    function invokeBrowserDownload(blob, filename) {
        const objectUrl = URL.createObjectURL(blob);
        const link = document.createElement("a");
        link.href = objectUrl;
        link.download = filename;
        link.style.display = "none";
        document.body.appendChild(link);
        try {
            link.click();
        } finally {
            link.remove();
            window.setTimeout(() => URL.revokeObjectURL(objectUrl), 0);
        }
    }

    function resetSupportBundleWorkflow() {
        supportBundleJobId = "";
        supportContextKind = "";
        supportExistingIssueNumber = "";
        supportBundleDownloaded = false;
        supportBundleFinalized = false;
        supportBundleReviewed.checked = false;
        supportBundleReviewed.disabled = false;
        supportBundleReview.classList.add("d-none");
        supportBundleReviewConsent.classList.add("d-none");
        finalizeSupportBundleButton.classList.add("d-none");
        createSupportBundleButton.classList.remove("d-none");
        clearSupportIntakeState();
        setDownloadAvailability(false);
    }

    async function deleteSupportBundle(jobId) {
        if (supportBundleDeleteInFlight || jobId !== supportBundleJobId) {
            return false;
        }
        supportBundleDeleteInFlight = true;
        const downloadWasAvailable = downloadSupportBundleButton.dataset.available === "true";
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(jobId)}`),
                { method: "DELETE" }
            );
            if (!response.ok) {
                throw new Error("delete request failed");
            }
            setDownloadAvailability(false);
            deleteSupportBundleButton.classList.add("d-none");
            resetSupportBundleWorkflow();
            setSupportBundleStatus("deleted", "The Pi-side support candidate was deleted. Any browser download remains under your control.");
            return true;
        } catch {
            setDownloadAvailability(downloadWasAvailable);
            deleteSupportBundleButton.classList.remove("d-none");
            setSupportBundleStatus(
                "cleanup-pending",
                "The Pi-side support candidate is still retained and will expire automatically within 24 hours."
            );
            showSupportBundleAlert(genericSupportBundleFailure("delete"));
            return false;
        } finally {
            supportBundleDeleteInFlight = false;
            setSupportBundleActions();
        }
    }

    async function downloadSupportBundle() {
        if (supportBundleDownloadInFlight || supportBundleJobId === "" ||
            downloadSupportBundleButton.dataset.available !== "true") {
            return;
        }
        const jobId = supportBundleJobId;
        supportBundleDownloadInFlight = true;
        clearSupportBundleAlert();
        setSupportBundleStatus("downloading", "Downloading support bundle.");
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(jobId)}/download`),
                { method: "GET", cache: "no-store" }
            );
            if (!response.ok) {
                throw new Error("download request failed");
            }
            const blob = await response.blob();
            if (blob.size === 0 || jobId !== supportBundleJobId) {
                throw new Error("incomplete download");
            }
            const filename = safeSupportBundleFilename(response.headers.get("Content-Disposition"));
            invokeBrowserDownload(blob, filename);
            supportBundleDownloaded = true;
            supportBundleReviewConsent.classList.remove("d-none");
            finalizeSupportBundleButton.classList.remove("d-none");
            setSupportBundleStatus("downloaded", `Downloaded: ${filename}. Your browser chose the save location. Open and review it before approval.`);
        } catch {
            setSupportBundleStatus("failed", "Support bundle download did not complete.");
            showSupportBundleAlert(genericSupportBundleFailure("download"));
        } finally {
            supportBundleDownloadInFlight = false;
            setSupportBundleActions();
        }
    }

    async function finalizeSupportBundle() {
        if (supportBundleFinalizeInFlight || !supportBundleDownloaded ||
            !supportBundleReviewed.checked || supportBundleJobId === "") return;
        supportBundleFinalizeInFlight = true;
        clearSupportBundleAlert();
        setSupportBundleStatus("finalizing", "Finalizing the exact reviewed candidate bytes.");
        setSupportBundleActions();
        try {
            const response = await fetchWithEndpointFallback(
                supportBundleEndpoint(`/${encodeURIComponent(supportBundleJobId)}/finalize`),
                { method: "POST" }
            );
            if (!response.ok) throw new Error("finalize request failed");
            const snapshot = await response.json();
            if (snapshot.workflow_state !== "finalized") throw new Error("finalize state missing");
            supportBundleFinalized = true;
            supportBundleReviewed.disabled = true;
            finalizeSupportBundleButton.classList.add("d-none");
            supportIntakePanel.classList.remove("d-none");
            setSupportBundleStatus("finalized", "Reviewed candidate finalized. These exact bytes are immutable and retained on the Pi for encryption.");
        } catch {
            setSupportBundleStatus("failed", "Candidate finalization did not complete.");
            showSupportBundleAlert(genericSupportBundleFailure("finalize"));
        } finally {
            supportBundleFinalizeInFlight = false;
            setSupportBundleActions();
        }
    }

    document.querySelectorAll('input[name="supportBundleContextKind"]').forEach((control) => {
        control.addEventListener("change", updateSupportContextFields);
    });
    supportBundleReviewed.addEventListener("change", setSupportBundleActions);
    createSupportBundleButton.addEventListener("click", () => {
        if (!supportBundleCreateInFlight && supportBundleJobId === "") {
            clearSupportBundleAlert();
            clearSupportContextErrors();
            supportBundleSetup.classList.remove("d-none");
            createSupportBundleButton.classList.add("d-none");
            updateSupportContextFields();
            supportBundleIssueNumber.focus();
        }
    });
    cancelCreateSupportBundleButton.addEventListener("click", () => {
        supportBundleSetup.classList.add("d-none");
        createSupportBundleButton.classList.remove("d-none");
        createSupportBundleButton.focus();
    });
    confirmCreateSupportBundleButton.addEventListener("click", createSupportBundle);
    downloadSupportBundleButton.addEventListener("click", downloadSupportBundle);
    finalizeSupportBundleButton.addEventListener("click", finalizeSupportBundle);
    checkSupportIntakeButton.addEventListener("click", checkSupportIntake);
    supportEncryptionConsent.addEventListener("change", setSupportBundleActions);
    supportDropboxHandoffConsent.addEventListener("change", setSupportBundleActions);
    openSupportDropboxButton.addEventListener("click", (event) => {
        if (!supportIntakeActive || !supportEncryptedDownloaded ||
            !supportDropboxHandoffConsent.checked) {
            event.preventDefault();
            return;
        }
        if (supportUploadReportPanel.classList.contains("d-none")) {
            supportUploadReportedComplete.checked = false;
            supportUploadReportPanel.classList.remove("d-none");
            supportUploadReportMessage.textContent =
                "The Dropbox page was requested. Opening it is not upload success or maintainer confirmation.";
            setSupportBundleActions();
        }
    });
    supportUploadReportedComplete.addEventListener("change", setSupportBundleActions);
    reportSupportUploadButton.addEventListener("click", reportSupportUploadComplete);
    copySupportGithubCommentButton.addEventListener("click", copySupportGithubComment);
    encryptSupportBundleButton.addEventListener("click", encryptSupportBundle);
    downloadEncryptedSupportBundleButton.addEventListener("click", () =>
        downloadPrivateArtifact("encrypted", downloadEncryptedSupportBundleButton, "wsprrypi-support-bundle.tar.gz.age"));
    downloadSupportReceiptButton.addEventListener("click", () =>
        downloadPrivateArtifact("receipt", downloadSupportReceiptButton, "wsprrypi-support-bundle.receipt.json"));
    deleteSupportBundleButton.addEventListener("click", () => {
        deleteSupportBundle(supportBundleJobId);
    });
    updateSupportContextFields();
    clearSupportIntakeState();
    window.addEventListener("pagehide", () => {
        supportBundlePageUnloading = true;
        stopSupportBundlePolling();
    }, { once: true });
    setDownloadAvailability(false);
});

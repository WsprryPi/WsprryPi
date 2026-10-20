let isUpdatingTransmitFromBackend = false;
let isUpdatingRebootBehaviorFromBackend = false;
let stopRequestInFlight = false;
let stopRequestTimeoutHandle = null;
let operationRetryInFlight = false;
const CONFIG_REQUEST_TIMEOUT_MS = 15000;
const STOP_REQUEST_TIMEOUT_MS = 10000;
const GPIO_BACKEND_UNAVAILABLE_MODAL_TITLE = "GPIO backend unavailable on Raspberry Pi 5";
const GPIO_BACKEND_UNAVAILABLE_MODAL_MESSAGE =
    "Transmit cannot be enabled with the GPIO backend on this Raspberry Pi. Choose a supported backend on the Setup page before enabling transmit.";

let currentRuntimeTransmitBackend = "gpio";
let operationSnapshotLoaded = false;
let gpioBackendUnavailableModalVisible = false;
let gpioBackendUnavailableModalDismissed = false;
let operationSnapshot = {
    mode: "",
    transmit: false,
    callsign: "",
    gridsquare: "",
    wsprFrequencyHz: 0,
    cwBaseFrequencyHz: 0,
    cwOffsetHz: 0,
    enableOnBoot: "Never",
};

function rebootBehaviorRadioValueFromConfig(value) {
    switch (String(value || "").trim()) {
        case "Follow":
            return "follow_last";
        case "Always":
            return "restart";
        case "Never":
        default:
            return "disable";
    }
}

function rebootBehaviorConfigValueFromRadio(value) {
    switch (String(value || "").trim()) {
        case "follow_last":
            return "Follow";
        case "restart":
            return "Always";
        case "disable":
        default:
            return "Never";
    }
}

function setRebootBehaviorFromBackend(value) {
    const radioValue = rebootBehaviorRadioValueFromConfig(value);
    isUpdatingRebootBehaviorFromBackend = true;
    $(`input[name="operation_reboot_behavior"][value="${radioValue}"]`).prop("checked", true);
    isUpdatingRebootBehaviorFromBackend = false;
    operationSnapshot.enableOnBoot = rebootBehaviorConfigValueFromRadio(radioValue);
}

function setRebootBehaviorUiDisabled(disabled) {
    $('input[name="operation_reboot_behavior"]').prop("disabled", !!disabled);
}

function browserOfflineOperationMessage() {
    return "This browser is offline, so runtime controls cannot reach the controller.";
}

function runtimeConnectionUnavailableMessage() {
    if (navigator.onLine === false) {
        return browserOfflineOperationMessage();
    }

    return "The controller connection is unavailable right now, so the action could not be completed.";
}

function transientRuntimeActionMessage(textStatus = "") {
    const normalizedStatus = typeof textStatus === "string"
        ? textStatus.trim().toLowerCase()
        : "";

    if (navigator.onLine === false) {
        return runtimeConnectionUnavailableMessage();
    }

    if (normalizedStatus === "timeout") {
        return "The controller did not respond before the action timed out. Check connectivity and try again.";
    }

    return runtimeConnectionUnavailableMessage();
}

function isTransientNetworkFailure(xhr, textStatus = "") {
    if (navigator.onLine === false) {
        return true;
    }

    const normalizedStatus = typeof textStatus === "string"
        ? textStatus.trim().toLowerCase()
        : "";

    if ((normalizedStatus === "timeout" || normalizedStatus === "error") &&
        (!xhr || typeof xhr.status !== "number" || xhr.status === 0)) {
        return true;
    }

    return !!xhr && typeof xhr.status === "number" && xhr.status === 0;
}

function selectedRuntimeTransmitBackend() {
    return ["si5351", "wtp"].includes(currentRuntimeTransmitBackend) ? currentRuntimeTransmitBackend : "gpio";
}

function setSelectedRuntimeTransmitBackend(backend) {
    currentRuntimeTransmitBackend = ["si5351", "wtp"].includes(backend) ? backend : "gpio";
}

function hasAnySupportedTransmitBackend() {
    if (selectedRuntimeTransmitBackend() === "wtp") return true;
    const platform = window.WSPRRYPI_PLATFORM || {};
    return (
        platform.gpioClockTransmissionSupported !== false ||
        platform.si5351Detected !== false
    );
}

function noBackendAvailableMessage() {
    return "No supported transmit backend is currently available on this system.";
}

function selectedBackendUnavailableMessage() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    const backend = selectedRuntimeTransmitBackend();

    if (backend === "gpio" && platform.gpioClockTransmissionSupported === false) {
        if (
            typeof platform.gpioClockTransmissionError === "string" &&
            platform.gpioClockTransmissionError.trim()
        ) {
            return platform.gpioClockTransmissionError.trim();
        }
        return "GPIO transmission is unavailable on this Raspberry Pi.";
    }

    if (backend === "si5351" && platform.si5351Detected === false) {
        return "No Si5351 detected on the configured I2C bus.";
    }

    return "";
}

function currentTransmitUnavailableMessage() {
    return hasAnySupportedTransmitBackend()
        ? selectedBackendUnavailableMessage()
        : noBackendAvailableMessage();
}

function isGpioUnsupportedReason(reason) {
    const normalized = String(reason || "").toLowerCase();
    if (!normalized) {
        return false;
    }

    return (
        normalized.includes("gpio transmission") ||
        normalized.includes("raspberry pi 5 and newer") ||
        normalized.includes("supported only on raspberry pi 1 through 4") ||
        normalized.includes("unsupported on this raspberry pi")
    );
}

function formatTransmitFailureMessage(reason) {
    if (reason === noBackendAvailableMessage()) {
        return "Transmit cannot be enabled because no supported backend is currently available.";
    }

    if (isGpioUnsupportedReason(reason)) {
        return "Transmit cannot be enabled with the GPIO backend on this Raspberry Pi.";
    }

    if (reason === "No Si5351 detected on the configured I2C bus.") {
        return "Transmit cannot be enabled because no Si5351 was detected on the configured I2C bus.";
    }

    return reason;
}

function showBackendStatus(message, level = "warning", source = "runtime") {
    const $status = $("#backendStatus");
    if (!$status.length) {
        return;
    }

    const alertClass =
        level === "danger" ? "alert-danger" :
        level === "info" ? "alert-info" :
        "alert-warning";

    $status
        .prop("hidden", false)
        .removeClass("alert-warning alert-danger alert-info")
        .addClass(alertClass)
        .attr("data-source", source)
        .text(message);
}

function clearBackendStatus(source = null) {
    const $status = $("#backendStatus");
    if (!$status.length) {
        return;
    }

    const currentSource = $status.attr("data-source") || "";
    if (source && currentSource && currentSource !== source) {
        return;
    }

    $status
        .prop("hidden", true)
        .removeClass("alert-warning alert-danger alert-info")
        .removeAttr("data-source")
        .text("");
}

function resetGpioBackendUnavailableModalState() {
    gpioBackendUnavailableModalVisible = false;
    gpioBackendUnavailableModalDismissed = false;
}

function operationSetupPageUrl() {
    const setupButton = document.getElementById("operationSetupButton");
    const setupTab = "#transmitter-hardware-pane";

    if (setupButton && typeof setupButton.href === "string" && setupButton.href) {
        try {
            const url = new URL(setupButton.href, window.location.href);
            url.searchParams.set("page", "config");
            url.searchParams.set("setup_tab", setupTab);
            return url.toString();
        } catch {
            return "index.php?page=config&setup_tab=#transmitter-hardware-pane";
        }
    }

    return "index.php?page=config&setup_tab=#transmitter-hardware-pane";
}

function navigateToOperationSetupPage() {
    window.location.assign(operationSetupPageUrl());
}

function showGpioBackendUnavailableModal(options = {}) {
    const force = options.force === true;

    if (!force && (gpioBackendUnavailableModalVisible || gpioBackendUnavailableModalDismissed)) {
        return;
    }

    if (typeof showConfirmationDialog !== "function") {
        navigateToOperationSetupPage();
        return;
    }

    const modalEl = document.getElementById("confirmModal");
    if (modalEl) {
        $(modalEl)
            .off("hidden.bs.modal.gpioUnavailable")
            .on("hidden.bs.modal.gpioUnavailable", function () {
                gpioBackendUnavailableModalVisible = false;
                gpioBackendUnavailableModalDismissed = true;
            });
    }

    gpioBackendUnavailableModalVisible = true;
    gpioBackendUnavailableModalDismissed = false;
    showConfirmationDialog({
        title: GPIO_BACKEND_UNAVAILABLE_MODAL_TITLE,
        message: GPIO_BACKEND_UNAVAILABLE_MODAL_MESSAGE,
        confirmLabel: "Go to Setup",
        cancelLabel: "Cancel",
        confirmClass: "btn-primary",
        onConfirm: navigateToOperationSetupPage
    });
}

function setOperationRecoveryUi({
    show = false,
    retryVisible = false,
    retryDisabled = false,
    retryLabel = "Retry now",
    setupVisible = false,
    hint = "",
} = {}) {
    const container = document.getElementById("operationRecoveryActions");
    const retryButton = document.getElementById("operationRetryButton");
    const setupButton = document.getElementById("operationSetupButton");
    const hintNode = document.getElementById("operationRecoveryHint");

    if (!container || !retryButton || !setupButton || !hintNode) {
        return;
    }

    container.hidden = !show;
    retryButton.hidden = !retryVisible;
    retryButton.disabled = !!retryDisabled || operationRetryInFlight;
    retryButton.textContent = operationRetryInFlight ? "Retrying..." : retryLabel;
    setupButton.hidden = !setupVisible;
    hintNode.textContent = operationRetryInFlight
        ? "Requesting a fresh runtime snapshot from the controller."
        : hint;
}

function finishOperationRetryFeedback() {
    if (!operationRetryInFlight) {
        return;
    }

    operationRetryInFlight = false;
    updateOperationStatusSummary(currentRuntimeStatus);
}

function retryOperationRuntimeLoad() {
    if (operationRetryInFlight) {
        return;
    }

    if (navigator.onLine === false) {
        showBackendStatus(browserOfflineOperationMessage(), "warning", "runtime");
        updateOperationStatusSummary(currentRuntimeStatus);
        return;
    }

    operationRetryInFlight = true;
    clearBackendStatus("runtime");
    clearBackendStatus("backend");
    updateOperationStatusSummary(currentRuntimeStatus);

    if (typeof reloadAllData === "function") {
        reloadAllData();
        return;
    }

    if (typeof populateConfig === "function") {
        populateConfig();
    }
    if (typeof getTxState === "function") {
        getTxState();
    }
}

function setRuntimeUiDisabled(disabled) {
    $("#transmit").prop("disabled", !!disabled);
    setRebootBehaviorUiDisabled(disabled);
    if (disabled) {
        $("#stop_transmit").prop("disabled", true);
        return;
    }

    syncStopButtonState();
    syncTransmitAvailabilityUi();
}

function setTransmitFromBackend(enabled) {
    isUpdatingTransmitFromBackend = true;
    $("#transmit").prop("checked", !!enabled);
    isUpdatingTransmitFromBackend = false;
    if (typeof updateRuntimeControlConfigStatus === "function") {
        updateRuntimeControlConfigStatus(
            operationSnapshot.mode || currentRuntimeConfigStatus.mode || "",
            !!enabled
        );
    }
}

function syncStopButtonState() {
    const $stop = $("#stop_transmit");
    if (!$stop.length) {
        return;
    }

    const transmitting =
        currentRuntimeStatus && currentRuntimeStatus.txState === "transmitting";
    $stop.prop("disabled", stopRequestInFlight || !transmitting);
}

function clearStopRequestTimeout() {
    if (stopRequestTimeoutHandle !== null) {
        clearTimeout(stopRequestTimeoutHandle);
        stopRequestTimeoutHandle = null;
    }
}

function failStopRequest(message) {
    clearStopRequestTimeout();
    stopRequestInFlight = false;
    syncStopButtonState();
    showBackendStatus(message, "warning", "runtime");
}

function syncTransmitAvailabilityUi() {
    const transmitField = document.getElementById("transmit");
    const transmitHint = document.getElementById("transmitAvailabilityHint");
    if (!transmitField) {
        return;
    }

    const unavailableMessage = currentTransmitUnavailableMessage();
    const useGpioUnavailableModal = isGpioUnsupportedReason(unavailableMessage);
    const formattedMessage = unavailableMessage
        ? formatTransmitFailureMessage(unavailableMessage)
        : "";
    const hintMessage = useGpioUnavailableModal ? "" : formattedMessage;
    const transmitEnabled = transmitField.checked;
    const shouldDisableEnable = !!unavailableMessage && !transmitEnabled;

    transmitField.disabled = shouldDisableEnable;
    if (shouldDisableEnable && hintMessage) {
        transmitField.setAttribute("title", hintMessage);
    } else {
        transmitField.removeAttribute("title");
    }

    if (transmitHint) {
        transmitHint.hidden = !hintMessage;
        transmitHint.textContent = hintMessage;
    }

    if (useGpioUnavailableModal) {
        showGpioBackendUnavailableModal();
    } else {
        resetGpioBackendUnavailableModalState();
    }
}

function requestTransmitEnabledChange(enabled, previousEnabled) {
    const $transmit = $("#transmit");

    if (enabled) {
        const unavailableMessage = currentTransmitUnavailableMessage();
        if (unavailableMessage) {
            setTransmitFromBackend(previousEnabled);
            if (isGpioUnsupportedReason(unavailableMessage)) {
                clearBackendStatus("runtime");
                showGpioBackendUnavailableModal({ force: true });
                return null;
            }

            const formattedMessage = formatTransmitFailureMessage(unavailableMessage);
            showBackendStatus(formattedMessage, "danger", "runtime");
            return null;
        }
    }

    if (navigator.onLine === false) {
        const message = runtimeConnectionUnavailableMessage();
        setTransmitFromBackend(previousEnabled);
        showBackendStatus(message, "warning", "runtime");
        return null;
    }

    $transmit.prop("disabled", true);

    const interactiveEnable = enabled && !(currentRuntimeTransmitBackend === "wtp") && window.LocalWtp;
    return (interactiveEnable ? window.LocalWtp.enable() : ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {
        type: "PATCH",
        contentType: "application/merge-patch+json",
        timeout: CONFIG_REQUEST_TIMEOUT_MS,
        data: JSON.stringify({
            Operation: {
                "Transmit": enabled,
            },
        }),
    }))
        .done(function () {
            lastSaveTimestamp = Date.now();
            setTransmitFromBackend(enabled);
            clearBackendStatus("runtime");
            if (typeof getTxState === "function") {
                getTxState();
            }
        })
        .fail(function (xhr, textStatus) {
            if (textStatus === "declined") { setTransmitFromBackend(previousEnabled); return; }
            let message = "Failed to update transmit state.";

            if (isTransientNetworkFailure(xhr, textStatus)) {
                message = transientRuntimeActionMessage(textStatus);
                showBackendStatus(message, "warning", "runtime");
                setTransmitFromBackend(previousEnabled);
                return;
            }

            if (xhr.responseJSON && typeof xhr.responseJSON === "object" &&
                typeof xhr.responseJSON.message === "string" && xhr.responseJSON.message.trim()) {
                message = xhr.responseJSON.message.trim();
            }

            setTransmitFromBackend(previousEnabled);
            showBackendStatus(message, "danger", "runtime");
        })
        .always(function () {
            $transmit.prop("disabled", false);
            syncTransmitAvailabilityUi();
        });
}

function patchTransmitControl() {
    if (isUpdatingTransmitFromBackend) {
        return;
    }

    const enabled = $("#transmit").is(":checked");
    requestTransmitEnabledChange(enabled, !enabled);
}

function patchRebootBehaviorControl() {
    if (isUpdatingRebootBehaviorFromBackend) {
        return;
    }

    const selectedRadio = document.querySelector('input[name="operation_reboot_behavior"]:checked');
    if (!selectedRadio) {
        return;
    }

    const previousValue = operationSnapshot.enableOnBoot || "Never";
    const nextValue = rebootBehaviorConfigValueFromRadio(selectedRadio.value);

    if (nextValue === previousValue) {
        return;
    }

    if (navigator.onLine === false) {
        setRebootBehaviorFromBackend(previousValue);
        showBackendStatus(runtimeConnectionUnavailableMessage(), "warning", "runtime");
        return;
    }

    setRebootBehaviorUiDisabled(true);

    ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {
        type: "PATCH",
        contentType: "application/merge-patch+json",
        timeout: CONFIG_REQUEST_TIMEOUT_MS,
        data: JSON.stringify({
            Operation: {
                "Enable on Boot": nextValue,
            },
        }),
    })
        .done(function () {
            lastSaveTimestamp = Date.now();
            operationSnapshot.enableOnBoot = nextValue;
            clearBackendStatus("runtime");
        })
        .fail(function (xhr, textStatus) {
            let message = "Failed to update reboot behavior.";

            if (isTransientNetworkFailure(xhr, textStatus)) {
                message = transientRuntimeActionMessage(textStatus);
                showBackendStatus(message, "warning", "runtime");
                setRebootBehaviorFromBackend(previousValue);
                return;
            }

            if (xhr.responseJSON && typeof xhr.responseJSON === "object" &&
                typeof xhr.responseJSON.message === "string" && xhr.responseJSON.message.trim()) {
                message = xhr.responseJSON.message.trim();
            }

            setRebootBehaviorFromBackend(previousValue);
            showBackendStatus(message, "danger", "runtime");
        })
        .always(function () {
            setRebootBehaviorUiDisabled(false);
        });
}

function stopTransmission(options = {}) {
    const $stop = $("#stop_transmit");
    if ($stop.prop("disabled")) {
        return false;
    }

    if (!ws || ws.readyState !== WebSocket.OPEN) {
        showBackendStatus(runtimeConnectionUnavailableMessage(), "warning", "runtime");
        return false;
    }

    stopRequestInFlight = true;
    syncStopButtonState();
    clearStopRequestTimeout();
    stopRequestTimeoutHandle = window.setTimeout(() => {
        failStopRequest("Stop command timed out before the controller confirmed it. Check controller connectivity and runtime state, then try again.");
    }, STOP_REQUEST_TIMEOUT_MS);

    ws.send(
        JSON.stringify({
            command: "stop",
            persist_transmit:
                options && options.persistTransmit === false ? false : true,
        })
    );
    return true;
}

function handleStopCommandResponse(message) {
    const response = message && typeof message === "object" ? message : {};
    const stopSucceeded =
        response.transmit_disabled === true || response.stop_performed === true;
    clearStopRequestTimeout();

    if (response.transmit_disabled === true) {
        setTransmitFromBackend(false);
    }
    if (typeof getTxState === "function") {
        getTxState();
    }

    stopRequestInFlight = false;
    syncStopButtonState();

    if (!stopSucceeded) {
        showBackendStatus("The controller did not confirm the stop action. Check runtime state and try again.", "warning", "runtime");
    }
}

function handleOperationConfigSnapshot(snapshot = {}) {
    finishOperationRetryFeedback();
    operationSnapshotLoaded = true;
    operationSnapshot = {
        mode: typeof snapshot.mode === "string" ? snapshot.mode : "",
        transmit: snapshot.transmit === true,
        callsign: typeof snapshot.callsign === "string" ? snapshot.callsign.trim() : "",
        gridsquare: typeof snapshot.gridsquare === "string" ? snapshot.gridsquare.trim() : "",
        wsprFrequencyHz: Number.isFinite(Number(snapshot.wsprFrequencyHz))
            ? Number(snapshot.wsprFrequencyHz)
            : 0,
        cwBaseFrequencyHz: Number.isFinite(Number(snapshot.cwBaseFrequencyHz))
            ? Number(snapshot.cwBaseFrequencyHz)
            : 0,
        cwOffsetHz: Number.isFinite(Number(snapshot.cwOffsetHz))
            ? Number(snapshot.cwOffsetHz)
            : 0,
        enableOnBoot: typeof snapshot.enableOnBoot === "string"
            ? snapshot.enableOnBoot
            : "Never",
    };
    setRebootBehaviorFromBackend(operationSnapshot.enableOnBoot);

    updateOperationStatusSummary(currentRuntimeStatus);
}

function getOperationFrequencyFallback(mode = "") {
    const normalizedMode = typeof mode === "string" ? mode : "";

    if (normalizedMode === "WSPR") {
        return {
            frequencyHz: operationSnapshot.wsprFrequencyHz,
            offsetHz: 0,
        };
    }

    if (normalizedMode === "QRSS") {
        return {
            frequencyHz: operationSnapshot.cwBaseFrequencyHz,
            offsetHz: 0,
        };
    }

    if (normalizedMode === "FSKCW" || normalizedMode === "DFCW") {
        return {
            frequencyHz: operationSnapshot.cwBaseFrequencyHz,
            offsetHz: operationSnapshot.cwOffsetHz,
        };
    }

    return {
        frequencyHz: 0,
        offsetHz: 0,
    };
}

function setOperationStatePresentation(stateNode, detailNode, hintNode, {
    state = "",
    tone = "idle",
    detail = "",
    hint = "",
} = {}) {
    const announcementNode = document.getElementById("operationStatusAnnouncement");
    stateNode.textContent = state;
    stateNode.setAttribute("data-state", tone);
    if (announcementNode) {
        announcementNode.setAttribute("data-state", tone);
        announcementNode.setAttribute("aria-label", state ? `Runtime status: ${state}. ${detail}` : "Runtime status updated.");
    }
    detailNode.textContent = detail;
    if (hintNode) {
        hintNode.textContent = hint;
    }
}

function updateOperationStatusSummary(status) {
    const stateNode = document.getElementById("operationCurrentState");
    const detailNode = document.getElementById("operationStateDetail");
    const hintNode = document.getElementById("operationNextActionHint");

    if (!stateNode || !detailNode) {
        return;
    }

    const callsignReady = !!operationSnapshot.callsign;
    const gridReady = !!operationSnapshot.gridsquare;
    const configuredEnough = callsignReady && gridReady;
    const txState = status && typeof status.txState === "string" ? status.txState : "";
    const transmitEnabled = currentRuntimeConfigStatus.transmitEnabled === true;
    const nextTransmissionAt =
        status && typeof status.nextTransmissionAt === "string"
            ? status.nextTransmissionAt
            : "";
    const unavailableMessage = currentTransmitUnavailableMessage();
    const websocketConnected = websocketCurrentlyConnected === true;
    const backendConnected = backendCurrentlyConnected === true;

    setOperationRecoveryUi();

    if (!operationSnapshotLoaded) {
        if (navigator.onLine === false) {
            setOperationStatePresentation(stateNode, detailNode, hintNode, {
                state: "Browser offline",
                tone: "offline",
                detail: "Operation is waiting for this browser to reconnect before it can load current controller state.",
                hint: "Reconnect this browser, then retry loading runtime state.",
            });
            setOperationRecoveryUi({
                show: true,
                retryVisible: true,
                retryDisabled: true,
                retryLabel: "Retry when online",
                hint: "Runtime loading will resume after this browser reconnects.",
            });
            return;
        }

        if (!backendConnected) {
            setOperationStatePresentation(stateNode, detailNode, hintNode, {
                state: "Controller unavailable",
                tone: "degraded",
                detail: "Operation could not load saved controller values, so live controls are temporarily unavailable.",
                hint: "Retry loading runtime state. If this persists after recovery, use Setup or Maintenance for investigation.",
            });
            setOperationRecoveryUi({
                show: true,
                retryVisible: true,
                setupVisible: true,
                hint: "Retry now after the controller comes back, or open Setup to review saved values once communication is restored.",
            });
            return;
        }

        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Loading runtime state",
            tone: "loading",
            detail: "Connecting to the controller and loading the latest operating values.",
            hint: "Wait for the current runtime snapshot to load before using live controls.",
        });
        if (operationRetryInFlight) {
            setOperationRecoveryUi({
                show: true,
                retryVisible: true,
            });
        }
        return;
    }

    if (!configuredEnough) {
        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Setup required",
            tone: "setup",
            detail: "Station identity is incomplete. Open Setup before normal operation so the controller can build valid transmit frames.",
            hint: "Open Setup to complete callsign and grid before relying on this page for normal operation.",
        });
        setOperationRecoveryUi({
            show: true,
            setupVisible: true,
            hint: "Complete callsign and grid in Setup before enabling normal on-air operation.",
        });
        return;
    }

    if (navigator.onLine === false) {
        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Browser offline",
            tone: "offline",
            detail: "Last known operating values remain visible, but live control is paused until this browser reconnects.",
            hint: "Reconnect this browser to resume live runtime control.",
        });
        setOperationRecoveryUi({
            show: true,
            retryVisible: true,
            retryDisabled: true,
            retryLabel: "Retry when online",
            hint: "Live control resumes automatically when this browser reconnects.",
        });
        return;
    }

    if (!backendConnected || !websocketConnected) {
        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Reconnecting",
            tone: "degraded",
            detail: "Last known operating values remain visible while Operation retries the controller connection.",
            hint: "Use Retry now if the controller is back but this page has not recovered yet.",
        });
        setOperationRecoveryUi({
            show: true,
            retryVisible: true,
            hint: "Retry reloads saved values and requests a fresh runtime state immediately.",
        });
        return;
    }

    if (stopRequestInFlight) {
        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Stopping transmission",
            tone: "stopping",
            detail: "A stop command is in flight. Wait for controller confirmation before retrying or changing transmit state.",
            hint: "If the controller does not confirm the stop request, a warning will appear and the control will re-enable.",
        });
        return;
    }

    if (unavailableMessage) {
        if (isGpioUnsupportedReason(unavailableMessage)) {
            showGpioBackendUnavailableModal();
            setOperationStatePresentation(stateNode, detailNode, hintNode, {
                state: "Setup required",
                tone: "setup",
                detail: "Runtime control is paused until transmitter hardware is reconfigured.",
                hint: "",
            });
            setOperationRecoveryUi({ show: false });
            return;
        }

        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Transmit unavailable",
            tone: "warning",
            detail: formatTransmitFailureMessage(unavailableMessage),
            hint: "Review Transmitter settings in Setup before enabling transmissions from this page.",
        });
        setOperationRecoveryUi({
            show: true,
            setupVisible: true,
            hint: "Open Setup to review backend hardware and saved RF output settings.",
        });
        return;
    }

    if (txState === "transmitting") {
        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Transmitting",
            tone: "active",
            detail: "An active transmission is underway. Use Stop only if you need to interrupt it immediately.",
            hint: "Monitor the current mode and plan below. Open Setup only if you need to change saved operating values after the transmission stops.",
        });
        return;
    }

    if (!transmitEnabled) {
        setOperationStatePresentation(stateNode, detailNode, hintNode, {
            state: "Transmit paused",
            tone: "warning",
            detail: "Saved runtime settings are loaded, but transmissions are disabled until you re-enable them here.",
            hint: "Enable transmissions when you are ready to resume normal scheduling.",
        });
        return;
    }

    setOperationStatePresentation(stateNode, detailNode, hintNode, {
        state: nextTransmissionAt ? "Standing by" : "Ready",
        tone: "ready",
        detail: nextTransmissionAt
            ? `The controller is idle and waiting for the next scheduled activity at ${nextTransmissionAt}.`
            : "The controller is connected and ready. Review the current mode and plan below for operating context.",
        hint: "Use this page for live monitoring and high-level control. Open Setup only when a saved value needs to change.",
    });
}

function handleRuntimeStatusUpdate(status) {
    syncStopButtonState();
    updateOperationStatusSummary(status);
}

function setOfflineDefaults() {
    setTransmitFromBackend(false);
    setRuntimeUiDisabled(true);
    updateOperationStatusSummary(null);
}

function clearOfflineDefaults() {
    setRuntimeUiDisabled(false);
    updateOperationStatusSummary(currentRuntimeStatus);
}

function bindOperationNetworkHandlers() {
    window.addEventListener("offline", () => {
        showBackendStatus(browserOfflineOperationMessage(), "warning", "runtime");
        updateOperationStatusSummary(currentRuntimeStatus);
    });

    window.addEventListener("online", () => {
        clearBackendStatus("runtime");
        syncTransmitAvailabilityUi();
        updateOperationStatusSummary(currentRuntimeStatus);
    });
}

function bindOperationActions() {
    $("#transmit").on("change", patchTransmitControl);
    $('input[name="operation_reboot_behavior"]').on("change", patchRebootBehaviorControl);
    $("#stop_transmit").on("click", stopTransmission);
    $("#operationRetryButton").on("click", retryOperationRuntimeLoad);
    bindOperationNetworkHandlers();
    syncTransmitAvailabilityUi();
    syncStopButtonState();
    updateOperationStatusSummary(currentRuntimeStatus);
}

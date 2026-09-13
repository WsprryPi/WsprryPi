let isUpdatingTransmitFromBackend = false;
let stopRequestInFlight = false;
const CONFIG_AUTOSAVE_DELAY_MS = 800;
const CONFIG_REQUEST_TIMEOUT_MS = 15000;
const STOP_REQUEST_TIMEOUT_MS = 10000;
const MODE_CHANGE_GUARD_STOP_TIMEOUT_MS = 15000;
const CONFIG_DRAFT_STORAGE_KEY = "wsprrypi.configDraft";
let configAutosaveTimer = null;
let configAutosaveSuspended = false;
let configAutosaveInFlight = false;
let configAutosavePendingAfterFlight = false;
let configAutosaveDirty = false;
let configAutosaveNetworkPaused = false;
let lastSavedConfigPayload = "";
let persistedStationIdentity = null;
let currentWsprBandPreferences = {};
let currentWsprBandPreferenceCatalog = null;
const WSPR_BAND_PREFERENCE_NAMES = Object.freeze([
    "2200m", "630m", "160m", "80m", "60m", "40m", "30m", "20m", "17m",
    "15m", "12m", "10m", "8m", "6m", "5m", "4m", "2m", "1.25m", "70cm"
]);
let lastFailedConfigPayload = "";
let lastFailedConfigMessage = "";
let cwDurationPolicyLatched = false;
let configSaveStatusClearTimer = null;
let configAutosaveNeedsRuntimeRefresh = false;
let pendingPersistedMode = "";
let currentConfigModeSelection = "WSPR";
let pendingModeChange = null;
let modeChangeGuardBusy = false;
let suppressModeChangeGuard = false;
let disabledModeSwitchReloadFailureSuppression = null;
let suppressNextConfigDraftRestore = false;
let configNetworkHandlersBound = false;
let configNavigationGuardBound = false;
let stopRequestTimeoutHandle = null;
let modeChangeGuardStopTimeoutHandle = null;
let cwSpeedSelectionOverride = null;
let rp1RouteUi = null;
let si5351AddressDiscoverySequence = 0;
let si5351AddressInventoryState = {
    bus: -1,
    addresses: [],
    error: "",
    loading: true,
};
const cwSpacingSelectionOverride = { conventional: null, dfcw: null };
const cwRepairRevealed = { conventional: false, dfcw: false };
const PAIRED_PLANNING_SHORT_MESSAGE =
    "Paired planning requires a compound callsign and 6-character locator.";

function validBandPreferenceValue(value) {
    return (typeof value === "number" && Number.isSafeInteger(value) && value > 0) ||
        (typeof value === "string" && value.length > 0);
}

function formatBandPreferenceFrequency(value) {
    return Number.isSafeInteger(value) && value > 0
        ? `${value.toLocaleString("en-US")} Hz`
        : "Unavailable";
}

function bandPreferenceMode(value) {
    if (typeof value === "number") return "custom";
    if (typeof value === "string" && value) return "preset";
    return "default";
}

function bandPreferencePresets(band) {
    return currentWsprBandPreferenceCatalog?.presets?.filter((entry) => entry.band === band) || [];
}

function defaultBandPreferenceEntry(band) {
    const profile = String($("#frequency_profile").val() || "existing_common");
    const presets = bandPreferencePresets(band);
    const selectedPreset = band === "60m" && profile === "wrc15"
        ? presets.find((entry) => entry.preset === "60m:wrc15")
        : presets.find((entry) => entry.existing_common === true);
    return selectedPreset || currentWsprBandPreferenceCatalog?.bands?.find((entry) => entry.band === band) || null;
}

function effectiveBandPreferenceFrequencies(band) {
    const preference = currentWsprBandPreferences[band];
    if (typeof preference === "number") {
        const offset = currentWsprBandPreferenceCatalog?.audioOffsetHz;
        return {
            dial: preference,
            tone: Number.isSafeInteger(offset) ? preference + offset : null
        };
    }
    if (typeof preference === "string") {
        const preset = bandPreferencePresets(band).find((entry) => entry.preset === preference);
        const offset = currentWsprBandPreferenceCatalog?.audioOffsetHz;
        return {
            dial: preset?.dial_frequency_hz,
            tone: preset && Number.isSafeInteger(offset) ? preset.dial_frequency_hz + offset : null
        };
    }
    const entry = defaultBandPreferenceEntry(band);
    const offset = currentWsprBandPreferenceCatalog?.audioOffsetHz;
    return {
        dial: entry?.dial_frequency_hz,
        tone: entry?.tone_frequency_hz ?? (entry && Number.isSafeInteger(offset)
            ? entry.dial_frequency_hz + offset : null)
    };
}

function validateBandPreferenceControls() {
    let valid = true;
    document.querySelectorAll(".band-preference-custom").forEach((field) => {
        if (field.hidden || field.disabled) {
            field.setCustomValidity("");
            clearFieldValidationState(field);
            return;
        }
        const raw = String(field.value || "").trim();
        const value = Number(raw);
        const fieldValid = /^[1-9]\d*$/.test(raw) && Number.isSafeInteger(value);
        field.setCustomValidity(fieldValid ? "" : "Enter a positive whole-number dial frequency in Hz.");
        setFieldValidationState(field, fieldValid);
        const error = field.closest("td")?.querySelector(".band-preferences__error");
        if (error) error.textContent = fieldValid ? "" : field.validationMessage;
        valid = valid && fieldValid;
    });
    return valid;
}

function updateBandPreferenceSummary() {
    const count = Object.values(currentWsprBandPreferences).filter(validBandPreferenceValue).length;
    const summary = document.getElementById("band-preferences-summary");
    if (summary) summary.textContent = count === 0 ? "No custom preferences" : `${count} preference${count === 1 ? "" : "s"}`;
}

function renderBandPreferenceRows() {
    const body = document.getElementById("band-preferences-body");
    if (!body) return;
    body.replaceChildren();
    for (const band of WSPR_BAND_PREFERENCE_NAMES) {
        const preference = currentWsprBandPreferences[band];
        const mode = bandPreferenceMode(preference);
        const presets = bandPreferencePresets(band);
        const row = document.createElement("tr");
        row.dataset.band = band;
        const presetOptions = presets.map((entry) =>
            `<option value="${entry.preset}">${entry.preset} — ${formatBandPreferenceFrequency(entry.dial_frequency_hz)}</option>`
        ).join("");
        row.innerHTML = `
            <td data-label="Band">${band}</td>
            <td data-label="Use"><select class="form-select band-preference-mode" aria-label="${band} preference type">
                <option value="default">Default</option>
                <option value="preset"${presets.length ? "" : " disabled"}>Preset</option>
                <option value="custom">Custom</option>
            </select></td>
            <td data-label="Selection"><div class="band-preferences__selection">
                <span class="band-preference-default-label">Built-in default</span>
                <select class="form-select band-preference-preset" aria-label="${band} named preset">${presetOptions}</select>
                <input class="form-control band-preference-custom" type="text" inputmode="numeric" autocomplete="off" aria-label="${band} custom dial frequency in Hz" placeholder="Dial Hz">
                <button type="button" class="btn btn-outline-secondary band-preference-clear" aria-label="Clear ${band} preference">Clear</button>
            </div><span class="band-preferences__error" aria-live="polite"></span></td>
            <td data-label="Effective dial" class="band-preferences__value band-preference-dial"></td>
            <td data-label="Effective RF" class="band-preferences__value band-preference-tone"></td>`;
        body.appendChild(row);
        row.querySelector(".band-preference-mode").value = mode;
        row.querySelector(".band-preference-preset").value = typeof preference === "string" ? preference : (presets[0]?.preset || "");
        row.querySelector(".band-preference-custom").value = typeof preference === "number" ? String(preference) : "";
    }
    refreshBandPreferenceRows();
}

function refreshBandPreferenceRows() {
    document.querySelectorAll("#band-preferences-body tr").forEach((row) => {
        const band = row.dataset.band;
        const mode = row.querySelector(".band-preference-mode").value;
        const preset = row.querySelector(".band-preference-preset");
        const custom = row.querySelector(".band-preference-custom");
        row.querySelector(".band-preference-default-label").hidden = mode !== "default";
        preset.hidden = mode !== "preset";
        preset.disabled = mode !== "preset";
        custom.hidden = mode !== "custom";
        custom.disabled = mode !== "custom";
        row.querySelector(".band-preference-clear").hidden = mode === "default";
        const effective = effectiveBandPreferenceFrequencies(band);
        row.querySelector(".band-preference-dial").textContent = formatBandPreferenceFrequency(effective.dial);
        row.querySelector(".band-preference-tone").textContent = formatBandPreferenceFrequency(effective.tone);
    });
    validateBandPreferenceControls();
    updateBandPreferenceSummary();
}

function updateBandPreferenceCatalog(catalog) {
    currentWsprBandPreferenceCatalog = catalog;
    const status = document.getElementById("band-preferences-status");
    if (status) status.textContent = catalog
        ? `Effective RF includes the ${formatBandPreferenceFrequency(catalog.audioOffsetHz)} audio offset.`
        : "Effective frequencies are unavailable while the controller catalog is unavailable.";
    renderBandPreferenceRows();
}

function handleBandPreferenceInput(event) {
    const row = event.target.closest("tr[data-band]");
    if (!row) return;
    const band = row.dataset.band;
    const mode = row.querySelector(".band-preference-mode").value;
    if (mode === "default") {
        delete currentWsprBandPreferences[band];
    } else if (mode === "preset") {
        const value = row.querySelector(".band-preference-preset").value;
        if (value) currentWsprBandPreferences[band] = value;
    } else {
        const raw = String(row.querySelector(".band-preference-custom").value || "").trim();
        const value = Number(raw);
        if (/^[1-9]\d*$/.test(raw) && Number.isSafeInteger(value)) {
            currentWsprBandPreferences[band] = value;
        } else {
            delete currentWsprBandPreferences[band];
        }
    }
    refreshBandPreferenceRows();
    validatePage();
    scheduleAutosave();
}

function browserOfflineConfigMessage() {
    return "This browser is offline. Changes stay local until the connection returns.";
}

function transientConfigSaveMessage(textStatus = "") {
    const normalizedStatus = typeof textStatus === "string"
        ? textStatus.trim().toLowerCase()
        : "";

    if (navigator.onLine === false) {
        return browserOfflineConfigMessage();
    }

    if (normalizedStatus === "timeout") {
        return "The controller did not respond before the save timed out. Changes stay local until retry.";
    }

    return "The controller could not be reached for this save. Changes stay local until retry.";
}

function runtimeConnectionUnavailableMessage() {
    if (navigator.onLine === false) {
        return "This browser is offline, so runtime controls cannot reach the controller.";
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

    if (normalizedStatus === "timeout" || normalizedStatus === "error") {
        if (!xhr || typeof xhr.status !== "number" || xhr.status === 0) {
            return true;
        }
    }

    return !!xhr && typeof xhr.status === "number" && xhr.status === 0;
}

function bindConfigNetworkHandlers() {
    if (configNetworkHandlersBound) {
        return;
    }
    configNetworkHandlersBound = true;

    window.addEventListener("offline", () => {
        if (configAutosaveInFlight || configAutosaveDirty) {
            pauseConfigAutosaveForNetworkFailure(browserOfflineConfigMessage());
        }
        showBackendStatus(browserOfflineConfigMessage(), "warning", "runtime");
    });

    window.addEventListener("online", () => {
        clearBackendStatus("runtime");
        resumeConfigAutosaveAfterControllerRecovery();
    });
}

function currentConfigPayloadSnapshot(options = {}) {
    try {
        return JSON.stringify(buildConfigPayload(options));
    } catch {
        return "";
    }
}

function hasUnsavedLocalConfigChanges() {
    if (systemPaused) {
        return false;
    }

    if (configAutosaveInFlight || configAutosaveDirty || configAutosavePendingAfterFlight) {
        return true;
    }

    const snapshot = currentConfigPayloadSnapshot();
    if (!snapshot) {
        return false;
    }

    return snapshot !== lastSavedConfigPayload;
}

function bindConfigNavigationGuard() {
    if (configNavigationGuardBound) {
        return;
    }
    configNavigationGuardBound = true;

    window.addEventListener("beforeunload", (event) => {
        if (!hasUnsavedLocalConfigChanges()) {
            return;
        }

        event.preventDefault();
        event.returnValue = "";
    });
}

function removePersistedConfigDraft() {
    try {
        window.sessionStorage.removeItem(CONFIG_DRAFT_STORAGE_KEY);
    } catch {
    }
}

function persistLocalConfigDraftIfPossible() {
    if (
        systemPaused ||
        typeof validatePage !== "function" ||
        !validatePage({ allowInvalidStationIdentity: true })
    ) {
        return;
    }

    const snapshot = currentConfigPayloadSnapshot();
    if (!snapshot || snapshot === lastSavedConfigPayload) {
        removePersistedConfigDraft();
        return;
    }

    try {
        const parsed = JSON.parse(snapshot);
        if (parsed && parsed.Operation) {
            delete parsed.Operation.Transmit;
        }
        window.sessionStorage.setItem(
            CONFIG_DRAFT_STORAGE_KEY,
            JSON.stringify({
                version: 1,
                savedAt: Date.now(),
                payload: parsed,
            })
        );
    } catch {
    }
}

function restorePersistedConfigDraft() {
    if (suppressNextConfigDraftRestore) {
        suppressNextConfigDraftRestore = false;
        removePersistedConfigDraft();
        return false;
    }

    let rawDraft = "";
    try {
        rawDraft = window.sessionStorage.getItem(CONFIG_DRAFT_STORAGE_KEY) || "";
    } catch {
        return false;
    }

    if (!rawDraft) {
        return false;
    }

    let draft;
    try {
        draft = JSON.parse(rawDraft);
    } catch {
        removePersistedConfigDraft();
        return false;
    }

    const payload = draft && typeof draft.payload === "object" ? draft.payload : null;
    if (!payload) {
        removePersistedConfigDraft();
        return false;
    }

    const draftSnapshot = JSON.stringify(payload);
    if (draftSnapshot === lastSavedConfigPayload) {
        removePersistedConfigDraft();
        return false;
    }

    if (typeof suspendConfigAutosave === "function") {
        suspendConfigAutosave(true);
    }

    const operation = payload.Operation || {};
    const gpio = payload.GPIO || {};
    const calibration = payload.Calibration || {};
    const si5351 = payload.Si5351 || {};
    const wspr = payload.WSPR || {};
    const cw = payload.CW || {};
    const bandGpio = payload["Band GPIO"] || {};

    if (typeof applyConfigModeSelection === "function") {
        applyConfigModeSelection(String(operation.Mode || "WSPR"));
    }

    $("#planner_preference").val(String(wspr["Planner Preference"] || "auto")).trigger("change");
    $("#frequency_profile").val(String(wspr["Frequency Profile"] || "existing_common")).trigger("change");
    currentWsprBandPreferences = wspr["Band Preferences"] &&
        typeof wspr["Band Preferences"] === "object" &&
        !Array.isArray(wspr["Band Preferences"])
        ? { ...wspr["Band Preferences"] }
        : {};
    renderBandPreferenceRows();
    window.WtpUi?.populate(payload.WTP);
    setTransmitBackendSelection(operation["Transmit Backend"], true);
    if (typeof updateBackendPlatformSupportUi === "function") {
        updateBackendPlatformSupportUi();
    }

    if (typeof setTxPin === "function") {
        setTxPin(Number(gpio["Transmit Pin"]));
    }
    $("#use_led").prop("checked", !!operation["Use LED"]).trigger("change");
    if (typeof setLEDPin === "function") {
        setLEDPin(Number(operation["LED Pin"]));
    }
    $("#use_shutdown").prop("checked", !!operation["Use Shutdown"]).trigger("change");
    if (typeof setShutdownPin === "function") {
        setShutdownPin(Number(operation["Shutdown Button"]));
    }
    if (typeof setAmpPin === "function") {
        const ampPin = Number(operation["Amp Pin"]);
        setAmpPin(ampPin);
        if (typeof setUseAmp === "function") {
            const useAmp = typeof operation["Use Amp"] === "boolean"
                ? operation["Use Amp"]
                : Number.isInteger(ampPin) && ampPin >= 0;
            setUseAmp(useAmp && Number.isInteger(ampPin) && ampPin >= 0);
        }
    }
    $("#amp_active_high").prop("checked", !!operation["Amp Pin Active High"]).trigger("change");
    if (typeof populateBandGpioForm === "function") {
        populateBandGpioForm(bandGpio);
    }

    $("#callsign").val(String(wspr["Call Sign"] || "")).trigger("change");
    $("#gridsquare").val(String(wspr["Grid Square"] || "")).trigger("change");
    $("#dbm").val(Number(wspr["TX Power"])).trigger("change");
    $("#frequencies").val(String(wspr["Frequency"] || "")).trigger("change");
    $("#useoffset").prop("checked", !!wspr["Use Random Offset"]).trigger("change");

    $("#dot_length").val(Number(cw["Dot Seconds"])).trigger("change");
    $("#fsk_offset").val(Number(cw["Shift Hz"])).trigger("change");
    $("#qrss_frequency").val(Number(cw["Base Frequency"])).trigger("change");
    $("#cw_intra_element_gap").val(Number(cw["Intra Element Gap"])).trigger("change");
    $("#cw_inter_character_gap").val(Number(cw["Inter Character Gap"])).trigger("change");
    $("#cw_inter_word_gap").val(Number(cw["Inter Word Gap"])).trigger("change");
    $("#dfcw_intra_element_gap").val(Number(cw["DFCW Intra Element Gap"] ?? 0.333333)).trigger("change");
    $("#dfcw_inter_character_gap").val(Number(cw["DFCW Inter Character Gap"] ?? 1.0)).trigger("change");
    $("#dfcw_inter_word_gap").val(Number(cw["DFCW Inter Word Gap"] ?? 3.0)).trigger("change");
    synchronizeCwTimingAfterPopulation();
    $("#tx_start_minute").val(Number(cw["Start Minute"])).trigger("change");
    $("#tx_start_second").val(Number(cw["Start Second"] ?? 5)).trigger("change");
    $("#tx_repeat_every").val(Number(cw["Repeat Minutes"])).trigger("change");
    $("#qrss_message").val(String(cw.Message || "")).trigger("change");

    $("#use_system_clock_frequency_estimate").prop(
        "checked",
        gpio["Use System Clock Frequency Estimate"] !== false
    ).trigger("change");
    $("#gpio_frequency_residual_ppm").val(Number(gpio["Frequency Residual PPM"] ?? 0)).trigger("change");
    $("#gpio_manual_ppm").val(Number(gpio["Manual PPM"] ?? 0)).trigger("change");
    $("#ppm").val(Number(calibration.PPM)).trigger("change");

    $("#gpio-power-range").val(Number(gpio["Power Level"])).trigger("input");
    populateRp1GpioDrive(gpio["RP1 Drive mA"] ?? 2);
    populateI2cBuses(si5351["I2C Bus"]);
    refreshSi5351Addresses(
        Number(si5351["I2C Bus"]),
        si5351["I2C Address"] || "0x60"
    );
    $("#si5351_reference_frequency").val(Number(si5351["Reference Frequency"])).trigger("change");
    $("#si5351_reference_source").val(si5351["Reference Source"] || "external_tcxo").trigger("change");
    $("#si5351_crystal_load_capacitance")
        .val(String(si5351["Crystal Load Capacitance"] || 10))
        .trigger("change");
    $("#si5351-power-range").val(Number(si5351["Power Level"])).trigger("input");

    validatePage();
    configAutosaveDirty = true;
    configAutosavePendingAfterFlight = false;
    setConfigSaveStatus(
        "error",
        "Local draft restored",
        "Unsaved local configuration from this tab was restored. Review the draft and save when ready."
    );

    if (typeof suspendConfigAutosave === "function") {
        suspendConfigAutosave(false);
    }

    persistLocalConfigDraftIfPossible();
    return true;
}

function suppressNextPersistedConfigDraftRestore() {
    suppressNextConfigDraftRestore = true;
    removePersistedConfigDraft();
}

function bindIndexActions() {
    bindConfigNetworkHandlers();
    bindConfigNavigationGuard();

    // Bind the Mode Switch
    $('input[name="mode_toggle"]').on('change', clickModeToggle);

    // Operation.Transmit is global and is patched immediately, independent of Save.
    if ($("#transmit").length) {
        $("#transmit").on("change", patchTransmitControl);
    }

    // Stop is an explicit operator action, separate from Operation.Transmit PATCH.
    if ($("#stop_transmit").length) {
        $("#stop_transmit").on("click", stopTransmission);
    }

    // Bind the shared CW mode radio buttons
    $('input[name="qrss_type"]').on('change', clickQRSSModeToggle);
    organizeCwControlLayout();
    $('input[name="cw_speed"]').on("change", handleCwSpeedChange);
    $('input[name="cw_spacing"]').on("change", handleCwSpacingChange);
    $(".cw-repair-close").on("click", handleCwRepairClose);

    // Bind the GPIO system-clock frequency-estimate switch.
    $("#use_system_clock_frequency_estimate").on("change", clickUseSystemClockFrequencyEstimate);
    $("#transmit_backend").on("change", clickTransmitBackend);
    $("#tx_pin").on("change", clickTransmitPin);
    initializeRp1RouteUi();

    // Wire up the LED switch
    $("#use_led").on("change", clickUseLED);

    // Wire up the LED switch
    $("#use_shutdown").on("change", clickUseShutdown);

    // Wire up the Amp Control switch
    $("#use_amp").on("change", clickUseAmp);

    // Wire up Band GPIO switches
    $("#wsprform").on("change", ".band-gpio-enabled", clickBandGpioEnabled);
    $("#wsprform").on("change", "#band-gpio-enabled-all", () => {
        applyBandGpioColumnToggle("enabled", $("#band-gpio-enabled-all").is(":checked"));
    });
    $("#wsprform").on("change", "#band-gpio-active-high-all", () => {
        applyBandGpioColumnToggle("activeHigh", $("#band-gpio-active-high-all").is(":checked"));
    });
    $("#wsprform").on("change", ".band-gpio-active-high", syncBandGpioColumnHeaderStates);
    $("#wsprform").on("input change", ".band-gpio-input, .band-gpio-active-high", handleBandGpioInputChange);

    // Wire up the pin dropdown menus (only in the form)
    $('#wsprform')
        .off('click.pin', '[aria-labelledby="ledDropdownButton"] .dropdown-item, [aria-labelledby="shutdownDropdownButton"] .dropdown-item, [aria-labelledby="ampDropdownButton"] .dropdown-item', selectPin)
        .on('click.pin', '[aria-labelledby="ledDropdownButton"] .dropdown-item, [aria-labelledby="shutdownDropdownButton"] .dropdown-item, [aria-labelledby="ampDropdownButton"] .dropdown-item', selectPin);

    // Bind the transmit power slider
    $("#gpio-power-range").on("input", updateGpioPowerLabel);
    $("#rp1_gpio_drive_ma").on("change", function () {
        if (supportedRp1GpioDrive(this.value)) {
            this.removeAttribute("data-invalid-source-value");
        }
        syncBackendPanelVisibility();
        validateRp1GpioDrive();
        scheduleAutosave();
    });
    $("#si5351-power-range").on("input", updateSi5351PowerLabel);
    $("#si5351_reference_source").on("change", syncSi5351ReferenceControls);
    $("#configSaveStatusDetail").on("click", navigateToFirstInvalidConfigControl);
    $("#configSaveStatusDetail").on("keydown", handleConfigSaveStatusDetailKeydown);

    // Bind clicks on buttons/switches for resetting tooltips
    $(document).on(
        "click",
        'a[data-bs-toggle="tooltip"], button[data-bs-toggle="tooltip"]',
        resetToolTips
    );

    // Update WSPRNet link and bind changes to callsign
    $("#callsign").on("input blur", updateCallsign);

    // Run validation live as the user types:
    $("#frequencies").on("input blur", validateFrequencies);

    // Run validation live as the user types:
    $("#qrss_frequency").on("input blur", validateCwBaseFrequency);
    $("#qrss_message").on("input change blur", function () {
        validateCwMessage();
        updateCwMessageLengthEstimate();
        if (typeof renderRuntimeStatus === "function") {
            renderRuntimeStatus(currentRuntimeStatus);
        }
    });
    $("#dot_length").on("input blur", function () {
        validateCwDotSeconds();
        updateCwMessageLengthEstimate();
    });
    $("#fsk_offset").on("input blur", validateCwShiftHz);
    $("#tx_repeat_every").on("input blur", validateCwRepeatMinutes);
    $("#tx_start_minute").on("input blur", validateCwStartMinute);
    $("#tx_start_second").on("input blur", validateCwStartSecond);
    $("#cw_intra_element_gap").on("input blur", function () {
        validatePositiveCwField(
            "cw_intra_element_gap",
            "Enter a positive CW intra-element gap."
        );
        updateCwMessageLengthEstimate();
    });
    $("#cw_inter_character_gap").on("input blur", function () {
        validatePositiveCwField(
            "cw_inter_character_gap",
            "Enter a positive CW inter-character gap."
        );
        updateCwMessageLengthEstimate();
    });
    $("#cw_inter_word_gap").on("input blur", function () {
        validatePositiveCwField(
            "cw_inter_word_gap",
            "Enter a positive CW inter-word gap."
        );
        updateCwMessageLengthEstimate();
    });
    $("#dfcw_intra_element_gap").on("input blur", function () {
        validatePositiveCwField(
            "dfcw_intra_element_gap",
            "Enter a positive DFCW intra-element gap."
        );
        updateCwMessageLengthEstimate();
    });
    $("#dfcw_inter_character_gap").on("input blur", function () {
        validatePositiveCwField(
            "dfcw_inter_character_gap",
            "Enter a positive DFCW inter-character gap."
        );
        updateCwMessageLengthEstimate();
    });
    $("#dfcw_inter_word_gap").on("input blur", function () {
        validatePositiveCwField(
            "dfcw_inter_word_gap",
            "Enter a positive DFCW inter-word gap."
        );
        updateCwMessageLengthEstimate();
    });
    $("#dot_length, #cw_intra_element_gap, #cw_inter_character_gap, #cw_inter_word_gap, #dfcw_intra_element_gap, #dfcw_inter_character_gap, #dfcw_inter_word_gap")
        .on("input", function () {
            const fieldId = this.id;
            if (fieldId === "dot_length") {
                cwSpeedSelectionOverride = "Advanced";
            } else {
                const group = fieldId.startsWith("dfcw_") ? "dfcw" : "conventional";
                cwSpacingSelectionOverride[group] = "Advanced";
            }
            syncCwTimingControls({ announce: true });
        });
    $("#si5351_i2c_bus").on("change", function () {
        populateI2cBuses(this.value);
        refreshSi5351Addresses(Number(this.value));
        validateTransmitterHardwareFields();
    });
    $("#si5351_i2c_address").on("change", validateSi5351I2cAddress);
    $("#si5351_i2c_bus, #si5351_reference_frequency").on(
        "input blur",
        validateTransmitterHardwareFields
    );

    $("#band-preferences-body").on(
        "input change",
        ".band-preference-mode, .band-preference-preset, .band-preference-custom",
        handleBandPreferenceInput
    );
    $("#band-preferences-body").on("click", ".band-preference-clear", function () {
        const row = this.closest("tr[data-band]");
        if (!row) return;
        row.querySelector(".band-preference-mode").value = "default";
        row.querySelector(".band-preference-custom").value = "";
        handleBandPreferenceInput({ target: row.querySelector(".band-preference-mode") });
    });
    $("#frequency_profile").on("change", refreshBandPreferenceRows);

    // Bind any text/number/select control changes
    $(document).on(
        "input change",
        '.form-control:not([type="range"], .form-check-input)',
        function () {
            validatePage();
            scheduleAutosave();
        }
    );

    $("#wsprform").on(
        "change input",
        'input:not(#transmit, [name="mode_toggle"], [name="qrss_type"]), select:not(#tx_pin), textarea',
        scheduleAutosave
    );

    bindTestToneControls();
    bindModeChangeGuardModal();
    currentConfigModeSelection = selectedConfigMode();
}

function setTransmitFromBackend(enabled) {
    isUpdatingTransmitFromBackend = true;
    $("#transmit").prop("checked", !!enabled);
    isUpdatingTransmitFromBackend = false;
    updateRuntimeControlStatusFromForm(null);
}

function syncStopButtonState() {
    const $stop = $("#stop_transmit");
    if (!$stop.length) {
        return;
    }

    const runtimeStatus =
        typeof currentRuntimeStatus === "object" && currentRuntimeStatus !== null
            ? currentRuntimeStatus
            : null;
    const runtimeConfigStatus =
        typeof currentRuntimeConfigStatus === "object" &&
        currentRuntimeConfigStatus !== null
            ? currentRuntimeConfigStatus
            : null;

    const transmitting = runtimeStatus && runtimeStatus.txState === "transmitting";
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

function requestTransmitEnabledChange(enabled, previousEnabled, options = {}) {
    const $transmit = $("#transmit");
    const updateCheckboxOnSuccess = options.updateCheckboxOnSuccess === true;
    const syncAutosaveBaselineOnSuccess =
        options.syncAutosaveBaselineOnSuccess !== false;
    const onSuccess =
        typeof options.onSuccess === "function" ? options.onSuccess : null;
    const onFailure =
        typeof options.onFailure === "function" ? options.onFailure : null;

    if (enabled) {
        const unavailableMessage = currentTransmitUnavailableMessage();
        if (unavailableMessage) {
            const formattedMessage = formatTransmitFailureMessage(unavailableMessage);
            setTransmitFromBackend(previousEnabled);
            showBackendStatus(formattedMessage, "danger", "runtime");
            if (typeof showMessageDialog === "function") {
                showMessageDialog({
                    title: "Transmit unavailable",
                    message: formattedMessage,
                    acknowledgeLabel: "Close"
                });
            }
            if (onFailure) {
                onFailure(formattedMessage);
            }
            return null;
        }
    }

    if (navigator.onLine === false) {
        const message = runtimeConnectionUnavailableMessage();
        setTransmitFromBackend(previousEnabled);
        showBackendStatus(message, "warning", "runtime");
        if (onFailure) {
            onFailure(message);
        }
        return null;
    }

    $transmit.prop("disabled", true);

    return ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {
        type: "PATCH",
        headers: window.WtpUi?.hostRevision ? { "If-Match": window.WtpUi.hostRevision } : {},
        contentType: "application/merge-patch+json",
        timeout: CONFIG_REQUEST_TIMEOUT_MS,
        data: JSON.stringify({
            Operation: {
                "Transmit": enabled,
            },
        }),
    })
        .done(function (data, textStatus, xhr) {
            window.WtpUi?.setHostRevision(xhr?.getResponseHeader?.("ETag"));
            lastSaveTimestamp = Date.now();
            if (onSuccess) {
                onSuccess();
            }
            if (updateCheckboxOnSuccess) {
                setTransmitFromBackend(enabled);
            }
            updateRuntimeControlStatusFromForm(null);
            clearBackendStatus("runtime");
            if (typeof getTxState === "function") {
                getTxState();
            }
            if (syncAutosaveBaselineOnSuccess &&
                typeof syncConfigAutosaveBaseline === "function") {
                syncConfigAutosaveBaseline();
            }
        })
        .fail(function (xhr, textStatus) {
            let message = "Failed to update transmit state.";
            console.error("Failed to update Operation.Transmit:", xhr);

            if (isTransientNetworkFailure(xhr, textStatus)) {
                message = transientRuntimeActionMessage(textStatus);
                showBackendStatus(message, "warning", "runtime");
                setTransmitFromBackend(previousEnabled);
                if (onFailure) {
                    onFailure(message);
                }
                return;
            }

            if (xhr.responseJSON && typeof xhr.responseJSON === "object") {
                message = buildConfigErrorMessage(xhr.responseJSON, message);
            } else if (typeof xhr.responseText === "string" && xhr.responseText.trim()) {
                try {
                    const parsedError = JSON.parse(xhr.responseText);
                    if (parsedError && typeof parsedError === "object") {
                        message = buildConfigErrorMessage(parsedError, message);
                    }
                } catch (error) {
                    console.warn("Unable to parse transmit toggle error response:", error);
                }
            }

            message = formatTransmitFailureMessage(message);
            setTransmitFromBackend(previousEnabled);
            showBackendStatus(message, "danger", "runtime");
            if (typeof showMessageDialog === "function") {
                showMessageDialog({
                    title: "Unable to update transmit state",
                    message,
                    acknowledgeLabel: "Close"
                });
            }
            if (onFailure) {
                onFailure(message);
            }
        })
        .always(function () {
            $transmit.prop("disabled", false);
        });
}

function patchTransmitControl() {
    if (isUpdatingTransmitFromBackend) return;

    const enabled = $("#transmit").is(":checked");
    const previous = !enabled;

    if (enabled && pendingPersistedMode) {
        setTransmitFromBackend(previous);
        if (!configAutosaveInFlight && typeof flushAutosave === "function") {
            flushAutosave();
        }
        showBackendStatus(
            "Wait for the mode change to save before enabling transmissions.",
            "warning",
            "runtime"
        );
        return;
    }

    requestTransmitEnabledChange(enabled, previous);
}

function stopTransmission(options = {}) {
    const $stop = $("#stop_transmit");
    if ($stop.prop("disabled")) {
        return false;
    }

    if (!ws || ws.readyState !== WebSocket.OPEN) {
        const message = runtimeConnectionUnavailableMessage();
        console.error("Failed to stop transmission: WebSocket is not connected.");
        showBackendStatus(message, "warning", "runtime");
        return false;
    }

    stopRequestInFlight = true;
    syncStopButtonState();
    clearStopRequestTimeout();
    stopRequestTimeoutHandle = window.setTimeout(() => {
        failStopRequest("Stop command timed out before the controller confirmed it. Check controller connectivity and runtime state, then try again.");
    }, STOP_REQUEST_TIMEOUT_MS);

    const persistTransmit =
        options && options.persistTransmit === false ? false : true;
    ws.send(
        JSON.stringify({
            command: "stop",
            persist_transmit: persistTransmit,
        })
    );
    return true;
}

function handleStopCommandResponse(message) {
    const response = message && typeof message === "object" ? message : {};
    const stopSucceeded =
        response.transmit_disabled === true ||
        response.stop_performed === true ||
        response.status === "ok";
    clearStopRequestTimeout();

    if (pendingModeChange && pendingModeChange.awaitingGuardedStop === true) {
        if (stopSucceeded) {
            completeGuardedActiveModeChange();
        } else {
            failGuardedActiveModeChange(
                response.message || "Failed to disable transmissions before switching modes."
            );
        }
    } else if (
        pendingModeChange &&
        pendingModeChange.guardedActiveModeChange !== true &&
        pendingModeChange.awaitingRuntimeIdle === false
    ) {
        if (stopSucceeded) {
            pendingModeChange.awaitingRuntimeIdle = true;
        } else {
            clearPendingModeChange();
        }
    }

    if (response.transmit_disabled === true) {
        setTransmitFromBackend(false);
    }
    if (typeof getTxState === "function") {
        getTxState();
    }

    stopRequestInFlight = false;
    syncStopButtonState();

    if (!stopSucceeded) {
        console.error("Failed to stop transmission:", response);
    }
}

function selectedConfigMode() {
    const mode = $('input[name="mode_toggle"]:checked').val();
    if (mode === "WSPR") {
        return "WSPR";
    }

    return $('input[name="qrss_type"]:checked').val() || "QRSS";
}

function organizeCwControlLayout() {
    if (typeof CwTimingState === "undefined" || !document.getElementById("cw_modulation_controls")) {
        return;
    }

    const moveField = (fieldId, targetId) => {
        const field = document.getElementById(fieldId);
        const target = document.getElementById(targetId);
        const wrapper = field ? field.closest(".config-stacked-field") : null;
        if (wrapper && target && !target.contains(wrapper)) target.appendChild(wrapper);
    };

    const modeSelect = document.getElementById("mode_select");
    const modeFieldset = modeSelect ? modeSelect.closest("fieldset") : null;
    if (modeFieldset) document.getElementById("cw_modulation_controls").appendChild(modeFieldset);
    moveField("dot_length", "cw_dot_duration_control");
    ["fsk_offset", "qrss_frequency"].forEach((id) => moveField(id, "cw_frequency_controls"));
    ["tx_start_minute", "tx_start_second", "tx_repeat_every"].forEach((id) => moveField(id, "cw_schedule_controls"));

    const shared = document.getElementById("cw_intra_element_gap");
    const sharedRow = shared ? shared.closest(".row") : null;
    const dfcw = document.getElementById("dfcw_intra_element_gap");
    const dfcwRow = dfcw ? dfcw.closest(".row") : null;
    if (sharedRow) document.getElementById("cw_conventional_gap_section").appendChild(sharedRow);
    if (dfcwRow) document.getElementById("cw_dfcw_gap_section").appendChild(dfcwRow);
    ["dot_length", ...cwTimingFieldIds("conventional"), ...cwTimingFieldIds("dfcw")]
        .forEach((id) => {
            const field = document.getElementById(id);
            if (field) field.dataset.cwTimingValue = "true";
        });

    document.querySelectorAll("#qrss_control > .row").forEach((row) => {
        if (!row.querySelector("input, select, textarea, button")) row.remove();
    });

    [
        ["cw_intra_element_gap", "cw-intra-gap-hint", "cw-intra-duration"],
        ["cw_inter_character_gap", "cw-inter-character-gap-hint", "cw-character-duration"],
        ["cw_inter_word_gap", "cw-inter-word-gap-hint", "cw-word-duration"],
        ["dfcw_intra_element_gap", "dfcw-intra-gap-hint", "dfcw-intra-duration"],
        ["dfcw_inter_character_gap", "dfcw-inter-character-gap-hint", "dfcw-character-duration"],
        ["dfcw_inter_word_gap", "dfcw-inter-word-gap-hint", "dfcw-word-duration"],
    ].forEach(([fieldId, hintId, outputId]) => {
        const hint = document.getElementById(hintId);
        const field = document.getElementById(fieldId);
        if (!hint || !field || document.getElementById(outputId)) return;
        const output = document.createElement("span");
        output.id = outputId;
        output.className = "cw-gap-duration";
        output.setAttribute("aria-live", "polite");
        hint.appendChild(output);
        const describedBy = new Set(String(field.getAttribute("aria-describedby") || "").split(/\s+/).filter(Boolean));
        describedBy.add(outputId);
        field.setAttribute("aria-describedby", Array.from(describedBy).join(" "));
    });
}

function readCwTimingStateFromDom() {
    const numberValue = (id) => Number(String($(id).val() ?? "").trim());
    return {
        dotSeconds: numberValue("#dot_length"),
        conventional: {
            intraElement: numberValue("#cw_intra_element_gap"),
            interCharacter: numberValue("#cw_inter_character_gap"),
            interWord: numberValue("#cw_inter_word_gap"),
        },
        dfcw: {
            intraElement: numberValue("#dfcw_intra_element_gap"),
            interCharacter: numberValue("#dfcw_inter_character_gap"),
            interWord: numberValue("#dfcw_inter_word_gap"),
        },
    };
}

function writeCwTimingStateToDom(state) {
    $("#dot_length").val(state.dotSeconds);
    $("#cw_intra_element_gap").val(state.conventional.intraElement);
    $("#cw_inter_character_gap").val(state.conventional.interCharacter);
    $("#cw_inter_word_gap").val(state.conventional.interWord);
    $("#dfcw_intra_element_gap").val(state.dfcw.intraElement);
    $("#dfcw_inter_character_gap").val(state.dfcw.interCharacter);
    $("#dfcw_inter_word_gap").val(state.dfcw.interWord);
}

function cwTimingFieldIds(group) {
    return group === "dfcw"
        ? ["dfcw_intra_element_gap", "dfcw_inter_character_gap", "dfcw_inter_word_gap"]
        : ["cw_intra_element_gap", "cw_inter_character_gap", "cw_inter_word_gap"];
}

function invalidCwTimingGroups(state = readCwTimingStateFromDom()) {
    const invalid = CwTimingState.invalidFields(state);
    return {
        dot: invalid.includes("dotSeconds"),
        conventional: invalid.some((name) => name.startsWith("conventional.")),
        dfcw: invalid.some((name) => name.startsWith("dfcw.")),
    };
}

function inactiveInvalidCwTimingGroup() {
    const active = CwTimingState.activeGroup(selectedConfigMode());
    const invalid = invalidCwTimingGroups();
    const inactive = active === "dfcw" ? "conventional" : "dfcw";
    return invalid[inactive] ? inactive : null;
}

function formatCwGapDuration(value) {
    if (!Number.isFinite(value)) return "Invalid duration";
    return `${Number(value.toFixed(6))} seconds`;
}

function syncCwTimingControls(options = {}) {
    if (typeof CwTimingState === "undefined") return;
    const state = readCwTimingStateFromDom();
    const mode = selectedConfigMode();
    if (mode === "WSPR") return;
    const active = CwTimingState.activeGroup(mode);
    const inferredSpeed = CwTimingState.inferSpeed(state.dotSeconds);
    const speed = cwSpeedSelectionOverride || inferredSpeed;
    $(`input[name="cw_speed"][value="${speed}"]`).prop("checked", true);
    $("#dot_length").prop("disabled", speed !== "Advanced");
    $("#dot-length-hint").text(speed === "Advanced"
        ? "Enter a positive finite base duration in seconds."
        : `${speed} determines this shared base duration. Select Advanced to edit it.`);

    const spacing = cwSpacingSelectionOverride[active] ||
        CwTimingState.inferSpacing(active, state[active]);
    $(`input[name="cw_spacing"][value="${spacing}"]`).prop("checked", true);
    const invalid = invalidCwTimingGroups(state);

    ["conventional", "dfcw"].forEach((group) => {
        const isActive = group === active;
        const isRepair = !isActive && cwRepairRevealed[group];
        const section = document.getElementById(group === "dfcw" ? "cw_dfcw_gap_section" : "cw_conventional_gap_section");
        const repairHeader = section ? section.querySelector(".cw-repair-header") : null;
        if (section) section.hidden = !isActive && !isRepair;
        if (repairHeader) repairHeader.hidden = !isRepair;
        cwTimingFieldIds(group).forEach((id) => {
            const editable = isRepair || (isActive && spacing === "Advanced");
            $(`#${id}`).prop("disabled", !editable);
        });
    });

    const durations = CwTimingState.gapDurations(state.dotSeconds, state[active]);
    const prefix = active === "dfcw" ? "dfcw" : "cw";
    const outputs = [`${prefix}-intra-duration`, `${prefix}-character-duration`, `${prefix}-word-duration`];
    const values = durations ? [durations.intraElement, durations.interCharacter, durations.interWord] : [NaN, NaN, NaN];
    outputs.forEach((id, index) => {
        const output = document.getElementById(id);
        if (output) output.textContent = ` × base duration = ${formatCwGapDuration(values[index])}`;
    });

    const explanation = document.getElementById("cw-mode-timing-explanation");
    if (explanation) {
        explanation.textContent = active === "dfcw"
            ? "DFCW uses equal-duration, frequency-distinguished elements and its 0.333333×/1×/3× standard spacing."
            : "QRSS and FSKCW use conventional dot, dash, and 1×/3×/7× standard spacing.";
    }
    if (options.announce) updateCwMessageLengthEstimate();
}

function handleCwSpeedChange(event) {
    const speed = event.target.value;
    const state = readCwTimingStateFromDom();
    cwSpeedSelectionOverride = speed;
    if (speed !== "Advanced") writeCwTimingStateToDom(CwTimingState.applySpeed(state, speed));
    syncCwTimingControls({ announce: true });
    validatePage();
    if (speed !== "Advanced") scheduleAutosave();
}

function handleCwSpacingChange(event) {
    const spacing = event.target.value;
    const mode = selectedConfigMode();
    const group = CwTimingState.activeGroup(mode);
    cwSpacingSelectionOverride[group] = spacing;
    if (spacing === "Standard") {
        writeCwTimingStateToDom(CwTimingState.applySpacing(readCwTimingStateFromDom(), mode, spacing));
    }
    syncCwTimingControls({ announce: true });
    validatePage();
    if (spacing === "Standard") scheduleAutosave();
}

function revealCwTimingRepair(group) {
    cwRepairRevealed[group] = true;
    syncCwTimingControls();
    const action = document.querySelector("#configSaveStatusDetail button[aria-controls]");
    if (action) action.setAttribute("aria-expanded", "true");
    const firstInvalid = cwTimingFieldIds(group)
        .map((id) => document.getElementById(id))
        .find((field) => field && CwTimingState.positiveFinite(field.value) === null);
    focusInvalidConfigControl(firstInvalid);
}

function handleCwRepairClose(event) {
    const group = event.currentTarget.dataset.group;
    const invalid = invalidCwTimingGroups();
    if (invalid[group]) return;
    cwRepairRevealed[group] = false;
    syncCwTimingControls();
    const action = document.querySelector("#configSaveStatusDetail button");
    if (action) {
        action.setAttribute("aria-expanded", "false");
        action.focus();
    }
}

function synchronizeCwTimingAfterPopulation() {
    cwSpeedSelectionOverride = null;
    cwSpacingSelectionOverride.conventional = null;
    cwSpacingSelectionOverride.dfcw = null;
    cwRepairRevealed.conventional = false;
    cwRepairRevealed.dfcw = false;
    syncCwTimingControls();
}

function isWsprConfigMode() {
    return selectedConfigMode() === "WSPR";
}

const CW_MESSAGE_MORSE_TABLE = CwTimingState.MORSE_TABLE;

function parsePositiveFormNumber(fieldId) {
    const field = document.getElementById(fieldId);
    if (!field) {
        return Number.NaN;
    }

    const value = Number.parseFloat(field.value);
    return Number.isFinite(value) && value > 0 ? value : Number.NaN;
}

function estimateCwMessageSeconds(message, mode, timing) {
    return CwTimingState.estimateMessageSeconds(message, mode, timing);
}

function formatCompactDuration(seconds) {
    const roundedSeconds = Math.round(seconds);
    if (roundedSeconds < 60) {
        return "";
    }

    const minutes = Math.floor(roundedSeconds / 60);
    const remainingSeconds = roundedSeconds % 60;
    return `${minutes}m ${remainingSeconds}s`;
}

function formatCwMessageLengthEstimate(seconds) {
    const secondsText = `${seconds.toFixed(1)} s`;
    const compact = formatCompactDuration(seconds);
    return compact ? `${secondsText} (${compact})` : secondsText;
}

function currentCwMessageTiming(mode) {
    const dotSeconds = parsePositiveFormNumber("dot_length");

    if (mode === "DFCW") {
        return {
            dotSeconds,
            intraElementGapSeconds:
                dotSeconds * parsePositiveFormNumber("dfcw_intra_element_gap"),
            interCharacterGapSeconds:
                dotSeconds * parsePositiveFormNumber("dfcw_inter_character_gap"),
            interWordGapSeconds:
                dotSeconds * parsePositiveFormNumber("dfcw_inter_word_gap"),
        };
    }

    return {
        dotSeconds,
        intraElementGapSeconds:
            dotSeconds * parsePositiveFormNumber("cw_intra_element_gap"),
        interCharacterGapSeconds:
            dotSeconds * parsePositiveFormNumber("cw_inter_character_gap"),
        interWordGapSeconds:
            dotSeconds * parsePositiveFormNumber("cw_inter_word_gap"),
    };
}

function updateCwMessageLengthEstimate() {
    const display = document.getElementById("cw_message_length_estimate");
    if (!display) {
        return;
    }

    const mode = selectedConfigMode();
    if (mode === "WSPR") {
        display.textContent = "Estimated Message Length: not applicable";
        updateCwDurationPolicyLatch();
        return;
    }

    const estimate = estimateCwMessageSeconds(
        $("#qrss_message").val(),
        mode,
        currentCwMessageTiming(mode)
    );

    display.textContent = estimate.ok
        ? `Estimated Message Length: ${formatCwMessageLengthEstimate(estimate.seconds)}`
        : `Estimated Message Length: ${estimate.reason}`;

    if (typeof WtpUi !== "undefined" && WtpUi.selected()) {
        const count = String($("#qrss_message").val() || "").length;
        const plan = picoCwMessagePlan(String($("#qrss_message").val() || ""),mode,currentCwMessageTiming(mode));
        display.textContent = plan.ok ? `Calculated Message Length: ${formatPicoDuration(plan.duration)} · ${count} / 32 characters · ${plan.events} / ${WtpUi.maximumJobEvents || 512} events · finite-job limit ${formatPicoDuration(picoDurationLimitNs())}` : `Calculated Message Length: ${plan.reason}`;
    }
    const messageHint = document.getElementById("qrss-message-hint");
    if (messageHint) messageHint.textContent = typeof WtpUi !== "undefined" && WtpUi.selected()
        ? "Pico: up to 32 characters including spaces. Duration and event limits also apply; the message is never split or truncated."
        : "Enter the exact CW message to send. This field cannot be empty.";
    updateCwDurationPolicyLatch();

    return estimate;
}

// Match the parent compiler: quantize each timing field to integer ns first,
// then sum marks/gaps. Conventional dash is three quantized dots.
function picoCwMessagePlan(message, mode, timing) {
    try {
        const ns = seconds => {
            if (!Number.isFinite(seconds) || seconds <= 0) throw new Error("Use positive finite timing values");
            const value = BigInt(Math.trunc(seconds * 1e9));
            if (value <= 0n || value > 9223372036854775807n) throw new Error("Timing is outside the finite nanosecond range");
            return value;
        };
        const dot=ns(timing.dotSeconds), dash=mode==="DFCW"?dot:dot*3n;
        const intra=ns(timing.intraElementGapSeconds), character=ns(timing.interCharacterGapSeconds), word=ns(timing.interWordGapSeconds);
        if (dash>9223372036854775807n) throw new Error("Dash timing is outside the finite nanosecond range");
        let duration=0n, events=0;
        for (let i=0;i<message.length;++i) {
            if (/[ \t\r\n\f\v]/.test(message[i])) continue;
            const code=CW_MESSAGE_MORSE_TABLE[message[i].toUpperCase()];
            if (!code || message.charCodeAt(i)>127) throw new Error("Use the supported Morse alphabet");
            for (let n=0;n<code.length;++n) {
                duration += mode==="DFCW" || code[n]==="." ? dot : dash; ++events;
                if (n+1<code.length) {duration+=intra;++events;}
            }
            let next=i+1;while(next<message.length && /[ \t\r\n\f\v]/.test(message[next])) ++next;
            if(next<message.length) {duration+=next===i+1?character:word;++events;}
        }
        if (!events) throw new Error("Include at least one Morse character");
        return {ok:true,duration,events};
    } catch(error) {return {ok:false,reason:error.message};}
}
function picoDurationLimitNs() {
    return BigInt(WtpUi.maximumJobDurationNs || Math.round((WtpUi.maximumJobDurationSeconds || 3600)*1e9));
}
function formatPicoDuration(ns) {
    const fraction=(ns%1000000000n).toString().padStart(9,"0").replace(/0+$/,"");
    const exact=(ns/1000000000n).toString()+(fraction?"."+fraction:"")+" s";
    const compact=formatCompactDuration(Math.floor(Number(ns)/1e9));
    return exact+(compact?" ("+compact+")":"");
}

function picoCwMessageConstraint() {
    if (typeof WtpUi === "undefined" || !WtpUi.selected() ||
        !["QRSS", "FSKCW", "DFCW"].includes(selectedConfigMode())) return null;
    const message = String($("#qrss_message").val() || "");
    if ([...message].some(character => character.charCodeAt(0) > 127)) return "Use the supported Morse alphabet; non-ASCII characters are not supported by the Pico.";
    const limit = picoDurationLimitNs();
    const eventLimit = WtpUi.maximumJobEvents || 512;
    if (message.length > 32) return `Pico messages allow 32 characters including spaces; this message has ${message.length}. Shorten the message.`;
    const plan = picoCwMessagePlan(message,selectedConfigMode(),currentCwMessageTiming(selectedConfigMode()));
    if (!plan.ok) return plan.reason;
    if (plan.duration > limit) return `Calculated duration: ${formatPicoDuration(plan.duration)}. This Pico allows ${formatPicoDuration(limit)} per finite job. Shorten the timing or message.`;
    if (plan.events > eventLimit) return `This message needs ${plan.events} events; the connected Pico allows ${eventLimit}. Use a simpler message or firmware with sufficient capacity.`;
    return "";
}

function cwMessageOrdinaryValidation(message) {
    const text = String(message || "").trim();
    if (!text) {
        return { valid: false, message: "CW message is required." };
    }

    for (const ch of text) {
        if (!/\s/.test(ch) && !CW_MESSAGE_MORSE_TABLE[ch.toUpperCase()]) {
            return {
                valid: false,
                message: `CW message contains unsupported character ${ch}.`,
            };
        }
    }

    return { valid: true, message: "" };
}

function currentCwDurationConstraint() {
    const mode = selectedConfigMode();
    if (!["QRSS", "FSKCW", "DFCW"].includes(mode)) {
        return { applicable: false };
    }

    const estimate = estimateCwMessageSeconds(
        $("#qrss_message").val(),
        mode,
        currentCwMessageTiming(mode)
    );
    const repeatMinutes = parsePositiveFormNumber("tx_repeat_every");
    if (!estimate.ok || !Number.isFinite(repeatMinutes)) {
        return { applicable: false, estimate, mode };
    }

    const repeatSeconds = repeatMinutes * 60;
    return {
        applicable: true,
        mode,
        seconds: estimate.seconds,
        repeatSeconds,
        overLimit: estimate.seconds > repeatSeconds,
    };
}

function cwDurationPolicyDetail(constraint) {
    return `The calculated ${constraint.mode} message duration is ${formatCwMessageLengthEstimate(constraint.seconds)}, which exceeds the repeat interval of ${formatCwMessageLengthEstimate(constraint.repeatSeconds)}. Shorten the message, shorten the dot length or active spacing, or increase the repeat interval.`;
}

function updateCwDurationPolicyLatch(options = {}) {
    const constraint = currentCwDurationConstraint();
    const shouldLatch = constraint.applicable && constraint.overLimit;
    const wasLatched = cwDurationPolicyLatched;
    cwDurationPolicyLatched = shouldLatch;

    if (cwDurationPolicyLatched) {
        if (configAutosaveTimer) {
            clearTimeout(configAutosaveTimer);
            configAutosaveTimer = null;
        }
        setConfigSaveStatus(
            "error",
            "Save failed",
            cwDurationPolicyDetail(constraint)
        );
    } else if (wasLatched) {
        lastFailedConfigPayload = "";
        lastFailedConfigMessage = "";
        setConfigSaveStatus("saving", "Saving...", "");
    }

    if (options.markDirty && cwDurationPolicyLatched) {
        configAutosaveDirty = true;
        persistLocalConfigDraftIfPossible();
    }

    return constraint;
}

function isCwDurationPolicyError(data) {
    return !!data && typeof data === "object" &&
        data.policy === "cw_duration_repeat_interval" &&
        data.field === "CW.Message";
}

function handleCwDurationPolicyFailure(messageOrData) {
    const structured = isCwDurationPolicyError(messageOrData);
    const message = typeof messageOrData === "string"
        ? messageOrData
        : (messageOrData && typeof messageOrData.message === "string"
            ? messageOrData.message
            : "");
    const recognizedMessage = /^Configured (QRSS|FSKCW|DFCW) message duration of .+ exceeds repeat_every interval of .+\. Reduce the message length, shorten the unit length, or increase repeat_every\.$/.test(message.trim());
    const constraint = currentCwDurationConstraint();

    if ((!structured && !recognizedMessage) ||
        !constraint.applicable || !constraint.overLimit) {
        return false;
    }

    cwDurationPolicyLatched = true;
    validateCwMessage();
    updateCwDurationPolicyLatch({ markDirty: true });
    return true;
}

function bindModeChangeGuardModal() {
    const modalEl = document.getElementById("modeChangeGuardModal");
    if (!modalEl) {
        return;
    }

    $(modalEl)
        .off("hidden.bs.modal.modeGuard")
        .on("hidden.bs.modal.modeGuard", function () {
            if (pendingModeChange && !modeChangeGuardBusy) {
                clearPendingModeChange();
            }
        });
}

function modeChangeGuardModalInstance() {
    const modalEl = document.getElementById("modeChangeGuardModal");
    if (!modalEl) {
        return null;
    }

    return bootstrap.Modal.getOrCreateInstance(modalEl, {
        backdrop: "static",
        keyboard: false,
    });
}

function clearModeChangeGuardStopTimeout() {
    if (modeChangeGuardStopTimeoutHandle !== null) {
        clearTimeout(modeChangeGuardStopTimeoutHandle);
        modeChangeGuardStopTimeoutHandle = null;
    }
}

function setModeChangeGuardActionBusy(busy, label = "Disable & switch") {
    const confirmBtn = document.getElementById("modeChangeGuardConfirmBtn");
    const cancelBtn = document.getElementById("modeChangeGuardCancelBtn");
    const modalEl = document.getElementById("modeChangeGuardModal");

    if (confirmBtn) {
        confirmBtn.disabled = !!busy;
        confirmBtn.textContent = busy ? "Working..." : label;
    }
    if (cancelBtn) {
        cancelBtn.disabled = !!busy;
    }
    if (modalEl) {
        const closeBtn = modalEl.querySelector(".btn-close");
        if (closeBtn) {
            closeBtn.disabled = !!busy;
        }
    }
}

function clearPendingModeChange() {
    clearModeChangeGuardStopTimeout();
    pendingModeChange = null;
    modeChangeGuardBusy = false;
    setModeChangeGuardActionBusy(false);
    if (typeof suspendConfigAutosave === "function") {
        suspendConfigAutosave(false);
    }
}

function markDisabledModeSwitchReloadFailureSuppression(mode) {
    disabledModeSwitchReloadFailureSuppression = {
        mode,
        expiresAt: Date.now() + CONFIG_REQUEST_TIMEOUT_MS,
    };
}

function clearDisabledModeSwitchReloadFailureSuppression() {
    disabledModeSwitchReloadFailureSuppression = null;
}

function shouldSuppressDisabledModeSwitchReloadFailure(message) {
    if (!disabledModeSwitchReloadFailureSuppression) {
        return false;
    }

    if (Date.now() > disabledModeSwitchReloadFailureSuppression.expiresAt) {
        clearDisabledModeSwitchReloadFailureSuppression();
        return false;
    }

    const transmitDisabled = !$("#transmit").is(":checked");
    const activeMode = selectedConfigMode();
    const expectedMode = disabledModeSwitchReloadFailureSuppression.mode;
    const normalizedMessage = String(message || "").trim();
    const isModePayloadError =
        /^(QRSS|FSKCW|DFCW) payload message is empty\.$/.test(normalizedMessage);

    if (transmitDisabled && activeMode === expectedMode && isModePayloadError) {
        clearDisabledModeSwitchReloadFailureSuppression();
        return true;
    }

    return false;
}

function persistDisabledModeChange(targetMode, previousMode = currentConfigModeSelection) {
    const normalizedTargetMode = ["WSPR", "QRSS", "FSKCW", "DFCW"].includes(targetMode)
        ? targetMode
        : previousMode;

    if (navigator.onLine === false) {
        const message = runtimeConnectionUnavailableMessage();
        showBackendStatus(message, "warning", "runtime");
        revertConfigModeSelection();
        setTransmitFromBackend(true);
        clearPendingModeChange();
        return null;
    }

    setTransmitFromBackend(false);
    updateRuntimeControlStatusFromForm(previousMode);
    markDisabledModeSwitchReloadFailureSuppression(normalizedTargetMode);

    return ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {
        type: "PATCH",
        headers: window.WtpUi?.hostRevision ? { "If-Match": window.WtpUi.hostRevision } : {},
        contentType: "application/merge-patch+json",
        timeout: CONFIG_REQUEST_TIMEOUT_MS,
        data: JSON.stringify({
            Operation: {
                "Mode": normalizedTargetMode,
                "Transmit": false,
            },
        }),
    })
        .done(function (data, textStatus, xhr) {
            window.WtpUi?.setHostRevision(xhr?.getResponseHeader?.("ETag"));
            lastSaveTimestamp = Date.now();
            pendingPersistedMode = "";
            configAutosaveNeedsRuntimeRefresh = true;
            clearBackendStatus("runtime");
            if (typeof suppressNextPersistedConfigDraftRestore === "function") {
                suppressNextPersistedConfigDraftRestore();
            }
            if (typeof suspendConfigAutosave === "function") {
                suspendConfigAutosave(true);
            }
            applyCommittedConfigMode(normalizedTargetMode, {
                skipAutosave: true,
                keepAutosaveSuspended: true,
            });
            setTransmitFromBackend(false);
            updateRuntimeControlStatusFromForm(normalizedTargetMode);
            if (typeof syncConfigAutosaveBaseline === "function") {
                syncConfigAutosaveBaseline();
            }
            if (typeof populateConfig === "function") {
                populateConfig();
            }
            if (typeof getTxState === "function") {
                getTxState();
            }
            clearPendingModeChange();
        })
        .fail(function (xhr, textStatus) {
            clearDisabledModeSwitchReloadFailureSuppression();
            let message = "Failed to disable transmissions and change mode.";

            if (isTransientNetworkFailure(xhr, textStatus)) {
                message = transientRuntimeActionMessage(textStatus);
            } else if (xhr.responseJSON && typeof xhr.responseJSON === "object") {
                message = buildConfigErrorMessage(xhr.responseJSON, message);
            } else if (typeof xhr.responseText === "string" && xhr.responseText.trim()) {
                try {
                    const parsedError = JSON.parse(xhr.responseText);
                    if (parsedError && typeof parsedError === "object") {
                        message = buildConfigErrorMessage(parsedError, message);
                    }
                } catch (error) {
                    console.warn("Unable to parse guarded mode-change error response:", error);
                }
            }

            revertConfigModeSelection();
            showBackendStatus(message, "danger", "runtime");
            if (typeof getTxState === "function") {
                getTxState();
            }
            clearPendingModeChange();
        });
}

function applyCommittedConfigMode(mode, options = {}) {
    suppressModeChangeGuard = true;
    applyConfigModeSelection(mode);
    suppressModeChangeGuard = false;
    updateRuntimeControlStatusFromForm(mode);
    if (typeof renderRuntimeStatus === "function") {
        renderRuntimeStatus(currentRuntimeStatus);
    }
    currentConfigModeSelection = selectedConfigMode();
    validatePage();
    if (options.skipAutosave === true) {
        pendingPersistedMode = "";
        configAutosaveNeedsRuntimeRefresh = false;
        if (
            options.keepAutosaveSuspended !== true &&
            typeof suspendConfigAutosave === "function"
        ) {
            suspendConfigAutosave(false);
        }
        return;
    }

    configAutosaveNeedsRuntimeRefresh = true;
    pendingPersistedMode = currentConfigModeSelection;
    if (typeof suspendConfigAutosave === "function") {
        suspendConfigAutosave(false);
    }
    scheduleAutosave();
    flushAutosave();
}

function revertConfigModeSelection() {
    suppressModeChangeGuard = true;
    applyConfigModeSelection(currentConfigModeSelection);
    suppressModeChangeGuard = false;
}

function showModeChangeGuardModal(options) {
    const modalEl = document.getElementById("modeChangeGuardModal");
    const modal = modeChangeGuardModalInstance();
    if (!modalEl || !modal) {
        return;
    }

    document.getElementById("modeChangeGuardModalLabel").textContent = options.title;
    document.getElementById("modeChangeGuardModalBody").textContent = options.message;
    const confirmBtn = document.getElementById("modeChangeGuardConfirmBtn");
    const cancelBtn = document.getElementById("modeChangeGuardCancelBtn");
    confirmBtn.textContent = options.confirmLabel;
    confirmBtn.className = options.confirmClass || "btn btn-danger";
    confirmBtn.disabled = false;
    cancelBtn.textContent = options.cancelLabel || "Cancel";
    cancelBtn.disabled = false;
    const closeBtn = modalEl.querySelector(".btn-close");
    if (closeBtn) {
        closeBtn.disabled = false;
    }

    $(confirmBtn)
        .off("click.modeGuard")
        .on("click.modeGuard", function () {
            options.onConfirm();
        });
    $(cancelBtn)
        .off("click.modeGuard")
        .on("click.modeGuard", function () {
            if (typeof options.onCancel === "function") {
                options.onCancel();
            }
        });

    modal.show();
}

function finalizePendingModeChange(targetModeOverride = null) {
    const targetMode =
        typeof targetModeOverride === "string" && targetModeOverride
            ? targetModeOverride
            : (pendingModeChange ? pendingModeChange.targetMode : "");

    if (!targetMode) {
        return;
    }

    if (pendingModeChange && pendingModeChange.prerequisite === "disable") {
        persistDisabledModeChange(
            targetMode,
            typeof pendingModeChange.previousMode === "string"
                ? pendingModeChange.previousMode
                : currentConfigModeSelection
        );
        return;
    }

    applyCommittedConfigMode(targetMode);
    clearPendingModeChange();
}

function failGuardedActiveModeChange(message) {
    clearModeChangeGuardStopTimeout();
    if (pendingModeChange) {
        pendingModeChange.awaitingGuardedStop = false;
        pendingModeChange.awaitingRuntimeIdle = false;
    }
    modeChangeGuardBusy = false;
    setModeChangeGuardActionBusy(false);
    revertConfigModeSelection();
    showBackendStatus(message, "danger", "runtime");
    if (typeof getTxState === "function") {
        getTxState();
    }
}

function completeGuardedActiveModeChange() {
    if (!pendingModeChange || pendingModeChange.awaitingGuardedStop !== true) {
        return;
    }

    clearModeChangeGuardStopTimeout();
    pendingModeChange.awaitingGuardedStop = false;
    pendingModeChange.awaitingRuntimeIdle = false;
    setTransmitFromBackend(false);

    const request = persistDisabledModeChange(
        pendingModeChange.targetMode,
        typeof pendingModeChange.previousMode === "string"
            ? pendingModeChange.previousMode
            : currentConfigModeSelection
    );

    if (request && typeof request.done === "function") {
        request.done(function () {
            const modal = modeChangeGuardModalInstance();
            if (modal) {
                modal.hide();
            }
        });
        request.always(function () {
            setModeChangeGuardActionBusy(false);
        });
    } else {
        setModeChangeGuardActionBusy(false);
    }
}

function startGuardedActiveModeChange() {
    if (!pendingModeChange || pendingModeChange.awaitingGuardedStop === true) {
        return;
    }

    if (!ws || ws.readyState !== WebSocket.OPEN) {
        failGuardedActiveModeChange(runtimeConnectionUnavailableMessage());
        return;
    }

    modeChangeGuardBusy = true;
    pendingModeChange.awaitingGuardedStop = true;
    pendingModeChange.awaitingRuntimeIdle = true;
    setModeChangeGuardActionBusy(true);
    setTransmitFromBackend(false);
    updateRuntimeControlStatusFromForm(pendingModeChange.previousMode);

    clearModeChangeGuardStopTimeout();
    modeChangeGuardStopTimeoutHandle = window.setTimeout(() => {
        failGuardedActiveModeChange(
            "Stop command timed out before the controller confirmed it. Check controller connectivity and runtime state, then try again."
        );
    }, MODE_CHANGE_GUARD_STOP_TIMEOUT_MS);

    ws.send(
        JSON.stringify({
            command: "stop",
            persist_transmit: false,
        })
    );
}

function handleRuntimeStatusUpdate(status) {
    if (
        pendingModeChange &&
        pendingModeChange.guardedActiveModeChange !== true &&
        pendingModeChange.awaitingRuntimeIdle === true &&
        (!status || status.txState !== "transmitting")
    ) {
        finalizePendingModeChange();
    }
}

function requestConfigModeChange(targetMode) {
    if (suppressModeChangeGuard) {
        return;
    }

    const normalizedTargetMode = ["WSPR", "QRSS", "FSKCW", "DFCW"].includes(targetMode)
        ? targetMode
        : "WSPR";
    const previousMode = currentConfigModeSelection;

    if (normalizedTargetMode === previousMode) {
        applyCommittedConfigMode(previousMode);
        return;
    }

    const runtimeStatus =
        typeof currentRuntimeStatus === "object" && currentRuntimeStatus !== null
            ? currentRuntimeStatus
            : null;
    const transmitting = runtimeStatus && runtimeStatus.txState === "transmitting";
    const transmitEnabled = document.getElementById("transmit")
        ? $("#transmit").is(":checked")
        : currentRuntimeConfigStatus.transmitEnabled === true;

    if (typeof suspendConfigAutosave === "function") {
        suspendConfigAutosave(true);
    }

    if (transmitting) {
        revertConfigModeSelection();
        pendingModeChange = {
            targetMode: normalizedTargetMode,
            prerequisite: "disable",
            previousMode,
            guardedActiveModeChange: true,
            awaitingRuntimeIdle: false,
        };
        showModeChangeGuardModal({
            title: "Disable transmissions to change mode",
            message: "Disable transmissions before switching modes.",
            confirmLabel: "Disable & switch",
            confirmClass: "btn btn-danger",
            onConfirm() {
                startGuardedActiveModeChange();
            },
            onCancel() {
                clearPendingModeChange();
            },
        });
        return;
    }

    if (transmitEnabled) {
        revertConfigModeSelection();
        pendingModeChange = {
            targetMode: normalizedTargetMode,
            previousMode,
            prerequisite: "disable",
        };
        showModeChangeGuardModal({
            title: "Disable transmissions to change mode",
            message: "Disable transmissions before switching modes.",
            confirmLabel: "Disable & switch",
            confirmClass: "btn btn-danger",
            onConfirm() {
                const requestedMode = normalizedTargetMode;
                modeChangeGuardBusy = true;
                const modal = modeChangeGuardModalInstance();
                if (modal) {
                    modal.hide();
                }
                setTransmitFromBackend(false);
                finalizePendingModeChange(requestedMode);
            },
            onCancel() {
                clearPendingModeChange();
            },
        });
        return;
    }

    applyCommittedConfigMode(normalizedTargetMode);
}

function updateRuntimeControlStatusFromForm(mode) {
    if (typeof updateRuntimeControlConfigStatus !== "function") {
        return;
    }

    const transmitField = document.getElementById("transmit");
    const transmitEnabled = transmitField
        ? $("#transmit").is(":checked")
        : currentRuntimeConfigStatus.transmitEnabled === true;

    updateRuntimeControlConfigStatus(
        mode || selectedConfigMode(),
        transmitEnabled
    );

    syncTransmitAvailabilityUi();
    syncStopButtonState();
}

function selectedTransmitBackend() {
    if (window.WtpUi?.selected()) return "wtp";
    return $("#transmit_backend").is(":checked") ? "si5351" : "gpio";
}

function setTransmitBackendSelection(backend, triggerChange = false) {
    window.WtpUi?.select(String(backend).toLowerCase() === "wtp");
    const $backend = $("#transmit_backend");
    $backend.prop("checked", transmitBackendForUi(backend) === "si5351");
    if (triggerChange) {
        $backend.trigger("change");
    }
}

function transmitBackendForPersistence() {
    const backend = selectedTransmitBackend();
    if (backend === "gpio" && isRp1GpioPlatform() && rp1GpioOperatorVisible()) {
        return "rp1-gpclk";
    }
    return backend;
}

function transmitBackendForUi(backend) {
    return String(backend || "gpio").toLowerCase() === "si5351"
        ? "si5351"
        : "gpio";
}

function isRp1GpioPlatform() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    return Number(platform.raspberryPiGeneration) === 5;
}

function rp1GpioOperatorVisible() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    return platform.rp1GpioOperatorVisible === true;
}

function rp1GpioRouteSelectable() {
    return isRp1GpioPlatform() && rp1GpioOperatorVisible();
}

function gpioBackendOperatorActive() {
    return selectedTransmitBackend() === "gpio" &&
        !(isRp1GpioPlatform() && !rp1GpioOperatorVisible());
}

function supportedRp1GpioDrive(value) {
    return [2, 4, 8, 12].includes(Number(value));
}

function populateRp1GpioDrive(value) {
    const field = document.getElementById("rp1_gpio_drive_ma");
    if (!field) {
        return;
    }

    const parsed = Number(value);
    if (supportedRp1GpioDrive(parsed)) {
        field.value = String(parsed);
        field.removeAttribute("data-invalid-source-value");
    } else {
        field.value = "";
        field.setAttribute("data-invalid-source-value", String(value));
    }
    validateRp1GpioDrive();
}

function validateRp1GpioDrive() {
    const field = document.getElementById("rp1_gpio_drive_ma");
    const error = document.getElementById("rp1-gpio-drive-error");
    if (!field) {
        return true;
    }

    const sourceValue = field.getAttribute("data-invalid-source-value");
    const relevant = rp1GpioOperatorVisible() &&
        ((selectedTransmitBackend() === "gpio" && isRp1GpioPlatform()) ||
         sourceValue !== null);
    const valid = supportedRp1GpioDrive(field.value);
    const message = sourceValue !== null
        ? `Saved RP1 drive value ${sourceValue} mA is unsupported. Select 2, 4, 8, or 12 mA.`
        : "Select an RP1 drive strength of 2, 4, 8, or 12 mA.";

    field.setCustomValidity(relevant && !valid ? message : "");
    setFieldValidationState(field, !relevant || valid);
    if (error) {
        error.hidden = !relevant || valid;
        error.textContent = relevant && !valid ? message : "";
    }
    return !relevant || valid;
}

function syncGpioDriveControls() {
    const gpioActive = selectedTransmitBackend() === "gpio";
    const rp1Active = gpioActive && isRp1GpioPlatform() &&
        rp1GpioOperatorVisible();
    const legacyActive = gpioActive && !isRp1GpioPlatform();
    const rp1Group = document.getElementById("rp1-gpio-drive-group");
    const legacyGroup = document.getElementById("legacy-gpio-power-group");
    const rp1Field = document.getElementById("rp1_gpio_drive_ma");
    const legacyField = document.getElementById("gpio-power-range");
    const rp1NeedsRecovery = rp1GpioOperatorVisible() && rp1Field &&
        rp1Field.getAttribute("data-invalid-source-value") !== null;

    if (rp1Group) rp1Group.hidden = !rp1Active && !rp1NeedsRecovery;
    if (legacyGroup) legacyGroup.hidden = !legacyActive;
    if (rp1Field) rp1Field.disabled = !rp1Active && !rp1NeedsRecovery;
    if (legacyField) legacyField.disabled = !legacyActive;
    validateRp1GpioDrive();
}

function syncTransmitAvailabilityUi() {
    const transmitField = document.getElementById("transmit");
    const transmitHint = document.getElementById("transmitAvailabilityHint");
    if (!transmitField) {
        return;
    }

    const unavailableMessage = currentTransmitUnavailableMessage();
    const formattedMessage = unavailableMessage
        ? formatTransmitFailureMessage(unavailableMessage)
        : "";
    const transmitEnabled = transmitField.checked;
    const shouldDisableEnable = !!unavailableMessage && !transmitEnabled;

    transmitField.disabled = shouldDisableEnable;
    if (shouldDisableEnable) {
        transmitField.setAttribute("title", formattedMessage);
    } else {
        transmitField.removeAttribute("title");
    }

    if (transmitHint) {
        transmitHint.hidden = !formattedMessage;
        transmitHint.textContent = formattedMessage;
    }
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

    if (source && $status.attr("data-source") !== source) {
        return;
    }

    $status
        .prop("hidden", true)
        .removeClass("alert-warning alert-danger alert-info")
        .removeAttr("data-source")
        .text("");
}

function clearRp1RouteBackendStatus() {
    const $status = $("#backendStatus");
    if (!$status.length) return;
    const message=String($status.text() || "");
    if ($status.attr("data-source") === "platform" || /\bRP1\b|clock route|route status|GPCLK provider/i.test(message)) {
        clearBackendStatus();
    }
}

function gpioPlatformRestrictionMessage() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    if (
        typeof platform.gpioClockTransmissionError === "string" &&
        platform.gpioClockTransmissionError.trim()
    ) {
        return platform.gpioClockTransmissionError.trim();
    }

    return "GPIO transmission is supported only on Raspberry Pi 1 through 4.";
}

function si5351UnavailableMessage() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    if (
        typeof platform.si5351DetectionError === "string" &&
        platform.si5351DetectionError.trim()
    ) {
        return platform.si5351DetectionError.trim();
    }

    return "Si5351 transmission is unavailable because no Si5351 device was detected on the I2C bus.";
}

function selectedBackendUnavailableMessage() {
    if (window.WtpUi?.selected()) return window.WtpUi.unavailable();
    const platform = window.WSPRRYPI_PLATFORM || {};
    const backend = selectedTransmitBackend();

    if (backend === "gpio" && platform.gpioClockTransmissionSupported === false) {
        if (rp1GpioRouteSelectable()) {
            return rp1RouteUnavailableMessage();
        }
        return gpioPlatformRestrictionMessage();
    }

    if (backend === "si5351" && platform.si5351Detected === false) {
        return si5351UnavailableMessage();
    }

    return "";
}

function rp1RouteUnavailableMessage() {
    if (!rp1RouteUi || rp1RouteUi.state === "checking") {
        return "RP1 route and provider readiness are unconfirmed. Transmission remains disabled.";
    }

    if (["unavailable", "runtime_unknown"].includes(rp1RouteUi.state)) {
        return "RP1 route status and provider readiness could not be confirmed. Review the status beside Transmit Pin; transmission remains disabled.";
    }

    const requested = rp1RouteUi.requested;
    const active = rp1RouteUi.active;
    if (requested === "None") {
        return active === "None"
            ? "No RP1 clock route is selected. Transmission remains disabled."
            : `${active} is active. Choose Remove route to return RP1 clock routing to None; transmission remains disabled.`;
    }
    if (requested === "Unavailable") {
        const draft = rp1RouteUi.routeValue(`GPIO${getTxPin()}`);
        return `${draft} is selected as the route to apply, but no active RP1 GPIO route is confirmed. Use the route action beside Transmit Pin; transmission remains disabled until the canonical provider is active.`;
    }
    if (active === requested) {
        return `${requested} is selected and active, but the canonical RP1 GPCLK provider is unavailable. Review the status beside Transmit Pin; transmission remains disabled.`;
    }
    if (active !== "Unavailable") {
        return `${requested} is selected, but ${active} is active. Use the route action or choose ${active} again; transmission remains disabled.`;
    }
    return `${requested} is selected, but no active RP1 GPIO route is confirmed. Use the route action beside Transmit Pin; transmission remains disabled until the canonical provider is active.`;
}

function si5351UiSupported() {
    return $("#si5351-backend-panel").length > 0;
}

function resolveSupportedTransmitBackend(preferredBackend = null) {
    if (preferredBackend === "wtp") return "wtp";
    const platform = window.WSPRRYPI_PLATFORM || {};
    const preferred = preferredBackend === "si5351" ? "si5351" : "gpio";
    const gpioSupported = platform.gpioClockTransmissionSupported !== false;
    const gpioSelectable = gpioSupported || rp1GpioRouteSelectable();
    const si5351Supported = si5351UiSupported();

    if (preferred === "si5351") {
        if (si5351Supported) {
            return "si5351";
        }
        if (gpioSelectable) {
            return "gpio";
        }
        return "si5351";
    }

    if (gpioSelectable) {
        return "gpio";
    }
    if (si5351Supported) {
        return "si5351";
    }
    return "gpio";
}

function hasAnySupportedTransmitBackend() {
    if (window.WtpUi?.selected()) return true;
    const platform = window.WSPRRYPI_PLATFORM || {};
    return (
        platform.gpioClockTransmissionSupported !== false ||
        rp1GpioRouteSelectable() ||
        si5351UiSupported()
    );
}

function noBackendAvailableMessage() {
    return "No supported transmit backend is currently available on this system.";
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

function isSi5351MissingReason(reason) {
    const normalized = String(reason || "").toLowerCase();
    if (!normalized) {
        return false;
    }

    return (
        normalized.includes("no si5351") ||
        normalized.includes("si5351 device was detected") ||
        normalized.includes("i2c bus")
    );
}

function backendInlineHintMessage() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    const backend = selectedTransmitBackend();

    if (backend === "si5351" && platform.si5351Detected === false) {
        return "No Si5351 detected on the configured I2C bus.";
    }

    if (backend === "gpio" && platform.gpioClockTransmissionSupported === false) {
        if (rp1GpioRouteSelectable()) {
            return rp1RouteUnavailableMessage();
        }
        return gpioPlatformRestrictionMessage();
    }

    return "";
}

function rp1RouteSelectorHint() {
    return "Off uses GPIO through the RP1 GPCLK provider. On uses the attached Si5351 synthesizer.";
}

function formatBackendBannerMessage(reason) {
    if (isGpioUnsupportedReason(reason)) {
        return `Transmission is unavailable with the GPIO backend. ${reason}`;
    }

    if (isSi5351MissingReason(reason)) {
        return "Transmission is unavailable because no Si5351 was detected on the configured I2C bus. Check I2C bus, address, wiring, and power, or select a different backend.";
    }

    return reason;
}

function formatTransmitFailureMessage(reason) {
    if (reason === noBackendAvailableMessage()) {
        return "Transmit cannot be enabled because no supported backend is currently available.";
    }

    if (isGpioUnsupportedReason(reason)) {
        return "Transmit cannot be enabled with the GPIO backend on this Raspberry Pi.";
    }

    if (isSi5351MissingReason(reason)) {
        return "Transmit cannot be enabled because no Si5351 was detected on the configured I2C bus.";
    }

    return reason;
}

function formatReloadFailureMessage(reason) {
    if (isGpioUnsupportedReason(reason)) {
        return "Configuration rejected: GPIO transmission is not supported on this Raspberry Pi.";
    }

    if (isSi5351MissingReason(reason)) {
        return "Configuration rejected: no Si5351 device was detected on the configured I2C bus.";
    }

    return reason;
}

function formatBackendRecoveryMessage(fromBackend, toBackend) {
    const fromLabel = fromBackend === "si5351" ? "Si5351" : "GPIO";
    const toLabel = toBackend === "si5351" ? "Si5351" : "GPIO";
    return `${fromLabel} is unavailable on this system. Switched to ${toLabel}.`;
}

function updateBackendPlatformSupportUi() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    const gpioSupported = platform.gpioClockTransmissionSupported !== false;
    const si5351Supported = si5351UiSupported();
    const anyBackendSupported = hasAnySupportedTransmitBackend();
    const currentBackend = selectedTransmitBackend();
    const resolvedBackend = resolveSupportedTransmitBackend(currentBackend);
    const $hint = $("#backendPlatformHint");
    const $selectorHint = $("#backend-selector-hint");
    const $backend = $("#transmit_backend");
    const rp1RouteSelectable = rp1GpioRouteSelectable();
    const hiddenRp1Selection = currentBackend === "gpio" &&
        isRp1GpioPlatform() && !rp1GpioOperatorVisible();
    const rp1StatusOwnedByRoutePanel = currentBackend === "gpio" &&
        rp1RouteSelectable && !gpioSupported;

    if (currentBackend !== resolvedBackend && !hiddenRp1Selection) {
        setTransmitBackendSelection(resolvedBackend);
    }

    const recoveryMessage =
        currentBackend !== resolvedBackend && !hiddenRp1Selection
            ? formatBackendRecoveryMessage(currentBackend, resolvedBackend)
            : "";
    const backendWarning = rp1StatusOwnedByRoutePanel
        ? ""
        : hiddenRp1Selection
        ? gpioPlatformRestrictionMessage()
        : (anyBackendSupported
            ? selectedBackendUnavailableMessage()
            : noBackendAvailableMessage());
    const inlineHint = rp1StatusOwnedByRoutePanel
        ? ""
        : hiddenRp1Selection
        ? gpioPlatformRestrictionMessage()
        : (anyBackendSupported
            ? backendInlineHintMessage()
            : noBackendAvailableMessage());

    $backend.prop("disabled", currentBackend === "wtp" || !anyBackendSupported);
    $selectorHint.text(
        currentBackend === "wtp" ? (window.WtpUi.developmentControlsVisible
            ? "Pico is selected below. Turn off Use Pico to choose GPIO or Si5351."
            : "GPIO and Si5351 selection is unavailable while Pico is in use.") :
        rp1RouteSelectable && !gpioSupported
            ? rp1RouteSelectorHint()
            : isRp1GpioPlatform() && !rp1GpioOperatorVisible()
            ? "GPIO is unavailable on this system. On uses the attached Si5351 synthesizer."
            : "Off uses Raspberry Pi GPIO directly. On uses the attached Si5351 synthesizer."
    );
    $hint
        .prop("hidden", !inlineHint)
        .text(inlineHint);

    if (backendWarning) {
        showBackendStatus(formatBackendBannerMessage(backendWarning), "warning", "platform");
    } else if (recoveryMessage) {
        showBackendStatus(recoveryMessage, "info", "platform");
    } else {
        if(rp1StatusOwnedByRoutePanel) clearRp1RouteBackendStatus();
        else clearBackendStatus("platform");
    }

    syncTransmitAvailabilityUi();
    syncGpioDriveControls();
    syncBackendPanelVisibility();
}

function syncCalibrationControls() {
    const si5351Active = selectedTransmitBackend() !== "gpio";
    const estimateEnabled = $("#use_system_clock_frequency_estimate").is(":checked");
    const $residual = $("#gpio_frequency_residual_ppm");
    $residual.prop("disabled", si5351Active || !estimateEnabled);
    $residual.prop("required", !si5351Active && estimateEnabled);

    [$("#ppm").get(0), $residual.get(0), $("#gpio_manual_ppm").get(0)].forEach((field) => {
        if (field && field.disabled) {
            field.setCustomValidity("");
            clearFieldValidationState(field);
        }
    });
}

function syncConfigModeSections() {
    const selected = $('input[name="mode_toggle"]:checked').val();
    if (selected === "QRSS") {
        $('#wspr_config').prop("hidden", true);
        $('#qrss_config').prop("hidden", false);
        if (!$('input[name="qrss_type"]:checked').length) {
            $('input[name="qrss_type"][value="QRSS"]').prop("checked", true);
        }
        syncSelectedCwModeControls();
    } else {
        $('#qrss_config').prop("hidden", true);
        $('#wspr_config').prop("hidden", false);
    }

    syncCalibrationControls();
    updateRuntimeControlStatusFromForm(null);
    updateCwMessageLengthEstimate();
}

function clickTransmitBackend() {
    const $gpioPanel = $("#gpio-backend-panel");
    const $si5351Panel = $("#si5351-backend-panel");
    updateBackendPlatformSupportUi();

    const backend = selectedTransmitBackend();
    const gpioActive = backend === "gpio" &&
        !(isRp1GpioPlatform() && !rp1GpioOperatorVisible());
    syncBackendPanelVisibility();

    $gpioPanel
        .find("input, select, button")
        .prop("disabled", !gpioActive);
    $si5351Panel
        .find("input, select, button")
        .prop("disabled", backend !== "si5351");

    syncBackendControlAvailability();

    syncGpioDriveControls();

    syncCalibrationControls();
    refreshGpioConflictOptions();
    validateTransmitterHardwareFields();
    validatePage();
    scheduleAutosave();
}

function syncBackendControlAvailability() {
    const gpioActive = selectedTransmitBackend() === "gpio" &&
        !(isRp1GpioPlatform() && !rp1GpioOperatorVisible());
    $("#tx_pin, #rp1-route-apply").prop("disabled", !gpioActive);
    $("#si5351_i2c_bus").prop("disabled", selectedTransmitBackend() !== "si5351" || availableI2cBuses().length === 0);
    $("#si5351_i2c_address").prop(
        "disabled",
        selectedTransmitBackend() !== "si5351" ||
            si5351AddressInventoryState.loading ||
            !!si5351AddressInventoryState.error ||
            si5351AddressInventoryState.addresses.length === 0
    );
}

function syncBackendPanelVisibility() {
    const gpioActive = selectedTransmitBackend() === "gpio" &&
        !(isRp1GpioPlatform() && !rp1GpioOperatorVisible());
    const rp1RecoveryRequired = rp1GpioOperatorVisible() &&
        document.getElementById("rp1_gpio_drive_ma")
        ?.getAttribute("data-invalid-source-value") !== null;
    $("#gpio-backend-panel").prop("hidden", !gpioActive && !rp1RecoveryRequired);
    $("#si5351-backend-panel").prop(
        "hidden",
        gpioActive || selectedTransmitBackend() !== "si5351"
    );
}

function clickTransmitPin() {
    refreshGpioConflictOptions();
    validatePage();
    if (rp1RouteUi && rp1RouteUi.visible()) {
        rp1RouteUi.select(`GPIO${getTxPin()}`);
        return;
    }
    scheduleAutosave();
}

const RP1_ROUTE_STATES = Object.freeze({
    runtime_inhibited: ["Application idle", "Switching restarts a running Wsprry Pi in idle mode. Transmission does not resume."],
    runtime_unsaved: ["Route Unsaved", "The selected route has not been applied. Use the adjacent action to apply it, or choose the active route to discard the draft."],
    runtime_ready: ["Route selected", "The selected route is active and Wsprry Pi is online in idle mode. This does not start or authorize transmission."],
    runtime_neutral_running: ["Route removed", "The RP1 clock route is removed and Wsprry Pi is back online. Transmission remains disabled."],
    runtime_neutral_stopped: ["Route removed; service stopped", "The RP1 clock route is removed. Wsprry Pi remains stopped because it was stopped before removal or is administrator-masked."],
    runtime_switch_queued: ["Route switch queued", "Wsprry Pi may disconnect briefly while the status dialog checks the selected route. Transmission remains disabled."],
    runtime_remove_queued: ["Removing route", "Wsprry Pi may disconnect while the route is removed and neutral state is verified. If it was running, it will return automatically. Transmission remains disabled."],
    runtime_recovery_queued: ["Recovery queued", "Wsprry Pi may disconnect and remain stopped while recovery establishes a neutral inhibited state. Transmission remains disabled."],
    runtime_restoring: ["Restoring application", "Wsprry Pi is reconnecting on the selected route. The status dialog will check again automatically."],
    runtime_restoration_failed: ["Application restoration failed", "Run runtime_route_client.py restore --execute to retry application restoration without switching the overlay."],
    runtime_neutral_restoration_failed: ["Route removed; service unavailable", "The RP1 clock route is neutral, but Wsprry Pi could not be restored. Correct the reported service error, then retry with the installed runtime route client."],
    runtime_preflight_ready: ["Route plan ready", "No route change has started. Transmission remains disabled until you confirm the switch."],
    runtime_preflight_failed: ["Route not switched", "The route plan was rejected before any change began. Transmission remains disabled."],
    runtime_recovery: ["Recovery required", "Inspect the controller error and ownership before explicit cleanup recovery."],
    runtime_unknown: ["Reconnecting", "Wsprry Pi is temporarily unavailable during route administration. Status checks will continue; do not repeat the route action."],
    checking: ["Checking", "Checking the external provider and active route…"],
    active: ["Active", "Requested and active routes match. No reboot is required."],
    reboot_required: ["Reboot required", "Review the requested route, then apply it and reboot or cancel the draft."],
    applying: ["Applying", "Staging the requested route. Transmission remains disabled."],
    staged: ["Staged", "The route is staged, but reboot could not be requested. Reboot or roll back before transmitting."],
    mismatch: ["Mismatch", "Requested and active routes do not match. Transmission remains disabled until recovery completes."],
    unavailable: ["Unavailable", "The RP1 route service or compatible provider is unavailable. The draft is preserved."],
    rollback: ["Rolling back", "Restoring the previously active route. Transmission remains disabled."],
    rollback_required: ["Rollback required", "Automatic recovery did not complete. Roll back the staged route before transmitting."]
});

class Rp1RouteUiController {
    constructor(endpoint, request = window.fetch.bind(window)) {
        this.endpoint=endpoint; this.request=request; this.persisted=""; this.active=""; this.outputValidated=false;
        this.requested="Unavailable"; this.state="checking";
        this.confirmedState="checking";
        this.completionUnknown=false; this.runtimeProfile=false; this.developmentCompatible=false; this.generation=0; this.inFlight=false;
        this.progressActive=false; this.progressOperation=""; this.progressRoute="";
        this.pollAttempt=0; this.pollTimer=null; this.maxAutomaticPolls=12;
        const initialPin=getTxPin();this.lastPersistableRoute=initialPin===20 ? "GPIO20" : "GPIO4";
    }
    visible() { return isRp1GpioPlatform() && rp1GpioOperatorVisible(); }
    routeValue(value) { return value === "GPIO4" || value === "GPIO20" || value === "None" ? value : "Unavailable"; }
    selectedRoute() { const pin=getTxPin(); return pin===4 || pin===20 ? `GPIO${pin}` : "None"; }
    async requestWithTimeout(options,timeoutMs=8000) {
        if(typeof AbortController==="undefined")return this.request(this.endpoint,options);
        const abortController=new AbortController();
        const timeout=window.setTimeout(()=>abortController.abort(),timeoutMs);
        try{return await this.request(this.endpoint,{...options,signal:abortController.signal});}
        finally{window.clearTimeout(timeout);}
    }
    setState(state, message="") {
        this.state=state;
        if(state==="runtime_unknown") this.completionUnknown=true;
        const panel=document.querySelector(".rp1-route-panel");
        const badge=document.getElementById("rp1-route-state");
        const feedback=document.getElementById("rp1-route-feedback");
        const definition=RP1_ROUTE_STATES[state] || RP1_ROUTE_STATES.unavailable;
        if(panel) panel.setAttribute("aria-busy",this.inFlight ? "true" : "false");
        if(badge){badge.dataset.state=state;badge.textContent=definition[0];}
        if(feedback) feedback.textContent=message || definition[1];
        this.renderProgress(state,message || definition[1]);
        this.syncActions(state);
        if (typeof updateBackendPlatformSupportUi === "function" && rp1RouteUi === this) {
            updateBackendPlatformSupportUi();
        }
    }
    syncActions(state) {
        const draft=this.selectedRoute();
        const changed=draft!=="Unavailable" && draft!==this.active;
        const recovery=["staged","mismatch","rollback_required","runtime_recovery"].includes(state);
        $("#rp1-route-apply").prop("disabled",this.inFlight || this.completionUnknown || ["runtime_switch_queued","runtime_remove_queued","runtime_recovery_queued","runtime_restoring","runtime_restoration_failed"].includes(state) || recovery || state==="runtime_unknown" || !changed);
        $("#rp1-route-cancel").prop("disabled",this.inFlight || draft===this.persisted);
        $("#rp1-route-rollback").prop("hidden",!recovery).prop("disabled",this.inFlight);
        $("#rp1-route-apply").text(draft==="None" ? "Remove route" : (this.runtimeProfile ? "Switch route" : (this.developmentCompatible ? "Apply route and reboot" : "Check route")));
    }
    render(data) {
        this.completionUnknown=false;
        this.runtimeProfile=data.profile==="runtime";
        const noneOption=document.querySelector('#tx_pin option[value=""]');
        if(noneOption){noneOption.hidden=!this.runtimeProfile;noneOption.disabled=!this.runtimeProfile;}
        // Completed removal reports "None"; initial neutral status has no active route.
        const confirmedNeutral=this.runtimeProfile && data.compatible===true && (!data.active || data.active==="None");
        this.persisted=this.routeValue(data.persisted); this.active=confirmedNeutral ? "None" : this.routeValue(data.active);
        if(this.persisted==="GPIO4" || this.persisted==="GPIO20")this.lastPersistableRoute=this.persisted;
        this.developmentCompatible=data.compatible===true;
        this.generation=Number.isSafeInteger(data.generation) ? data.generation : 0;
        const requested=this.progressActive && ["remove","recover"].includes(this.progressOperation)
            ? "None"
            : (confirmedNeutral ? "None" : this.routeValue(data.requested || this.persisted));
        this.requested=requested;
        if(requested==="None") setTxPin(null);
        else if(requested!=="Unavailable") setTxPin(Number(requested.slice(4)));
        $("#rp1-route-requested").text(requested);
        $("#rp1-route-persisted").text(this.persisted);
        $("#rp1-route-configured").text(this.routeValue(data.configured));
        $("#rp1-route-active").text(this.active);
        $("#rp1-route-module").text(this.routeValue(data.moduleRoute));
        for (const id of ["rp1-route-module-fact", "rp1-route-endpoint-fact",
            "rp1-route-output-inhibited-fact", "rp1-route-operational-ready-fact",
            "rp1-development-policy-fact", "rp1-operation-lifecycle-fact",
            "rp1-route-eligible-fact"]) {
            const fact=document.getElementById(id);
            if(fact) fact.hidden=this.runtimeProfile;
        }
        $("#rp1-route-reconciled").text(data.reconciled===true ? "Yes" : "No");
        $("#rp1-route-boot-ownership").text(data.bootOwnership || "Unknown");
        $("#rp1-route-pending").text(data.journal || "Unknown");
        const services=data.services && typeof data.services==="object"
            ? Object.entries(data.services).map(([name,state])=>`${name}: ${state}`).join("; ")
            : "Not reported";
        $("#rp1-route-services").text(services || "Not reported");
        const endpoint=data.endpointOwned===true
            ? (data.endpointOpen===true ? "Owned; open owner detected" : "Owned; closed")
            : "Ownership unconfirmed";
        $("#rp1-route-endpoint").text(endpoint);
        $("#rp1-route-output-inhibited").text(data.outputInhibited || "Unknown");
        $("#rp1-route-operational-ready").text(data.operationalReady || "Unknown");
        $("#rp1-development-policy").text(data.developmentPolicy || "Disabled");
        const lifecycle=data.operationLifecycle && typeof data.operationLifecycle==="object"
            ? `Lease ${data.operationLifecycle.lease || "none"}; generation ${data.operationLifecycle.generation || "none"}; ${data.operationLifecycle.terminalReasonName || data.operationLifecycle.terminalReason || "no terminal result"}`
            : "No active lease or generation";
        $("#rp1-operation-lifecycle").text(lifecycle);
        $("#rp1-route-eligible").text("Unqualified");
        $("#rp1-route-rollback").text(this.runtimeProfile ? "Recover to no route" : "Roll back");
        const reported=String(data.state || (requested===this.active ? "active" : "mismatch")).replaceAll("-","_");
        const displayed=confirmedNeutral && reported==="runtime_inhibited" ? "runtime_neutral_stopped" : reported;
        this.confirmedState=displayed;
        this.setState(displayed,
            typeof data.message==="string" ? data.message : "");
        if(this.progressActive) this.handleProgressData(data,displayed);
    }
    select(route) {
        const requested=this.routeValue(route); this.requested=requested; $("#rp1-route-requested").text(requested);
        this.setState(this.runtimeProfile
            ? (requested===this.active ? this.confirmedState : "runtime_unsaved")
            : (requested===this.active ? "active" : "reboot_required"));
    }
    async query() {
        this.inFlight=true; this.setState("checking");
        try { const response=await this.requestWithTimeout({headers:{Accept:"application/json"}});
            if(!response.ok) throw new Error(); this.inFlight=false; this.render(await response.json());
        } catch(_){this.inFlight=false;this.setState(this.runtimeProfile ? "runtime_unknown" : "unavailable");}
    }
    async operate(operation) {
        if(this.runtimeProfile && operation==="rollback") {
            if(!window.confirm("Recover to no route? Wsprry Pi will stop and remain inhibited. The status dialog will check the result without repeating recovery."))return;
            operation="recover";this.beginProgress("recover","None");
        }
        const requested=["remove","recover"].includes(operation)
            ? (this.active==="GPIO4" || this.active==="GPIO20" ? this.active : this.persisted)
            : this.selectedRoute();
        this.inFlight=true; this.setState(operation==="rollback" ? "rollback" : "applying");
        try { const response=await this.requestWithTimeout({method:"POST",
                headers:{"Content-Type":"application/json",Accept:"application/json"},
                body:JSON.stringify({operation, route:requested, generation:this.generation})});
            const data=await response.json(); this.inFlight=false;
            if(!response.ok && !data.state) throw new Error(); this.render(data);
            if(this.progressActive && ["runtime_switch_queued","runtime_remove_queued","runtime_recovery_queued","runtime_restoring","runtime_unknown"].includes(this.state)) this.scheduleProgressPoll();
        } catch(_){this.inFlight=false;this.setState(this.runtimeProfile ? "runtime_unknown" : "unavailable");if(this.progressActive)this.scheduleProgressPoll();}
    }
    async applyAndReboot() {
        if(this.inFlight || this.progressActive)return;
        const requested=this.selectedRoute();
        if(this.runtimeProfile && requested==="None") {
            const confirmed=window.confirm("Remove the active RP1 clock route? Transmission will remain disabled. If Wsprry Pi is running, it will disconnect briefly and return after the neutral route is verified. A service that was already stopped or administrator-masked will remain stopped.");
            if(!confirmed){this.cancel();return;}
            this.beginProgress("remove","None");
            await this.operate("remove");
            return;
        }
        this.inFlight=true; this.setState("checking");
        try {
            const preflightResponse=await this.requestWithTimeout({method:"POST",
                headers:{"Content-Type":"application/json",Accept:"application/json"},
                body:JSON.stringify({operation:"preflight",route:requested,generation:this.generation})});
            const preflight=await preflightResponse.json();
            if(!preflightResponse.ok || preflight.ok!==true){
                this.inFlight=false;
                if(["runtime_preflight_failed","runtime_unknown"].includes(preflight.state) &&
                    preflight.changeStarted===false &&
                    preflight.recoveryRequired!==true) {
                    this.setState("runtime_preflight_failed",
                        typeof preflight.message==="string" ? preflight.message : "");
                } else {
                    this.render(preflight);
                }
                return;
            }
            this.generation=preflight.generation;
            const confirmed=window.confirm(this.runtimeProfile
                ? `Switch to ${requested} and keep WsprryPi idle? A running Wsprry Pi will restart in idle mode. The status dialog will remain open and check the route while the browser reconnects. Transmission will not resume. No reboot is requested.`
                :
                `Apply ${requested} and reboot? The package executor will stop only wsprrypi.service and soapyremote-server.service before changing its owned boot block.`
            );
            if(!confirmed){
                this.inFlight=false;
                this.cancel();
                this.setState(this.active==="None" ? "runtime_neutral" : "runtime_inhibited",
                    "Route switch canceled. No changes were made. Transmission remains disabled.");
                return;
            }
            if(this.runtimeProfile) { this.beginProgress("switch",requested); await this.operate("switch"); return; }
            this.setState("applying");
            const applyResponse=await this.requestWithTimeout({method:"POST",
                headers:{"Content-Type":"application/json",Accept:"application/json"},
                body:JSON.stringify({operation:"apply-and-reboot",route:requested,generation:this.generation})});
            const applied=await applyResponse.json(); this.inFlight=false; this.render(applied);
        } catch(_){this.inFlight=false;this.setState(this.runtimeProfile ? "runtime_unknown" : "unavailable");}
    }
    cancel() {
        const restored=this.active!=="Unavailable" ? this.active : this.persisted;
        if(restored==="None") setTxPin(null);
        else if(restored!=="Unavailable") setTxPin(Number(restored.slice(4)));
        this.select(restored);
    }
    modalElement() { return document.getElementById("rp1-route-progress-modal"); }
    beginProgress(operation,route) {
        this.stopProgressPoll(); this.progressActive=true; this.progressOperation=operation;this.progressRoute=route;this.pollAttempt=0;
        const modal=this.modalElement(); if(!modal)return;
        modal.querySelector(".modal-body")?.setAttribute("aria-busy","true");
        $("#rp1-route-progress-title").text(operation==="remove" ? "Removing transmit route" : operation==="recover" ? "Recovering route controller" : `Switching transmit route to ${route}`);
        $("#rp1-route-progress-close").prop("disabled",true);
        $("#rp1-route-progress-retry-button").prop("hidden",true);
        this.renderProgress("checking","Confirming the route operation…");
        if(typeof syncConnectionAlert==="function")syncConnectionAlert();
        if(typeof bootstrap!=="undefined" && bootstrap.Modal) bootstrap.Modal.getOrCreateInstance(modal,{backdrop:"static",keyboard:false}).show();
    }
    renderProgress(state,message) {
        if(!this.progressActive)return;
        const definition=RP1_ROUTE_STATES[state] || RP1_ROUTE_STATES.unavailable;
        const transient=["runtime_switch_queued","runtime_remove_queued","runtime_recovery_queued","runtime_restoring","runtime_unknown"].includes(state);
        const badge=document.getElementById("rp1-route-progress-state");
        if(badge){badge.dataset.state=state;badge.textContent=definition[0];}
        const pausedMessage=state==="runtime_unknown" && String(message).startsWith("Automatic status checks paused.");
        $("#rp1-route-progress-message").text(transient && !pausedMessage ? definition[1] : (message || definition[1]));
    }
    handleProgressData(data,state) {
        const switchComplete=this.progressOperation==="switch" && state==="runtime_ready" && this.active===this.progressRoute;
        const removalComplete=this.progressOperation==="remove" && ["runtime_neutral_running","runtime_neutral_stopped"].includes(state);
        const failed=data.ok===false && !["runtime_unknown","runtime_restoring"].includes(state);
        if(switchComplete || removalComplete || failed) this.finishProgress();
    }
    finishProgress() {
        this.stopProgressPoll();
        const modal=this.modalElement();modal?.querySelector(".modal-body")?.setAttribute("aria-busy","false");
        $("#rp1-route-progress-retry").text("Status check complete");
        $("#rp1-route-progress-retry-button").prop("hidden",true);
        $("#rp1-route-progress-close").prop("disabled",false);
    }
    stopProgressPoll() { if(this.pollTimer!==null){window.clearTimeout(this.pollTimer);this.pollTimer=null;} }
    scheduleProgressPoll() {
        if(!this.progressActive || this.pollTimer!==null)return;
        if(this.pollAttempt>=this.maxAutomaticPolls){
            this.modalElement()?.querySelector(".modal-body")?.setAttribute("aria-busy","false");
            $("#rp1-route-progress-retry").text("Automatic checks paused");
            this.renderProgress("runtime_unknown","Automatic status checks paused. Retry status, or run runtime_route_client.py query from the operator client. Do not repeat the route action.");
            $("#rp1-route-progress-retry-button").prop("hidden",false);
            $("#rp1-route-progress-close").prop("disabled",false);
            return;
        }
        const delays=[1000,2000,3000,5000,8000,10000];
        const delay=delays[Math.min(this.pollAttempt,delays.length-1)];this.pollAttempt++;
        $("#rp1-route-progress-retry").text(`Checking again in ${Math.ceil(delay/1000)} s`);
        this.pollTimer=window.setTimeout(()=>{this.pollTimer=null;this.pollStatus();},delay);
    }
    async pollStatus() {
        if(!this.progressActive)return;
        $("#rp1-route-progress-retry").text("Checking current status…");
        try {
            const response=await this.requestWithTimeout({headers:{Accept:"application/json"}});
            if(!response.ok)throw new Error();
            this.render(await response.json());
        } catch(_) {
            this.completionUnknown=true;this.state="runtime_unknown";
            this.renderProgress("runtime_unknown",RP1_ROUTE_STATES.runtime_unknown[1]);
        }
        if(this.progressActive && this.modalElement()?.querySelector(".modal-body")?.getAttribute("aria-busy")==="true") this.scheduleProgressPoll();
    }
    retryProgress() {
        if(!this.progressActive)return;
        this.pollAttempt=0;this.modalElement()?.querySelector(".modal-body")?.setAttribute("aria-busy","true");
        $("#rp1-route-progress-retry-button").prop("hidden",true);$("#rp1-route-progress-close").prop("disabled",true);
        this.pollStatus();
    }
    closeProgress() {
        this.stopProgressPoll();this.progressActive=false;this.progressOperation="";this.progressRoute="";
        if(typeof syncConnectionAlert==="function")syncConnectionAlert();
    }
}

function initializeRp1RouteUi() {
    const panel=document.getElementById("rp1-route-panel");
    const visible=panel && isRp1GpioPlatform() && rp1GpioOperatorVisible();
    const action=document.getElementById("rp1-route-apply");
    const badge=document.getElementById("rp1-route-state");
    const noneOption=document.querySelector('#tx_pin option[value=""]');
    if(!panel) return; panel.hidden=true;
    if(action)action.hidden=!visible;if(badge)badge.hidden=!visible;
    if(noneOption){noneOption.hidden=true;noneOption.disabled=true;}
    if(!visible) return;
    if(!rp1RouteUi) {
        rp1RouteUi=new Rp1RouteUiController(window.WSPRRYPI_PATHS?.rp1RoutePath || "/api/rp1-gpclk-route");
        $("#rp1-route-apply").on("click",()=>rp1RouteUi.applyAndReboot());
        $("#rp1-route-cancel").on("click",()=>rp1RouteUi.cancel());
        $("#rp1-route-rollback").on("click",()=>rp1RouteUi.operate("rollback"));
        $("#rp1-route-progress-retry-button").on("click",()=>rp1RouteUi.retryProgress());
        document.getElementById("rp1-route-progress-modal")?.addEventListener("hidden.bs.modal",()=>rp1RouteUi.closeProgress());
    }
    rp1RouteUi.query();
}

window.isRp1RouteStatusModalActive=()=>!!(rp1RouteUi && rp1RouteUi.progressActive);

// GPIO transmit power slider update
function updateGpioPowerLabel() {
    var val = this.value;
    var rangeValues = {
        0: ["2mA", "3.0dBm"],
        1: ["4mA", "6.0dBm"],
        2: ["6mA", "7.8dBm"],
        3: ["8mA", "9.0dBm"],
        4: ["10mA", "10.0dBm"],
        5: ["12mA", "10.8dBm"],
        6: ["14mA", "11.5dBm"],
        7: ["16mA", "12.0dBm"],
    };
    var label = rangeValues[val] || [String(val)];
    var labelElement = document.getElementById("gpio-power-range-value");

    if (!labelElement) {
        return;
    }

    labelElement.replaceChildren();
    label.forEach((part, index) => {
        if (index > 0) {
            labelElement.appendChild(document.createElement("br"));
        }
        labelElement.appendChild(document.createTextNode(part));
    });
}

function updateSi5351PowerLabel() {
    var val = this.value;
    var rangeValues = {
        1: "2mA",
        2: "4mA",
        3: "6mA",
        4: "8mA",
    };
    var label = rangeValues[val] || val;
    var labelElement = document.getElementById("si5351-power-range-value");

    if (!labelElement) {
        return;
    }

    labelElement.textContent = label;
}

function syncSi5351ReferenceControls() {
    const crystalSelected = $("#si5351_reference_source").val() === "crystal";
    const hardwareDisabled = $("#si5351_reference_source").prop("disabled");
    $("#si5351-crystal-load-group").prop("hidden", !crystalSelected);
    $("#si5351_crystal_load_capacitance").prop(
        "disabled",
        hardwareDisabled || !crystalSelected
    );
}

function clickUseLED() {
    const on = $("#use_led").prop("checked");
    $("#ledDropdownButton").prop("disabled", !on);
    refreshGpioConflictOptions();
    scheduleAutosave();
}

function clickUseShutdown() {
    const on = $('#use_shutdown').prop('checked');
    $('#shutdownDropdownButton').prop('disabled', !on);
    refreshGpioConflictOptions();
    scheduleAutosave();
}

function setUseAmp(enabled) {
    $("#use_amp").prop("checked", !!enabled);
    syncAmpControlState();
}

function getUseAmp() {
    return $("#use_amp").is(":checked");
}

function syncAmpControlState() {
    $("#ampDropdownButton").prop("disabled", !getUseAmp());
    refreshGpioConflictOptions();
}

function clickUseAmp() {
    syncAmpControlState();
    validatePage();
    scheduleAutosave();
}

function getBandGpioRows() {
    return $("#bandGpioTable tbody tr[data-band]");
}

function getBandGpioHeaderCheckboxes() {
    return {
        enabled: $("#band-gpio-enabled-all"),
        activeHigh: $("#band-gpio-active-high-all")
    };
}

function getBandGpioColumnCheckboxes(column) {
    if (column === "enabled") {
        return getBandGpioRows().find(".band-gpio-enabled");
    }

    if (column === "activeHigh") {
        return getBandGpioRows().find(".band-gpio-active-high");
    }

    return $();
}

function syncBandGpioColumnHeaderState(column) {
    const headerCheckboxes = getBandGpioHeaderCheckboxes();
    const $header = headerCheckboxes[column];
    if (!$header || !$header.length) {
        return;
    }

    const $columnCheckboxes = getBandGpioColumnCheckboxes(column);
    const total = $columnCheckboxes.length;

    if (total === 0) {
        $header.prop({
            checked: false,
            indeterminate: false,
            disabled: true
        });
        return;
    }

    const checkedCount = $columnCheckboxes.filter(":checked").length;
    $header.prop("disabled", false);
    $header.prop("checked", checkedCount === total);
    $header.prop("indeterminate", checkedCount > 0 && checkedCount < total);
}

function syncBandGpioColumnHeaderStates() {
    syncBandGpioColumnHeaderState("enabled");
    syncBandGpioColumnHeaderState("activeHigh");
}

function applyBandGpioColumnToggle(column, checked) {
    const $columnCheckboxes = getBandGpioColumnCheckboxes(column);
    if (!$columnCheckboxes.length) {
        syncBandGpioColumnHeaderStates();
        return;
    }

    if (column === "enabled") {
        getBandGpioRows().each(function () {
            const $row = $(this);
            setBandGpioRowState($row, checked);
        });
    } else {
        $columnCheckboxes.prop("checked", checked);
    }

    refreshGpioConflictOptions();
    syncBandGpioColumnHeaderStates();
    validateBandGpioFields();
    scheduleAutosave();
}

function setBandGpioRowState($row, enabled) {
    $row.find(".band-gpio-enabled").prop("checked", enabled);
    $row.find(".band-gpio-input").prop("disabled", !enabled);
    $row.find(".band-gpio-active-high").prop("disabled", !enabled);
}

function clickBandGpioEnabled() {
    const $row = $(this).closest("tr[data-band]");
    setBandGpioRowState($row, $(this).is(":checked"));
    refreshGpioConflictOptions();
    syncBandGpioColumnHeaderStates();
    validateBandGpioFields();
    scheduleAutosave();
}

function handleBandGpioInputChange() {
    refreshGpioConflictOptions();
    validateBandGpioFields();
    scheduleAutosave();
}

function getDropdownButtonPin(buttonId) {
    const txt = $(`#${buttonId}`).text().trim();
    const m = txt.match(/\d+/);
    return m ? parseInt(m[0], 10) : null;
}

function setDropdownButtonPin(buttonId, gpioNumber, blankTitle = "Disabled", allowBlank = false) {
    const $btn = $(`#${buttonId}`);
    const pinNumber = Number(gpioNumber);

    if (!Number.isInteger(pinNumber) || pinNumber < 0) {
        if (!allowBlank) {
            debugConsole("warn", "GPIO value not found:", gpioNumber);
            return;
        }

        $btn.text("");
        $btn.attr("title", blankTitle);
        $(`[aria-labelledby="${buttonId}"] .dropdown-item`).removeClass("active");
        $(`[aria-labelledby="${buttonId}"] .dropdown-item[data-val=""]`).addClass("active");
        return;
    }

    const code = "GPIO" + pinNumber;
    const $item = $(`[aria-labelledby="${buttonId}"] .dropdown-item[data-val="${code}"]`);
    if ($item.length) {
        $btn.text(code);
        $btn.attr("title", $item.text().trim());
        $(`[aria-labelledby="${buttonId}"] .dropdown-item`).removeClass("active");
        $item.addClass("active");
    } else {
        debugConsole("warn", "GPIO value not found:", code);
    }
}

function getSelectedBandGpioPins() {
    const pins = new Set();

    getBandGpioRows().each(function () {
        const $row = $(this);
        if (!$row.find(".band-gpio-enabled").is(":checked")) {
            return;
        }

        const pin = parseInt($row.find(".band-gpio-input").val(), 10);
        if (Number.isInteger(pin)) {
            pins.add(String(pin));
        }
    });

    return pins;
}

function getReservedPiControlPins(excludeButtonId = "") {
    const reservedPins = new Set();

    if ($("#use_led").is(":checked")) {
        const ledPin = getLEDPin();
        if (Number.isInteger(ledPin) && excludeButtonId !== "ledDropdownButton") {
            reservedPins.add(String(ledPin));
        }
    }

    if ($("#use_shutdown").is(":checked")) {
        const shutdownPin = getShutdownPin();
        if (Number.isInteger(shutdownPin) && excludeButtonId !== "shutdownDropdownButton") {
            reservedPins.add(String(shutdownPin));
        }
    }

    if (getUseAmp()) {
        const ampPin = getAmpPin();
        if (Number.isInteger(ampPin) && excludeButtonId !== "ampDropdownButton") {
            reservedPins.add(String(ampPin));
        }
    }

    return reservedPins;
}

function getReservedBandGpioPins() {
    const reservedPins = getReservedPiControlPins();
    const rfOutputPin = getReservedGpioRfOutputPin();
    if (Number.isInteger(rfOutputPin)) {
        reservedPins.add(String(rfOutputPin));
    }
    return reservedPins;
}

function getReservedGpioRfOutputPin() {
    return selectedTransmitBackend() === "gpio" ? getTxPin() : null;
}

function refreshPiControlDropdownOptions() {
    const bandPins = getSelectedBandGpioPins();
    const buttonIds = ["ledDropdownButton", "shutdownDropdownButton", "ampDropdownButton"];

    buttonIds.forEach((buttonId) => {
        const reservedPins = getReservedPiControlPins(buttonId);
        const rfOutputPin = getReservedGpioRfOutputPin();
        if (Number.isInteger(rfOutputPin)) {
            reservedPins.add(String(rfOutputPin));
        }
        bandPins.forEach((pin) => reservedPins.add(pin));
        const currentPin = getDropdownButtonPin(buttonId);

        $(`[aria-labelledby="${buttonId}"] .dropdown-item`).each(function () {
            const $item = $(this);
            const code = String($item.data("val") || "");
            const match = code.match(/\d+/);
            const optionValue = match ? match[0] : "";
            const isBlank = optionValue === "";
            const keepCurrentSelection = Number.isInteger(currentPin) && String(currentPin) === optionValue;
            const shouldDisable = !isBlank && reservedPins.has(optionValue) && !keepCurrentSelection;

            $item.prop("disabled", shouldDisable);
            $item.toggleClass("disabled", shouldDisable);
        });
    });
}

function refreshTransmitGpioOptions() {
    const reservedPins = getReservedPiControlPins();
    getSelectedBandGpioPins().forEach((pin) => reservedPins.add(pin));
    const currentPin = getTxPin();

    $("#tx_pin option").each(function () {
        const $option = $(this);
        const optionValue = String($option.val() || "");
        const keepCurrentSelection =
            Number.isInteger(currentPin) && String(currentPin) === optionValue;
        const shouldDisable = reservedPins.has(optionValue) && !keepCurrentSelection;

        $option.prop("disabled", shouldDisable);
    });
}

function refreshBandGpioOptions() {
    const reservedPins = getReservedBandGpioPins();

    getBandGpioRows().each(function () {
        const $select = $(this).find(".band-gpio-input");
        const currentValue = $select.val();

        $select.find("option").each(function () {
            const $option = $(this);
            const optionValue = String($option.val() || "");
            const isPlaceholder = optionValue === "";
            const isReserved = reservedPins.has(optionValue);
            const keepCurrentSelection = currentValue !== "" && currentValue === optionValue;
            const shouldDisable = !isPlaceholder && isReserved && !keepCurrentSelection;

            $option.prop("disabled", shouldDisable);
            $option.prop("hidden", shouldDisable);
        });
    });
}

function refreshGpioConflictOptions() {
    refreshPiControlDropdownOptions();
    refreshBandGpioOptions();
    refreshTransmitGpioOptions();
}

function populateBandGpioForm(bandGpioConfig = {}) {
    getBandGpioRows().each(function () {
        const $row = $(this);
        const band = $row.data("band");
        const $gpioInput = $row.find(".band-gpio-input");
        const bandConfig = bandGpioConfig && typeof bandGpioConfig === "object"
            ? bandGpioConfig[band]
            : null;
        const backendEnabled = !!(bandConfig && bandConfig["Enabled"] === true);
        const gpio = bandConfig && Number.isInteger(bandConfig["GPIO"])
            ? bandConfig["GPIO"]
            : -1;
        const gpioValue = gpio >= 0 ? String(gpio) : "";

        $gpioInput.val(gpioValue);

        const resolvedGpioValue = $gpioInput.val();
        const hasSelectableGpio = gpioValue !== "" && resolvedGpioValue === gpioValue;
        const enabled = backendEnabled && hasSelectableGpio;
        const activeHigh = enabled && !!(bandConfig && bandConfig["Active High"] === true);

        if (!hasSelectableGpio) {
            $gpioInput.val("");
        }

        $row.find(".band-gpio-active-high").prop("checked", activeHigh);
        setBandGpioRowState($row, enabled);
    });

    refreshGpioConflictOptions();
    syncBandGpioColumnHeaderStates();
    validateBandGpioFields();
}

function collectBandGpioConfig() {
    const bandGpio = {};

    getBandGpioRows().each(function () {
        const $row = $(this);
        const band = $row.data("band");
        const enabled = $row.find(".band-gpio-enabled").is(":checked");
        const gpioRaw = $row.find(".band-gpio-input").val();
        const gpioValue = gpioRaw === "" || gpioRaw === null
            ? -1
            : parseInt(gpioRaw, 10);
        const activeHigh = $row.find(".band-gpio-active-high").is(":checked");
        const validEnabledRow = enabled && gpioValue >= 0;

        bandGpio[band] = validEnabledRow
            ? {
                "GPIO": gpioValue,
                "Enabled": true,
                "Active High": activeHigh,
            }
            : {
                "GPIO": -1,
                "Enabled": false,
                "Active High": false,
            };
    });

    return bandGpio;
}

function validateBandGpioFields() {
    let invalidCount = 0;

    getBandGpioRows().each(function () {
        const $row = $(this);
        const band = String($row.data("band") || "").trim();
        const enabled = $row.find(".band-gpio-enabled").is(":checked");
        const gpioField = $row.find(".band-gpio-input").get(0);
        const gpioValue = gpioField ? gpioField.value : "";
        const valid = !enabled || (gpioValue !== "" && gpioValue !== null);

        if (gpioField) {
            gpioField.setCustomValidity(
                valid
                    ? ""
                    : `Select a GPIO pin for ${band || "this band"} before enabling Band GPIO.`
            );

            if (enabled) {
                setFieldValidationState(gpioField, valid);
            } else {
                clearFieldValidationState(gpioField);
            }
        }

        if (!valid) {
            invalidCount++;
        }
    });

    return invalidCount === 0;
}

function validateGpioConflictFields() {
    const reservedAssignments = [];
    const bandAssignments = [];
    ["ledDropdownButton", "shutdownDropdownButton", "ampDropdownButton"].forEach((buttonId) => {
        const field = document.getElementById(buttonId);
        if (field) {
            setPinDropdownValidationState(field, "");
        }
    });
    const txPinField = document.getElementById("tx_pin");
    clearStoredGpioConflictState(txPinField);
    getBandGpioRows().each(function () {
        clearStoredGpioConflictState($(this).find(".band-gpio-input").get(0));
    });

    function addReservedAssignment(pin, field, label) {
        if (Number.isInteger(pin)) {
            reservedAssignments.push({ pin: String(pin), field, label });
        }
    }

    addReservedAssignment(
        $("#use_led").is(":checked") ? getLEDPin() : null,
        document.getElementById("ledDropdownButton"),
        "Transmit LED"
    );
    addReservedAssignment(
        $("#use_shutdown").is(":checked") ? getShutdownPin() : null,
        document.getElementById("shutdownDropdownButton"),
        "Shutdown Button"
    );
    addReservedAssignment(
        getUseAmp() ? getAmpPin() : null,
        document.getElementById("ampDropdownButton"),
        "Amp Control"
    );

    getBandGpioRows().each(function () {
        const $row = $(this);
        if (!$row.find(".band-gpio-enabled").is(":checked")) {
            return;
        }
        const pin = parseInt($row.find(".band-gpio-input").val(), 10);
        if (Number.isInteger(pin)) {
            bandAssignments.push({
                pin: String(pin),
                field: $row.find(".band-gpio-input").get(0),
                activeHigh: $row.find(".band-gpio-active-high").is(":checked"),
            });
        }
    });

    let invalidCount = 0;
    const reservedByPin = new Map();
    const rfOutputPin = getReservedGpioRfOutputPin();
    const rfOutputPinText = Number.isInteger(rfOutputPin) ? String(rfOutputPin) : "";

    reservedAssignments.forEach((assignment) => {
        if (!reservedByPin.has(assignment.pin)) {
            reservedByPin.set(assignment.pin, assignment);
        }
    });

    reservedAssignments.forEach((assignment) => {
        const conflictingAssignment = reservedAssignments.find((other) =>
            other !== assignment && other.pin === assignment.pin
        );
        const conflictsWithRfOutput = assignment.pin === rfOutputPinText;
        const valid = !conflictingAssignment && !conflictsWithRfOutput;
        const message = conflictsWithRfOutput
            ? `GPIO${assignment.pin} is reserved by GPIO RF Output.`
            : valid
                ? ""
                : `GPIO${assignment.pin} is already assigned to ${conflictingAssignment.label}.`;
        if (assignment.field) {
            setPinDropdownValidationState(assignment.field, message);
        }
        if (conflictsWithRfOutput && txPinField) {
            setGpioConflictFieldState(txPinField, message);
        }
        if (!valid) {
            invalidCount++;
        }
    });

    bandAssignments.forEach((assignment) => {
        if (assignment.pin === rfOutputPinText) {
            const message = `GPIO${assignment.pin} is reserved by GPIO RF Output.`;
            if (assignment.field) {
                setGpioConflictFieldState(assignment.field, message);
            }
            if (txPinField) {
                setGpioConflictFieldState(txPinField, message);
            }
            invalidCount++;
            return;
        }

        const reservedAssignment = reservedByPin.get(assignment.pin);
        const valid = !reservedAssignment;
        const message = valid
            ? ""
            : `GPIO${assignment.pin} is reserved by ${reservedAssignment.label}.`;
        if (assignment.field) {
            setGpioConflictFieldState(assignment.field, message);
        }
        if (!valid && reservedAssignment.field) {
            setPinDropdownValidationState(reservedAssignment.field, message);
        }
        if (!valid) {
            invalidCount++;
        }
    });

    const bandsByPin = new Map();
    bandAssignments.forEach((assignment) => {
        const assignments = bandsByPin.get(assignment.pin) || [];
        assignments.push(assignment);
        bandsByPin.set(assignment.pin, assignments);
    });

    bandsByPin.forEach((assignments, pin) => {
        const conflictingPolarity = assignments.some((assignment) =>
            assignment.activeHigh !== assignments[0].activeHigh
        );
        if (!conflictingPolarity) {
            return;
        }

        const message = `Bands sharing GPIO${pin} must use the same Active High setting.`;
        assignments.forEach((assignment) => {
            if (assignment.field) {
                setGpioConflictFieldState(assignment.field, message);
            }
        });
        invalidCount += assignments.length;
    });

    return invalidCount === 0;
}

function validateAmpControlFields() {
    const button = document.getElementById("ampDropdownButton");
    if (!button) {
        return true;
    }

    const requiredMessage = "Select an Amp Pin before enabling Amp Control.";
    const valid = !getUseAmp() || Number.isInteger(getAmpPin());
    if (!valid) {
        setPinDropdownValidationState(button, requiredMessage);
    } else if (button.dataset.validationMessage === requiredMessage) {
        setPinDropdownValidationState(button, "");
    }

    return valid;
}

function setPinDropdownValidationState(field, message) {
    if (!field) {
        return;
    }

    const normalizedMessage = String(message || "");
    field.setCustomValidity(normalizedMessage);
    field.dataset.validationMessage = normalizedMessage;
    field.classList.toggle("is-invalid", normalizedMessage !== "");
    field.setAttribute("aria-invalid", normalizedMessage === "" ? "false" : "true");

    const feedback = document.getElementById(field.id.replace("DropdownButton", "-pin-error"));
    if (feedback) {
        feedback.textContent = normalizedMessage;
        feedback.hidden = normalizedMessage === "";
    }
}

function setGpioConflictFieldState(field, message) {
    if (!field) {
        return;
    }
    if (field.classList.contains("pin-dropdown-btn")) {
        setPinDropdownValidationState(field, message);
        return;
    }

    const normalizedMessage = String(message || "");
    field.setCustomValidity(normalizedMessage);
    if (normalizedMessage) {
        field.dataset.gpioConflictMessage = normalizedMessage;
    } else {
        delete field.dataset.gpioConflictMessage;
    }
    if (normalizedMessage) {
        setFieldValidationState(field, false);
    } else {
        clearFieldValidationState(field);
    }

    const feedbackId = field.id === "tx_pin" ? "tx-pin-error" : `${field.id}-error`;
    const feedback = document.getElementById(feedbackId);
    if (feedback) {
        feedback.textContent = normalizedMessage;
        feedback.hidden = normalizedMessage === "";
    }
}

function clearStoredGpioConflictState(field) {
    if (!field || !field.dataset.gpioConflictMessage) {
        return;
    }

    const storedMessage = field.dataset.gpioConflictMessage;
    if (field.validationMessage === storedMessage) {
        field.setCustomValidity("");
        clearFieldValidationState(field);
    }
    delete field.dataset.gpioConflictMessage;

    const feedbackId = field.id === "tx_pin" ? "tx-pin-error" : `${field.id}-error`;
    const feedback = document.getElementById(feedbackId);
    if (feedback) {
        feedback.textContent = "";
        feedback.hidden = true;
    }
}

function isPlaceholderCallsign(callsign) {
    if (typeof callsign !== "string") return false;

    const value = callsign.trim().toUpperCase();
    return value === "N0CALL" || value === "NXXX";
}

function isPlaceholderGridSquare(gridSquare) {
    if (typeof gridSquare !== "string") return false;

    return gridSquare.trim().toUpperCase() === "ZZ99";
}

function trimIdentityValue(value) {
    return typeof value === "string" ? value.trim() : "";
}

function isLightweightCallsign(value) {
    const trimmed = trimIdentityValue(value);
    if (!trimmed || /\s/.test(trimmed)) {
        return false;
    }

    return /^(?:[A-Za-z0-9\/]+|<[A-Za-z0-9\/]+>)$/.test(trimmed);
}

function isLightweightGridSquare(value) {
    const trimmed = trimIdentityValue(value);
    if (!trimmed || /\s/.test(trimmed)) {
        return false;
    }

    return /^[A-Za-z]{2}[0-9]{2}(?:[A-Za-z]{2})?$/.test(trimmed);
}

function setIdentityValidity(ctrl) {
    if (ctrl.id === "callsign") {
        const callsign = trimIdentityValue(ctrl.value);
        if (!callsign) {
            ctrl.setCustomValidity("Callsign is required.");
        } else if (!isLightweightCallsign(callsign)) {
            ctrl.setCustomValidity(
                "Enter a callsign using letters, digits, '/', or explicit Type 3 form like <CALLSIGN>."
            );
        } else if (isPlaceholderCallsign(callsign)) {
            ctrl.setCustomValidity("Placeholder callsign is not allowed.");
        } else {
            ctrl.setCustomValidity("");
        }
    }

    if (ctrl.id === "gridsquare") {
        const gridSquare = trimIdentityValue(ctrl.value);
        if (!gridSquare) {
            ctrl.setCustomValidity("Grid square is required.");
        } else if (!isLightweightGridSquare(gridSquare)) {
            ctrl.setCustomValidity(
                "Enter a 4-character or 6-character Maidenhead locator such as EM18 or EM18IG."
            );
        } else if (isPlaceholderGridSquare(gridSquare)) {
            ctrl.setCustomValidity("Placeholder grid square ZZ99 is not allowed.");
        } else {
            ctrl.setCustomValidity("");
        }
    }
}

function buildConfigErrorMessage(data, fallbackMessage) {
    if (!data || typeof data !== "object") {
        return fallbackMessage;
    }

    const lines = [];
    if (typeof data.message === "string" && data.message.trim()) {
        lines.push(data.message.trim());
    } else {
        lines.push(fallbackMessage);
    }

    if (typeof data.plan_status === "string" && data.plan_status.trim()) {
        lines.push(`Plan status: ${data.plan_status.trim()}`);
    }

    if (typeof data.rationale === "string" && data.rationale.trim()) {
        lines.push(data.rationale.trim());
    }

    if (
        typeof data.normalized_callsign === "string" &&
        data.normalized_callsign.trim()
    ) {
        lines.push(`Normalized callsign: ${data.normalized_callsign.trim()}`);
    }

    if (
        typeof data.normalized_locator === "string" &&
        data.normalized_locator.trim()
    ) {
        lines.push(`Normalized locator: ${data.normalized_locator.trim()}`);
    }

    return lines.join("\n");
}

function isPairedPlanningUnavailableError(data) {
    return !!(
        data &&
        typeof data === "object" &&
        typeof data.plan_status === "string" &&
        data.plan_status.trim() === "PairedTransmissionUnavailable"
    );
}

function openSetupDetailsDialog(detail) {
    if (typeof detail !== "string" || !detail.trim()) {
        return;
    }

    showMessageDialog({
        title: "Setup details",
        message: detail,
        acknowledgeLabel: "Close",
        preserveLineBreaks: true,
    });
}

function describeControlLabel(control) {
    if (!control) {
        return "the highlighted field";
    }

    const controlId = typeof control.id === "string" ? control.id.trim() : "";
    if (controlId) {
        const selectorId =
            typeof CSS !== "undefined" && typeof CSS.escape === "function"
                ? CSS.escape(controlId)
                : controlId;
        const label = document.querySelector(`label[for="${selectorId}"]`);
        if (label) {
            const text = label.textContent.replace(/\s+/g, " ").trim().replace(/:\s*$/, "");
            if (text) {
                return text;
            }
        }
    }

    const ariaLabel = String(control.getAttribute("aria-label") || "").trim();
    if (ariaLabel) {
        return ariaLabel.replace(/:\s*$/, "");
    }

    return "the highlighted field";
}

function describeControlSection(control) {
    if (!control || typeof control.closest !== "function") {
        return "";
    }

    const fieldset = control.closest("fieldset");
    if (!fieldset) {
        return "";
    }

    const legend = fieldset.querySelector("legend");
    if (!legend) {
        return "";
    }

    return legend.textContent.replace(/\s+/g, " ").trim();
}

function describeControlTab(control) {
    if (!control || typeof control.closest !== "function") {
        return "";
    }

    const tabPane = control.closest('[role="tabpanel"]');
    if (!tabPane) {
        return "";
    }

    const labelledBy = String(tabPane.getAttribute("aria-labelledby") || "").trim();
    if (!labelledBy) {
        return "";
    }

    const tabButton = document.getElementById(labelledBy);
    if (!tabButton) {
        return "";
    }

    return tabButton.textContent.replace(/\s+/g, " ").trim();
}

function firstInvalidConfigControl() {
    const candidates = document.querySelectorAll(
        '#wsprform input, #wsprform select, #wsprform textarea, #wsprform button.pin-dropdown-btn'
    );

    let firstHiddenInvalid = null;

    for (const control of candidates) {
        if (control.disabled) {
            continue;
        }

        const ariaInvalid = control.getAttribute("aria-invalid") === "true";
        const nativeInvalid =
            typeof control.checkValidity === "function" && !control.checkValidity();

        if (ariaInvalid || nativeInvalid) {
            const isVisible =
                control.offsetParent !== null || control.getClientRects().length > 0;
            if (isVisible) {
                return control;
            }

            if (!firstHiddenInvalid) {
                firstHiddenInvalid = control;
            }
        }
    }

    return firstHiddenInvalid;
}

function invalidAutosaveDetailMessage() {
    const invalidControl = firstInvalidConfigControl();
    if (!invalidControl) {
        return "Fix the highlighted fields before autosave can continue.";
    }

    const label = describeControlLabel(invalidControl);
    const section = describeControlSection(invalidControl);
    const tab = describeControlTab(invalidControl);
    const validationMessage =
        String(
            invalidControl.dataset.validationMessage ||
            (typeof invalidControl.validationMessage === "string"
                ? invalidControl.validationMessage
                : "")
        ).trim();
    let location = label;
    if (section) {
        location += ` in ${section}`;
    }
    if (tab) {
        location += ` on ${tab}`;
    }

    if (validationMessage) {
        return `Fix ${location}: ${validationMessage}`;
    }

    return `Fix ${location} before autosave can continue.`;
}

function revealControlTab(control) {
    if (!control || typeof control.closest !== "function") {
        return null;
    }

    const tabPane = control.closest('[role="tabpanel"]');
    if (!tabPane) {
        return null;
    }

    const tabList = document.getElementById("configTabs");
    const paneSelector = tabPane.id ? `#${tabPane.id}` : "";
    if (!tabList || !paneSelector || typeof bootstrap === "undefined" || !bootstrap.Tab) {
        return null;
    }

    const trigger = findTabTriggerBySelector(tabList, paneSelector);
    if (!trigger) {
        return null;
    }

    bootstrap.Tab.getOrCreateInstance(trigger).show();
    return trigger;
}

function focusInvalidConfigControl(control) {
    if (!control) {
        return;
    }

    window.requestAnimationFrame(() => {
        if (typeof control.focus === "function") {
            control.focus({ preventScroll: false });
        }
        if (typeof control.scrollIntoView === "function") {
            control.scrollIntoView({ block: "center", behavior: "smooth" });
        }
    });
}

function navigateToFirstInvalidConfigControl() {
    const invalidControl = firstInvalidConfigControl();
    if (!invalidControl) {
        return;
    }

    const targetPane = invalidControl.closest('[role="tabpanel"]');
    const paneNeedsReveal =
        !!targetPane &&
        (!targetPane.classList.contains("active") || !targetPane.classList.contains("show"));
    const trigger = revealControlTab(invalidControl);

    if (paneNeedsReveal && trigger) {
        $(trigger).one("shown.bs.tab", () => {
            focusInvalidConfigControl(invalidControl);
        });
        return;
    }

    focusInvalidConfigControl(invalidControl);
}

function handleConfigSaveStatusDetailKeydown(event) {
    if (!event) {
        return;
    }

    if (event.key !== "Enter" && event.key !== " ") {
        return;
    }

    event.preventDefault();
    navigateToFirstInvalidConfigControl();
}

function validatePage(options = {}) {
    let invalidCount = 0;
    const activeSelectors = ["#global_runtime_control"];
    const mode = selectedConfigMode();
    updateCwMessageLengthEstimate();

    if (mode === "WSPR") {
        activeSelectors.push("#wspr_config");
        if (!validateFrequencies()) {
            invalidCount++;
        }
        if (!validateBandPreferenceControls()) {
            invalidCount++;
        }
        clearValidationState("#qrss_config");
    } else {
        activeSelectors.push("#qrss_config");
        if (!validateCwBaseFrequency()) {
            invalidCount++;
        }
        if (!validateCwDotSeconds()) {
            invalidCount++;
        }
        if (!validateCwMessage()) {
            invalidCount++;
        }
        if (!validateCwShiftHz()) {
            invalidCount++;
        }
        if (!validateCwRepeatMinutes()) {
            invalidCount++;
        }
        if (!validateCwStartMinute()) {
            invalidCount++;
        }
        if (!validateCwStartSecond()) {
            invalidCount++;
        }
        [
            ["cw_intra_element_gap", "Enter a positive finite QRSS/FSKCW intra-element gap."],
            ["cw_inter_character_gap", "Enter a positive finite QRSS/FSKCW inter-character gap."],
            ["cw_inter_word_gap", "Enter a positive finite QRSS/FSKCW inter-word gap."],
            ["dfcw_intra_element_gap", "Enter a positive finite DFCW intra-element gap."],
            ["dfcw_inter_character_gap", "Enter a positive finite DFCW inter-character gap."],
            ["dfcw_inter_word_gap", "Enter a positive finite DFCW inter-word gap."],
        ].forEach(([fieldId, message]) => {
            if (!validatePositiveCwField(fieldId, message)) invalidCount++;
        });
        clearValidationState("#wspr_config");
    }

    if (!validateTransmitterHardwareFields()) {
        invalidCount++;
    }

    if (!validateBandGpioFields()) {
        invalidCount++;
    }

    if (!validateGpioConflictFields()) {
        invalidCount++;
    }

    if (!validateAmpControlFields()) {
        invalidCount++;
    }

    // ONLY visible/relevant .form-control elements for the selected mode.
    document
        .querySelectorAll(
            activeSelectors.join(", ") + " .form-control:not(.form-check-input)"
        )
        .forEach((ctrl) => {
            if (ctrl.dataset.cwTimingValue === "true") return;
            setIdentityValidity(ctrl);

            const isStationIdentity = ctrl.id === "callsign" || ctrl.id === "gridsquare";
            if (ctrl.checkValidity()) {
                setFieldValidationState(ctrl, true);
            } else {
                setFieldValidationState(ctrl, false);
                if (!(options.allowInvalidStationIdentity && isStationIdentity)) {
                    invalidCount++;
                }
            }
        });

    return invalidCount === 0;
}

function clearValidationState(selector) {
    document.querySelectorAll(`${selector} .form-control`).forEach((ctrl) => {
        ctrl.setCustomValidity("");
        clearFieldValidationState(ctrl);
    });
}

function setFieldValidationState(field, valid) {
    if (!field) {
        return;
    }

    field.classList.toggle("is-invalid", !valid);
    field.classList.toggle("is-valid", !!valid);
    field.setAttribute("aria-invalid", valid ? "false" : "true");
}

function clearFieldValidationState(field) {
    if (!field) {
        return;
    }

    field.classList.remove("is-valid", "is-invalid");
    field.removeAttribute("aria-invalid");
}

function applyConfigModeSelection(mode) {
    const normalizedMode = ["WSPR", "QRSS", "FSKCW", "DFCW"].includes(mode)
        ? mode
        : "WSPR";

    if (normalizedMode === "WSPR") {
        $('input[name="mode_toggle"][value="WSPR"]').prop("checked", true);
    } else {
        $('input[name="mode_toggle"][value="QRSS"]').prop("checked", true);
        $(`input[name="qrss_type"][value="${normalizedMode}"]`).prop("checked", true);
    }

    syncConfigModeSections();
    currentConfigModeSelection = selectedConfigMode();
}

function clickModeToggle() {
    requestConfigModeChange(selectedConfigMode());
}


function clickQRSSModeToggle() {
    requestConfigModeChange(selectedConfigMode());
}

function syncSelectedCwModeControls() {
    const selectedMode = $('input[name="qrss_type"]:checked').val();
    const shiftField = document.getElementById("fsk_offset");
    const dfcwSelected = selectedMode === "DFCW";
    const activeGroup = dfcwSelected ? "dfcw" : "conventional";
    cwSpacingSelectionOverride[activeGroup] = null;

    // CW.Shift Hz is only used by FSKCW and DFCW.
    if (selectedMode === "QRSS") {
        $('#fsk_offset').prop('disabled', true);
    } else {
        $('#fsk_offset').prop('disabled', false);
    }

    $(".cw-shared-gap-control").removeClass("d-none");
    $(".dfcw-gap-control").removeClass("d-none");
    syncCwTimingControls({ announce: true });

    validateCwShiftHz();
    updateCwMessageLengthEstimate();

    if (shiftField && shiftField.disabled) {
        shiftField.setCustomValidity("");
        clearFieldValidationState(shiftField);
    }
}

function clickUseSystemClockFrequencyEstimate() {
    syncCalibrationControls();
    validatePage();
    scheduleAutosave();
}

function setTxPin(gpioNumber) {
    const normalizedPin = gpioNumber === null ? "" : (gpioNumber === 20 ? "20" : "4");
    $("#tx_pin").val(normalizedPin);
    refreshGpioConflictOptions();
}

function getTxPin() {
    const raw = String($("#tx_pin").val() || "").trim();
    const pin = parseInt(raw, 10);
    return Number.isInteger(pin) ? pin : null;
}

function formatSi5351Address(value) {
    const raw = String(value ?? "").trim();
    if (!raw) {
        return "";
    }

    if (!/^(?:0[xX][0-9A-Fa-f]+|[0-9]+)$/.test(raw)) {
        return raw;
    }

    const parsed = Number.parseInt(raw, 0);
    if (!Number.isInteger(parsed) || parsed < 0) {
        return raw;
    }

    return "0x" + parsed.toString(16).toUpperCase();
}

function setSi5351AddressValue(value) {
    $("#si5351_i2c_address").val(formatSi5351Address(value)).trigger("change");
}

function normalizedSi5351Addresses(addresses) {
    if (!Array.isArray(addresses)) return [];
    return [...new Set(addresses.map(formatSi5351Address).filter((address) => {
        if (!/^0x[0-9A-F]+$/.test(address)) return false;
        const parsed = Number.parseInt(address, 0);
        return Number.isInteger(parsed) && parsed >= 0x60 && parsed <= 0x6F;
    }))].sort((a, b) => Number.parseInt(a, 0) - Number.parseInt(b, 0));
}

function populateSi5351Addresses(bus, savedAddress, addresses, error = "", inventoryBus = bus) {
    const field = document.getElementById("si5351_i2c_address");
    if (!field) return;
    const normalizedBus = Number(bus);
    const inventoryMatches = Number(inventoryBus) === normalizedBus;
    const normalizedAddresses = inventoryMatches
        ? normalizedSi5351Addresses(addresses)
        : [];
    const normalizedSaved = formatSi5351Address(savedAddress);
    const present = normalizedAddresses.includes(normalizedSaved);

    si5351AddressInventoryState = {
        bus: normalizedBus,
        addresses: normalizedAddresses,
        error: inventoryMatches ? String(error || "") : "Address information is stale for the selected bus.",
        loading: false,
    };
    field.replaceChildren();
    const placeholder = si5351AddressInventoryState.error
        ? "Address discovery unavailable"
        : normalizedAddresses.length === 0
            ? "No compatible Si5351 found"
            : "Select a detected address";
    field.add(new Option(placeholder, "", true, !present));
    if (normalizedSaved && !present) {
        const unavailable = new Option(`${normalizedSaved} (not detected)`, normalizedSaved, true, true);
        unavailable.disabled = true;
        field.add(unavailable);
    }
    for (const address of normalizedAddresses) field.add(new Option(address, address));
    // Preserve the saved value for other-backend updates; discovery determines
    // Si5351 availability, not whether its inactive settings may be retained.
    field.value = normalizedSaved;
    field.disabled = selectedTransmitBackend() !== "si5351" ||
        normalizedAddresses.length === 0 || !!si5351AddressInventoryState.error;
    field.removeAttribute("aria-busy");

    const hint = document.getElementById("si5351-address-hint");
    hint.textContent = si5351AddressInventoryState.error
        ? `${si5351AddressInventoryState.error} Reload or select the bus again to retry.`
        : normalizedAddresses.length === 0
            ? `No register-compatible Si5351 device was detected from 0x60 through 0x6F on I2C bus ${normalizedBus}.`
            : !present
                ? `The saved address ${normalizedSaved || "is unavailable"}. Select a detected address on I2C bus ${normalizedBus}.`
                : `Detected ${normalizedAddresses.length} compatible address${normalizedAddresses.length === 1 ? "" : "es"} on I2C bus ${normalizedBus}.`;
    validateSi5351I2cAddress();
}

function refreshSi5351Addresses(bus, savedAddressOverride = null) {
    const field = document.getElementById("si5351_i2c_address");
    if (!field || !Number.isInteger(bus) || bus < 0) return;
    const savedAddress = savedAddressOverride === null
        ? field.value
        : formatSi5351Address(savedAddressOverride);
    const requestSequence = ++si5351AddressDiscoverySequence;
    si5351AddressInventoryState = { bus, addresses: [], error: "", loading: true };
    field.replaceChildren(new Option("Checking I2C bus…", savedAddress, true, true));
    field.disabled = true;
    field.setAttribute("aria-busy", "true");
    document.getElementById("si5351-address-hint").textContent =
        `Checking I2C bus ${bus} for compatible addresses from 0x60 through 0x6F.`;
    validateTransmitterHardwareFields();

    ajaxWithEndpointFallback(SI5351_ADDRESSES_ENDPOINT, {
        data: { bus },
        dataType: "json",
        cache: false,
        timeout: CONFIG_REQUEST_TIMEOUT_MS,
    }).done((inventory) => {
        if (requestSequence !== si5351AddressDiscoverySequence || selectedI2cBusValue() !== bus) return;
        populateSi5351Addresses(bus, savedAddress, inventory?.Addresses,
            inventory?.["Discovery Error"], inventory?.["I2C Bus"]);
        validatePage();
        scheduleAutosave();
    }).fail(() => {
        if (requestSequence !== si5351AddressDiscoverySequence || selectedI2cBusValue() !== bus) return;
        populateSi5351Addresses(bus, savedAddress, [],
            "The controller could not provide Si5351 address discovery.", bus);
        validatePage();
    });
}

function normalizeIntegerInputValue(selector, fallback) {
    const parsed = parseInt($(selector).val(), 10);
    return Number.isInteger(parsed) ? parsed : fallback;
}

function validateSi5351I2cAddress() {
    const fld = document.getElementById("si5351_i2c_address");
    if (!fld) return true;

    const address = formatSi5351Address(fld.value);
    const valid = !si5351AddressInventoryState.loading &&
        si5351AddressInventoryState.bus === selectedI2cBusValue() &&
        !si5351AddressInventoryState.error &&
        si5351AddressInventoryState.addresses.includes(address);
    fld.setCustomValidity(valid
        ? ""
        : "Select a detected Si5351 address on the selected I2C bus.");

    setFieldValidationState(fld, valid);
    return valid;
}

function availableI2cBuses() {
    const platform = window.WSPRRYPI_PLATFORM || {};
    if (platform.i2cBusDiscoveryError || !Array.isArray(platform.i2cBuses)) return [];
    return platform.i2cBuses.filter(bus => bus && Number.isInteger(bus.Number) && bus.Number >= 0 && bus.Number <= 2147483647)
        .sort((a, b) => a.Number - b.Number);
}

function selectedI2cBusValue() {
    const field = document.getElementById("si5351_i2c_bus");
    // Native value retains a selected disabled option, unlike jQuery val().
    return /^\d+$/.test(field.value) ? Number(field.value) : NaN;
}

function populateI2cBuses(savedBus) {
    const field = document.getElementById("si5351_i2c_bus");
    if (!field) return;
    const platform = window.WSPRRYPI_PLATFORM || {};
    const buses = availableI2cBuses();
    const value = Number(savedBus);
    field.replaceChildren();
    const present = buses.some(bus => bus.Number === value);
    if (!present) {
        const option = new Option(`I2C ${savedBus} — unavailable`, String(savedBus), true, true);
        option.disabled = true;
        field.add(option);
    }
    for (const bus of buses) {
        field.add(new Option(`I2C ${bus.Number}${typeof bus.Name === "string" && bus.Name ? " — " + bus.Name : ""}`, String(bus.Number)));
    }
    field.value = String(savedBus);
    field.title = field.selectedOptions[0]?.textContent || "I2C Bus";
    field.disabled = selectedTransmitBackend() !== "si5351" || buses.length === 0;
    const hint = document.getElementById("si5351-bus-hint");
    hint.textContent = platform.i2cBusDiscoveryError
        ? "Unable to read I2C buses. Reload to try again."
        : !Array.isArray(platform.i2cBuses)
        ? "I2C bus information is unavailable. Reconnect or reload to try again."
        : buses.length === 0
        ? "No I2C buses available. Check that I2C is enabled, then reload."
        : !present
        ? "The saved bus is unavailable. Select an available bus."
        : "Select an I2C bus present on this system.";
}

function validateTransmitterHardwareFields() {
    const wtpValid = window.WtpUi?.validate() ?? true;
    const backend = selectedTransmitBackend();
    const gpioOperatorActive = gpioBackendOperatorActive();
    let invalidCount = 0;

    const gpioPower = normalizeIntegerInputValue("#gpio-power-range", 7);
    const rp1GpioDrive = normalizeIntegerInputValue("#rp1_gpio_drive_ma", 2);
    const si5351Bus = selectedI2cBusValue();
    const si5351Reference = normalizeIntegerInputValue("#si5351_reference_frequency", 27000000);
    const si5351Power = normalizeIntegerInputValue("#si5351-power-range", 1);
    const si5351ReferenceSource = String($("#si5351_reference_source").val() || "external_tcxo");
    const si5351CrystalLoad = normalizeIntegerInputValue("#si5351_crystal_load_capacitance", 10);

    const txPin = getTxPin();
    const txPinValid = txPin === 4 || txPin === 20 || (txPin===null && rp1GpioOperatorVisible());
    const txPinField = document.getElementById("tx_pin");
    if (txPinField) {
        txPinField.setCustomValidity(
            gpioOperatorActive && !txPinValid
                ? "Only GPIO4 and GPIO20 support GPCLK0 clock output."
                : ""
        );
    }
    setFieldValidationState(txPinField, !gpioOperatorActive || txPinValid);
    if (gpioOperatorActive && !txPinValid) {
        invalidCount++;
    } else if (!gpioOperatorActive) {
        txPinField.setCustomValidity("");
        clearFieldValidationState(txPinField);
    }

    const gpioPowerField = document.getElementById("gpio-power-range");
    const legacyGpioActive = backend === "gpio" && !isRp1GpioPlatform();
    const gpioPowerValid = gpioPower >= 0 && gpioPower <= 7;
    setFieldValidationState(
        gpioPowerField,
        !legacyGpioActive || gpioPowerValid
    );
    if (legacyGpioActive && !gpioPowerValid) {
        invalidCount++;
    } else if (!legacyGpioActive && gpioPowerField) {
        gpioPowerField.setCustomValidity("");
        clearFieldValidationState(gpioPowerField);
    }

    if (!validateRp1GpioDrive()) {
        invalidCount++;
    }

    const busValid = availableI2cBuses().some(bus => bus.Number === si5351Bus);
    const si5351BusField = $("#si5351_i2c_bus").get(0);
    si5351BusField.setCustomValidity(busValid ? "" : "Select an available I2C bus.");
    setFieldValidationState(si5351BusField, !(backend === "si5351") || busValid);
    if (backend === "si5351" && !busValid) {
        invalidCount++;
    } else if (backend !== "si5351") {
        si5351BusField.setCustomValidity("");
        clearFieldValidationState(si5351BusField);
    }

    const refValid = si5351Reference > 0;
    const si5351ReferenceField = $("#si5351_reference_frequency").get(0);
    si5351ReferenceField.setCustomValidity(refValid ? "" : "Reference frequency must be greater than 0.");
    setFieldValidationState(si5351ReferenceField, !(backend === "si5351") || refValid);
    if (backend === "si5351" && !refValid) {
        invalidCount++;
    } else if (backend !== "si5351") {
        si5351ReferenceField.setCustomValidity("");
        clearFieldValidationState(si5351ReferenceField);
    }

    const si5351PowerField = document.getElementById("si5351-power-range");
    const si5351PowerValid = si5351Power >= 1 && si5351Power <= 4;
    setFieldValidationState(
        si5351PowerField,
        !(backend === "si5351") || si5351PowerValid
    );
    if (backend === "si5351" && !si5351PowerValid) {
        invalidCount++;
    } else if (backend !== "si5351" && si5351PowerField) {
        si5351PowerField.setCustomValidity("");
        clearFieldValidationState(si5351PowerField);
    }

    const sourceValid = ["external_tcxo", "crystal"].includes(si5351ReferenceSource);
    const sourceField = document.getElementById("si5351_reference_source");
    sourceField.setCustomValidity(sourceValid ? "" : "Select External clock / TCXO or Passive crystal.");
    setFieldValidationState(sourceField, backend !== "si5351" || sourceValid);
    if (backend === "si5351" && !sourceValid) invalidCount++;

    const crystalLoadValid = [6, 8, 10].includes(si5351CrystalLoad);
    const crystalLoadField = document.getElementById("si5351_crystal_load_capacitance");
    crystalLoadField.setCustomValidity(crystalLoadValid ? "" : "Select 6, 8, or 10 pF.");
    setFieldValidationState(
        crystalLoadField,
        backend !== "si5351" || si5351ReferenceSource !== "crystal" || crystalLoadValid
    );
    if (backend === "si5351" && si5351ReferenceSource === "crystal" && !crystalLoadValid) {
        invalidCount++;
    }

    if (backend === "si5351" && !validateSi5351I2cAddress()) {
        invalidCount++;
    } else if (backend !== "si5351") {
        const fld = document.getElementById("si5351_i2c_address");
        if (fld) {
            fld.setCustomValidity("");
            clearFieldValidationState(fld);
        }
    }

    return invalidCount === 0 && wtpValid;
}

function setLEDPin(gpioNumber) {
    setDropdownButtonPin("ledDropdownButton", gpioNumber, "");
}

/**
 * Read the current LED‐pin selection out of your custom dropdown.
 * @returns {number|null} the pin number, e.g. 18, or null if nothing
 * is selected
 */
function getLEDPin() {
    return getDropdownButtonPin("ledDropdownButton");
}

/**
 * Universal dropdown-pin selector
 */
function selectPin(e) {
    const $item = $(this);
    if ($item.prop("disabled")) {
        e.preventDefault();
        return;
    }

    const code = $item.data('val');
    const menuId = $item.closest('.dropdown-menu').attr('aria-labelledby');
    const $btn = $('#' + menuId);

    // Update the toggle button text with the short code
    $btn.text(code);
    $btn.attr("title", $item.text().trim());

    // Mark this item active, clear others
    const $menu = $item.closest('.dropdown-menu');
    $menu.find('.dropdown-item').removeClass('active');
    $item.addClass('active');

    // Clear focus from item and (after hide) from the button
    $item.trigger('blur');
    setTimeout(() => $btn.trigger('blur').removeClass('active show'), 0);
    refreshGpioConflictOptions();
    validatePage();
    scheduleAutosave();
}

/**
 * Programmatically set a pin in your custom dropdown.
 * @param {number} gpioNumber  e.g. 18
 */
function setShutdownPin(gpioNumber) {
    setDropdownButtonPin("shutdownDropdownButton", gpioNumber, "");
}

/**
 * Read the current shutdown pin selection out of your custom dropdown.
 * @returns {number|null} the pin number, e.g. 18, or null if nothing
 * is selected
 */
function getShutdownPin() {
    return getDropdownButtonPin("shutdownDropdownButton");
}

function setAmpPin(gpioNumber) {
    setDropdownButtonPin("ampDropdownButton", gpioNumber, "Disabled", true);
}

function getAmpPin() {
    return getDropdownButtonPin("ampDropdownButton");
}

function buildConfigPayload(options = {}) {
    // Mode: WSPR uses WSPR fields; QRSS/FSKCW/DFCW use the shared CW section.
    let mode = selectedConfigMode();

    // Runtime
    const transmitField = document.getElementById("transmit");
    let transmit = transmitField
        ? parseBool($("#transmit").is(":checked"))
        : !!(currentRuntimeConfigStatus && currentRuntimeConfigStatus.transmitEnabled);
    let use_led = parseBool($("#use_led").is(":checked"));
    let led_pin = parseInt(getLEDPin()) || 18;
    let use_shutdown = parseBool($("#use_shutdown").is(":checked"));
    let shutdown_pin = parseInt(getShutdownPin()) || 19;
    const use_amp = parseBool(getUseAmp());
    const amp_pin_value = getAmpPin();
    const amp_pin = Number.isInteger(amp_pin_value) ? amp_pin_value : -1;
    const amp_pin_active_high = parseBool($("#amp_active_high").is(":checked"));
    let band_gpio = collectBandGpioConfig();
    let transmit_backend = transmitBackendForPersistence();
    if (!["gpio", "rp1-gpclk", "si5351", "wtp"].includes(transmit_backend)) {
        transmit_backend = isRp1GpioPlatform() && rp1GpioOperatorVisible()
            ? "rp1-gpclk"
            : "gpio";
    }

    // WSPR
    let planner_preference = String($("#planner_preference").val() || "auto");
    const validPlannerPreferences = new Set(["auto", "prefer_paired", "require_paired"]);
    if (!validPlannerPreferences.has(planner_preference)) {
        planner_preference = "auto";
    }
    let frequency_profile = String($("#frequency_profile").val() || "existing_common");
    if (!["existing_common", "wrc15"].includes(frequency_profile)) {
        frequency_profile = "existing_common";
    }
    const band_preferences = { ...currentWsprBandPreferences };
    let callsign = trimIdentityValue($("#callsign").val());
    let gridsquare = trimIdentityValue($("#gridsquare").val());
    if (
        options.preservePersistedInvalidIdentity === true &&
        stationIdentityIsInvalid() &&
        persistedStationIdentity
    ) {
        callsign = persistedStationIdentity.callsign;
        gridsquare = persistedStationIdentity.gridsquare;
    }
    let dbm = parseInt($("#dbm").val());
    let frequencies = String($("#frequencies").val() || "").trim();
    let useoffset = parseBool($("#useoffset").is(":checked"));
    const validWsprDbmValues = new Set([0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60]);
    if (!validWsprDbmValues.has(dbm)) {
        dbm = 20;
    }

    // CW shared non-WSPR settings
    let dot_length = Number(String($('#dot_length').val() ?? "").trim());
    let fsk_offset = parseInt($('#fsk_offset').val(), 10);
    let cw_base_frequency = parseFrequencyWithOptionalUnits($('#qrss_frequency').val());
    let tx_start_minute = parseInt($('#tx_start_minute').val(), 10);
    let tx_start_second = Number(String($('#tx_start_second').val() ?? "").trim());
    let tx_repeat_every = parseInt($('#tx_repeat_every').val(), 10);
    let cw_intra_element_gap = Number(String($('#cw_intra_element_gap').val() ?? "").trim());
    let cw_inter_character_gap = Number(String($('#cw_inter_character_gap').val() ?? "").trim());
    let cw_inter_word_gap = Number(String($('#cw_inter_word_gap').val() ?? "").trim());
    let dfcw_intra_element_gap = Number(String($('#dfcw_intra_element_gap').val() ?? "").trim());
    let dfcw_inter_character_gap = Number(String($('#dfcw_inter_character_gap').val() ?? "").trim());
    let dfcw_inter_word_gap = Number(String($('#dfcw_inter_word_gap').val() ?? "").trim());
    let cw_message = String($('#qrss_message').val() || "").trim();
    if (!Number.isInteger(fsk_offset) || fsk_offset <= 0) fsk_offset = 5;
    if (!Number.isFinite(cw_base_frequency) || cw_base_frequency <= 0) cw_base_frequency = 14096900.0;
    if (!Number.isInteger(tx_start_minute) || tx_start_minute < 0 || tx_start_minute > 59) tx_start_minute = 0;
    if (!Number.isInteger(tx_start_second) || tx_start_second < 0 || tx_start_second > 59) tx_start_second = 5;
    if (!Number.isInteger(tx_repeat_every) || tx_repeat_every < 1) tx_repeat_every = 10;

    // Backend-specific frequency calibration.
    let use_system_clock_frequency_estimate = parseBool($("#use_system_clock_frequency_estimate").is(":checked"));
    let gpio_frequency_residual_ppm = Number($("#gpio_frequency_residual_ppm").val());
    let gpio_manual_ppm = Number($("#gpio_manual_ppm").val());
    let ppm_val = Number($("#ppm").val());
    if (!Number.isFinite(gpio_frequency_residual_ppm)) gpio_frequency_residual_ppm = 0.0;
    if (!Number.isFinite(gpio_manual_ppm)) gpio_manual_ppm = 0.0;
    if (!Number.isFinite(ppm_val)) ppm_val = 0.0;

    let gpio_tx_pin = parseInt(getTxPin(), 10);
    if (gpio_tx_pin !== 4 && gpio_tx_pin !== 20) {
        const persistedRp1Pin=rp1RouteUi && (rp1RouteUi.lastPersistableRoute==="GPIO4" || rp1RouteUi.lastPersistableRoute==="GPIO20")
            ? Number(rp1RouteUi.lastPersistableRoute.slice(4)) : 4;
        gpio_tx_pin = persistedRp1Pin;
    }

    const raw = $("#gpio-power-range").val();
    let transmit_power = parseInt(raw, 10);
    if (!(transmit_power >= 0 && transmit_power <= 7)) {
        transmit_power = 7;
    }
    let rp1_gpio_drive_ma = parseInt($("#rp1_gpio_drive_ma").val(), 10);
    if (!supportedRp1GpioDrive(rp1_gpio_drive_ma)) {
        const invalidSource = $("#rp1_gpio_drive_ma").attr("data-invalid-source-value");
        rp1_gpio_drive_ma = invalidSource === undefined
            ? 2
            : parseInt(invalidSource, 10);
    }

    const si5351_i2c_bus = selectedI2cBusValue();

    const si5351_i2c_address = formatSi5351Address(
        document.getElementById("si5351_i2c_address").value
    );

    let si5351_reference_frequency = parseInt($("#si5351_reference_frequency").val(), 10);
    if (!Number.isInteger(si5351_reference_frequency) || si5351_reference_frequency <= 0) {
        si5351_reference_frequency = 27000000;
    }

    let si5351_power_level = parseInt($("#si5351-power-range").val(), 10);
    if (!(si5351_power_level >= 1 && si5351_power_level <= 4)) {
        si5351_power_level = 1;
    }
    let si5351_reference_source = String($("#si5351_reference_source").val() || "external_tcxo");
    if (!["external_tcxo", "crystal"].includes(si5351_reference_source)) {
        si5351_reference_source = "external_tcxo";
    }
    let si5351_crystal_load_capacitance = parseInt($("#si5351_crystal_load_capacitance").val(), 10);
    if (![6, 8, 10].includes(si5351_crystal_load_capacitance)) {
        si5351_crystal_load_capacitance = 10;
    }

    var Operation = {
        "Mode": mode,
        "Transmit": transmit,
        "Transmit Backend": transmit_backend,
        "Use LED": use_led,
        "LED Pin": led_pin,
        "Use Shutdown": use_shutdown,
        "Shutdown Button": shutdown_pin,
        "Use Amp": use_amp,
        "Amp Pin": amp_pin,
        "Amp Pin Active High": amp_pin_active_high,
    };

    var GPIO = {
        "Power Level": transmit_power,
        "RP1 Drive mA": rp1_gpio_drive_ma,
        "Use System Clock Frequency Estimate": use_system_clock_frequency_estimate,
        "Frequency Residual PPM": gpio_frequency_residual_ppm,
        "Manual PPM": gpio_manual_ppm,
        "Transmit Pin": gpio_tx_pin,
    };

    var Si5351 = {
        "I2C Bus": si5351_i2c_bus,
        "I2C Address": si5351_i2c_address,
        "Reference Frequency": si5351_reference_frequency,
        "Reference Source": si5351_reference_source,
        "Crystal Load Capacitance": si5351_crystal_load_capacitance,
        "Power Level": si5351_power_level,
    };

    var Calibration = {
        "PPM": ppm_val,
    };

    var WSPR = {
        "Call Sign": callsign,
        "Grid Square": gridsquare,
        "TX Power": dbm,
        "Frequency": frequencies,
        "Frequency Profile": frequency_profile,
        "Band Preferences": band_preferences,
        "Planner Preference": planner_preference,
        "Use Random Offset": useoffset,
    };

    var CW = {
        "Message": cw_message,
        "Base Frequency": cw_base_frequency,
        "Shift Hz": fsk_offset,
        "Dot Seconds": dot_length,
        "Intra Element Gap": cw_intra_element_gap,
        "Inter Character Gap": cw_inter_character_gap,
        "Inter Word Gap": cw_inter_word_gap,
        "DFCW Intra Element Gap": dfcw_intra_element_gap,
        "DFCW Inter Character Gap": dfcw_inter_character_gap,
        "DFCW Inter Word Gap": dfcw_inter_word_gap,
        "Start Minute": tx_start_minute,
        "Start Second": tx_start_second,
        "Repeat Minutes": tx_repeat_every,
    };

    return {
        Operation,
        GPIO,
        Si5351,
        ...(window.WtpUi ? { WTP: window.WtpUi.read() } : {}),
        Calibration,
        WSPR,
        CW,
        "Band GPIO": band_gpio,
    };
}

function setConfigSaveStatus(state, message = "", detail = "", options = {}) {
    const node = document.getElementById("configSaveStatus");
    const detailNode = document.getElementById("configSaveStatusDetail");
    if (!node) {
        return;
    }

    if (configSaveStatusClearTimer) {
        clearTimeout(configSaveStatusClearTimer);
        configSaveStatusClearTimer = null;
    }

    node.dataset.state = state || "";
    node.textContent = message;
    node.classList.toggle("is-visible", !!message);
    if (detailNode) {
        const detailActionLabel =
            typeof options.detailActionLabel === "string"
                ? options.detailActionLabel.trim()
                : "";
        const onDetailAction =
            typeof options.onDetailAction === "function"
                ? options.onDetailAction
                : null;
        const detailActionControls =
            typeof options.detailActionControls === "string"
                ? options.detailActionControls.trim()
                : "";

        detailNode.innerHTML = "";
        detailNode.hidden = false;
        detailNode.tabIndex = -1;
        detailNode.removeAttribute("role");
        detailNode.removeAttribute("aria-label");

        if (detailActionLabel && onDetailAction) {
            if (detail) {
                const detailText = document.createElement("span");
                detailText.textContent = `${detail} `;
                detailNode.appendChild(detailText);
            }
            const actionButton = document.createElement("button");
            actionButton.type = "button";
            actionButton.className = "btn btn-link btn-sm p-0 align-baseline";
            actionButton.textContent = detailActionLabel;
            if (detailActionControls) {
                actionButton.setAttribute("aria-controls", detailActionControls);
                actionButton.setAttribute("aria-expanded", "false");
            }
            actionButton.addEventListener("click", onDetailAction);
            detailNode.appendChild(actionButton);
        } else {
            detailNode.textContent = detail;
            const isActionable = state === "invalid" && !!firstInvalidConfigControl();
            detailNode.tabIndex = isActionable ? 0 : -1;
            if (isActionable) {
                detailNode.setAttribute("role", "button");
                detailNode.setAttribute(
                    "aria-label",
                    `${detail} Activate to jump to the invalid field.`
                );
            }
        }
    }

    if (state === "saved" && message) {
        configSaveStatusClearTimer = window.setTimeout(() => {
            node.textContent = "";
            node.dataset.state = "";
            node.classList.remove("is-visible");
            if (detailNode) {
                detailNode.hidden = false;
                detailNode.textContent = "";
                detailNode.tabIndex = -1;
                detailNode.removeAttribute("role");
                detailNode.removeAttribute("aria-label");
            }
            configSaveStatusClearTimer = null;
        }, 1800);
    }
}

function stationIdentityIsInvalid() {
    if (selectedConfigMode() !== "WSPR") {
        return false;
    }

    const callsign = trimIdentityValue($("#callsign").val());
    const gridSquare = trimIdentityValue($("#gridsquare").val());
    return !isLightweightCallsign(callsign) ||
        isPlaceholderCallsign(callsign) ||
        !isLightweightGridSquare(gridSquare) ||
        isPlaceholderGridSquare(gridSquare);
}

function setInvalidIdentitySaveStatus(message) {
    setConfigSaveStatus(
        "warning",
        message,
        "The station identity remains local and is not valid for transmission. Fix Call Sign and Grid locator before transmitting.",
        {
            detailActionLabel: "Review station identity",
            onDetailAction: navigateToFirstInvalidConfigControl,
        }
    );
}

function suspendConfigAutosave(suspended) {
    configAutosaveSuspended = !!suspended;
    if (configAutosaveSuspended && configAutosaveTimer) {
        clearTimeout(configAutosaveTimer);
        configAutosaveTimer = null;
    }
}

function syncConfigAutosaveBaseline() {
    if (
        typeof validatePage !== "function" ||
        !validatePage({ allowInvalidStationIdentity: true })
    ) {
        lastSavedConfigPayload = "";
        lastFailedConfigPayload = "";
        lastFailedConfigMessage = "";
        configAutosaveDirty = false;
        removePersistedConfigDraft();
        if (cwDurationPolicyLatched) {
            updateCwDurationPolicyLatch();
        } else {
            setConfigSaveStatus("", "", "");
        }
        return;
    }

    persistedStationIdentity = {
        callsign: trimIdentityValue($("#callsign").val()),
        gridsquare: trimIdentityValue($("#gridsquare").val()),
    };
    const payloadJson = JSON.stringify(buildConfigPayload());
    lastSavedConfigPayload = payloadJson;
    lastFailedConfigPayload = "";
    lastFailedConfigMessage = "";
    configAutosaveDirty = false;
    configAutosavePendingAfterFlight = false;
    pendingPersistedMode = "";
    removePersistedConfigDraft();
    if (stationIdentityIsInvalid()) {
        setInvalidIdentitySaveStatus("Station identity needs attention");
    } else {
        setConfigSaveStatus("saved", "Saved", "");
    }
}

function showConfigAutosavePendingStatus() {
    const statusNode = document.getElementById("configSaveStatus");
    const currentState = statusNode ? statusNode.dataset.state : "";
    if (
        currentState === "error" ||
        currentState === "invalid" ||
        currentState === "load-error"
    ) {
        return;
    }

    setConfigSaveStatus("pending", "Changes pending", "");
}

function pauseConfigAutosaveForNetworkFailure(message) {
    configAutosaveNetworkPaused = true;
    configAutosaveDirty = true;
    persistLocalConfigDraftIfPossible();
    setConfigSaveStatus("error", "Save paused", message);
}

function isConfigAutosaveNetworkPaused() {
    return configAutosaveNetworkPaused;
}

function resumeConfigAutosaveAfterControllerRecovery() {
    if (!configAutosaveNetworkPaused) {
        return false;
    }

    configAutosaveNetworkPaused = false;
    if (!configAutosaveDirty) {
        return false;
    }

    scheduleAutosave();
    setConfigSaveStatus(
        "pending",
        "Connection restored",
        "Pending changes will retry automatically."
    );
    return true;
}

function scheduleAutosave() {
    if (configAutosaveSuspended) {
        return;
    }

    const durationConstraint = updateCwDurationPolicyLatch({ markDirty: true });
    if (durationConstraint.applicable && durationConstraint.overLimit) {
        return;
    }

    configAutosaveDirty = true;
    persistLocalConfigDraftIfPossible();
    showConfigAutosavePendingStatus();
    if (configAutosaveNetworkPaused) {
        return;
    }
    if (configAutosaveTimer) {
        clearTimeout(configAutosaveTimer);
    }

    configAutosaveTimer = window.setTimeout(() => {
        configAutosaveTimer = null;
        flushAutosave();
    }, CONFIG_AUTOSAVE_DELAY_MS);
}

function flushAutosave() {
    if (configAutosaveSuspended) {
        return;
    }

    if (configAutosaveNetworkPaused) {
        return;
    }

    const durationConstraint = updateCwDurationPolicyLatch({ markDirty: true });
    if (durationConstraint.applicable && durationConstraint.overLimit) {
        return;
    }

    if (navigator.onLine === false) {
        pauseConfigAutosaveForNetworkFailure(browserOfflineConfigMessage());
        showBackendStatus(browserOfflineConfigMessage(), "warning", "runtime");
        return;
    }

    if (!validatePage({ allowInvalidStationIdentity: true })) {
        const inactiveGroup = inactiveInvalidCwTimingGroup();
        if (inactiveGroup) {
            const label = inactiveGroup === "dfcw" ? "DFCW spacing" : "QRSS/FSKCW spacing";
            setConfigSaveStatus(
                "invalid",
                "Invalid - not saved",
                `${label} contains an invalid preserved value.`,
                {
                    detailActionLabel: `Review ${label}`,
                    detailActionControls: inactiveGroup === "dfcw"
                        ? "cw_dfcw_gap_section"
                        : "cw_conventional_gap_section",
                    onDetailAction: () => revealCwTimingRepair(inactiveGroup),
                }
            );
            return;
        }
        setConfigSaveStatus(
            "invalid",
            "Invalid - not saved",
            invalidAutosaveDetailMessage()
        );
        return;
    }

    const invalidStationIdentity = stationIdentityIsInvalid();
    const payloadJson = JSON.stringify(buildConfigPayload({
        preservePersistedInvalidIdentity: invalidStationIdentity,
    }));

    if (payloadJson === lastSavedConfigPayload) {
        configAutosaveDirty = false;
        lastFailedConfigPayload = "";
        lastFailedConfigMessage = "";
        if (invalidStationIdentity) {
            setInvalidIdentitySaveStatus("Identity change not saved");
        } else {
            setConfigSaveStatus("saved", "Saved", "");
        }
        return;
    }

    if (payloadJson === lastFailedConfigPayload) {
        configAutosaveDirty = false;
        debugConsole("warn", "Suppressing autosave retry for unchanged rejected payload.");
        setConfigSaveStatus("error", "Save failed", lastFailedConfigMessage);
        return;
    }

    if (configAutosaveInFlight) {
        configAutosavePendingAfterFlight = true;
        return;
    }

    configAutosaveInFlight = true;
    configAutosaveDirty = false;
    configAutosavePendingAfterFlight = false;
    setConfigSaveStatus("saving", "Saving...", "");

    ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {
        type: "PATCH",
        headers: window.WtpUi?.hostRevision ? { "If-Match": window.WtpUi.hostRevision } : {},
        contentType: "application/merge-patch+json",
        timeout: CONFIG_REQUEST_TIMEOUT_MS,
        data: payloadJson,
    })
        .done(function (data, textStatus, xhr) {
            window.WtpUi?.setHostRevision(xhr?.getResponseHeader?.("ETag"));
            configAutosaveNetworkPaused = false;
            lastSaveTimestamp = Date.now();
            lastSavedConfigPayload = payloadJson;
            lastFailedConfigPayload = "";
            lastFailedConfigMessage = "";
            pendingPersistedMode = "";
            const completedPayloadIsCurrent =
                payloadJson === currentConfigPayloadSnapshot({
                    preservePersistedInvalidIdentity: invalidStationIdentity,
                });
            if (
                completedPayloadIsCurrent &&
                !configAutosaveDirty &&
                !configAutosavePendingAfterFlight
            ) {
                if (invalidStationIdentity) {
                    setInvalidIdentitySaveStatus("Other changes saved");
                } else {
                    persistedStationIdentity = {
                        callsign: trimIdentityValue($("#callsign").val()),
                        gridsquare: trimIdentityValue($("#gridsquare").val()),
                    };
                    setConfigSaveStatus("saved", "Saved", "");
                }
            } else {
                showConfigAutosavePendingStatus();
            }
            clearBackendStatus("runtime");
            if (configAutosaveNeedsRuntimeRefresh && typeof getTxState === "function") {
                configAutosaveNeedsRuntimeRefresh = false;
                getTxState();
            }
        })
        .fail(function (xhr, textStatus) {
            let message = "Save failed";
            let parsedError = null;

            if (isTransientNetworkFailure(xhr, textStatus)) {
                const networkMessage = transientConfigSaveMessage(textStatus);
                debugConsole("warn", "Autosave paused by transient network failure.");
                lastFailedConfigPayload = "";
                lastFailedConfigMessage = "";
                pauseConfigAutosaveForNetworkFailure(networkMessage);
                showBackendStatus(networkMessage, "warning", "runtime");
                return;
            }

            if (xhr.responseJSON && typeof xhr.responseJSON === "object") {
                parsedError = xhr.responseJSON;
                message = buildConfigErrorMessage(parsedError, message);
            } else if (typeof xhr.responseText === "string" && xhr.responseText.trim()) {
                try {
                    parsedError = JSON.parse(xhr.responseText);
                    if (parsedError && typeof parsedError === "object") {
                        message = buildConfigErrorMessage(parsedError, message);
                    }
                } catch (error) {
                    debugConsole("warn", "Unable to parse settings error response:", error);
                }
            }

            const isPairedPlanningFailure =
                isPairedPlanningUnavailableError(parsedError);

            if (handleCwDurationPolicyFailure(parsedError)) {
                debugConsole("warn", "Autosave rejected by CW duration policy.");
                lastFailedConfigPayload = payloadJson;
                lastFailedConfigMessage = message;
                configAutosaveDirty = true;
                return;
            }

            debugConsole("error", "Autosave failed:", message);
            configAutosaveNetworkPaused = false;
            lastFailedConfigPayload = payloadJson;
            lastFailedConfigMessage = message;
            configAutosaveDirty = false;
            if (isPairedPlanningFailure) {
                setConfigSaveStatus(
                    "error",
                    PAIRED_PLANNING_SHORT_MESSAGE,
                    message,
                    {
                        detailActionLabel: "More",
                        onDetailAction: () => openSetupDetailsDialog(message),
                    }
                );
            } else {
                setConfigSaveStatus("error", "Save failed", message);
            }
            showBackendStatus(message, "danger", "runtime");
        })
        .always(function () {
            configAutosaveInFlight = false;

            if (
                !configAutosaveNetworkPaused &&
                (configAutosavePendingAfterFlight || configAutosaveDirty)
            ) {
                configAutosavePendingAfterFlight = false;
                scheduleAutosave();
            }
        });
}

/**
 * Validate the WSPR “Frequencies” field.
 * @returns {boolean} true if valid, false otherwise.
 */
function splitWsprFrequencyTokens(raw) {
    return String(raw || "")
        .replace(/,/g, " ")
        .trim()
        .split(/\s+/)
        .filter((token) => token.length > 0);
}

function validateWsprFrequencyBaseToken(token) {
    const trimmed = String(token || "").trim();
    if (!trimmed) {
        return false;
    }

    const bandAliases = new Set([
        "lf",
        "2200m",
        "mf",
        "630m",
        "160m",
        "80m",
        "60m",
        "60m:legacy",
        "60m:wrc15",
        "40m",
        "30m",
        "20m",
        "17m",
        "15m",
        "12m",
        "10m",
        "6m",
        "4m",
        "2m",
        "1.25m",
        "70cm",
    ]);
    if (bandAliases.has(trimmed.toLowerCase())) {
        return true;
    }

    const numericRx = /^(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:hz|khz|mhz|ghz)?$/i;
    if (!numericRx.test(trimmed)) {
        return false;
    }

    return Number.isFinite(Number.parseFloat(trimmed));
}

function validateWsprFrequencyToken(token) {
    const trimmed = String(token || "").trim();
    if (!trimmed) {
        return false;
    }

    const atPos = trimmed.indexOf("@");
    if (atPos === -1) {
        return validateWsprFrequencyBaseToken(trimmed);
    }

    if (trimmed.indexOf("@", atPos + 1) !== -1) {
        return false;
    }

    const baseToken = trimmed.slice(0, atPos).trim();
    let gpioToken = trimmed.slice(atPos + 1).trim();
    if (!baseToken || !gpioToken) {
        return false;
    }

    const suffix = gpioToken.slice(-1).toUpperCase();
    if (suffix === "H" || suffix === "L") {
        gpioToken = gpioToken.slice(0, -1).trim();
        if (!gpioToken) {
            return false;
        }
    }

    if (!/^\d+$/.test(gpioToken)) {
        return false;
    }

    const gpio = Number.parseInt(gpioToken, 10);
    if (!Number.isInteger(gpio) || gpio < 0 || gpio > 27) {
        return false;
    }

    return validateWsprFrequencyBaseToken(baseToken);
}

function validateFrequencies() {
    let valid = true;
    const fld = document.getElementById("frequencies");
    const raw = fld.value.trim();

    // Empty is invalid
    if (!raw) {
        valid = false;
    }

    const tokens = splitWsprFrequencyTokens(raw);

    for (const tok of tokens) {
        if (!validateWsprFrequencyToken(tok)) {
            valid = false;
            break;
        }
    }

    fld.setCustomValidity(
        valid
            ? ""
            : "Enter WSPR presets such as 20m, 60m:legacy, or 60m:wrc15; numeric frequencies; or 0. Separate entries with spaces or commas. Optional @GPIO, @GPIOH, or @GPIOL suffixes are supported."
    );

    setFieldValidationState(fld, valid);

    return valid;
}

function parseFrequencyWithOptionalUnits(rawValue) {
    const raw = String(rawValue || "").trim();
    if (!raw) {
        return Number.NaN;
    }

    const numericRx = /^((?:(?:\d+(?:\.\d*)?)|(?:\.\d+)))(hz|khz|mhz|ghz)?$/i;
    const match = raw.match(numericRx);
    if (!match) {
        return Number.NaN;
    }

    const numericPart = match[1];
    const value = Number.parseFloat(numericPart);
    if (!Number.isFinite(value)) {
        return Number.NaN;
    }

    const unit = (match[2] || "").toLowerCase();
    if (!unit && numericPart.includes(".")) {
        return Number.NaN;
    }

    let normalizedValue = value;
    if (unit === "ghz") {
        normalizedValue = value * 1e9;
    } else if (unit === "mhz") {
        normalizedValue = value * 1e6;
    } else if (unit === "khz") {
        normalizedValue = value * 1e3;
    }

    const roundedValue = Math.round(normalizedValue);
    if (normalizedValue <= 0 || Math.abs(normalizedValue - roundedValue) > 1e-6) {
        return Number.NaN;
    }

    return roundedValue;
}

/**
 * Validate the shared CW “Base Frequency” field.
 * @returns {boolean} true if valid, false otherwise.
 */
function validateCwBaseFrequency() {
    const fld = document.getElementById("qrss_frequency");
    const raw = fld.value.trim();

    let valid = true;

    if (!raw) {
        valid = false;
    }

    // Only accept one frequency
    const tokens = raw.split(/\s+/);
    if (tokens.length !== 1) {
        valid = false;
    }

    if (valid) {
        const value = parseFrequencyWithOptionalUnits(raw);
        if (!Number.isFinite(value) || value <= 0) {
            valid = false;
        }
    }

    fld.setCustomValidity(
        valid
            ? ""
            : "Enter CW base frequency as whole-number Hz or as a value with Hz, kHz, MHz, or GHz."
    );

    // Apply visual styling
    setFieldValidationState(fld, valid);

    return valid;
}

function validateCwMessage() {
    const fld = document.getElementById("qrss_message");
    const ordinary = cwMessageOrdinaryValidation(fld.value);
    const constraint = currentCwDurationConstraint();
    const durationInvalid = constraint.applicable && constraint.overLimit;
    updateCwDurationPolicyLatch();
    const picoConstraint = picoCwMessageConstraint();
    const valid = ordinary.valid && !durationInvalid && !picoConstraint;

    fld.setCustomValidity(
        !ordinary.valid
            ? ordinary.message
            : (picoConstraint || (durationInvalid ? cwDurationPolicyDetail(constraint) : ""))
    );
    setFieldValidationState(fld, valid);

    return valid;
}

function validateCwDotSeconds() {
    const fld = document.getElementById("dot_length");
    const mode = selectedConfigMode();

    if (mode === "WSPR") {
        fld.setCustomValidity("");
        clearFieldValidationState(fld);
        return true;
    }

    const value = Number(String(fld.value || "").trim());
    const valid = Number.isFinite(value) && value > 0;

    fld.setCustomValidity(valid ? "" : "Enter a positive finite CW base duration.");
    setFieldValidationState(fld, valid);

    return valid;
}

function validateCwShiftHz() {
    const fld = document.getElementById("fsk_offset");
    const selectedCwMode = $('input[name="qrss_type"]:checked').val() || "QRSS";

    if (!fld) {
        return true;
    }

    if (selectedConfigMode() === "WSPR" || selectedCwMode === "QRSS" || fld.disabled) {
        fld.setCustomValidity("");
        clearFieldValidationState(fld);
        return true;
    }

    const raw = String(fld.value || "").trim();
    const value = Number.parseInt(raw, 10);
    const valid = /^\d+$/.test(raw) && Number.isInteger(value) && value > 0;

    fld.setCustomValidity(valid ? "" : "Enter a whole-number CW frequency offset in Hz.");
    setFieldValidationState(fld, valid);

    return valid;
}

function validateCwRepeatMinutes() {
    const fld = document.getElementById("tx_repeat_every");
    const mode = selectedConfigMode();

    if (mode === "WSPR" || fld.disabled) {
        fld.setCustomValidity("");
        clearFieldValidationState(fld);
        return true;
    }

    const value = Number.parseInt(fld.value, 10);
    const valid = Number.isInteger(value) && value > 0;

    fld.setCustomValidity(valid ? "" : "Enter a repeat interval of at least 1 minute.");
    setFieldValidationState(fld, valid);

    return valid;
}

function validateCwStartMinute() {
    const fld = document.getElementById("tx_start_minute");
    const mode = selectedConfigMode();

    if (mode === "WSPR" || !fld || fld.disabled) {
        if (fld) {
            fld.setCustomValidity("");
            clearFieldValidationState(fld);
        }
        return true;
    }

    const value = Number.parseInt(fld.value, 10);
    const valid = Number.isInteger(value) && value >= 0 && value <= 59;

    fld.setCustomValidity(valid ? "" : "Enter a CW start minute from 0 through 59.");
    setFieldValidationState(fld, valid);

    return valid;
}

function validateCwStartSecond() {
    const fld = document.getElementById("tx_start_second");
    const mode = selectedConfigMode();

    if (mode === "WSPR" || !fld || fld.disabled) {
        if (fld) {
            fld.setCustomValidity("");
            clearFieldValidationState(fld);
        }
        return true;
    }

    const raw = String(fld.value || "").trim();
    const value = Number(raw);
    const valid = /^\d+$/.test(raw) && Number.isInteger(value) && value >= 0 && value <= 59;

    fld.setCustomValidity(valid ? "" : "Enter a CW start second from 0 through 59.");
    setFieldValidationState(fld, valid);

    return valid;
}

function validatePositiveCwField(fieldId, errorMessage) {
    const fld = document.getElementById(fieldId);
    const mode = selectedConfigMode();

    if (mode === "WSPR" || !fld) {
        if (fld) {
            fld.setCustomValidity("");
            clearFieldValidationState(fld);
        }
        return true;
    }

    const value = Number(String(fld.value || "").trim());
    const valid = Number.isFinite(value) && value > 0;

    fld.setCustomValidity(valid ? "" : errorMessage);
    setFieldValidationState(fld, valid);

    return valid;
}


function setHardwareControlsDisabled(disabled) {
    const controlIds = [
        "#transmit",
        "#stop_transmit",
        "#planner_preference",
        "#frequency_profile",
        "#band-preferences-body select",
        "#band-preferences-body input",
        "#band-preferences-body button",
        "#transmit_backend",
        "#tx_pin",
        "#gpio-power-range",
        "#rp1_gpio_drive_ma",
        "#use_system_clock_frequency_estimate",
        "#gpio_frequency_residual_ppm",
        "#gpio_manual_ppm",
        "#si5351_i2c_bus",
        "#si5351_i2c_address",
        "#si5351_reference_frequency",
        "#si5351_reference_source",
        "#si5351_crystal_load_capacitance",
        "#si5351-power-range",
        "#use_led",
        "#ledDropdownButton",
        "#use_shutdown",
        "#shutdownDropdownButton",
        "#use_amp",
        "#ampDropdownButton",
        "#amp_active_high",
        "#test_tone"
    ];

    controlIds.forEach((selector) => {
        $(selector).prop("disabled", disabled);
    });

    if (!disabled) {
        syncStopButtonState();
        syncAmpControlState();
        syncGpioDriveControls();
        syncBackendControlAvailability();
    }

    syncSi5351ReferenceControls();

    syncCalibrationControls();

    getBandGpioRows().each(function () {
        const $row = $(this);
        $row.find(".band-gpio-enabled").prop("disabled", disabled);
        $row.find(".band-gpio-input").prop("disabled", disabled || !$row.find(".band-gpio-enabled").is(":checked"));
        $row.find(".band-gpio-active-high").prop("disabled", disabled || !$row.find(".band-gpio-enabled").is(":checked"));
    });

    const headerCheckboxes = getBandGpioHeaderCheckboxes();
    Object.values(headerCheckboxes).forEach(($header) => {
        if ($header && $header.length) {
            $header.prop("disabled", disabled || !getBandGpioRows().length);
        }
    });
    syncBandGpioColumnHeaderStates();
}

function setOfflineDefaults() {
    setTransmitFromBackend(false);
    setTransmitBackendSelection(resolveSupportedTransmitBackend("gpio"));
    setTxPin(4);
    $("#gpio-power-range").val(7);
    updateGpioPowerLabel.call(document.getElementById("gpio-power-range"));
    populateRp1GpioDrive(2);
    $("#use_system_clock_frequency_estimate").prop("checked", true);
    $("#gpio_frequency_residual_ppm").val(0);
    $("#gpio_manual_ppm").val(0);
    populateI2cBuses(1);
    setSi5351AddressValue(0x60);
    $("#si5351_reference_frequency").val(27000000);
    $("#si5351_reference_source").val("external_tcxo").trigger("change");
    $("#si5351_crystal_load_capacitance").val("10");
    $("#si5351-power-range").val(1);
    updateSi5351PowerLabel.call(document.getElementById("si5351-power-range"));
    clickTransmitBackend();
    $("#use_led").prop("checked", false);
    $("#use_shutdown").prop("checked", false);
    setUseAmp(false);
    setAmpPin(-1);
    $("#amp_active_high").prop("checked", false);
    populateBandGpioForm({});

    $("#ledDropdownButton")
        .text("GPIO18")
        .attr("title", "GPIO18 (Pin 12 - TAPR LED)");

    $("#shutdownDropdownButton")
        .text("GPIO19")
        .attr("title", "GPIO19 (Pin 35 - TAPR Shutdown)");

    setHardwareControlsDisabled(true);
}

function clearOfflineDefaults() {
    setHardwareControlsDisabled(false);
    clickTransmitBackend();
}

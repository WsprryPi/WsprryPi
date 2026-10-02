// Debug Logging Level (via debugConsole("LEVEL", "message"))
CONSOLE_LOG_LEVEL = "log";

// Service Components
const PATHS = window.WSPRRYPI_PATHS || {};

function normalizeAppBasePath(path) {
    const raw = typeof path === "string" ? path.trim() : "";
    if (!raw || raw === "/") {
        return "";
    }

    const withLeadingSlash = raw.startsWith("/") ? raw : `/${raw}`;
    return withLeadingSlash.replace(/\/+$/, "");
}

function normalizeSameOriginPath(path, fallback) {
    const candidate = typeof path === "string" && path.trim() !== ""
        ? path.trim()
        : fallback;

    try {
        const url = new URL(candidate, window.location.origin);
        return `${url.pathname}${url.search}${url.hash}`;
    } catch {
        return fallback;
    }
}

function buildWebSocketUrl(path) {
    const url = new URL(path, window.location.href);
    url.protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    return url.toString();
}

function buildDirectRestFallbackUrl(path) {
    const protocol = window.location.protocol === "https:" ? "https:" : "http:";
    return `${protocol}//${window.location.hostname}:31415${path}`;
}

function buildDirectWebSocketFallbackUrl(path) {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    return `${protocol}//${window.location.hostname}:31416${path}`;
}

function createEndpointDefinition(name, proxyPath, directUrl) {
    return Object.freeze({
        name,
        proxyPath,
        proxyUrl: proxyPath,
        directUrl,
    });
}

const APP_BASE_PATH = normalizeAppBasePath(PATHS.basePath);
const SETTINGS_PATH = normalizeSameOriginPath(
    PATHS.configPath,
    `${APP_BASE_PATH}/config`
);
const VERSION_PATH = normalizeSameOriginPath(
    PATHS.versionPath,
    `${APP_BASE_PATH}/version`
);
const UI_IDENTITY_PATH = normalizeSameOriginPath(
    PATHS.uiIdentityPath,
    `${APP_BASE_PATH}/ui-version.php`
);
const REPAIR_PATH = normalizeSameOriginPath(
    PATHS.repairPath,
    `${APP_BASE_PATH}/config/repair`
);
const SI5351_ADDRESSES_PATH = `${APP_BASE_PATH}/config/si5351-addresses`;
const SUPPORT_BUNDLES_PATH = normalizeSameOriginPath(
    PATHS.supportBundlesPath,
    `${APP_BASE_PATH}/api/support-bundles`
);
const SUPPORT_INTAKE_PATH = normalizeSameOriginPath(
    PATHS.supportIntakePath,
    `${APP_BASE_PATH}/api/support-intake`
);
const NETWORK_SAFETY_PATH = normalizeSameOriginPath(
    PATHS.networkSafetyPath,
    `${APP_BASE_PATH}/api/network-safety`
);
const WEBSOCKET_PATH = normalizeSameOriginPath(
    PATHS.socketPath,
    `${APP_BASE_PATH}/socket`
);
const LOG_STREAM_PATH = normalizeSameOriginPath(
    PATHS.logStreamPath,
    `${APP_BASE_PATH}/log_stream.php`
);

const SETTINGS_ENDPOINT = createEndpointDefinition(
    "config",
    SETTINGS_PATH,
    buildDirectRestFallbackUrl("/config")
);
const VERSION_ENDPOINT = createEndpointDefinition(
    "version",
    VERSION_PATH,
    buildDirectRestFallbackUrl("/version")
);
const UI_IDENTITY_ENDPOINT = createEndpointDefinition(
    "UI identity",
    UI_IDENTITY_PATH,
    UI_IDENTITY_PATH
);
const REPAIR_ENDPOINT = createEndpointDefinition(
    "config/repair",
    REPAIR_PATH,
    buildDirectRestFallbackUrl("/config/repair")
);
const SI5351_ADDRESSES_ENDPOINT = createEndpointDefinition(
    "Si5351 address discovery",
    SI5351_ADDRESSES_PATH,
    buildDirectRestFallbackUrl("/config/si5351-addresses")
);
const SUPPORT_BUNDLES_ENDPOINT = createEndpointDefinition(
    "support bundles",
    SUPPORT_BUNDLES_PATH,
    buildDirectRestFallbackUrl("/api/support-bundles")
);
const SUPPORT_INTAKE_ENDPOINT = createEndpointDefinition(
    "support intake",
    SUPPORT_INTAKE_PATH,
    buildDirectRestFallbackUrl("/api/support-intake")
);
const NETWORK_SAFETY_ENDPOINT = createEndpointDefinition(
    "network safety",
    NETWORK_SAFETY_PATH,
    NETWORK_SAFETY_PATH
);
const WEBSOCKET_ENDPOINT = createEndpointDefinition(
    "socket",
    buildWebSocketUrl(WEBSOCKET_PATH),
    buildDirectWebSocketFallbackUrl("/socket")
);

// Service URLs
const SETTINGS_URL = SETTINGS_ENDPOINT.proxyUrl;
const VERSION_URL = VERSION_ENDPOINT.proxyUrl;
const REPAIR_URL = REPAIR_ENDPOINT.proxyUrl;
const LOG_STREAM_URL = LOG_STREAM_PATH;
const WSPRNET_URL =
    "https://www.wsprnet.org/olddb?mode=html&band=all&limit=50&findreporter=&sort=date&findcall=";
const TAB_STATE_STORAGE_PREFIX = "wsprrypi.activeTab";
const TEST_TONE_COMMAND_TIMEOUT_MS = 15000;
const WSPR_BAND_CATALOG_TIMEOUT_MS = 5000;
const CANONICAL_WSPR_BAND_NAMES = Object.freeze([
    "2200m", "630m", "160m", "80m", "60m", "40m", "30m",
    "20m", "17m", "15m", "12m", "10m", "6m", "4m", "2m", "1.25m", "70cm"
]);
const EFFECTIVE_WSPR_BAND_NAMES = Object.freeze([
    "2200m", "630m", "160m", "80m", "60m", "40m", "30m",
    "20m", "17m", "15m", "12m", "10m", "8m", "6m", "5m", "4m", "2m", "1.25m", "70cm"
]);
const TEST_TONE_SELECTION_MODES = Object.freeze({
    WSPR_BAND: "wspr_band",
    CUSTOM_RF: "custom_rf"
});
const TEST_TONE_LIFECYCLE = Object.freeze({
    IDLE: "idle",
    START_PENDING: "start_pending",
    ACTIVE: "active",
    END_PENDING: "end_pending",
    UNKNOWN: "unknown"
});
const UPDATE_CHECK_CACHE_PREFIX = "wsprrypi.updateCheck";
const UPDATE_CHECK_FAILURE_CACHE_PREFIX = "wsprrypi.updateCheckFailure";
const UPDATE_CHECK_DISABLED_KEY = "wsprrypi.updateCheckDisabled";
const UPDATE_MODAL_STATE_KEY = "wsprrypi.updateModalState";
const UPDATE_CHECK_CACHE_SCHEMA_VERSION = 7;
const UPDATE_CHECK_CACHE_TTL_MS = 60 * 60 * 1000;
const UPDATE_CHECK_FAILURE_RATE_LIMIT_MS = 5 * 60 * 1000;
const UPDATE_MODAL_RATE_LIMIT_MS = 2 * 60 * 60 * 1000;
const UPDATE_CHECK_RELEASES_URL = "https://github.com/WsprryPi/WsprryPi/releases";
const UPDATE_CHECK_API_BASE = "https://api.github.com/repos/WsprryPi/WsprryPi";
const UI_BUILD_POLL_INTERVAL_MS = 60 * 1000;
const UI_REFRESH_REQUEST_PARAM = "ui_refresh";
const GITHUB_UPDATE_POLL_INTERVAL_MS = 60 * 60 * 1000;
const UPDATE_CHECK_ERROR_MESSAGES = Object.freeze({
    missing_version_data: "Update check failed: local version metadata is incomplete.",
    missing_commit: "Update check failed: local commit metadata is missing.",
    missing_branch: "Update check failed: local branch metadata is missing.",
    branch_missing: "Update check failed: the update branch could not be found on GitHub.",
    rate_limited: "Update check failed: GitHub API rate limit reached.",
    network: "Update check failed: GitHub could not be reached.",
    malformed_response: "Update check failed: GitHub returned malformed update data.",
    comparison_unavailable: "Update check failed: GitHub could not establish the update relationship.",
    unsafe_target: "Update check failed: no trustworthy update target could be established.",
    detached_target_unknown: "Update check failed: detached or unknown branch state has no safe update target.",
    github_http: "Update check failed: GitHub returned an error.",
    unknown: "Update check failed."
});

function warnRestFallback(endpoint, reason) {
    debugConsole(
        "warn",
        `Proxy request for ${endpoint.name} failed (${reason}).`
    );
}

function warnWebSocketFallback(endpoint, reason) {
    debugConsole(
        "warn",
        `Proxy websocket for ${endpoint.name} failed (${reason}).`
    );
}

function warnRestFallbackAttempt(endpoint) {
    debugConsole(
        "warn",
        `Attempting direct fallback for ${endpoint.name} via ${endpoint.directUrl}`
    );
}

function warnWebSocketFallbackAttempt(endpoint) {
    debugConsole(
        "warn",
        `Attempting direct websocket fallback for ${endpoint.name} via ${endpoint.directUrl}`
    );
}

function directFallbackUsesTls(endpoint) {
    return typeof endpoint?.directUrl === "string" &&
        (endpoint.directUrl.startsWith("https://") || endpoint.directUrl.startsWith("wss://"));
}

function warnRestFallbackFailure(endpoint, reason) {
    if (directFallbackUsesTls(endpoint)) {
        debugConsole(
            "warn",
            `Direct TLS fallback failed; backend does not support TLS. Proxy required. (${endpoint.name}: ${reason})`
        );
        return;
    }

    debugConsole(
        "warn",
        `Direct fallback for ${endpoint.name} failed (${reason}).`
    );
}

function warnWebSocketFallbackFailure(endpoint, reason) {
    if (directFallbackUsesTls(endpoint)) {
        debugConsole(
            "warn",
            `Direct TLS fallback failed; backend does not support TLS. Proxy required. (${endpoint.name}: ${reason})`
        );
        return;
    }

    debugConsole(
        "warn",
        `Direct websocket fallback for ${endpoint.name} failed (${reason}).`
    );
}

function cloneAjaxOptions(options) {
    return Object.assign({}, options || {});
}

function ajaxWithEndpointFallback(endpoint, options = {}) {
    const primaryOptions = cloneAjaxOptions(options);
    const fallbackOptions = cloneAjaxOptions(options);
    const deferred = $.Deferred();
    let activeRequest = null;
    let settled = false;

    function resolveDeferred(context, args) {
        if (settled) {
            return;
        }

        settled = true;
        deferred.resolveWith(context, args);
    }

    function rejectDeferred(context, args) {
        if (settled) {
            return;
        }

        settled = true;
        deferred.rejectWith(context, args);
    }

    function startRequest(requestOptions, useFallback) {
        activeRequest = $.ajax(requestOptions)
            .done(function (...args) {
                resolveDeferred(this, args);
            })
            .fail(function (jqXHR, textStatus, errorThrown) {
                if (textStatus === "abort") {
                    rejectDeferred(this, [jqXHR, textStatus, errorThrown]);
                    return;
                }

                if (!useFallback) {
                    const reason = jqXHR && typeof jqXHR.status === "number" && jqXHR.status > 0
                        ? `HTTP ${jqXHR.status}`
                        : (textStatus || "network error");
                    warnRestFallback(endpoint, reason);
                    warnRestFallbackAttempt(endpoint);
                    startRequest(fallbackOptions, true);
                    return;
                }

                const fallbackReason = jqXHR && typeof jqXHR.status === "number" && jqXHR.status > 0
                    ? `HTTP ${jqXHR.status}`
                    : (textStatus || "network error");
                warnRestFallbackFailure(endpoint, fallbackReason);
                rejectDeferred(this, [jqXHR, textStatus, errorThrown]);
            });
    }

    primaryOptions.url = endpoint.proxyUrl;
    fallbackOptions.url = endpoint.directUrl;
    startRequest(primaryOptions, false);

    const promise = deferred.promise();
    promise.abort = function (statusText) {
        if (activeRequest && typeof activeRequest.abort === "function") {
            activeRequest.abort(statusText);
        }

        return promise;
    };

    return promise;
}

function getJsonWithEndpointFallback(endpoint) {
    return ajaxWithEndpointFallback(endpoint, {
        type: "GET",
        dataType: "json",
        cache: false,
    });
}

function cloneFetchInit(init = {}) {
    const cloned = Object.assign({}, init);
    if (init.headers !== undefined) {
        cloned.headers = init.headers;
    }
    return cloned;
}

async function fetchWithEndpointFallback(endpoint, init = {}) {
    const primaryInit = cloneFetchInit(init);
    const fallbackInit = cloneFetchInit(init);

    try {
        const response = await fetch(endpoint.proxyUrl, primaryInit);
        if (response.ok) {
            return response;
        }

        warnRestFallback(endpoint, `HTTP ${response.status}`);
        warnRestFallbackAttempt(endpoint);
    } catch (error) {
        const reason =
            error && typeof error.message === "string" && error.message.trim() !== ""
                ? error.message.trim()
                : "network error";
        warnRestFallback(endpoint, reason);
        warnRestFallbackAttempt(endpoint);
    }

    try {
        const fallbackResponse = await fetch(endpoint.directUrl, fallbackInit);
        if (!fallbackResponse.ok) {
            warnRestFallbackFailure(endpoint, `HTTP ${fallbackResponse.status}`);
        }
        return fallbackResponse;
    } catch (error) {
        const reason =
            error && typeof error.message === "string" && error.message.trim() !== ""
                ? error.message.trim()
                : "network error";
        warnRestFallbackFailure(endpoint, reason);
        throw error;
    }
}

// Allow reloading data after communication interruption
let communicationInterrupted = false;
let reloadAfterReconnectPending = false;
let backendConnectedOnce = false;
let websocketConnectedOnce = false;
let backendCurrentlyConnected = false;
let websocketCurrentlyConnected = false;
let outageBannerArmed = false;
let pageUnloading = false;
let dismissedUiRefreshBuildId = null;
let uiBuildPollTimer = null;
let githubUpdatePollTimer = null;
let uiBuildVersionCheckRunning = false;
let uiRefreshPromptActive = false;
let uiConsistencyDiagnosticActive = false;
let pendingTestToneStopDisableAction = null;
let pendingTestToneStartRequest = false;
let unresolvedTestToneStartContext = null;
let pendingTestToneStartTimeoutHandle = null;
let pendingTestToneEndTimeoutHandle = null;
let testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
let testToneStartQuarantinedSocket = null;
let testToneStartQuarantinedConnectionGeneration = 0;
let retainingTestToneStartContextForResponse = false;
let currentRuntimeStatus = null;
let currentRuntimeConfigStatus = {
    mode: "",
    transmitEnabled: false
};
let currentTestToneConfigContext = {
    mode: "",
    configuredFrequencyHz: 0,
    wsprFrequencyValue: "",
    cwBaseFrequencyHz: 0
};
let currentTestToneSelection = invalidTestToneSelection(
    "Select a Test Tone frequency source."
);
let wsprBandDialFrequenciesHz = Object.create(null);
let wsprAudioOffsetHz = 0;
let wsprBandCatalogConnectionGeneration = 0;
let wsprBandCatalogRequestGeneration = 0;
// The backend catalog response has no correlation ID, so one catalog request is
// intentionally supported per WebSocket connection. Same-socket refresh needs
// protocol-level correlation before it can be made safe.
let wsprBandCatalogRequestedSockets = new WeakSet();
let wsprBandCatalogPendingRequest = null;
let wsprBandCatalogAuthorized = false;
let wsprBandCatalogStatusMessage = "WSPR band catalog unavailable. Test Tone Start is disabled.";
let runtimeStatusRefreshTimer = null;
let chromeOffsetSyncHandle = null;
let lastNavbarOffset = null;
let lastFooterOffset = null;
let websocketReconnectTimer = null;

// Websocket Creation
let ws;
const WS_RECONNECT = 5000; // Retry again every 5s

// Save the last time we sent a config to avoid reload messages on WebSockets
let lastSaveTimestamp = null;
// Keep track of any scheduled reloads
let pendingPopulateConfigTimeout = null;

function currentViewKey() {
    return typeof window.WSPRRYPI_VIEW === "string" ? window.WSPRRYPI_VIEW : "";
}

function isConfigView() {
    return currentViewKey() === "config";
}

function isOperationView() {
    return currentViewKey() === "operation";
}

function isRuntimeControlView() {
    const view = currentViewKey();
    return view === "config" || view === "operation";
}

// Semaphore for singleton data load
let populateConfigRunning = false;
// Semaphore to pause processing (reboot or shutdown)
let systemPaused = false;

// For "are you sure?"
let pendingSystemAction = null;  // "reboot" or "shutdown"

const loggedConfigWarnings =
    window.loggedConfigWarnings ||
    (window.loggedConfigWarnings = new Set());

// Wait for page to load
window.addEventListener("beforeunload", () => {
    pageUnloading = true;
});

window.addEventListener("pagehide", () => {
    pageUnloading = true;
});

$(window).on("load", function () {
    bindActions();
    loadPage();
});

function closeFooterMetaPanel() {
    const footerMeta = document.querySelector("footer .footer-meta");
    if (!footerMeta) {
        return;
    }

    footerMeta.open = false;
}

function initFooterMetaPanelInteractions() {
    const footerMeta = document.querySelector("footer .footer-meta");
    if (!footerMeta) {
        return;
    }

    document.addEventListener("click", function (event) {
        if (!footerMeta.open) {
            return;
        }

        if (event.target instanceof Node && footerMeta.contains(event.target)) {
            return;
        }

        closeFooterMetaPanel();
    });

    document.addEventListener("keydown", function (event) {
        if (event.key !== "Escape" || !footerMeta.open) {
            return;
        }

        closeFooterMetaPanel();
    });
}

const configSchema = {
    Operation: {
        required: false,
        keys: {
            "Mode": { required: false, type: "string" },
            "Transmit": { required: false, type: "boolean" },
            "Transmit Backend": { required: false, type: "string" },
            "Enable on Boot": { required: false, type: "string" },
            "Use LED": { required: false, type: "boolean" },
            "LED Pin": { required: false, type: "number" },
            "Web Port": { required: false, type: "number" },
            "Socket Port": { required: false, type: "number" },
            "Use Shutdown": { required: false, type: "boolean" },
            "Shutdown Button": { required: false, type: "number" },
            "Use Amp": { required: false, type: "boolean" },
            "Amp Pin": { required: false, type: "number" },
            "Amp Pin Active High": { required: false, type: "boolean" }
        }
    },
    GPIO: {
        required: false,
        keys: {
            "Transmit Pin": { required: false, type: "number" },
            "Power Level": { required: false, type: "number" },
            "RP1 Drive mA": { required: false, type: "number" },
            "Use System Clock Frequency Estimate": { required: false, type: "boolean" },
            "Frequency Residual PPM": { required: false, type: "number" },
            "Manual PPM": { required: false, type: "number" }
        }
    },
    Platform: {
        required: false,
        keys: {
            "Model": { required: false, type: "string" },
            "Raspberry Pi Generation": { required: false, type: "number" },
            "GPIO Clock Transmission Supported": { required: false, type: "boolean" },
            "GPIO Clock Transmission Error": { required: false, type: "string" },
            "RP1 GPIO Operator Visible": { required: false, type: "boolean" },
            "I2C Buses": { required: false, type: "array" },
            "I2C Bus Discovery Error": { required: false, type: "string" },
            "Si5351 Detected": { required: false, type: "boolean" },
            "Si5351 Detection Error": { required: false, type: "string" }
        }
    },
    WTP: { required: false, keys: {
        "Endpoint": { required: false, type: "string" },
        "USB Serial": { required: false, type: "string" },
        "Device ID": { required: false, type: "string" },
        "USB Vendor ID": { required: false, type: "number" },
        "USB Product ID": { required: false, type: "number" },
        "Start Uncertainty ns": { required: false, type: "number" },
        "Allow Frequency Adjustment": { required: false, type: "boolean" }
    } },
    Si5351: {
        required: false,
        keys: {
            "I2C Bus": { required: false, type: "number" },
            "I2C Address": { required: false, type: "string" },
            "Reference Frequency": { required: false, type: "number" },
            "Reference Source": { required: false, type: "string" },
            "Crystal Load Capacitance": { required: false, type: "number" },
            "TX Output": { required: false, type: "string" },
            "Power Level": { required: false, type: "number" }
        }
    },
    Calibration: {
        required: false,
        keys: {
            "PPM": { required: false, type: "number" }
        }
    },
    WSPR: {
        required: false,
        keys: {
            "Call Sign": { required: false, type: "string" },
            "Grid Square": { required: false, type: "string" },
            "TX Power": { required: false, type: "number" },
            "Frequency": { required: false, type: "string" },
            "Frequency Profile": { required: false, type: "string" },
            "Band Preferences": { required: false, type: "object" },
            "Planner Preference": { required: false, type: "string" },
            "Use Random Offset": { required: false, type: "boolean" }
        }
    },
    CW: {
        required: false,
        keys: {
            "Message": { required: false, type: "string" },
            "Base Frequency": { required: false, type: "number" },
            "Shift Hz": { required: false, type: "number" },
            "Dot Seconds": { required: false, type: "number" },
            "Start Minute": { required: false, type: "number" },
            "Start Second": { required: false, type: "number" },
            "Repeat Minutes": { required: false, type: "number" }
        }
    },
    "Band GPIO": {
        required: false,
        keys: {
            "2200m": { required: false, type: "object" },
            "630m": { required: false, type: "object" },
            "160m": { required: false, type: "object" },
            "80m": { required: false, type: "object" },
            "60m": { required: false, type: "object" },
            "40m": { required: false, type: "object" },
            "30m": { required: false, type: "object" },
            "8m": { required: false, type: "object" },
            "20m": { required: false, type: "object" },
            "17m": { required: false, type: "object" },
            "15m": { required: false, type: "object" },
            "12m": { required: false, type: "object" },
            "10m": { required: false, type: "object" },
            "6m": { required: false, type: "object" },
            "5m": { required: false, type: "object" },
            "4m": { required: false, type: "object" },
            "2m": { required: false, type: "object" },
            "1.25m": { required: false, type: "object" },
            "70cm": { required: false, type: "object" }
        }
    }
};

function logConfigWarningOnce(message) {
    if (loggedConfigWarnings.has(message)) return;
    loggedConfigWarnings.add(message);
    debugConsole("warn", message);
}

function loadPage() {
    initThemeToggle();
    scheduleChromeOffsetSync();
    hideConnectionAlert();
    setConnectionState("disconnected");
    connectWebSocket(WEBSOCKET_ENDPOINT, WS_RECONNECT);
    updateClocks();
    if (typeof initLogStream === "function") {
        initLogStream();
    }
    initUiBuildChangePolling();
    initGithubUpdatePolling();
    populateConfig();
}

// Verify a requested refresh before polling for later installed-UI changes.
checkUiRefreshConvergence();

// Start UI build polling from global script initialization as soon as site.js
// runs. The shared modal markup is already present before this script is
// included, and initUiBuildChangePolling() guards against duplicate intervals.
initUiBuildChangePolling();
initGithubUpdatePolling();

function getPersistedTabStorageKey(tabList) {
    if (!(tabList instanceof Element)) {
        return "";
    }

    const pageKey =
        typeof window.currentPage === "string" && window.currentPage
            ? window.currentPage
            : "unknown";
    const tabListKey = tabList.id || tabList.getAttribute("aria-label") || "tabs";
    return `${TAB_STATE_STORAGE_PREFIX}:${pageKey}:${tabListKey}`;
}

function getPersistedTabUrlParamKey(tabList) {
    if (!(tabList instanceof Element)) {
        return "";
    }

    const configuredKey = tabList.dataset.persistTabQueryParam;
    if (typeof configuredKey === "string" && configuredKey.trim()) {
        return configuredKey.trim();
    }

    const tabListKey = tabList.id || tabList.getAttribute("aria-label") || "";
    if (!tabListKey) {
        return "";
    }

    return `tab_${tabListKey.replace(/[^A-Za-z0-9_-]+/g, "_")}`;
}

function getPersistedTabRestoreScope(tabList) {
    if (!(tabList instanceof Element)) {
        return "always";
    }

    const scope = tabList.dataset.persistTabStateScope;
    return typeof scope === "string" && scope.trim() ? scope.trim() : "always";
}

function getNavigationType() {
    try {
        const entries = window.performance?.getEntriesByType?.("navigation");
        if (Array.isArray(entries) && entries.length > 0) {
            return typeof entries[0].type === "string" ? entries[0].type : "";
        }
    } catch {
    }

    try {
        if (window.performance?.navigation) {
            switch (window.performance.navigation.type) {
                case window.performance.navigation.TYPE_RELOAD:
                    return "reload";
                case window.performance.navigation.TYPE_BACK_FORWARD:
                    return "back_forward";
                default:
                    return "navigate";
            }
        }
    } catch {
    }

    return "";
}

function shouldRestorePersistedTabState(tabList) {
    const scope = getPersistedTabRestoreScope(tabList);
    if (scope !== "reload") {
        return true;
    }

    return getNavigationType() === "reload";
}

function getDefaultTabSelector(tabList) {
    if (!(tabList instanceof Element)) {
        return "";
    }

    const defaultTrigger = tabList.querySelector('[data-bs-toggle="tab"]');

    return getPersistedTabSelector(defaultTrigger);
}

function clearPersistedTabState(tabList) {
    const storageKey = getPersistedTabStorageKey(tabList);
    if (!storageKey) {
        return;
    }

    try {
        window.localStorage.removeItem(storageKey);
    } catch {
    }
}

function clearPersistedTabUrlState(tabList) {
    const queryParamKey = getPersistedTabUrlParamKey(tabList);
    if (!queryParamKey) {
        return;
    }

    try {
        const url = new URL(window.location.href);
        if (!url.searchParams.has(queryParamKey)) {
            return;
        }
        url.searchParams.delete(queryParamKey);
        window.history.replaceState(window.history.state, "", url.toString());
    } catch {
    }
}

function getPersistedTabSelector(trigger) {
    if (!(trigger instanceof Element)) {
        return "";
    }

    const target = trigger.getAttribute("data-bs-target");
    if (typeof target === "string" && target.trim()) {
        return target.trim();
    }

    const href = trigger.getAttribute("href");
    if (typeof href === "string" && href.startsWith("#")) {
        return href.trim();
    }

    return "";
}

function getPersistedTabSelectorFromUrl(tabList) {
    const queryParamKey = getPersistedTabUrlParamKey(tabList);
    if (!queryParamKey) {
        return "";
    }

    try {
        const url = new URL(window.location.href);
        return url.searchParams.get(queryParamKey) || "";
    } catch {
        return "";
    }
}

function persistTabSelectorInUrl(tabList, selector) {
    const queryParamKey = getPersistedTabUrlParamKey(tabList);
    if (!queryParamKey) {
        return;
    }

    try {
        const url = new URL(window.location.href);
        const defaultSelector = getDefaultTabSelector(tabList);
        if (!selector || (defaultSelector && selector === defaultSelector)) {
            url.searchParams.delete(queryParamKey);
        } else {
            url.searchParams.set(queryParamKey, selector);
        }
        window.history.replaceState(window.history.state, "", url.toString());
    } catch {
    }
}

function findTabTriggerBySelector(tabList, selector) {
    if (!(tabList instanceof Element) || typeof selector !== "string" || !selector.trim()) {
        return null;
    }

    const normalizedSelector = selector.trim();
    const trigger = tabList.querySelector(
        `[data-bs-toggle="tab"][data-bs-target="${normalizedSelector}"], ` +
        `[data-bs-toggle="tab"][href="${normalizedSelector}"]`
    );
    return trigger && !trigger.disabled && !trigger.closest("[hidden]") ? trigger : null;
}

function restorePersistedTabState(tabList) {
    if (!(tabList instanceof Element)) {
        return;
    }

    const urlSelector = getPersistedTabSelectorFromUrl(tabList);
    if (urlSelector) {
        const urlTrigger = findTabTriggerBySelector(tabList, urlSelector);
        if (urlTrigger) {
            const tabInstance = bootstrap.Tab.getOrCreateInstance(urlTrigger);
            tabInstance.show();
            return;
        }

        clearPersistedTabUrlState(tabList);
    }

    if (!shouldRestorePersistedTabState(tabList)) {
        clearPersistedTabState(tabList);
        return;
    }

    const storageKey = getPersistedTabStorageKey(tabList);
    if (!storageKey) {
        return;
    }

    let storedSelector = "";
    try {
        storedSelector = window.localStorage.getItem(storageKey) || "";
    } catch {
        storedSelector = "";
    }

    if (!storedSelector) {
        return;
    }

    const trigger = findTabTriggerBySelector(tabList, storedSelector);
    if (!trigger) {
        try {
            window.localStorage.removeItem(storageKey);
        } catch {
        }
        return;
    }

    const tabInstance = bootstrap.Tab.getOrCreateInstance(trigger);
    tabInstance.show();
}

function initPersistedTabState() {
    document
        .querySelectorAll('[data-persist-tab-state="true"]')
        .forEach((tabList) => {
            if (tabList.dataset.persistTabStateInitialized === "true") {
                return;
            }

            tabList.dataset.persistTabStateInitialized = "true";
            tabList.addEventListener("shown.bs.tab", (event) => {
                const trigger = event.target;
                const selector = getPersistedTabSelector(trigger);
                const storageKey = getPersistedTabStorageKey(tabList);

                persistTabSelectorInUrl(tabList, selector);

                if (!selector || !storageKey) {
                    return;
                }

                try {
                    window.localStorage.setItem(storageKey, selector);
                } catch {
                }
            });

            restorePersistedTabState(tabList);
        });
}

// Called after populateConfig() runs
function pageLoaded() {
    // Update items with callsign
    updateCallsign();

    // Update footer
    updateWsprryPiVersion();

    //
    // Per-Page Loaded Actions
    //

    // If fetchSPots() exists (on view_spots.php) then run it
    if (typeof fetchSpots === "function") {
        fetchSpots();
    }
}

function bindActions() {
    initFooterMetaPanelInteractions();
    initUpdateCheckControls();

    // Tooltips only hover (no focus), so clicking into inputs still works
    $('[data-bs-toggle="tooltip"]').tooltip({
        trigger: "hover",
    });
    // Reset tooltips on buttons/switch clicks
    $(document).on(
        "click",
        'a[data-bs-toggle="tooltip"], button[data-bs-toggle="tooltip"]',
        resetToolTips
    );

    // Bind the theme toggle
    $("#themeToggle").on("click", clickThemeToggle);

    const navbar = document.getElementById("mainNavbar");
    const mainNav = document.getElementById("mainNav");
    if (navbar && mainNav) {
        mainNav.addEventListener("shown.bs.collapse", scheduleChromeOffsetSync);
        mainNav.addEventListener("hidden.bs.collapse", scheduleChromeOffsetSync);
    }
    window.addEventListener("resize", scheduleChromeOffsetSync, { passive: true });
    initPersistedTabState();

    // Grab the modal element and its Bootstrap instance
    const systemModalEl = document.getElementById("systemModal");
    const systemModal = new bootstrap.Modal(systemModalEl, {
        backdrop: "static",
        keyboard: false,
    });

    // Confirm shutdown/reboot
    const confirmModalEl = document.getElementById('confirmModal');
    // create/get the Bootstrap modal instance:
    const confirmModal = new bootstrap.Modal(confirmModalEl, {
        backdrop: 'static',
        keyboard: false
    });
    $('#rebootButton').off('click').on('click', () => {
        openConfirmModal('reboot', confirmModal);
    });
    $('#shutdownButton').off('click').on('click', () => {
        openConfirmModal('shutdown', confirmModal);
    });

    // Hook the Reload button
    $("#systemModal").on("click", ".reload-btn", () => {
        location.reload();
    });

    // Clean up on Exit / X (just unpause, do NOT reload)
    systemModalEl.addEventListener("hidden.bs.modal", () => {
        systemPaused = false;
    });

    //
    // Per-Page Bind Actions
    //

    // Config page bindings
    if (typeof bindIndexActions === "function") {
        bindIndexActions();
    }

    if (typeof bindOperationActions === "function") {
        bindOperationActions();
    }

    // Log viewer bindings
    if (typeof bindLogViewActions === "function") {
        bindLogViewActions();
    }

    // Spot viewer bindings
    if (typeof bindViewSpotsActions === "function") {
        bindViewSpotsActions();
    }
}

function syncFixedChromeOffsets() {
    const root = document.documentElement;
    const navbar = document.getElementById("mainNavbar");
    const footer = document.querySelector("footer.fixed-bottom");

    if (navbar) {
        const navbarOffset = Math.ceil(navbar.getBoundingClientRect().height);
        if (navbarOffset !== lastNavbarOffset) {
            root.style.setProperty("--navbar-offset", `${navbarOffset}px`);
            lastNavbarOffset = navbarOffset;
        }
    }

    if (footer) {
        const footerOffset = Math.ceil(footer.getBoundingClientRect().height);
        if (footerOffset !== lastFooterOffset) {
            root.style.setProperty("--footer-offset", `${footerOffset}px`);
            lastFooterOffset = footerOffset;
        }
    }
}

function scheduleChromeOffsetSync() {
    if (chromeOffsetSyncHandle !== null) return;

    chromeOffsetSyncHandle = window.requestAnimationFrame(() => {
        chromeOffsetSyncHandle = null;
        syncFixedChromeOffsets();
    });
}

function normalizeThemePreference(value) {
    return value === "dark" ? "dark" : "light";
}

function readThemePreference() {
    try {
        const stored = localStorage.getItem("theme");
        if (stored === "light" || stored === "dark") {
            return stored;
        }
        if (stored !== null) {
            localStorage.removeItem("theme");
        }
    } catch (error) {
        // Storage can fail in private browsing or locked-down appliance browsers.
    }

    return "light";
}

function writeThemePreference(theme) {
    try {
        localStorage.setItem("theme", normalizeThemePreference(theme));
    } catch (error) {
        // Theme still applies for this page even if persistence is unavailable.
    }
}

function applyThemePreference(theme) {
    const normalizedTheme = normalizeThemePreference(theme);
    const isDark = normalizedTheme === "dark";

    $("#themeToggle").prop("checked", isDark);
    document.documentElement.setAttribute("data-bs-theme", normalizedTheme);
    updateLabel(isDark);
}

// Initialize on page load: read saved theme, set switch & label
function initThemeToggle() {
    applyThemePreference(readThemePreference());
}

// Update the theme toggle label
function updateLabel(isDark) {
    $("#themeToggleLabel").text(isDark ? "Dark" : "Light");
}

// Handler for clicking the theme toggle
function clickThemeToggle() {
    const newTheme = this.checked ? "dark" : "light";
    applyThemePreference(newTheme);
    writeThemePreference(newTheme);
}

// Helper to parse a true bool
function parseBool(val, fallback = false) {
    if (val === undefined || val === null) return fallback;
    if (typeof val === "boolean") return val;
    if (typeof val === "string") return val.toLowerCase() === "true";
    return fallback;
}

// function getConfigSection(obj, key) {
//     if (!obj || typeof obj !== "object") return {};
//     return obj[key] && typeof obj[key] === "object" ? obj[key] : {};
// }

// function getConfigValue(section, key, fallback) {
//     if (!section || typeof section !== "object") return fallback;
//     return section[key] !== undefined ? section[key] : fallback;
// }

function showLoadAlert() {
    const container = document.querySelector(".card-body");
    if (!container) return;

    const alert = document.createElement("div");
    alert.className = "alert alert-warning";
    alert.innerText =
        "Warning: Failed to fully load configuration. Defaults are in use.";

    container.prepend(alert);
}

function recoverFromPopulateConfigFailure() {
    document.querySelectorAll("input, select, button").forEach(el => {
        el.disabled = false;
    });
}

function configTypeMatches(value, expectedType) {
    if (value === undefined || value === null) return true;

    if (expectedType === "array") return Array.isArray(value);

    if (expectedType === "number") {
        if (typeof value === "number") {
            return !Number.isNaN(value);
        }

        if (typeof value === "string") {
            const trimmed = value.trim();
            if (trimmed === "") return false;
            return !Number.isNaN(Number(trimmed));
        }

        return false;
    }

    if (expectedType === "boolean") {
        return (
            typeof value === "boolean" ||
            value === "true" ||
            value === "false" ||
            value === "1" ||
            value === "0" ||
            value === 1 ||
            value === 0
        );
    }

    if (expectedType === "string") {
        return typeof value === "string";
    }

    return true;
}

function getConfigSection(configJson, name) {
    const section = configJson[name];

    if (section === undefined || section === null) {
        logConfigWarningOnce(
            'Config section "' + name + '" not found. Using defaults.'
        );
        return {};
    }

    if (typeof section !== "object" || Array.isArray(section)) {
        logConfigWarningOnce(
            'Config section "' + name + '" is not an object. Using defaults.'
        );
        return {};
    }

    return section;
}

function getConfigValue(section, sectionName, key, fallback) {
    if (!section || typeof section !== "object") {
        logConfigWarningOnce(
            'Config section "' +
            sectionName +
            '" unavailable while reading "' +
            key +
            '". Using default.'
        );
        return fallback;
    }

    const value = section[key];

    if (value === undefined || value === null) {
        logConfigWarningOnce(
            'Config key "' +
            sectionName +
            "." +
            key +
            '" missing. Using default.'
        );
        return fallback;
    }

    return value;
}

function hasConfigValue(section, key) {
    return !!(
        section &&
        typeof section === "object" &&
        section[key] !== undefined &&
        section[key] !== null
    );
}

function getConfigIntValue(section, sectionName, key, fallback) {
    const rawValue = getConfigValue(section, sectionName, key, fallback);
    const value = parseInt(rawValue, 10);

    if (Number.isNaN(value)) {
        logConfigWarningOnce(
            'Config key "' +
            sectionName +
            "." +
            key +
            '" is not a valid integer. Using default.'
        );
        return fallback;
    }

    return value;
}

function getConfigFloatValue(section, sectionName, key, fallback) {
    const rawValue = getConfigValue(section, sectionName, key, fallback);
    const value = parseFloat(rawValue);

    if (Number.isNaN(value)) {
        logConfigWarningOnce(
            'Config key "' +
            sectionName +
            "." +
            key +
            '" is not a valid number. Using default.'
        );
        return fallback;
    }

    return value;
}

function isSubnormalPpm(value) {
    return Number.isFinite(value) && value !== 0 && Math.abs(value) < 2 ** -1022;
}

function markSubnormalGpioManualPpm(field, savedValue) {
    const error = document.getElementById("gpio-manual-ppm-error");
    field.value = "";
    field.dataset.invalidSubnormalPpm = "true";
    field.setCustomValidity("Enter 0 or a measured Fallback PPM value.");
    if (typeof setFieldValidationState === "function") {
        setFieldValidationState(field, false);
    }
    if (error) {
        error.textContent = savedValue
            ? "The saved Fallback PPM is too small to use. Enter 0 or a measured value to repair it."
            : "Fallback PPM is too small to use. Enter 0 or a measured value.";
        error.hidden = false;
    }
}

function clearSubnormalGpioManualPpm(field) {
    const wasSubnormal = field.dataset.invalidSubnormalPpm === "true";
    delete field.dataset.invalidSubnormalPpm;
    field.setCustomValidity("");
    if (wasSubnormal && typeof clearFieldValidationState === "function") {
        clearFieldValidationState(field);
    }
    const error = document.getElementById("gpio-manual-ppm-error");
    if (error) {
        error.textContent = "";
        error.hidden = true;
    }
}

function populateGpioManualPpm(value) {
    const field = document.getElementById("gpio_manual_ppm");
    if (!field) return;
    if (isSubnormalPpm(Number(value))) {
        markSubnormalGpioManualPpm(field, true);
    } else {
        field.value = String(value);
        clearSubnormalGpioManualPpm(field);
    }
    $(field).trigger("change");
    if (typeof syncBackendPanelVisibility === "function") {
        syncBackendPanelVisibility();
    }
}

function getConfigTimingValue(section, sectionName, key, fallback) {
    const rawValue = getConfigValue(section, sectionName, key, fallback);
    return typeof rawValue === "number" ? rawValue : Number(String(rawValue).trim());
}

function getConfigBoolValue(section, sectionName, key, fallback) {
    const rawValue = getConfigValue(section, sectionName, key, fallback);

    if (
        rawValue === true ||
        rawValue === false ||
        rawValue === "true" ||
        rawValue === "false" ||
        rawValue === "1" ||
        rawValue === "0" ||
        rawValue === 1 ||
        rawValue === 0
    ) {
        return parseBool(rawValue);
    }

    logConfigWarningOnce(
        'Config key "' +
        sectionName +
        "." +
        key +
        '" is not a valid boolean. Using default.'
    );
    return fallback;
}

function validateConfigSchema(json, schema) {
    Object.keys(schema).forEach(function (sectionName) {
        const sectionRule = schema[sectionName];
        const section = json[sectionName];
        if (sectionRule.disabled) return; // Return early if section is disabled

        if (section === undefined || section === null) {
            if (sectionRule.required) {
                logConfigWarningOnce(
                    'Missing required config section "' + sectionName + '".'
                );
            } else {
                logConfigWarningOnce(
                    'Missing optional config section "' +
                    sectionName +
                    '". Defaults will be used.'
                );
            }
            return;
        }

        if (typeof section !== "object" || Array.isArray(section)) {
            logConfigWarningOnce(
                'Invalid config section "' +
                sectionName +
                '". Expected object. Defaults will be used.'
            );
            return;
        }

        Object.keys(sectionRule.keys).forEach(function (keyName) {
            const keyRule = sectionRule.keys[keyName];
            const value = section[keyName];

            if (value === undefined || value === null) {
                if (keyRule.required) {
                    logConfigWarningOnce(
                        'Missing required config key "' +
                        sectionName +
                        "." +
                        keyName +
                        '".'
                    );
                } else {
                    logConfigWarningOnce(
                        'Missing optional config key "' +
                        sectionName +
                        "." +
                        keyName +
                        '". Default will be used.'
                    );
                }
                return;
            }

            if (!configTypeMatches(value, keyRule.type)) {
                logConfigWarningOnce(
                    'Invalid type for config key "' +
                    sectionName +
                    "." +
                    keyName +
                    '". Expected ' +
                    keyRule.type +
                    ". Default may be used."
                );
            }
        });
    });
}

/**
 * Reload all UI data after communication is restored.
 */
function reloadAllData() {
    debugConsole("debug", "Reloading all UI data after communication recovery.");

    populateConfig(() => {
        if (typeof fetchSpots === "function") {
            fetchSpots();
        }

        if (typeof getTxState === "function") {
            getTxState();
        }
    });
}

function pageHasConnectionAlert() {
    return document.getElementById("connection-alert") !== null;
}

function showConnectionAlert() {
    if (!pageHasConnectionAlert()) {
        return;
    }

    const alertElement = document.getElementById("connection-alert");
    alertElement.hidden = false;
    alertElement.classList.add("show");
}

function hideConnectionAlert() {
    if (!pageHasConnectionAlert()) {
        return;
    }

    const alertElement = document.getElementById("connection-alert");
    alertElement.classList.remove("show");
    alertElement.hidden = true;
}

function shouldShowBackendLossStatus() {
    return pageHasConnectionAlert() && outageBannerArmed && !pageUnloading;
}

function shouldShowWebSocketLossStatus() {
    return pageHasConnectionAlert() && outageBannerArmed && !pageUnloading;
}

function armOutageBannerIfReady() {
    if (backendConnectedOnce && websocketConnectedOnce) {
        outageBannerArmed = true;
    }
}

function clearPendingPopulateConfigRetry() {
    if (pendingPopulateConfigTimeout) {
        clearTimeout(pendingPopulateConfigTimeout);
        pendingPopulateConfigTimeout = null;
    }
}

function schedulePopulateConfigRetry(callback = null, delayMs = 10000) {
    clearPendingPopulateConfigRetry();
    pendingPopulateConfigTimeout = setTimeout(
        function () {
            pendingPopulateConfigTimeout = null;
            populateConfig(callback);
        },
        delayMs
    );
}

function clearWebSocketReconnectTimer() {
    if (websocketReconnectTimer) {
        clearTimeout(websocketReconnectTimer);
        websocketReconnectTimer = null;
    }
}

function syncConnectionAlert() {
    if (typeof window.isRp1RouteStatusModalActive === "function" && window.isRp1RouteStatusModalActive()) {
        hideConnectionAlert();
        return;
    }
    if (!outageBannerArmed || pageUnloading) {
        hideConnectionAlert();
        return;
    }

    if (backendCurrentlyConnected && websocketCurrentlyConnected) {
        hideConnectionAlert();
        return;
    }

    showConnectionAlert();
}

function configLoadFailureMessage() {
    if (navigator.onLine === false) {
        return "This browser is offline. Runtime controls are temporarily read-only until the controller can be reached again.";
    }

    return "The controller is temporarily unavailable. Last known values remain visible, and hardware controls are read-only while the page retries.";
}

function setConfigLoadFailureState() {
    if (!isRuntimeControlView()) {
        return;
    }

    if (typeof finishOperationRetryFeedback === "function") {
        finishOperationRetryFeedback();
    }

    const message = configLoadFailureMessage();
    if (typeof showBackendStatus === "function") {
        showBackendStatus(message, "warning", "backend");
    }
    const autosaveIsNetworkPaused =
        typeof isConfigAutosaveNetworkPaused === "function" &&
        isConfigAutosaveNetworkPaused();
    if (
        isConfigView() &&
        !autosaveIsNetworkPaused &&
        typeof setConfigSaveStatus === "function"
    ) {
        setConfigSaveStatus("load-error", "Controller unavailable", message);
    }
}

function clearConfigLoadFailureState() {
    if (!isRuntimeControlView()) {
        return;
    }

    if (typeof finishOperationRetryFeedback === "function") {
        finishOperationRetryFeedback();
    }

    if (typeof clearBackendStatus === "function") {
        clearBackendStatus("backend");
    }

    const node = document.getElementById("configSaveStatus");
    if (
        isConfigView() &&
        node &&
        node.dataset &&
        node.dataset.state === "load-error" &&
        typeof setConfigSaveStatus === "function"
    ) {
        setConfigSaveStatus("", "", "");
    }
}

// Data Load
function createOperationConfigSnapshot({
    mode,
    transmit,
    transmitBackend,
    enableOnBoot,
    callsign,
    gridsquare,
    wsprFrequencyValue,
    cwBaseFrequencyHz,
    cwOffsetHz,
}) {
    return {
        mode,
        transmit,
        transmitBackend,
        enableOnBoot,
        callsign,
        gridsquare,
        wsprFrequencyHz: parseConfiguredWsprFrequencyHz(wsprFrequencyValue),
        cwBaseFrequencyHz,
        cwOffsetHz,
    };
}

function populateConfig(callback = null) {
    if (populateConfigRunning) return;
    populateConfigRunning = true;

    getJsonWithEndpointFallback(SETTINGS_ENDPOINT)
        .done(function (configJson, textStatus, xhr) {
            window.WtpUi?.setHostRevision(xhr?.getResponseHeader?.("ETag"));
            try {
                if (!configJson || typeof configJson !== "object") {
                    throw new Error("Invalid JSON data received.");
                }

                backendCurrentlyConnected = true;
                backendConnectedOnce = true;
                armOutageBannerIfReady();
                syncConnectionAlert();
                clearConfigLoadFailureState();
                clearPendingPopulateConfigRetry();

                validateConfigSchema(configJson, configSchema);

                const operation = getConfigSection(configJson, "Operation");
                const gpio = getConfigSection(configJson, "GPIO");
                const calibration = getConfigSection(configJson, "Calibration");
                const si5351 = getConfigSection(configJson, "Si5351");
                const wspr = getConfigSection(configJson, "WSPR");
                const cw = getConfigSection(configJson, "CW");
                const bandGpio = getConfigSection(configJson, "Band GPIO");
                const platform = getConfigSection(configJson, "Platform");

                window.WSPRRYPI_PLATFORM = {
                    model: getConfigValue(platform, "Platform", "Model", ""),
                    raspberryPiGeneration: getConfigIntValue(
                        platform,
                        "Platform",
                        "Raspberry Pi Generation",
                        -1
                    ),
                    gpioClockTransmissionSupported: getConfigBoolValue(
                        platform,
                        "Platform",
                        "GPIO Clock Transmission Supported",
                        true
                    ),
                    gpioClockTransmissionError: getConfigValue(
                        platform,
                        "Platform",
                        "GPIO Clock Transmission Error",
                        ""
                    ),
                    rp1GpioOperatorVisible: getConfigBoolValue(
                        platform,
                        "Platform",
                        "RP1 GPIO Operator Visible",
                        false
                    ),
                    i2cBuses: Array.isArray(platform["I2C Buses"]) ? platform["I2C Buses"] : null,
                    i2cBusDiscoveryError: String(platform["I2C Bus Discovery Error"] || ""),
                    si5351AddressBus: getConfigIntValue(
                        platform, "Platform", "Si5351 Address Bus", -1
                    ),
                    si5351Addresses: Array.isArray(platform["Si5351 Addresses"])
                        ? platform["Si5351 Addresses"] : null,
                    si5351AddressDiscoveryError: String(
                        platform["Si5351 Address Discovery Error"] || ""
                    ),
                    si5351Detected: getConfigBoolValue(
                        platform,
                        "Platform",
                        "Si5351 Detected",
                        true
                    ),
                    si5351DetectionError: getConfigValue(
                        platform,
                        "Platform",
                        "Si5351 Detection Error",
                        ""
                    )
                };

                let mode = getConfigValue(operation, "Operation", "Mode", "WSPR");
                if (!["WSPR", "QRSS", "FSKCW", "DFCW"].includes(mode)) {
                    mode = "WSPR";
                }

                let plannerPreference = getConfigValue(
                    wspr,
                    "WSPR",
                    "Planner Preference",
                    "auto"
                );
                if (
                    plannerPreference !== "auto" &&
                    plannerPreference !== "prefer_paired" &&
                    plannerPreference !== "require_paired"
                ) {
                    plannerPreference = "auto";
                }

                let frequencyProfile = getConfigValue(
                    wspr,
                    "WSPR",
                    "Frequency Profile",
                    "existing_common"
                );
                if (!["existing_common", "wrc15"].includes(frequencyProfile)) {
                    frequencyProfile = "existing_common";
                }
                const bandPreferencesValue = getConfigValue(
                    wspr, "WSPR", "Band Preferences", {}
                );
                currentWsprBandPreferences = bandPreferencesValue &&
                    typeof bandPreferencesValue === "object" &&
                    !Array.isArray(bandPreferencesValue)
                    ? { ...bandPreferencesValue }
                    : {};
                let transmit = getConfigBoolValue(
                    operation,
                    "Operation",
                    "Transmit",
                    false
                );
                let transmitBackend = String(
                    getConfigValue(
                        operation,
                        "Operation",
                        "Transmit Backend",
                        "gpio"
                    ) || "gpio"
                ).toLowerCase();
                if (!["gpio", "rp1-gpclk", "si5351", "wtp"].includes(transmitBackend)) {
                    transmitBackend = "gpio";
                }
                let enableOnBoot = String(
                    getConfigValue(
                        operation,
                        "Operation",
                        "Enable on Boot",
                        "Never"
                    ) || "Never"
                ).trim();
                if (!["Never", "Follow", "Always"].includes(enableOnBoot)) {
                    enableOnBoot = "Never";
                }
                const callsignWasLoaded = hasConfigValue(wspr, "Call Sign");
                let callsign = String(getConfigValue(
                    wspr,
                    "WSPR",
                    "Call Sign",
                    "N0CALL"
                ) || "").trim() || "N0CALL";
                if (
                    !callsignWasLoaded &&
                    typeof callsign === "string" &&
                    ["N0CALL", "NXXX"].includes(callsign.toUpperCase())
                ) {
                    logConfigWarningOnce(
                        'Config key "WSPR.Call Sign" is placeholder (' + callsign + ').'
                    );
                }
                const gridSquareWasLoaded = hasConfigValue(wspr, "Grid Square");
                let gridsquare = String(getConfigValue(
                    wspr,
                    "WSPR",
                    "Grid Square",
                    "ZZ99"
                ) || "").trim() || "ZZ99";
                if (
                    !gridSquareWasLoaded &&
                    typeof gridsquare === "string" &&
                    gridsquare.toUpperCase() === "ZZ99"
                ) {
                    logConfigWarningOnce(
                        'Config key "WSPR.Grid Square" is placeholder (ZZ99).'
                    );
                }
                let dbm = getConfigIntValue(wspr, "WSPR", "TX Power", 20);
                let frequencies = String(getConfigValue(
                    wspr,
                    "WSPR",
                    "Frequency",
                    "20m"
                ) || "").trim() || "20m";
                let tx_pin = getConfigIntValue(
                    gpio,
                    "GPIO",
                    "Transmit Pin",
                    4
                );
                let use_led = getConfigBoolValue(
                    operation,
                    "Operation",
                    "Use LED",
                    false
                );
                let led_pin = getConfigIntValue(
                    operation,
                    "Operation",
                    "LED Pin",
                    18
                );
                let use_system_clock_frequency_estimate = getConfigBoolValue(
                    gpio,
                    "GPIO",
                    "Use System Clock Frequency Estimate",
                    true
                );
                let gpio_frequency_residual_ppm = getConfigFloatValue(
                    gpio,
                    "GPIO",
                    "Frequency Residual PPM",
                    0.0
                );
                let gpio_manual_ppm = getConfigFloatValue(
                    gpio,
                    "GPIO",
                    "Manual PPM",
                    0.0
                );
                let ppm = getConfigFloatValue(calibration, "Calibration", "PPM", 0.0);
                let use_offset = getConfigBoolValue(
                    wspr,
                    "WSPR",
                    "Use Random Offset",
                    true
                );
                let power_level = getConfigIntValue(
                    gpio,
                    "GPIO",
                    "Power Level",
                    7
                );
                let rp1GpioDriveMa = getConfigIntValue(
                    gpio,
                    "GPIO",
                    "RP1 Drive mA",
                    2
                );
                let si5351I2cBus = getConfigIntValue(
                    si5351,
                    "Si5351",
                    "I2C Bus",
                    1
                );
                let si5351I2cAddressRaw = getConfigValue(
                    si5351,
                    "Si5351",
                    "I2C Address",
                    "0x60"
                );
                let si5351ReferenceFrequency = getConfigIntValue(
                    si5351,
                    "Si5351",
                    "Reference Frequency",
                    27000000
                );
                let si5351ReferenceSource = String(getConfigValue(
                    si5351, "Si5351", "Reference Source", "external_tcxo"
                ) || "external_tcxo").toLowerCase();
                if (!["external_tcxo", "crystal"].includes(si5351ReferenceSource)) {
                    si5351ReferenceSource = "external_tcxo";
                }
                let si5351CrystalLoadCapacitance = getConfigIntValue(
                    si5351, "Si5351", "Crystal Load Capacitance", 10
                );
                if (![6, 8, 10].includes(si5351CrystalLoadCapacitance)) {
                    si5351CrystalLoadCapacitance = 10;
                }
                let si5351PowerLevel = getConfigIntValue(
                    si5351,
                    "Si5351",
                    "Power Level",
                    1
                );
                let use_shutdown = getConfigBoolValue(
                    operation,
                    "Operation",
                    "Use Shutdown",
                    false
                );
                let shutdown_pin = getConfigIntValue(
                    operation,
                    "Operation",
                    "Shutdown Button",
                    19
                );
                let amp_pin = getConfigIntValue(
                    operation,
                    "Operation",
                    "Amp Pin",
                    -1
                );
                let use_amp = getConfigBoolValue(
                    operation,
                    "Operation",
                    "Use Amp",
                    amp_pin >= 0
                );
                if (amp_pin < 0) {
                    use_amp = false;
                }
                let amp_pin_active_high = getConfigBoolValue(
                    operation,
                    "Operation",
                    "Amp Pin Active High",
                    false
                );
                let dot_length = getConfigTimingValue(cw, "CW", "Dot Seconds", 3.0);
                let fsk_offset = getConfigFloatValue(cw, "CW", "Shift Hz", 5.0);
                let cw_base_frequency = getConfigFloatValue(cw, "CW", "Base Frequency", 14096900.0);
                let cw_intra_element_gap = getConfigTimingValue(cw, "CW", "Intra Element Gap", 1.0);
                let cw_inter_character_gap = getConfigTimingValue(cw, "CW", "Inter Character Gap", 3.0);
                let cw_inter_word_gap = getConfigTimingValue(cw, "CW", "Inter Word Gap", 7.0);
                let dfcw_intra_element_gap = getConfigTimingValue(cw, "CW", "DFCW Intra Element Gap", 0.333333);
                let dfcw_inter_character_gap = getConfigTimingValue(cw, "CW", "DFCW Inter Character Gap", 1.0);
                let dfcw_inter_word_gap = getConfigTimingValue(cw, "CW", "DFCW Inter Word Gap", 3.0);
                let tx_start_minute = getConfigIntValue(cw, "CW", "Start Minute", 0);
                let tx_start_second = getConfigIntValue(cw, "CW", "Start Second", 5);
                let tx_repeat_every = getConfigIntValue(cw, "CW", "Repeat Minutes", 10);
                let cw_message = String(getConfigValue(cw, "CW", "Message", "") || "").trim();
                const cwBaseFrequencyHz = Number.isFinite(cw_base_frequency)
                    ? cw_base_frequency
                    : 0;
                updateTestToneConfigContext(mode, frequencies, cwBaseFrequencyHz);
                updateTestToneFrequencyContext();
                updateTestToneFrequencyInputDefault();

                // Operation.Web Port and Operation.Socket Port remain
                // backend-managed settings without visible controls on this
                // page. Selector polarity is modeled per band under
                // Band GPIO.<band>.Active High and per-frequency via
                // @GPIO[H|L] metadata, not as a single GPIO-wide setting.

                // If we are on the config page
                if (isConfigView()) {
                    if (typeof suspendConfigAutosave === "function") {
                        suspendConfigAutosave(true);
                    }

                    // Load form elements
                    //
                    if (typeof applyConfigModeSelection === "function") {
                        applyConfigModeSelection(mode);
                    } else if (mode === "WSPR") {
                        $('input[name="mode_toggle"][value="WSPR"]')
                            .prop("checked", true)
                            .trigger("change");
                    } else {
                        $('input[name="mode_toggle"][value="QRSS"]')
                            .prop("checked", true)
                            .trigger("change");
                        $(`input[name="qrss_type"][value="${mode}"]`)
                            .prop("checked", true)
                            .trigger("change");
                    }

                    if (typeof clearOfflineDefaults === "function") {
                        clearOfflineDefaults();
                    }

                    // Hardware Control
                    if (typeof setTransmitFromBackend === "function") {
                        setTransmitFromBackend(transmit);
                    } else {
                        $("#transmit").prop("checked", transmit);
                    }
                    if (typeof updateRuntimeControlConfigStatus === "function") {
                        updateRuntimeControlConfigStatus(mode, transmit);
                    }
                    $("#planner_preference").val(plannerPreference).trigger("change");
                    $("#frequency_profile").val(frequencyProfile).trigger("change");
                    if (typeof renderBandPreferenceRows === "function") {
                        renderBandPreferenceRows();
                    }
                    if (typeof setTransmitBackendSelection === "function") {
                        window.WtpUi?.populate(configJson.WTP);
                        // Inventory starts once, after the saved bus/address are populated below.
                        setTransmitBackendSelection(transmitBackend, true, false);
                    } else {
                        $("#transmit_backend")
                            .prop("checked", transmitBackend === "si5351")
                            .trigger("change");
                    }
                    if (typeof updateBackendPlatformSupportUi === "function") {
                        updateBackendPlatformSupportUi();
                    }
                    if (typeof setTxPin === "function") {
                        setTxPin(tx_pin);
                    }
                    if (typeof initializeRp1RouteUi === "function") {
                        initializeRp1RouteUi();
                    }
                    $("#use_led").prop("checked", use_led).trigger("change");
                    setLEDPin(led_pin);
                    $("#use_shutdown")
                        .prop("checked", use_shutdown)
                        .trigger("change");
                    setShutdownPin(shutdown_pin);
                    if (typeof setAmpPin === "function") {
                        setAmpPin(amp_pin);
                    }
                    if (typeof setUseAmp === "function") {
                        setUseAmp(use_amp);
                    } else {
                        $("#use_amp").prop("checked", use_amp).trigger("change");
                    }
                    $("#amp_active_high")
                        .prop("checked", amp_pin_active_high)
                        .trigger("change");
                    if (typeof populateBandGpioForm === "function") {
                        populateBandGpioForm(bandGpio);
                    }

                    // Operator Information
                    $("#callsign").val(callsign).trigger("change");
                    $("#gridsquare").val(gridsquare).trigger("change");

                    // Transmitter Information
                    $("#dbm").val(dbm).trigger("change");
                    $("#frequencies").val(frequencies).trigger("change");
                    $("#useoffset").prop("checked", use_offset).trigger("change");

                    // CW shared non-WSPR configuration
                    $("#dot_length").val(dot_length).trigger("change");
                    $("#fsk_offset").val(fsk_offset).trigger("change");
                    $("#qrss_frequency").val(cw_base_frequency).trigger("change");
                    $("#cw_intra_element_gap").val(cw_intra_element_gap).trigger("change");
                    $("#cw_inter_character_gap").val(cw_inter_character_gap).trigger("change");
                    $("#cw_inter_word_gap").val(cw_inter_word_gap).trigger("change");
                    $("#dfcw_intra_element_gap").val(dfcw_intra_element_gap).trigger("change");
                    $("#dfcw_inter_character_gap").val(dfcw_inter_character_gap).trigger("change");
                    $("#dfcw_inter_word_gap").val(dfcw_inter_word_gap).trigger("change");
                    if (typeof synchronizeCwTimingAfterPopulation === "function") {
                        synchronizeCwTimingAfterPopulation();
                    }
                    $("#tx_start_minute").val(tx_start_minute).trigger("change");
                    $("#tx_start_second").val(tx_start_second).trigger("change");
                    $("#tx_repeat_every").val(tx_repeat_every).trigger("change");
                    $('#qrss_message').val(cw_message).trigger("change");
                    if (typeof updateCwMessageLengthEstimate === "function") {
                        updateCwMessageLengthEstimate();
                    }

                    // Frequency Calibration
                    $("#use_system_clock_frequency_estimate").prop("checked", use_system_clock_frequency_estimate).trigger("change");
                    $("#gpio_frequency_residual_ppm").val(gpio_frequency_residual_ppm).trigger("change");
                    populateGpioManualPpm(gpio_manual_ppm);
                    $("#ppm").val(ppm).trigger("change");

                    $("#gpio-power-range").val(power_level).trigger("input");
                    if (typeof populateRp1GpioDrive === "function") {
                        populateRp1GpioDrive(rp1GpioDriveMa);
                    }
                    populateI2cBuses(si5351I2cBus);
                    if (typeof populateSi5351Addresses === "function") {
                        populateSi5351Addresses(
                            si5351I2cBus,
                            si5351I2cAddressRaw,
                            window.WSPRRYPI_PLATFORM.si5351Addresses,
                            window.WSPRRYPI_PLATFORM.si5351AddressDiscoveryError,
                            window.WSPRRYPI_PLATFORM.si5351AddressBus
                        );
                    } else {
                        $("#si5351_i2c_address").val(si5351I2cAddressRaw).trigger("change");
                    }
                    $("#si5351_reference_frequency")
                        .val(si5351ReferenceFrequency)
                        .trigger("change");
                    $("#si5351_reference_source")
                        .val(si5351ReferenceSource)
                        .trigger("change");
                    $("#si5351_crystal_load_capacitance")
                        .val(String(si5351CrystalLoadCapacitance))
                        .trigger("change");
                    $("#si5351-power-range").val(si5351PowerLevel).trigger("input");

                    if (selectedTransmitBackend() === "si5351" &&
                        typeof refreshSi5351Addresses === "function") {
                        refreshSi5351Addresses(si5351I2cBus, si5351I2cAddressRaw);
                    }

                    // Enable the form
                    $("#test_tone").prop("disabled", false);
                    $("#wsprform").prop("disabled", false);

                    validatePage();
                    if (typeof syncConfigAutosaveBaseline === "function") {
                        syncConfigAutosaveBaseline();
                    }
                    if (typeof restorePersistedConfigDraft === "function") {
                        restorePersistedConfigDraft();
                    }
                    if (typeof suspendConfigAutosave === "function") {
                        suspendConfigAutosave(false);
                    }
                    if (typeof resumeConfigAutosaveAfterControllerRecovery === "function") {
                        resumeConfigAutosaveAfterControllerRecovery();
                    }
                } else if (isOperationView()) {
                    if (typeof setTransmitFromBackend === "function") {
                        setTransmitFromBackend(transmit);
                    } else {
                        $("#transmit").prop("checked", transmit);
                    }
                    if (typeof setSelectedRuntimeTransmitBackend === "function") {
                        setSelectedRuntimeTransmitBackend(transmitBackend);
                    }
                    if (typeof updateRuntimeControlConfigStatus === "function") {
                        updateRuntimeControlConfigStatus(mode, transmit);
                    }
                    if (typeof handleOperationConfigSnapshot === "function") {
                        handleOperationConfigSnapshot(createOperationConfigSnapshot({
                            mode,
                            transmit,
                            transmitBackend,
                            enableOnBoot,
                            callsign,
                            gridsquare,
                            wsprFrequencyValue: frequencies,
                            cwBaseFrequencyHz,
                            cwOffsetHz: fsk_offset,
                        }));
                    }
                    if (typeof clearOfflineDefaults === "function") {
                        clearOfflineDefaults();
                    }
                } else if (window.currentPage == "view_logs.php") {
                    $("#callsign").val(callsign);
                } else if (window.currentPage == "view_spots.php") {
                    $("#callsign").val(callsign);
                }

                // Do actions after page loaded
                pageLoaded();

                // Run callback if provided
                if (typeof callback === "function") {
                    callback();
                }
            } catch (error) {
                if (isConfigView() &&
                    typeof suspendConfigAutosave === "function") {
                    suspendConfigAutosave(false);
                }
                debugConsole("error", "Error parsing config JSON:", error);
                backendCurrentlyConnected = false;
                syncConnectionAlert();
                setConfigLoadFailureState();
                if (isRuntimeControlView() && shouldShowBackendLossStatus() && typeof setOfflineDefaults === "function") {
                    setOfflineDefaults();
                }
                // Only try to load if the system is *not* paused
                if (!systemPaused) {
                    schedulePopulateConfigRetry(callback, 10000);
                }
            }
        })
        .fail(function (jqXHR, textStatus, errorThrown) {
            if (isConfigView() &&
                typeof suspendConfigAutosave === "function") {
                suspendConfigAutosave(false);
            }
            debugConsole(
                "error",
                "Error fetching config JSON:",
                textStatus,
                errorThrown
            );
            backendCurrentlyConnected = false;
            syncConnectionAlert();
            setConfigLoadFailureState();
            if (isRuntimeControlView() && shouldShowBackendLossStatus() && typeof setOfflineDefaults === "function") {
                setOfflineDefaults();
            }
            // Only try to load if the system is *not* paused
            if (!systemPaused) {
                schedulePopulateConfigRetry(callback, 10000);
            }
        })
        .always(function () {
            populateConfigRunning = false;
        });
}

function resetToolTips(e) {
    const el = e.currentTarget;
    const inst = bootstrap.Tooltip.getInstance(el);
    if (inst) inst.hide();
}

/**
 * Update the connection-status icon, visible text, and tooltip.
 *
 * @param {'disconnected'|'connecting'|'connected'|'transmitting'} state
 * @param {string} [timestamp]  Optional timestamp for “transmitting”
 */
function setConnectionState(state, timestamp = "") {
    const icon = document.getElementById("connIcon");
    const textElement = document.getElementById("connStatusText");
    if (!icon) return;

    // Remove old state classes
    icon.classList.remove(
        "state-disconnected",
        "state-connecting",
        "state-connected",
        "state-transmitting"
    );

    // Add the new one
    icon.classList.add(`state-${state}`);

    // Choose the tooltip text
    let text;
    switch (state) {
        case "disconnected":
            text = "Controller disconnected.";
            break;
        case "connecting":
            text = "Connecting to controller…";
            break;
        case "connected":
            text = "Controller connected.";
            break;
        case "transmitting":
            text = "Transmission in progress.";
            break;
        default:
            text = "";
    }

    if (textElement) {
        textElement.textContent = text.replace(/\.$/, "");
    }

    // Update Bootstrap’s tooltip data attr (do NOT set title)
    icon.setAttribute("data-bs-original-title", text);

    // Remove the native title so the browser never shows it
    icon.removeAttribute("title");

    // (Re)initialize or fetch the Tooltip instance, then update its content
    let inst = bootstrap.Tooltip.getInstance(icon);
    if (!inst) {
        inst = new bootstrap.Tooltip(icon, { trigger: "hover" });
    }
    inst.setContent({ ".tooltip-inner": text });
}

function normalizeRuntimeStatus(msg) {
    if (!msg || typeof msg !== "object") {
        return null;
    }

    const planType = typeof msg.plan_type === "string" ? msg.plan_type : "";
    const frameCount = Number.isFinite(Number(msg.frame_count))
        ? Number(msg.frame_count)
        : 0;
    const currentFrame = Number.isFinite(Number(msg.current_frame))
        ? Number(msg.current_frame)
        : 0;
    const cwActiveCharIndex = Number.isInteger(Number(msg.cw_active_char_index))
        ? Number(msg.cw_active_char_index)
        : -1;

    const normalizeCorrectionProvenance = (value) => {
        const source = value && typeof value === "object" ? value : {};
        return {
            available: source.available === true &&
                [source.selected_component_ppm, source.conducted_residual_ppm,
                 source.correction_ppm].every(value => typeof value === "number" && Number.isFinite(value)),
            active: source.active === true,
            processorProfile: typeof source.processor_profile === "string" ? source.processor_profile : "",
            selectedParent: typeof source.selected_parent === "string" ? source.selected_parent : "",
            nominalRateHz: Number.isFinite(Number(source.nominal_rate_hz)) ? Number(source.nominal_rate_hz) : 0,
            selectedComponentPpm: Number.isFinite(Number(source.selected_component_ppm)) ? Number(source.selected_component_ppm) : 0,
            conductedResidualPpm: Number.isFinite(Number(source.conducted_residual_ppm)) ? Number(source.conducted_residual_ppm) : 0,
            correctionPpm: Number.isFinite(Number(source.correction_ppm)) ? Number(source.correction_ppm) : 0,
            correctionMode: typeof source.correction_mode === "string" ? source.correction_mode : "",
            providerName: typeof source.provider_name === "string" ? source.provider_name : "",
            providerSourceSignature: typeof source.provider_source_signature === "string" ? source.provider_source_signature : "",
            providerSnapshotTime: typeof source.provider_snapshot_time === "string" ? source.provider_snapshot_time : "",
            executionIdentity: typeof source.execution_identity === "string" ? source.execution_identity : ""
        };
    };
    return {
        eventState: typeof msg.state === "string" ? msg.state : "",
        txState:
            typeof msg.tx_state === "string"
                ? msg.tx_state
                : (typeof msg.state === "string" ? msg.state : ""),
        runtimeMode:
            typeof msg.runtime_mode === "string" ? msg.runtime_mode : "",
        transmitBackend:
            typeof msg.transmit_backend === "string" ? msg.transmit_backend : "",
        nextTransmissionAt:
            typeof msg.next_transmission_at === "string"
                ? msg.next_transmission_at
                : "",
        frequencyHz: Number.isFinite(Number(msg.frequency_hz))
            ? Number(msg.frequency_hz)
            : 0,
        offsetHz: Number.isFinite(Number(msg.offset_hz))
            ? Number(msg.offset_hz)
            : 0,
        frequencyIsSkip: msg.frequency_is_skip === true,
        selectorGpioEnabled: msg.selector_gpio_enabled === true,
        selectorGpio: Number.isInteger(Number(msg.selector_gpio))
            ? Number(msg.selector_gpio)
            : -1,
        selectorGpioActiveHigh: msg.selector_gpio_active_high === true,
        planType,
        powerDbm: Number.isFinite(Number(msg.power_dbm))
            ? Number(msg.power_dbm)
            : 0,
        frameCount,
        currentFrame,
        callsignRaw: typeof msg.callsign_raw === "string" ? msg.callsign_raw : "",
        callsignNormalized:
            typeof msg.callsign_normalized === "string" ? msg.callsign_normalized : "",
        locatorRaw: typeof msg.locator_raw === "string" ? msg.locator_raw : "",
        locatorNormalized:
            typeof msg.locator_normalized === "string" ? msg.locator_normalized : "",
        frequencyEstimateQualification:
            typeof msg.frequency_estimate_qualification === "string"
                ? msg.frequency_estimate_qualification
                : "unavailable",
        frequencyEstimateProvider:
            typeof msg.frequency_estimate_provider === "string"
                ? msg.frequency_estimate_provider
                : "",
        frequencyEstimateProvenance:
            typeof msg.frequency_estimate_provenance === "string"
                ? msg.frequency_estimate_provenance
                : "",
        frequencyCorrectionMode:
            typeof msg.frequency_correction_mode === "string"
                ? msg.frequency_correction_mode
                : "uncalibrated",
        frequencyEstimateReason:
            typeof msg.frequency_estimate_reason === "string"
                ? msg.frequency_estimate_reason
                : "",
        frequencyEstimatePpm:
            msg.frequency_estimate_ppm !== null && Number.isFinite(Number(msg.frequency_estimate_ppm))
                ? Number(msg.frequency_estimate_ppm)
                : null,
        gpioFrequencyResidualPpm: Number.isFinite(Number(msg.gpio_frequency_residual_ppm))
            ? Number(msg.gpio_frequency_residual_ppm)
            : 0,
        effectiveGpioPpm: Number.isFinite(Number(msg.effective_gpio_ppm))
            ? Number(msg.effective_gpio_ppm)
            : 0,
        frequencyEstimateAgeSeconds: Number.isFinite(Number(msg.frequency_estimate_age_seconds))
            ? Number(msg.frequency_estimate_age_seconds)
            : 0,
        gpioCorrectionCandidate: normalizeCorrectionProvenance(msg.gpio_correction_candidate),
        gpioCorrectionCommitted: normalizeCorrectionProvenance(msg.gpio_correction_committed),
        frameCallsign: typeof msg.frame_callsign === "string" ? msg.frame_callsign : "",
        frameLocator: typeof msg.frame_locator === "string" ? msg.frame_locator : "",
        cwMessage: typeof msg.cw_message === "string" ? msg.cw_message : "",
        cwActiveCharIndex,
        timestamp: typeof msg.timestamp === "string" ? msg.timestamp : ""
    };
}

function parseOperationFrequencyWithOptionalUnits(rawValue) {
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

function parseConfiguredWsprFrequencyHz(rawValue) {
    const tokens = String(rawValue || "")
        .replace(/,/g, " ")
        .trim()
        .split(/\s+/)
        .filter((token) => token.length > 0);

    for (const token of tokens) {
        const baseToken = token.split("@", 1)[0].trim();
        if (!baseToken || baseToken === "0") {
            continue;
        }

        const numericFrequency = parseOperationFrequencyWithOptionalUnits(baseToken);
        if (Number.isFinite(numericFrequency) && numericFrequency > 0) {
            return numericFrequency;
        }

        const normalizedAlias = baseToken.toLowerCase();
        const canonicalAlias = normalizedAlias === "lf"
            ? "2200m"
            : normalizedAlias === "mf"
                ? "630m"
                : normalizedAlias;
        const aliasFrequency = wsprBandDialFrequenciesHz[canonicalAlias];
        if (Number.isFinite(aliasFrequency) && aliasFrequency > 0) {
            return aliasFrequency;
        }
    }

    return 0;
}

function validateWsprBandCatalog(response) {
    if (!response || typeof response !== "object" || Array.isArray(response) ||
        response.command !== "wspr_band_catalog" || response.status !== "ok") {
        return null;
    }

    const audioOffsetHz = response.audio_offset_hz;
    if (typeof audioOffsetHz !== "number" ||
        !Number.isSafeInteger(audioOffsetHz) || audioOffsetHz < 0) {
        return null;
    }

    if (!["existing_common", "wrc15"].includes(response.frequency_profile)) {
        return null;
    }

    if (!Array.isArray(response.bands) || !Array.isArray(response.presets) ||
        !response.band_preferences || typeof response.band_preferences !== "object" ||
        Array.isArray(response.band_preferences)) {
        return null;
    }

    const nextCatalog = Object.create(null);
    let previousBandIndex = -1;
    for (let index = 0; index < response.bands.length; index += 1) {
        const entry = response.bands[index];
        const bandIndex = EFFECTIVE_WSPR_BAND_NAMES.indexOf(entry?.band);
        if (!entry || typeof entry !== "object" || Array.isArray(entry) ||
            bandIndex <= previousBandIndex ||
            typeof entry.dial_frequency_hz !== "number" ||
            !Number.isSafeInteger(entry.dial_frequency_hz) || entry.dial_frequency_hz <= 0 ||
            typeof entry.tone_frequency_hz !== "number" ||
            !Number.isSafeInteger(entry.tone_frequency_hz) || entry.tone_frequency_hz <= 0 ||
            !["built_in_preset", "profile_preset", "band_preference_preset", "band_preference_numeric"].includes(entry.resolution_source) ||
            !(entry.preset === null || typeof entry.preset === "string")) {
            return null;
        }
        previousBandIndex = bandIndex;

        const expectedToneFrequencyHz = entry.dial_frequency_hz + audioOffsetHz;
        if (!Number.isSafeInteger(expectedToneFrequencyHz) ||
            entry.tone_frequency_hz !== expectedToneFrequencyHz) {
            return null;
        }

        nextCatalog[entry.band] = entry.dial_frequency_hz;
    }

    if (CANONICAL_WSPR_BAND_NAMES.some((band) => !Number.isSafeInteger(nextCatalog[band]))) {
        return null;
    }

    for (const preset of response.presets) {
        if (!preset || typeof preset !== "object" || Array.isArray(preset) ||
            typeof preset.preset !== "string" || !/^[a-z0-9.]+(?::[a-z0-9_]+)?$/.test(preset.preset) ||
            !CANONICAL_WSPR_BAND_NAMES.includes(preset.band) ||
            !Number.isSafeInteger(preset.dial_frequency_hz) || preset.dial_frequency_hz <= 0 ||
            typeof preset.existing_common !== "boolean") {
            return null;
        }
    }

    return {
        audioOffsetHz,
        dialFrequenciesHz: nextCatalog,
        bands: response.bands.map((entry) => ({ ...entry })),
        presets: response.presets.map((entry) => ({ ...entry })),
        bandPreferences: { ...response.band_preferences }
    };
}

function invalidTestToneSelection(error) {
    return Object.freeze({
        valid: false,
        error
    });
}

function createWsprBandTestToneSelection(band, catalog) {
    if (typeof band !== "string" || !EFFECTIVE_WSPR_BAND_NAMES.includes(band)) {
        return invalidTestToneSelection("Select a canonical WSPR band.");
    }
    if (!catalog || typeof catalog !== "object" ||
        !Number.isSafeInteger(catalog.audioOffsetHz) || catalog.audioOffsetHz < 0 ||
        !catalog.dialFrequenciesHz || typeof catalog.dialFrequenciesHz !== "object") {
        return invalidTestToneSelection("A validated WSPR band catalog is required.");
    }

    const dialFrequencyHz = catalog.dialFrequenciesHz[band];
    if (!Number.isSafeInteger(dialFrequencyHz) || dialFrequencyHz <= 0) {
        return invalidTestToneSelection("The selected WSPR band is unavailable in the catalog.");
    }

    const toneFrequencyHz = dialFrequencyHz + catalog.audioOffsetHz;
    if (!Number.isSafeInteger(toneFrequencyHz) || toneFrequencyHz <= 0) {
        return invalidTestToneSelection("The selected WSPR band produces an unsafe tone frequency.");
    }

    return Object.freeze({
        valid: true,
        mode: TEST_TONE_SELECTION_MODES.WSPR_BAND,
        band,
        dialFrequencyHz,
        audioOffsetHz: catalog.audioOffsetHz,
        toneFrequencyHz,
        payload: Object.freeze({
            command: "tone_start",
            frequency_source: TEST_TONE_SELECTION_MODES.WSPR_BAND,
            band
        })
    });
}

function createCustomRfTestToneSelection(rawFrequencyHz) {
    if (typeof rawFrequencyHz !== "string" || !/^[1-9]\d*$/.test(rawFrequencyHz)) {
        return invalidTestToneSelection("Enter a positive whole-number RF frequency in Hz.");
    }

    const frequencyHz = Number(rawFrequencyHz);
    if (!Number.isSafeInteger(frequencyHz) || frequencyHz <= 0) {
        return invalidTestToneSelection("The RF frequency must be a safe whole-number Hz value.");
    }

    return Object.freeze({
        valid: true,
        mode: TEST_TONE_SELECTION_MODES.CUSTOM_RF,
        frequencyHz,
        payload: Object.freeze({
            command: "tone_start",
            frequency_source: TEST_TONE_SELECTION_MODES.CUSTOM_RF,
            frequency_hz: frequencyHz
        })
    });
}

function createTestToneSelection(mode, value, catalog) {
    if (mode === TEST_TONE_SELECTION_MODES.WSPR_BAND) {
        return createWsprBandTestToneSelection(value, catalog);
    }
    if (mode === TEST_TONE_SELECTION_MODES.CUSTOM_RF) {
        return createCustomRfTestToneSelection(value);
    }
    return invalidTestToneSelection("Select a Test Tone frequency source.");
}

function createTestToneSelectionPreview(selection) {
    if (!selection || selection.valid !== true) {
        return Object.freeze({
            valid: false,
            text: selection && selection.error
                ? selection.error
                : "Select a valid Test Tone frequency source."
        });
    }

    if (selection.mode === TEST_TONE_SELECTION_MODES.WSPR_BAND) {
        return Object.freeze({
            valid: true,
            mode: selection.mode,
            band: selection.band,
            dialFrequencyHz: selection.dialFrequencyHz,
            audioOffsetHz: selection.audioOffsetHz,
            toneFrequencyHz: selection.toneFrequencyHz,
            text: `${selection.band}: WSPR dial ${selection.dialFrequencyHz} Hz + ${selection.audioOffsetHz} Hz offset = ${selection.toneFrequencyHz} Hz RF tone.`
        });
    }

    if (selection.mode === TEST_TONE_SELECTION_MODES.CUSTOM_RF) {
        return Object.freeze({
            valid: true,
            mode: selection.mode,
            frequencyHz: selection.frequencyHz,
            text: `${selection.frequencyHz} Hz exact RF. No WSPR offset is applied; the backend validates and resolves the band at start.`
        });
    }

    return Object.freeze({
        valid: false,
        text: "Select a valid Test Tone frequency source."
    });
}

function currentValidatedTestToneCatalog() {
    if (!wsprBandCatalogAuthorized) {
        return null;
    }

    return Object.freeze({
        audioOffsetHz: wsprAudioOffsetHz,
        dialFrequenciesHz: wsprBandDialFrequenciesHz
    });
}

function displayTestToneCatalog() {
    if (!Number.isSafeInteger(wsprAudioOffsetHz) || wsprAudioOffsetHz < 0 ||
        !wsprBandDialFrequenciesHz || typeof wsprBandDialFrequenciesHz !== "object") {
        return null;
    }

    return Object.freeze({
        audioOffsetHz: wsprAudioOffsetHz,
        dialFrequenciesHz: wsprBandDialFrequenciesHz
    });
}

function configuredWsprCatalogBand(rawValue, catalog) {
    if (!catalog || !catalog.dialFrequenciesHz) {
        return "";
    }

    const tokens = String(rawValue || "")
        .replace(/,/g, " ")
        .trim()
        .split(/\s+/)
        .filter((token) => token.length > 0);
    for (const token of tokens) {
        const baseToken = token.split("@", 1)[0].trim();
        const normalized = baseToken.toLowerCase();
        const canonical = normalized === "lf"
            ? "2200m"
            : normalized === "mf"
                ? "630m"
                : normalized;
        if (EFFECTIVE_WSPR_BAND_NAMES.includes(canonical) &&
            Number.isSafeInteger(catalog.dialFrequenciesHz[canonical])) {
            return canonical;
        }

        const numericFrequencyHz = parseOperationFrequencyWithOptionalUnits(baseToken);
        if (!Number.isSafeInteger(numericFrequencyHz) || numericFrequencyHz <= 0) {
            continue;
        }
        for (const band of EFFECTIVE_WSPR_BAND_NAMES) {
            if (catalog.dialFrequenciesHz[band] === numericFrequencyHz) {
                return band;
            }
        }
    }

    return "";
}

function populateTestToneBandOptions(catalog) {
    const select = document.getElementById("testToneBand");
    if (!select) {
        return;
    }

    const previousValue = select.value;
    select.replaceChildren();
    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Select a WSPR band";
    select.appendChild(placeholder);
    if (catalog && catalog.dialFrequenciesHz) {
        for (const band of EFFECTIVE_WSPR_BAND_NAMES) {
            if (!Number.isSafeInteger(catalog.dialFrequenciesHz[band])) {
                continue;
            }
            const option = document.createElement("option");
            option.value = band;
            option.textContent = band;
            select.appendChild(option);
        }
    }
    select.value = previousValue;
}

function selectedTestToneMode() {
    if (document.getElementById("testToneSourceBand")?.checked) {
        return TEST_TONE_SELECTION_MODES.WSPR_BAND;
    }
    if (document.getElementById("testToneSourceCustom")?.checked) {
        return TEST_TONE_SELECTION_MODES.CUSTOM_RF;
    }
    return "";
}

function setSelectedTestToneMode(mode) {
    const bandRadio = document.getElementById("testToneSourceBand");
    const customRadio = document.getElementById("testToneSourceCustom");
    if (bandRadio) {
        bandRadio.checked = mode === TEST_TONE_SELECTION_MODES.WSPR_BAND;
    }
    if (customRadio) {
        customRadio.checked = mode === TEST_TONE_SELECTION_MODES.CUSTOM_RF;
    }
}

function renderTestToneSelection() {
    const catalog = currentValidatedTestToneCatalog();
    const mode = selectedTestToneMode();
    const band = document.getElementById("testToneBand");
    const frequency = document.getElementById("testToneFrequencyHz");
    const previewNode = document.getElementById("testToneSelectionPreview");
    const errorNode = document.getElementById("testToneSelectionError");

    if (band) {
        band.disabled = mode !== TEST_TONE_SELECTION_MODES.WSPR_BAND || !catalog;
    }
    if (frequency) {
        frequency.disabled = mode !== TEST_TONE_SELECTION_MODES.CUSTOM_RF;
    }

    currentTestToneSelection = createTestToneSelection(
        mode,
        mode === TEST_TONE_SELECTION_MODES.WSPR_BAND ? band?.value : frequency?.value,
        catalog
    );
    const preview = createTestToneSelectionPreview(currentTestToneSelection);
    if (previewNode) {
        previewNode.textContent = preview.valid ? preview.text : "";
    }
    if (errorNode) {
        errorNode.textContent = currentTestToneSelection.valid
            ? ""
            : (!catalog ? "" : currentTestToneSelection.error);
    }
    syncTestToneControlState(isTestToneRuntimeActive());
    return currentTestToneSelection;
}

function initializeTestToneSelectionControls() {
    const catalog = currentValidatedTestToneCatalog();
    populateTestToneBandOptions(catalog || displayTestToneCatalog());
    if (!catalog) {
        setSelectedTestToneMode("");
        return renderTestToneSelection();
    }

    const configuredBand = currentTestToneConfigContext.mode === "WSPR"
        ? configuredWsprCatalogBand(currentTestToneConfigContext.wsprFrequencyValue, catalog)
        : "";
    const band = document.getElementById("testToneBand");
    const frequency = document.getElementById("testToneFrequencyHz");
    if (configuredBand) {
        setSelectedTestToneMode(TEST_TONE_SELECTION_MODES.WSPR_BAND);
        if (band) {
            band.value = configuredBand;
        }
    } else {
        const configuredFrequencyHz = testToneDefaultTransmitFrequencyHz();
        if (Number.isSafeInteger(configuredFrequencyHz) && configuredFrequencyHz > 0) {
            setSelectedTestToneMode(TEST_TONE_SELECTION_MODES.CUSTOM_RF);
            if (frequency) {
                frequency.value = String(configuredFrequencyHz);
            }
        } else {
            setSelectedTestToneMode("");
        }
    }
    return renderTestToneSelection();
}

function updateWsprBandCatalog(response) {
    const validatedCatalog = validateWsprBandCatalog(response);
    if (!validatedCatalog) {
        debugConsole("warn", "WSPR band catalog response was invalid:", response);
        return false;
    }

    wsprBandDialFrequenciesHz = validatedCatalog.dialFrequenciesHz;
    wsprAudioOffsetHz = validatedCatalog.audioOffsetHz;
    if (typeof updateBandPreferenceCatalog === "function") {
        updateBandPreferenceCatalog(validatedCatalog);
    }
    updateTestToneConfigContext(
        currentTestToneConfigContext.mode,
        currentTestToneConfigContext.wsprFrequencyValue,
        currentTestToneConfigContext.cwBaseFrequencyHz
    );
    updateTestToneFrequencyContext();
    updateTestToneFrequencyInputDefault();
    return true;
}

function updateWsprBandCatalogUiState() {
    updateTestToneFrequencyContext();
    const modal = document.getElementById("testToneModal");
    if (modal?.classList?.contains("show")) {
        initializeTestToneSelectionControls();
        return;
    }
    syncTestToneControlState(isTestToneRuntimeActive());
}

function clearWsprBandCatalogPendingRequest(request = wsprBandCatalogPendingRequest) {
    if (!request || wsprBandCatalogPendingRequest !== request) {
        return false;
    }

    window.clearTimeout(request.timeoutHandle);
    wsprBandCatalogPendingRequest = null;
    return true;
}

function invalidateWsprBandCatalogAuthorization(
    message,
    request = wsprBandCatalogPendingRequest
) {
    clearWsprBandCatalogPendingRequest(request);
    wsprBandCatalogAuthorized = false;
    wsprBandCatalogStatusMessage = message;
    updateWsprBandCatalogUiState();
}

function isCurrentWsprBandCatalogRequest(request, socket) {
    return !!request &&
        wsprBandCatalogPendingRequest === request &&
        request.socket === socket &&
        ws === socket &&
        request.connectionGeneration === wsprBandCatalogConnectionGeneration &&
        socket.readyState === WebSocket.OPEN;
}

function requestWsprBandCatalog(socket = ws) {
    if (!socket || socket.readyState !== WebSocket.OPEN ||
        wsprBandCatalogRequestedSockets.has(socket)) {
        debugConsole("debug", "WSPR band catalog request is already settled for this connection.");
        return false;
    }

    wsprBandCatalogConnectionGeneration += 1;
    wsprBandCatalogRequestGeneration += 1;
    wsprBandCatalogRequestedSockets.add(socket);

    let request;
    const timeoutHandle = window.setTimeout(() => {
        if (!isCurrentWsprBandCatalogRequest(request, socket)) {
            return;
        }
        invalidateWsprBandCatalogAuthorization(
            "WSPR band catalog request timed out. Test Tone Start is disabled.",
            request
        );
    }, WSPR_BAND_CATALOG_TIMEOUT_MS);
    request = Object.freeze({
        socket,
        connectionGeneration: wsprBandCatalogConnectionGeneration,
        requestGeneration: wsprBandCatalogRequestGeneration,
        timeoutHandle
    });
    wsprBandCatalogPendingRequest = request;
    wsprBandCatalogAuthorized = false;
    wsprBandCatalogStatusMessage = "Loading WSPR band catalog. Test Tone Start is disabled.";
    updateWsprBandCatalogUiState();

    try {
        socket.send(JSON.stringify({ command: "wspr_band_catalog" }));
    } catch (error) {
        debugConsole("warn", "Unable to request WSPR band catalog:", error);
        invalidateWsprBandCatalogAuthorization(
            "WSPR band catalog request could not be sent. Test Tone Start is disabled.",
            request
        );
        return false;
    }
    return request;
}

function handleWsprBandCatalogResponse(response, socket, request) {
    if (!isCurrentWsprBandCatalogRequest(request, socket)) {
        debugConsole("debug", "Ignoring stale or duplicate WSPR band catalog response.");
        return false;
    }

    if (!updateWsprBandCatalog(response)) {
        invalidateWsprBandCatalogAuthorization(
            response && response.status !== "ok"
                ? "WSPR band catalog is unavailable from the controller. Test Tone Start is disabled."
                : "WSPR band catalog response is invalid. Test Tone Start is disabled.",
            request
        );
        return false;
    }

    clearWsprBandCatalogPendingRequest(request);
    wsprBandCatalogAuthorized = true;
    wsprBandCatalogStatusMessage = "";
    updateWsprBandCatalogUiState();
    return true;
}

function normalizeTestToneMode(mode) {
    return ["WSPR", "QRSS", "FSKCW", "DFCW"].includes(mode) ? mode : "";
}

function configuredTestToneFrequencyForMode(mode, wsprFrequencyHz, cwBaseFrequencyHz) {
    const normalizedMode = normalizeTestToneMode(mode);
    const frequencyHz = normalizedMode === "WSPR"
        ? Number(wsprFrequencyHz)
        : Number(cwBaseFrequencyHz);

    return Number.isFinite(frequencyHz) && frequencyHz > 0 ? frequencyHz : 0;
}

function updateTestToneConfigContext(mode, wsprFrequencyValue, cwBaseFrequencyHz) {
    const wsprFrequencyHz = parseConfiguredWsprFrequencyHz(wsprFrequencyValue);
    currentTestToneConfigContext = {
        mode: normalizeTestToneMode(mode),
        configuredFrequencyHz: configuredTestToneFrequencyForMode(
            mode,
            wsprFrequencyHz,
            cwBaseFrequencyHz
        ),
        wsprFrequencyValue: String(wsprFrequencyValue || ""),
        cwBaseFrequencyHz: Number.isFinite(Number(cwBaseFrequencyHz))
            ? Number(cwBaseFrequencyHz)
            : 0
    };
}

function testToneDefaultTransmitFrequencyHz() {
    const configuredFrequencyHz = Number(
        currentTestToneConfigContext.configuredFrequencyHz
    );
    if (!Number.isFinite(configuredFrequencyHz) || configuredFrequencyHz <= 0) {
        return 0;
    }

    const transmitFrequencyHz = currentTestToneConfigContext.mode === "WSPR"
        ? configuredFrequencyHz + wsprAudioOffsetHz
        : configuredFrequencyHz;

    return Number.isFinite(transmitFrequencyHz) && transmitFrequencyHz > 0
        ? Math.round(transmitFrequencyHz)
        : 0;
}

function updateTestToneFrequencyInputDefault() {
    const input = document.getElementById("testToneFrequencyHz");
    if (!input) {
        return;
    }

    const frequencyHz = testToneDefaultTransmitFrequencyHz();
    input.value = frequencyHz > 0 ? String(frequencyHz) : "";
}

function formatTestToneFrequencyMhz(frequencyHz) {
    const numericFrequencyHz = Number(frequencyHz);
    if (!Number.isFinite(numericFrequencyHz) || numericFrequencyHz <= 0) {
        return "";
    }

    const fixedMhz = (numericFrequencyHz / 1e6).toFixed(6);
    const parts = fixedMhz.split(".");
    const whole = parts[0];
    return `${whole}.${parts[1]} MHz`;
}

function testToneFrequencyContextText() {
    if (!wsprBandCatalogAuthorized) {
        return wsprBandCatalogStatusMessage ||
            "WSPR band catalog unavailable. Test Tone Start is disabled.";
    }

    const configured = formatTestToneFrequencyMhz(
        currentTestToneConfigContext.configuredFrequencyHz
    );
    if (!configured) {
        return "Configured frequency: unavailable.";
    }

    if (currentTestToneConfigContext.mode === "WSPR") {
        const detected = formatTestToneFrequencyMhz(
            currentTestToneConfigContext.configuredFrequencyHz + wsprAudioOffsetHz
        );
        if (detected) {
            return `Configured frequency: ${configured}. WSPR uses USB dial-frequency semantics, so the test tone should be detected ${wsprAudioOffsetHz} Hz higher at ${detected}.`;
        }
    }

    return `Configured frequency: ${configured}.`;
}

function updateTestToneFrequencyContext() {
    const node = document.getElementById("testToneFrequencyContext");
    if (!node) {
        return;
    }

    node.textContent = testToneFrequencyContextText();
}

function testToneFrequencyOverridePayload() {
    if (!currentTestToneSelection || currentTestToneSelection.valid !== true ||
        !currentTestToneSelection.payload) {
        return {};
    }

    const payload = currentTestToneSelection.payload;
    if (payload.frequency_source === TEST_TONE_SELECTION_MODES.WSPR_BAND &&
        typeof payload.band === "string") {
        return {
            frequency_source: TEST_TONE_SELECTION_MODES.WSPR_BAND,
            band: payload.band
        };
    }
    if (payload.frequency_source === TEST_TONE_SELECTION_MODES.CUSTOM_RF &&
        Number.isSafeInteger(payload.frequency_hz) && payload.frequency_hz > 0) {
        return {
            frequency_source: TEST_TONE_SELECTION_MODES.CUSTOM_RF,
            frequency_hz: payload.frequency_hz
        };
    }
    return {};
}

function setTestToneExecutionResult(message, state = "") {
    const node = document.getElementById("testToneExecutionResult");
    if (!node) {
        return;
    }

    node.textContent = typeof message === "string" ? message : "";
    node.className = state
        ? `small mb-0 mt-2 text-${state}`
        : "small mb-0 mt-2";
}

function clearTestToneExecutionResult() {
    setTestToneExecutionResult("");
}

function validCommittedToneFrequency(value) {
    return typeof value === "number" && Number.isSafeInteger(value) && value > 0;
}

function committedTestToneSelectorText(response) {
    if (response.selector_gpio_enabled === false) {
        return { valid: true, text: "Selector: disabled." };
    }
    if (response.selector_gpio_enabled !== true ||
        !Number.isSafeInteger(response.selector_gpio) || response.selector_gpio < 0 ||
        typeof response.selector_gpio_active_high !== "boolean") {
        return { valid: false };
    }

    return {
        valid: true,
        text: `Selector: GPIO ${response.selector_gpio}, ${response.selector_gpio_active_high ? "active high" : "active low"}.`
    };
}

function committedTestToneExecutionText(
    response,
    expectedFrequencySource = "",
    expectedRequest = null
) {
    if (!response || typeof response !== "object" || response.started !== true ||
        !Object.values(TEST_TONE_SELECTION_MODES).includes(expectedFrequencySource)) {
        return { applicable: false, valid: false };
    }

    if (response.frequency_source !== expectedFrequencySource) {
        return { applicable: true, valid: false };
    }

    const selector = committedTestToneSelectorText(response);
    if (expectedFrequencySource === TEST_TONE_SELECTION_MODES.WSPR_BAND) {
        const expectedRf = response.dial_frequency_hz + response.audio_offset_hz;
        if (!EFFECTIVE_WSPR_BAND_NAMES.includes(response.band) ||
            !validCommittedToneFrequency(response.dial_frequency_hz) ||
            !Number.isSafeInteger(response.audio_offset_hz) || response.audio_offset_hz < 0 ||
            !validCommittedToneFrequency(response.actual_rf_frequency_hz) ||
            !Number.isSafeInteger(expectedRf) || expectedRf !== response.actual_rf_frequency_hz ||
            !selector.valid) {
            return { applicable: true, valid: false };
        }
        const requestedBand = expectedRequest?.band;
        const requestedDial = requestedBand
            ? wsprBandDialFrequenciesHz[requestedBand]
            : 0;
        const requestedRf = requestedDial + wsprAudioOffsetHz;
        const matchedRequest = requestedBand === response.band &&
            requestedDial === response.dial_frequency_hz &&
            wsprAudioOffsetHz === response.audio_offset_hz &&
            requestedRf === response.actual_rf_frequency_hz;
        return {
            applicable: true,
            valid: true,
            text: matchedRequest
                ? `Test Tone started at the requested ${formatTestToneFrequencyMhz(response.actual_rf_frequency_hz)} RF. ${selector.text}`
                : `Test Tone started on ${response.band} at committed ${formatTestToneFrequencyMhz(response.actual_rf_frequency_hz)} RF (requested values differed). ${selector.text}`
        };
    }

    if (expectedFrequencySource === TEST_TONE_SELECTION_MODES.CUSTOM_RF) {
        if (!EFFECTIVE_WSPR_BAND_NAMES.includes(response.band) ||
            !validCommittedToneFrequency(response.actual_rf_frequency_hz) || !selector.valid) {
            return { applicable: true, valid: false };
        }
        const matchedRequest = expectedRequest?.frequency_hz ===
            response.actual_rf_frequency_hz;
        return {
            applicable: true,
            valid: true,
            text: matchedRequest
                ? `Test Tone started at the requested exact ${formatTestToneFrequencyMhz(response.actual_rf_frequency_hz)} RF on ${response.band}. No WSPR offset was applied. ${selector.text}`
                : `Test Tone started on ${response.band} at committed exact ${formatTestToneFrequencyMhz(response.actual_rf_frequency_hz)} RF (different from the request). No WSPR offset was applied. ${selector.text}`
        };
    }

    return { applicable: false, valid: false };
}

function renderRuntimeStatus(status) {
    renderRuntimeControlStatus();
}

function renderGpioFrequencyCorrection(status) {
    const panelNode = document.getElementById("gpio_frequency_correction_panel");
    const valueNode = document.getElementById("gpio_frequency_correction_value");
    const detailNode = document.getElementById("gpio_frequency_correction_detail");
    if (!panelNode || !valueNode || !detailNode || !status) {
        return;
    }
    panelNode.hidden = ["si5351", "wtp"].includes(status.transmitBackend);
    if (panelNode.hidden) {
        return;
    }

    const formatProvenance = (label, value) => {
        if (!value?.available) {
            return `${label}: unavailable`;
        }
        const usesEstimate = ["qualified_estimate_plus_residual", "stale_estimate_plus_residual"].includes(value.correctionMode);
        const source = usesEstimate
            ? value.providerSourceSignature || value.providerName || "estimate"
            : value.correctionMode === "fixed_manual" ? "manual" : "no correction";
        const identity = value.executionIdentity ? ` · ${value.executionIdentity}` : "";
        const active = value.active ? " · active" : "";
        const residual = !usesEstimate
            ? `${value.conductedResidualPpm.toFixed(3)} residual configured, not applied`
            : `${value.conductedResidualPpm.toFixed(3)} residual`;
        const snapshot = usesEstimate && value.providerSnapshotTime ? ` · sampled ${value.providerSnapshotTime}` : "";
        return `${label}: ${value.correctionPpm.toFixed(3)} PPM correction · ` +
            `${value.selectedComponentPpm.toFixed(3)} selected · ${residual} · ` +
            `${source}${snapshot}${identity}${active}`;
    };

    valueNode.textContent = formatProvenance(
        "Candidate", status.gpioCorrectionCandidate);
    detailNode.textContent =
        `${formatProvenance("Committed", status.gpioCorrectionCommitted)}. ` +
        "Candidate refreshes do not alter a committed transmission. " +
        `Estimate: ${status.frequencyEstimateQualification}` +
        (status.frequencyEstimateReason ? ` (${status.frequencyEstimateReason})` : "") +
        ` · age ${status.frequencyEstimateAgeSeconds.toFixed(0)} s.`;
}

function applyRuntimeStatus(msg) {
    const status = normalizeRuntimeStatus(msg);
    if (!status) {
        return;
    }

    currentRuntimeStatus = status;
    renderGpioFrequencyCorrection(status);
    const selectedMode =
        typeof selectedConfigMode === "function" ? selectedConfigMode() : "";
    if (status.runtimeMode && !selectedMode) {
        currentRuntimeConfigStatus.mode = status.runtimeMode;
    }
    renderRuntimeStatus(currentRuntimeStatus);
    if (typeof handleRuntimeStatusUpdate === "function") {
        handleRuntimeStatusUpdate(currentRuntimeStatus);
    }
}

function queueRuntimeStatusRefresh(delayMs = 100) {
    if (runtimeStatusRefreshTimer !== null) {
        window.clearTimeout(runtimeStatusRefreshTimer);
    }

    runtimeStatusRefreshTimer = window.setTimeout(() => {
        runtimeStatusRefreshTimer = null;
        getTxState();
    }, delayMs);
}

function updateRuntimeControlConfigStatus(mode, transmitEnabled) {
    if (typeof mode === "string" && mode) {
        currentRuntimeConfigStatus.mode = mode;
    }

    if (transmitEnabled !== undefined && transmitEnabled !== null) {
        currentRuntimeConfigStatus.transmitEnabled = !!transmitEnabled;
    }

    renderRuntimeControlStatus();
}

function renderRuntimeControlStatus() {
    const modeNode = document.getElementById("runtime_mode_value");
    const frequencyNode = document.getElementById("runtime_frequency_value");
    const planLabelNode = document.getElementById("runtime_plan_label");
    const planNode = document.getElementById("runtime_wspr_plan_value");

    if (typeof syncStopButtonState === "function") {
        syncStopButtonState();
    }

    if (modeNode) {
        modeNode.textContent = currentRuntimeConfigStatus.mode || "Unknown";
    }

    if (!planNode) {
        return;
    }

    const selectedMode =
        typeof selectedConfigMode === "function" ? selectedConfigMode() : "";
    const currentMode =
        selectedMode ||
        currentRuntimeConfigStatus.mode ||
        (currentRuntimeStatus && currentRuntimeStatus.runtimeMode) ||
        "";

    renderRuntimeFrequencyPane(frequencyNode, currentMode, currentRuntimeStatus);

    if (currentMode === "QRSS" || currentMode === "FSKCW" || currentMode === "DFCW") {
        const configuredMessage = document.getElementById("qrss_message");
        const fallbackMessage =
            configuredMessage && typeof configuredMessage.value === "string"
                ? configuredMessage.value.trim()
                : "";
        const message =
            currentRuntimeStatus && currentRuntimeStatus.cwMessage
                ? currentRuntimeStatus.cwMessage
                : fallbackMessage;
        const nextTransmissionAt =
            currentRuntimeStatus && currentRuntimeStatus.nextTransmissionAt
                ? currentRuntimeStatus.nextTransmissionAt
                : "";
        const transmitEnabled =
            currentRuntimeConfigStatus &&
            currentRuntimeConfigStatus.transmitEnabled === true;
        const isTransmitting =
            currentRuntimeStatus &&
            currentRuntimeStatus.txState === "transmitting";
        const activeCharIndex =
            isTransmitting &&
                Number.isInteger(currentRuntimeStatus.cwActiveCharIndex)
                ? currentRuntimeStatus.cwActiveCharIndex
                : -1;

        if (isTransmitting) {
            if (planLabelNode) {
                planLabelNode.textContent = "Message progression";
            }
            renderCwRuntimeMessage(planNode, message, activeCharIndex);
            planNode.setAttribute("title", message || "No CW message configured.");
        } else {
            if (planLabelNode) {
                planLabelNode.textContent = "Next message at:";
            }
            const idleValue = transmitEnabled
                ? (nextTransmissionAt || "Not scheduled")
                : "Disabled";
            planNode.textContent = idleValue;
            planNode.setAttribute("title", idleValue);
        }
        return;
    }

    if (planLabelNode) {
        planLabelNode.textContent = "Current WSPR plan";
    }

    if (
        currentMode !== "WSPR" ||
        !currentRuntimeStatus ||
        !currentRuntimeStatus.planType
    ) {
        planNode.textContent = "Not available";
        planNode.setAttribute("title", "");
        return;
    }

    let summary = currentRuntimeStatus.planType;
    if (currentRuntimeStatus.frameCount > 1 && currentRuntimeStatus.currentFrame > 0) {
        summary += ` F${currentRuntimeStatus.currentFrame}/${currentRuntimeStatus.frameCount}`;
    }

    const frameIdentity = [
        currentRuntimeStatus.frameCallsign,
        currentRuntimeStatus.frameLocator
    ]
        .filter(Boolean)
        .join(" ");
    if (frameIdentity) {
        summary += ` ${frameIdentity}`;
    }
    if (Number.isFinite(currentRuntimeStatus.powerDbm) && currentRuntimeStatus.powerDbm > 0) {
        summary += ` ${currentRuntimeStatus.powerDbm}dBm`;
    }

    const overallIdentity = [
        currentRuntimeStatus.callsignNormalized,
        currentRuntimeStatus.locatorNormalized
    ]
        .filter(Boolean)
        .join(" ");
    const titleParts = [summary];
    if (overallIdentity) {
        titleParts.push(`Overall: ${overallIdentity}`);
    }
    if (
        currentRuntimeStatus.callsignRaw &&
        currentRuntimeStatus.locatorRaw &&
        (currentRuntimeStatus.callsignRaw !== currentRuntimeStatus.callsignNormalized ||
            currentRuntimeStatus.locatorRaw !== currentRuntimeStatus.locatorNormalized)
    ) {
        titleParts.push(`Raw: ${currentRuntimeStatus.callsignRaw} ${currentRuntimeStatus.locatorRaw}`);
    }
    if (currentRuntimeStatus.timestamp) {
        titleParts.push(`Updated: ${currentRuntimeStatus.timestamp}`);
    }

    planNode.textContent = summary;
    planNode.setAttribute("title", titleParts.join(" | "));
}

function renderRuntimeFrequencyPane(node, currentMode, status) {
    if (!node) {
        return;
    }

    const primaryLabelNode = document.getElementById("runtime_frequency_primary_label");
    const secondaryLabelNode = document.getElementById("runtime_frequency_secondary_label");
    const items = buildRuntimeFrequencyItems(currentMode, status);
    node.replaceChildren();
    node.classList.remove("operation-panel__stack--split");

    if (primaryLabelNode) {
        primaryLabelNode.textContent = runtimeFrequencyPrimaryLabel(currentMode, status);
    }

    if (secondaryLabelNode) {
        secondaryLabelNode.hidden = true;
        secondaryLabelNode.textContent = "";
    }

    if (!items.length) {
        const fallbackNode = document.createElement("div");
        fallbackNode.className = "operation-panel__value";
        fallbackNode.textContent = "Not available";
        node.appendChild(fallbackNode);
        return;
    }

    if (items.length > 1) {
        node.classList.add("operation-panel__stack--split");
        if (secondaryLabelNode) {
            secondaryLabelNode.hidden = false;
            secondaryLabelNode.textContent = "Offset";
        }
    }

    items.forEach((item) => {
        const itemNode = document.createElement("div");
        itemNode.className = "operation-panel__item";

        const valueNode = document.createElement("div");
        valueNode.className = "operation-panel__item-value";
        valueNode.textContent = item.value;

        itemNode.append(valueNode);
        node.appendChild(itemNode);
    });
}

function runtimeFrequencyPrimaryLabel(currentMode, status) {
    const normalizedMode = typeof currentMode === "string" ? currentMode : "";
    if (normalizedMode !== "WSPR") {
        return "Frequency";
    }

    const txState = status && typeof status.txState === "string"
        ? status.txState
        : "";
    const eventState = status && typeof status.eventState === "string"
        ? status.eventState
        : "";

    if (
        txState === "transmitting" ||
        eventState === "starting" ||
        (status && status.frequencyIsSkip === true && eventState === "skipped")
    ) {
        return "Frequency";
    }

    return "Next Frequency";
}

function buildRuntimeFrequencyItems(currentMode, status) {
    const normalizedMode = typeof currentMode === "string" ? currentMode : "";
    const fallback = typeof getOperationFrequencyFallback === "function"
        ? getOperationFrequencyFallback(normalizedMode)
        : { frequencyHz: 0, offsetHz: 0 };
    const isSkipWindow = status && status.frequencyIsSkip === true;
    const frequencyValue = formatDisplayFrequencyWithSelector(
        isSkipWindow
            ? 0
            : (status && Number.isFinite(status.frequencyHz) && status.frequencyHz > 0
                ? status.frequencyHz
                : fallback.frequencyHz),
        status,
        { skipWindow: isSkipWindow }
    );
    const offsetValue = formatDisplayFrequency(
        status && Number.isFinite(status.offsetHz) && status.offsetHz > 0
            ? status.offsetHz
            : fallback.offsetHz,
        { forceUnit: "Hz" }
    );

    if (normalizedMode === "WSPR") {
        return frequencyValue === "Not available"
            ? []
            : [{ label: "", value: frequencyValue }];
    }
    if (normalizedMode === "QRSS") {
        return frequencyValue === "Not available"
            ? []
            : [{ label: "", value: frequencyValue }];
    }
    if (normalizedMode === "FSKCW" || normalizedMode === "DFCW") {
        return [
            { label: "", value: frequencyValue },
            { label: "", value: offsetValue }
        ];
    }

    return [];
}

function formatDisplayFrequency(valueHz, options = {}) {
    if (options.skipWindow === true) {
        return "(Skip)";
    }

    if (!Number.isFinite(valueHz) || valueHz <= 0) {
        return "Not available";
    }

    const forcedUnit = typeof options.forceUnit === "string"
        ? options.forceUnit
        : "";
    const units = [
        { suffix: "Hz", divisor: 1 },
        { suffix: "kHz", divisor: 1e3 },
        { suffix: "MHz", divisor: 1e6 },
        { suffix: "GHz", divisor: 1e9 },
        { suffix: "THz", divisor: 1e12 }
    ];

    let selectedUnit = units[0];
    if (forcedUnit) {
        const matchedUnit = units.find((unit) => unit.suffix === forcedUnit);
        if (matchedUnit) {
            selectedUnit = matchedUnit;
        }
    } else {
        for (const unit of units) {
            if (valueHz / unit.divisor < 1000) {
                selectedUnit = unit;
                break;
            }
            selectedUnit = unit;
        }
    }

    const value = valueHz / selectedUnit.divisor;
    const formatted = value.toLocaleString(undefined, {
        minimumFractionDigits: 0,
        maximumFractionDigits: selectedUnit.suffix === "Hz" ? 4 : 6,
        useGrouping: false
    });
    return `${formatted} ${selectedUnit.suffix}`;
}

function formatSelectorGpioSuffix(status) {
    if (
        !status ||
        status.selectorGpioEnabled !== true ||
        !Number.isInteger(status.selectorGpio) ||
        status.selectorGpio < 0
    ) {
        return "";
    }

    return ` @ GPIO${status.selectorGpio}${status.selectorGpioActiveHigh ? "H" : "L"}`;
}

function formatDisplayFrequencyWithSelector(valueHz, status, options = {}) {
    const frequency = formatDisplayFrequency(valueHz, options);
    if (frequency === "Not available" || frequency === "(Skip)") {
        return frequency;
    }

    return `${frequency}${formatSelectorGpioSuffix(status)}`;
}

function renderCwRuntimeMessage(node, message, activeCharIndex) {
    node.replaceChildren();

    const container = document.createElement("div");
    container.className = "config-runtime-cw-message";

    if (!message) {
        const emptyNode = document.createElement("div");
        emptyNode.className = "config-runtime-cw-message__empty";
        emptyNode.textContent = "No CW message configured.";
        container.appendChild(emptyNode);
        node.appendChild(container);
        return;
    }

    const messageNode = document.createElement("div");
    messageNode.className = "config-runtime-cw-message__text";

    Array.from(message).forEach((character, index) => {
        const charNode = document.createElement("span");
        charNode.className = "config-runtime-cw-message__char";
        const isActive = index === activeCharIndex;
        if (isActive) {
            charNode.classList.add("is-active");
        }
        charNode.textContent = character;
        messageNode.appendChild(charNode);
    });

    container.appendChild(messageNode);
    node.appendChild(container);
}

function getCallerLocation() {
    try {
        const err = new Error();
        const stack = err.stack?.split('\n');

        if (!stack || stack.length < 3) return '';

        // stack[0] = "Error"
        // stack[1] = this function (getCallerLocation)
        // stack[2] = debugConsole
        // stack[3] = actual caller
        const callerLine = stack[3] || stack[2];

        // Chrome format: "    at func (file:line:col)"
        // Firefox: "func@file:line:col"
        const match = callerLine.match(/(?:at\s+)?(.*?)(?:\s+\(|@)(.*):(\d+):\d+\)?/);

        if (!match) return '';

        const func = match[1];
        const file = match[2].split('/').pop();
        const line = match[3];

        return `${func} ${file}:${line}`;
    } catch {
        return '';
    }
}

const CALLER_LOCATION_WIDTH = 36;

const LOG_LEVEL_STYLES = {
    debug: "color: #1f6feb; font-weight: 600;",
    log: "color: #57606a; font-weight: 600;",
    warn: "color: #9a6700; font-weight: 600;",
    error: "color: #cf222e; font-weight: 700;"
};

function normalizeCallerLocation(location) {
    const normalized = typeof location === "string" ? location.trim() : "";

    if (!normalized) {
        return "";
    }

    const lowered = normalized.toLowerCase();
    if (
        lowered === "<anonymous>" ||
        lowered.includes("debugger eval code:") ||
        lowered.includes("eval code:") ||
        lowered.includes("eval at ")
    ) {
        return "";
    }

    return normalized;
}

function formatCallerLocation(location, width = CALLER_LOCATION_WIDTH) {
    const normalized = normalizeCallerLocation(location);

    if (!normalized) {
        return "";
    }

    if (normalized.length > width) {
        if (width <= 3) {
            return normalized.slice(-width);
        }

        return "..." + normalized.slice(-(width - 3));
    }
    return normalized.padEnd(width, " ");
}

/**
 * Logs at the specified level, if it meets or exceeds the
 * configured CONSOLE_LOG_LEVEL.
 *
 * @param {'debug'|'log'|'warn'|'error'} method
 *   The console method to invoke.
 * @param  {...any} args
 *   The message (or messages) to log.
 */
function debugConsole(method, ...args) {
    const levels = ["debug", "log", "warn", "error"];
    const threshold = String(CONSOLE_LOG_LEVEL || "debug").toLowerCase();
    const thresholdIndex = levels.indexOf(threshold);
    const currentLevelIndex = thresholdIndex >= 0 ? thresholdIndex : 0;
    const m = String(method).toLowerCase();
    const methodIndex = levels.indexOf(m);
    const validMethod = methodIndex >= 0 ? m : "log";

    if (methodIndex < currentLevelIndex) {
        return;
    }

    const tags = {
        debug: "[DEBUG]",
        log: "[LOG  ]",
        warn: "[WARN ]",
        error: "[ERROR]"
    };

    const tag = tags[validMethod];
    const consoleMethod =
        typeof console.log === 'function'
            ? console.log.bind(console)
            : null;

    if (!consoleMethod) {
        return;
    }

    const includeLocation =
        validMethod === "debug" ||
        validMethod === "warn" ||
        validMethod === "error";
    let location = "";

    if (includeLocation) {
        try {
            location = normalizeCallerLocation(getCallerLocation());
        } catch {
            location = "";
        }
    }

    const callerField = formatCallerLocation(location);
    const prefix = callerField ? `${callerField} ` : "";
    const style = LOG_LEVEL_STYLES[validMethod] || "";
    const normalizedArgs = args.map((arg) => {
        if (typeof arg === "string") {
            return arg;
        }

        if (arg instanceof Error) {
            return arg.stack || arg.message || String(arg);
        }

        if (arg === null || arg === undefined) {
            return String(arg);
        }

        if (typeof arg === "object") {
            try {
                return JSON.stringify(arg);
            } catch {
                return Object.prototype.toString.call(arg);
            }
        }

        return String(arg);
    });

    try {
        consoleMethod(`%c${tag}%c ${prefix}`, style, "", ...normalizedArgs);
    } catch {
        const fallbackArgs =
            prefix ? [tag, callerField, ...normalizedArgs] : [tag, ...normalizedArgs];
        consoleMethod(...fallbackArgs);
    }
}

/**
 * connectWebSocket
 * ----------------
 * Opens a WebSocket to the configured same-origin proxy path, falls back to
 * the direct backend port if the proxy socket never opens, updates the UI
 * connection state via setConnectionState(), and automatically reconnects if
 * the socket closes or errors out.
 *
 * @param {{proxyUrl: string, directUrl: string, name: string}|string} endpoint
 *   The websocket endpoint definition or URL.
 * @param {number} [reconnectDelay=5000]
 *   Milliseconds to wait before trying to reconnect after a close or error.
 */
function connectWebSocket(endpoint, reconnectDelay = 5000, attemptIndex = 0) {
    if (attemptIndex === 0) {
        clearWebSocketReconnectTimer();
    }

    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return ws;
    }

    const endpointConfig =
        typeof endpoint === "string"
            ? createEndpointDefinition("socket", endpoint, endpoint)
            : endpoint;
    const usingFallback = attemptIndex > 0;
    const url = usingFallback ? endpointConfig.directUrl : endpointConfig.proxyUrl;
    let opened = false;
    let fallbackStarted = false;

    function getWebSocketFailureReason(errorLike, defaultReason) {
        if (errorLike && typeof errorLike.message === "string" && errorLike.message.trim() !== "") {
            return errorLike.message.trim();
        }

        if (errorLike && typeof errorLike.code === "number") {
            return `close code ${errorLike.code}`;
        }

        return defaultReason;
    }

    function beginWebSocketFallback(reason) {
        if (usingFallback || fallbackStarted) {
            return false;
        }

        fallbackStarted = true;
        warnWebSocketFallback(endpointConfig, reason);
        warnWebSocketFallbackAttempt(endpointConfig);
        ws = null;
        connectWebSocket(endpointConfig, reconnectDelay, 1);
        return true;
    }

    function handleFallbackFailure(reason, errorLike) {
        warnWebSocketFallbackFailure(endpointConfig, reason);

        if (errorLike) {
            debugConsole("error", "WebSocket ❌ error", errorLike);
        } else {
            debugConsole("error", `WebSocket ❌ ${reason}`);
        }

        communicationInterrupted = true;
        reloadAfterReconnectPending = true;
        websocketCurrentlyConnected = false;
        setConnectionState("disconnected");
        syncConnectionAlert();
    }

    function scheduleReconnectFromProxy() {
        if (systemPaused || websocketReconnectTimer !== null) {
            return;
        }

        websocketReconnectTimer = setTimeout(() => {
            websocketReconnectTimer = null;
            connectWebSocket(endpointConfig, reconnectDelay, 0);
        }, reconnectDelay);
    }

    // Notify the UI we’re attempting to connect
    setConnectionState("connecting");
    debugConsole("debug", `WebSocket ▶️ connecting to ${url}`);

    // Create the WebSocket
    try {
        ws = new WebSocket(url);
    } catch (error) {
        if (!usingFallback) {
            const reason = getWebSocketFailureReason(error, "constructor error");
            beginWebSocketFallback(reason);
            return ws;
        }

        handleFallbackFailure(getWebSocketFailureReason(error, "constructor error"), error);
        ws = null;
        scheduleReconnectFromProxy();
        return ws;
    }
    const socket = ws;
    let catalogRequest = null;

    // On open: update UI and log
    socket.addEventListener("open", () => {
        opened = true;
        debugConsole("debug", "WebSocket ▶️ open");
        websocketCurrentlyConnected = true;
        websocketConnectedOnce = true;
        armOutageBannerIfReady();
        setConnectionState("connected");
        syncTestToneControlState(isTestToneRuntimeActive());
        const requestedCatalog = requestWsprBandCatalog(socket);
        if (requestedCatalog) {
            catalogRequest = requestedCatalog;
        }

        const $reload = $("#systemModal .reload-btn");
        if ($reload.is(":visible")) {
            $("#systemModalBody").text("System has restarted, reload page.");
            $reload.prop("disabled", false);
        }

        if (reloadAfterReconnectPending) {
            reloadAfterReconnectPending = false;
            communicationInterrupted = false;
            reloadAllData();
        } else if (communicationInterrupted) {
            communicationInterrupted = false;
            reloadAllData();
        } else {
            getTxState();
            syncConnectionAlert();
            if (isRuntimeControlView() && typeof clearOfflineDefaults === "function") {
                clearOfflineDefaults();
            }
        }
    });

    // On message: Try to parse JSON and react to “transmitting” or
    // "tx_state" state
    socket.addEventListener("message", (ev) => {
        debugConsole("debug", "WebSocket ◀️ message:", ev.data);
        let msg;
        try {
            msg = JSON.parse(ev.data);
        } catch (err) {
            debugConsole("warn", "WebSocket ⚠️ invalid JSON:", err);
            return;
        }

        if (String(msg.command || "").toLowerCase() === "stop") {
            if (typeof handleStopCommandResponse === "function") {
                handleStopCommandResponse(msg);
            }
            handleTestToneStopDisableResponse(msg);
            return;
        }

        if (msg.command === "tone_start" || msg.command === "tone_end") {
            if (typeof handleTestToneCommandResponse === "function") {
                handleTestToneCommandResponse(msg, socket);
            }
            return;
        }

        if (msg.command === "wspr_band_catalog") {
            handleWsprBandCatalogResponse(msg, socket, catalogRequest);
            return;
        }

        // If the server pushes a “transmit” event:
        if (msg.type === "transmit") {
            applyRuntimeStatus(msg);
            if (msg.state === "starting" || msg.tx_state === "transmitting") {
                const ts = new Date(msg.timestamp);
                setConnectionState("transmitting", ts);
                debugConsole(
                    "debug",
                    "Received transmit event:",
                    msg.state,
                    "tx_state=",
                    msg.tx_state,
                    "at",
                    ts.toString()
                );
            } else if (msg.state === "finished") {
                setConnectionState("connected");
                queueRuntimeStatusRefresh();
                debugConsole(
                    "debug",
                    "Transmit finished at:",
                    new Date(msg.timestamp).toString()
                );
            } else if (
                msg.state === "canceled" ||
                msg.state === "skipped" ||
                msg.state === "stopped"
            ) {
                setConnectionState("connected");
                queueRuntimeStatusRefresh();
                debugConsole(
                    "debug",
                    "Received transmit event:",
                    msg.state,
                    "tx_state=",
                    msg.tx_state
                );
            }
            return;
        }

        // If the server is replying to our get_tx_state command:
        if (msg.tx_state !== undefined) {
            applyRuntimeStatus(msg);
            setConnectionState(msg.tx_state === "transmitting" ? "transmitting" : "connected");
            debugConsole("debug", "Received tx_state reply:", msg.tx_state);
            return;
        }
        // {"state":"reload","timestamp":"2025-04-27T22:25:43Z","type":"configuration"}
        if (msg.type === "configuration" && msg.state === "reload") {
            // Clear any pending retry
            clearPendingPopulateConfigRetry();

            // Reload if it’s been more than 2 min since our last save
            const now = Date.now();
            if (!lastSaveTimestamp || now - lastSaveTimestamp > 2 * 60 * 1000) {
                debugConsole("debug", "Reloading config by notification.");
                populateConfig();
            }
        }

        if (msg.type === "configuration" && msg.state === "reload_failed") {
            const message =
                typeof msg.message === "string" && msg.message.trim()
                    ? msg.message.trim()
                    : "Configuration reload failed.";
            const formattedMessage =
                typeof formatReloadFailureMessage === "function"
                    ? formatReloadFailureMessage(message)
                    : message;
            if (
                typeof shouldSuppressDisabledModeSwitchReloadFailure === "function" &&
                shouldSuppressDisabledModeSwitchReloadFailure(message)
            ) {
                debugConsole(
                    "debug",
                    "Suppressed disabled mode-switch reload failure:",
                    message
                );
                return;
            }
            if (
                typeof handleCwDurationPolicyFailure === "function" &&
                handleCwDurationPolicyFailure(message)
            ) {
                debugConsole(
                    "debug",
                    "Routed CW duration reload failure to Setup validation:",
                    message
                );
                return;
            }
            if (typeof showBackendStatus === "function") {
                showBackendStatus(formattedMessage, "danger", "runtime");
            }
            if (typeof showMessageDialog === "function") {
                showMessageDialog({
                    title: "Configuration Reload Failed",
                    message: formattedMessage,
                    confirmClass: "btn-danger"
                });
            }
        }

        // …any other message types…
    });

    // On error: Log and treat as a disconnection
    socket.addEventListener("error", (err) => {
        if (!opened && beginWebSocketFallback(getWebSocketFailureReason(err, "network error"))) {
            return;
        }

        if (!opened && usingFallback) {
            handleFallbackFailure(getWebSocketFailureReason(err, "network error"), err);
            ws = null;
            scheduleReconnectFromProxy();
            return;
        }

        if (ws !== socket) {
            return;
        }
        debugConsole("error", "WebSocket ❌ error", err);
        if (testToneLifecycleState !== TEST_TONE_LIFECYCLE.IDLE) {
            markTestToneOutcomeUnknown(
                "Controller connection lost. The Test Tone outcome is unknown; End will remain available for recovery after reconnect."
            );
        }
        clearPendingTestToneStartRequest();
        communicationInterrupted = true;
        reloadAfterReconnectPending = true;
        websocketCurrentlyConnected = false;
        if (ws === socket) {
            invalidateWsprBandCatalogAuthorization(
                "WSPR band catalog unavailable while reconnecting. Test Tone Start is disabled."
            );
        }
        setConnectionState("disconnected");
        syncConnectionAlert();
    });

    // On close: Schedule a reconnect
    socket.addEventListener("close", (ev) => {
        const reason = getWebSocketFailureReason(ev, "close before open");

        if (!opened && beginWebSocketFallback(reason)) {
            return;
        }

        if (!opened && usingFallback) {
            handleFallbackFailure(reason);
            ws = null;
            scheduleReconnectFromProxy();
            return;
        }

        if (ws !== socket) {
            return;
        }
        debugConsole(
            "debug",
            `WebSocket 🔌 closed (code=${ev.code}), reconnecting in ${reconnectDelay}ms`
        );
        if (testToneLifecycleState !== TEST_TONE_LIFECYCLE.IDLE) {
            markTestToneOutcomeUnknown(
                "Controller connection lost. The Test Tone outcome is unknown; End will remain available for recovery after reconnect."
            );
        }
        if (ws === socket) {
            invalidateWsprBandCatalogAuthorization(
                "WSPR band catalog unavailable while disconnected or reconnecting. Test Tone Start is disabled."
            );
        }
        clearPendingTestToneStartRequest();
        communicationInterrupted = true;
        reloadAfterReconnectPending = true;
        websocketCurrentlyConnected = false;
        setConnectionState("disconnected");
        syncConnectionAlert();

        scheduleReconnectFromProxy();
    });

    // Return the socket in case the caller wants to send or inspect it
    return ws;
}

/**
 * Request the current transmit state from the server.
 * Server should reply with JSON: { tx_state: true|false }
 */
function getTxState() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ command: "get_tx_state" }));
    } else {
        debugConsole("warn", "WebSocket not open; cannot request TX state.");
    }
}

function updateCallsign(forceCallsign) {
    // This function is used across multiple pages. Not all pages include the
    // callsign input, so guard all DOM access to avoid exceptions.
    const $link = $("#wsprnet-link");
    if (!$link.length) {
        return;
    }

    const $text = $link.find(".ms-2");
    const $cs = $("#callsign");
    const isLightweightCallsign = function (value) {
        const trimmed = typeof value === "string" ? value.trim() : "";
        if (!trimmed || /\s/.test(trimmed)) {
            return false;
        }

        return /^(?:[A-Za-z0-9\/]+|<[A-Za-z0-9\/]+>)$/.test(trimmed);
    };

    let callsign = "";

    if (typeof forceCallsign === "string") {
        callsign = forceCallsign.trim();
    } else if ($cs.length && typeof $cs.val() === "string") {
        callsign = $cs.val().trim();
    } else if (window.config && typeof window.config.callsign === "string") {
        callsign = window.config.callsign.trim();
    }

    const isValid = isLightweightCallsign(callsign);

    if ((isValid || !$cs.length) && callsign !== "") {
        $link
            .attr("href", WSPRNET_URL + encodeURIComponent(callsign))
            .attr("title", `${callsign} on WSPRNet`);

        if ($text.length) {
            $text.text(`${callsign} on WSPRNet`);
        }
    } else {
        $link.attr("href", WSPRNET_URL).attr("title", "WSPRNet Database");
        if ($text.length) {
            $text.text("WSPRNet Database");
        }
    }

    // Update Spots For page card header
    if (typeof refreshSpotsHeader === "function") {
        refreshSpotsHeader();
    }
}

function updateWsprryPiVersion() {
    let versionElement = document.getElementById("versionText");

    if (!versionElement) {
        debugConsole("error", "Version element not found.");
        return;
    }

    getJsonWithEndpointFallback(VERSION_ENDPOINT)
        .done(function (response) {
            if (response && response.wspr_version) {
                lastWsprryPiVersionResponse = response;
                versionElement.textContent = response.wspr_version;
                versionElement.title = response.wspr_version;
                checkForWsprryPiUpdate(response);
            } else {
                versionElement.textContent = "Service unavailable";
                versionElement.removeAttribute("title");
                clearWsprryPiUpdateFooter();
                debugConsole("error", "Invalid JSON format from version.");
            }
            syncFixedChromeOffsets();
        })
        .fail(function (jqXHR, textStatus, errorThrown) {
            versionElement.textContent = "Service unavailable";
            versionElement.removeAttribute("title");
            clearWsprryPiUpdateFooter();

            debugConsole(
                "error",
                "Error fetching WSPR version: "
                + textStatus
                + (errorThrown ? " (" + errorThrown + ")" : "")
            );
            syncFixedChromeOffsets();
        });
}

function checkUiBuildVersion() {
    if (uiBuildVersionCheckRunning) {
        return;
    }

    uiBuildVersionCheckRunning = true;
    getJsonWithEndpointFallback(UI_IDENTITY_ENDPOINT)
        .done(function (response) {
            if (response && response.installed_ui_build_id) {
                maybePromptForUiRefresh(response);
            }
        })
        .always(function () {
            uiBuildVersionCheckRunning = false;
        });
}

function initUiBuildChangePolling() {
    if (uiBuildPollTimer !== null) {
        return;
    }

    uiBuildPollTimer = window.setInterval(
        checkUiBuildVersion,
        UI_BUILD_POLL_INTERVAL_MS
    );

    document.addEventListener("visibilitychange", () => {
        if (!document.hidden) {
            checkUiBuildVersion();
        }
    });
}

function initGithubUpdatePolling() {
    if (githubUpdatePollTimer !== null) {
        return;
    }

    githubUpdatePollTimer = window.setInterval(
        updateWsprryPiVersion,
        GITHUB_UPDATE_POLL_INTERVAL_MS
    );
}

function forceUpdateCheckNow() {
    if (isUpdateCheckDisabled()) {
        renderUpdateCheckPanelDisabled();
        markWsprryPiUpdateChecksDisabled();
        return;
    }

    const elements = updateCheckPanelElements();
    if (elements) {
        elements.status.textContent = "Checking...";
        elements.status.dataset.state = "checking";
        renderUpdateCheckPanelTitle(elements, null, "Checking update status");
        renderUpdateCheckTechnicalDetails(elements, [
            {
                label: "Summary",
                value: "Checking GitHub now."
            },
            {
                label: "Check now",
                value: "Bypassing the normal update-check cache and failure rate limit."
            }
        ]);
        elements.checkNowButton.disabled = true;
    }

    getJsonWithEndpointFallback(VERSION_ENDPOINT)
        .done(function (response) {
            lastWsprryPiVersionResponse = response;
            checkForWsprryPiUpdate(response, {
                bypassCache: true
            });
        })
        .fail(function (jqXHR, textStatus, errorThrown) {
            renderUpdateCheckPanelFailure(
                buildUpdateCheckFailure(
                    "network",
                    `Could not load local /version metadata: ${textStatus}${errorThrown ? ` (${errorThrown})` : ""}.`
                )
            );
        })
        .always(function () {
            if (elements?.checkNowButton) {
                elements.checkNowButton.disabled = false;
            }
        });
}

function shortSha(value) {
    return typeof value === "string" ? value.substring(0, 7) : "";
}

function findFullShaInValue(value) {
    if (typeof value === "string") {
        const match = value.match(/\b[0-9a-f]{40}\b/i);
        return match ? match[0] : "";
    }

    if (Array.isArray(value)) {
        for (const item of value) {
            const match = findFullShaInValue(item);
            if (match) {
                return match;
            }
        }
        return "";
    }

    if (value && typeof value === "object") {
        for (const key of Object.keys(value)) {
            const match = findFullShaInValue(value[key]);
            if (match) {
                return match;
            }
        }
    }

    return "";
}

function buildUpdateCheckFailure(code, detail = "") {
    return {
        updateCheckFailed: true,
        code,
        message: UPDATE_CHECK_ERROR_MESSAGES[code] || UPDATE_CHECK_ERROR_MESSAGES.unknown,
        detail
    };
}

// Dirty means the local source tree had uncommitted or staged modifications
// when this binary was built. It is local build metadata, not evidence that a
// remote update exists.
function parseBuildDirtyState(response, rawDisplayVersion, rawUiVersion, rawExeVersion) {
    const structuredDirtyState = response?.wspr_build_dirty_state;
    if (structuredDirtyState && typeof structuredDirtyState === "object") {
        if (structuredDirtyState.known === true) {
            return {
                known: true,
                dirty: structuredDirtyState.dirty === true,
                source: "structured"
            };
        }
        return {
            known: false,
            dirty: false,
            source: "structured"
        };
    }

    const structuredDirty = response?.wspr_build_dirty ?? response?.wspr_dirty;
    if (typeof structuredDirty === "boolean") {
        return {
            known: true,
            dirty: structuredDirty,
            source: "structured"
        };
    }

    if (typeof structuredDirty === "string") {
        const normalized = structuredDirty.trim().toLowerCase();
        if (normalized === "true" || normalized === "1" || normalized === "dirty") {
            return {
                known: true,
                dirty: true,
                source: "structured"
            };
        }
        if (normalized === "false" || normalized === "0" || normalized === "clean") {
            return {
                known: true,
                dirty: false,
                source: "structured"
            };
        }
    }

    const combinedVersionText = [rawDisplayVersion, rawUiVersion, rawExeVersion]
        .filter((value) => typeof value === "string" && value)
        .join(" ");
    if (/(^|[+.\-\s])dirty(\b|[.\-\s])/i.test(combinedVersionText)) {
        return {
            known: true,
            dirty: true,
            source: "version_text"
        };
    }

    return {
        known: false,
        dirty: false,
        source: "unavailable"
    };
}

function parseBranchState(response, currentBranch) {
    const rawState = typeof response?.wspr_branch_state === "string"
        ? response.wspr_branch_state.trim().toLowerCase()
        : "";

    if (rawState === "branch" || rawState === "detached" || rawState === "unknown") {
        return rawState;
    }

    if (currentBranch === "HEAD") {
        return "detached";
    }

    if (!currentBranch || currentBranch === "unknown") {
        return "unknown";
    }

    return "branch";
}

function parseStructuredSemanticVersion(value) {
    if (!value || typeof value !== "object" || value.valid !== true) {
        return null;
    }

    const major = Number(value.major);
    const minor = Number(value.minor);
    const patch = Number(value.patch);
    if (![major, minor, patch].every(Number.isInteger)) {
        return null;
    }

    const prerelease = Array.isArray(value.prerelease)
        ? value.prerelease.map((identifier) => String(identifier).toLowerCase())
        : [];
    const build = Array.isArray(value.build)
        ? value.build.map((identifier) => String(identifier).toLowerCase())
        : [];
    const normalizedPrerelease = normalizeSemanticIdentifiers(prerelease.join("."));
    const normalizedBuild = normalizeSemanticIdentifiers(build.join("."), true);
    if (normalizedPrerelease === null || normalizedBuild === null) {
        return null;
    }

    return {
        major,
        minor,
        patch,
        prerelease: normalizedPrerelease,
        build: normalizedBuild,
        raw: typeof value.raw === "string" ? value.raw : `${major}.${minor}.${patch}`,
        normalized: `${major}.${minor}.${patch}${normalizedPrerelease.length ? `-${normalizedPrerelease.join(".")}` : ""}`
    };
}

function parseWsprryPiVersionResponse(response) {
    const rawDisplayVersion = typeof response?.wspr_version === "string"
        ? response.wspr_version.trim()
        : "";
    const rawUiVersion = typeof response?.ui_version === "string"
        ? response.ui_version.trim()
        : "";
    const rawVersion = typeof response?.wspr_version_raw === "string"
        ? response.wspr_version_raw.trim()
        : "";
    const rawExeVersion = rawVersion || (typeof response?.wspr_exe_version === "string"
        ? response.wspr_exe_version.trim()
        : "");
    const branchFieldPresent = Object.prototype.hasOwnProperty.call(response || {}, "wspr_branch");
    const commitFieldPresent = Object.prototype.hasOwnProperty.call(response || {}, "wspr_commit");
    const branchStateFieldPresent = Object.prototype.hasOwnProperty.call(response || {}, "wspr_branch_state");
    const rawBackendBranch = typeof response?.wspr_branch === "string"
        ? response.wspr_branch.trim()
        : "";
    const rawBranchState = typeof response?.wspr_branch_state === "string"
        ? response.wspr_branch_state.trim().toLowerCase()
        : "";
    const rawBackendCommit = typeof response?.wspr_commit === "string"
        ? response.wspr_commit.trim()
        : "";
    const backendCommit = /^[0-9a-f]{7,40}$/i.test(rawBackendCommit)
        ? rawBackendCommit
        : "";
    const currentDisplayVersion = rawDisplayVersion || rawUiVersion;
    // Update-check precedence is structured backend metadata first. Display
    // strings are legacy fallback only when the matching structured field is
    // absent, because display copy is allowed to change for users.
    const displayVersionForParsing = rawDisplayVersion || rawUiVersion;
    const versionForShaParsing = rawUiVersion || rawDisplayVersion;
    const branchMatch = displayVersionForParsing.match(/\(([^()]+)\)/);
    const displayBranch = branchMatch ? branchMatch[1].trim() : "";
    const currentBranch = rawBackendBranch || displayBranch;
    const branchState = parseBranchState(response, currentBranch);
    const fullSha = findFullShaInValue(response);
    const shortShaMatch = versionForShaParsing.match(/[+.:-]([0-9a-f]{7,40})(?:\b|[^0-9a-f])/i) ||
        versionForShaParsing.match(/\b([0-9a-f]{7,40})\b/i);
    const currentSha = backendCommit || fullSha || (shortShaMatch ? shortShaMatch[1] : "");
    const dirtyState = parseBuildDirtyState(response, rawDisplayVersion, rawUiVersion, rawExeVersion);
    const structuredVersionPresent = Object.prototype.hasOwnProperty.call(response || {}, "wspr_version_parsed");
    const localVersionParsedObject = parseStructuredSemanticVersion(response?.wspr_version_parsed);

    if (!currentDisplayVersion) {
        return buildUpdateCheckFailure("missing_version_data", "The /version response did not include wspr_version or ui_version.");
    }

    if (structuredVersionPresent && response?.wspr_version_parsed?.valid === true && !localVersionParsedObject) {
        return buildUpdateCheckFailure("missing_version_data", "The /version response included malformed wspr_version_parsed metadata.");
    }

    if (branchFieldPresent && !rawBackendBranch) {
        return buildUpdateCheckFailure("missing_branch", "The /version response included wspr_branch, but it was empty or malformed.");
    }

    if (
        branchStateFieldPresent &&
        rawBranchState !== "branch" &&
        rawBranchState !== "detached" &&
        rawBranchState !== "unknown"
    ) {
        return buildUpdateCheckFailure("missing_branch", "The /version response included wspr_branch_state, but it was not branch, detached, or unknown.");
    }

    if (!currentBranch) {
        return buildUpdateCheckFailure("missing_branch", "The /version response did not include wspr_branch or a parseable display branch.");
    }

    if (commitFieldPresent && !backendCommit) {
        return buildUpdateCheckFailure("missing_commit", "The /version response included wspr_commit, but it was not a valid SHA.");
    }

    if (!currentSha) {
        return buildUpdateCheckFailure("missing_commit", "The /version response did not include wspr_commit or a parseable commit SHA.");
    }

    return {
        ok: true,
        currentDisplayVersion,
        currentModalVersion: rawExeVersion || rawUiVersion || rawDisplayVersion,
        currentSha,
        currentBranch,
        branchState,
        displayBranch,
        currentShaIsFull: currentSha.length === 40,
        localVersionParsedObject,
        buildDirtyKnown: dirtyState.known,
        buildDirty: dirtyState.dirty,
        buildDirtySource: dirtyState.source
    };
}

function updateCheckShaMatches(currentSha, targetHeadSha) {
    if (typeof currentSha !== "string" || typeof targetHeadSha !== "string") {
        return false;
    }

    const normalizedCurrent = currentSha.trim().toLowerCase();
    const normalizedHead = targetHeadSha.trim().toLowerCase();

    if (!normalizedCurrent || !normalizedHead) {
        return false;
    }

    if (normalizedCurrent.length >= 40) {
        return normalizedCurrent === normalizedHead;
    }

    return normalizedHead.startsWith(normalizedCurrent);
}

function updateCheckNoUpdateResult() {
    return {
        updateAvailable: false
    };
}

function updateCheckCommitComparisonResult(currentSha, headSha, status = "") {
    if (updateCheckShaMatches(currentSha, headSha) || status === "identical") {
        return {
            updateAvailable: false,
            versionComparisonStatus: "equal"
        };
    }

    // GitHub compare direction is base=currentSha, head=target branch HEAD.
    // In this direction, status "ahead" means the target branch contains the
    // installed commit and has newer commits, including empty commits.
    if (status === "ahead") {
        return {
            updateAvailable: true,
            versionComparisonStatus: "update_available"
        };
    }

    // Status "behind" means the installed commit is ahead of the target branch.
    // Diverged is a distinct successful no-update result because neither
    // history is a safe upgrade path from the other.
    return {
        updateAvailable: false,
        versionComparisonStatus: status === "behind"
            ? "local_ahead"
            : status === "diverged"
                ? "diverged"
                : "commit_fallback"
    };
}

function branchAllowsCommitUpdate(branch) {
    return branch !== "main";
}

function formatSemanticVersion(version) {
    if (!version) {
        return "";
    }

    const prerelease = version.prerelease.length ? `-${version.prerelease.join(".")}` : "";
    return `${version.major}.${version.minor}.${version.patch}${prerelease}`;
}

function normalizeSemanticIdentifiers(value, allowLeadingZeroNumeric = false) {
    if (!value) {
        return [];
    }

    const identifiers = value.split(".");
    for (const identifier of identifiers) {
        if (
            !identifier ||
            !/^[0-9A-Za-z-]+$/.test(identifier) ||
            (!allowLeadingZeroNumeric && /^\d+$/.test(identifier) && identifier.length > 1 && identifier.startsWith("0"))
        ) {
            return null;
        }
    }

    return identifiers.map((identifier) => identifier.toLowerCase());
}

function parseSemanticVersion(value) {
    const source = typeof value === "string" ? value.trim() : "";
    const match = source.match(/(?:^|[^0-9A-Za-z])v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?(?=$|[^0-9A-Za-z.-])/);
    if (!match) {
        return null;
    }

    const prerelease = normalizeSemanticIdentifiers(match[4]);
    const build = normalizeSemanticIdentifiers(match[5], true);
    if (prerelease === null || build === null) {
        return null;
    }

    return {
        major: Number(match[1]),
        minor: Number(match[2]),
        patch: Number(match[3]),
        prerelease,
        build,
        raw: match[0].replace(/^[^v0-9]*/, ""),
        normalized: `${match[1]}.${match[2]}.${match[3]}${prerelease.length ? `-${prerelease.join(".")}` : ""}`
    };
}

function comparePrereleaseIdentifier(left, right) {
    const leftNumeric = /^\d+$/.test(left);
    const rightNumeric = /^\d+$/.test(right);

    if (leftNumeric && rightNumeric) {
        return Number(left) - Number(right);
    }

    if (leftNumeric !== rightNumeric) {
        return leftNumeric ? -1 : 1;
    }

    // Known channel names keep the intended prerelease progression explicit.
    // Unknown channels still fall back to normal lexical SemVer ordering.
    const knownChannelOrder = new Map([
        ["alpha", 0],
        ["beta", 1],
        ["rc", 2]
    ]);
    if (knownChannelOrder.has(left) && knownChannelOrder.has(right)) {
        return knownChannelOrder.get(left) - knownChannelOrder.get(right);
    }

    return left < right ? -1 : left > right ? 1 : 0;
}

function compareSemanticVersions(left, right) {
    for (const key of ["major", "minor", "patch"]) {
        if (left[key] !== right[key]) {
            return left[key] - right[key];
        }
    }

    if (!left.prerelease.length && !right.prerelease.length) {
        return 0;
    }

    if (!left.prerelease.length) {
        return 1;
    }

    if (!right.prerelease.length) {
        return -1;
    }

    const length = Math.max(left.prerelease.length, right.prerelease.length);
    for (let index = 0; index < length; index += 1) {
        if (left.prerelease[index] === undefined) {
            return -1;
        }
        if (right.prerelease[index] === undefined) {
            return 1;
        }

        const result = comparePrereleaseIdentifier(left.prerelease[index], right.prerelease[index]);
        if (result !== 0) {
            return result;
        }
    }

    return 0;
}

function newerSemanticRelease(left, right) {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    return compareSemanticVersions(left.version, right.version) < 0 ? right : left;
}

async function fetchGithubReleases() {
    const releases = await fetchGithubJson(`${UPDATE_CHECK_API_BASE}/releases?per_page=100`);
    if (!Array.isArray(releases)) {
        const error = new Error("GitHub releases response was not an array");
        error.code = "malformed_response";
        throw error;
    }

    return releases;
}

function summarizeSemanticReleases(releases) {
    let latestStable = null;
    let latestPrerelease = null;
    const prereleasesByChannel = new Map();

    for (const release of releases) {
        const tag = typeof release?.tag_name === "string" ? release.tag_name.trim() : "";
        const version = parseSemanticVersion(tag);
        if (!version) {
            continue;
        }

        const candidate = {
            version,
            release,
            releaseUrl: typeof release.html_url === "string" && release.html_url
                ? release.html_url
                : UPDATE_CHECK_RELEASES_URL,
            releaseTitle: release.name || release.tag_name || "",
            normalized: formatSemanticVersion(version)
        };

        if (version.prerelease.length) {
            latestPrerelease = newerSemanticRelease(latestPrerelease, candidate);
            const channel = version.prerelease[0];
            prereleasesByChannel.set(
                channel,
                newerSemanticRelease(prereleasesByChannel.get(channel), candidate)
            );
        } else {
            latestStable = newerSemanticRelease(latestStable, candidate);
        }
    }

    return { latestStable, latestPrerelease, prereleasesByChannel };
}

function updateCheckCacheKey(versionInfo) {
    const dirtyKey = versionInfo.buildDirtyKnown
        ? versionInfo.buildDirty ? "dirty" : "clean"
        : "dirty-unknown";
    // Site-global key: do not include page path so navigation between pages
    // cannot bypass update-check success or failure rate limits.
    return `${UPDATE_CHECK_CACHE_PREFIX}:${versionInfo.branchState || "branch"}:${versionInfo.currentBranch}:${versionInfo.currentSha}:${dirtyKey}`;
}

function updateCheckFailureCacheKey(versionInfo) {
    // Use the same site-global identity as successful checks, but store failure
    // state separately so a failed check can never masquerade as no-update.
    return updateCheckCacheKey(versionInfo).replace(
        `${UPDATE_CHECK_CACHE_PREFIX}:`,
        `${UPDATE_CHECK_FAILURE_CACHE_PREFIX}:`
    );
}

function readUpdateCheckCache(versionInfo) {
    try {
        const raw = window.localStorage.getItem(updateCheckCacheKey(versionInfo));
        if (!raw) {
            return null;
        }

        const cached = JSON.parse(raw);
        if (
            !cached ||
            cached.schemaVersion !== UPDATE_CHECK_CACHE_SCHEMA_VERSION ||
            cached.currentSha !== versionInfo.currentSha ||
            cached.currentBranch !== versionInfo.currentBranch ||
            cached.branchState !== versionInfo.branchState ||
            Date.now() - Number(cached.checkedAt || 0) >= UPDATE_CHECK_CACHE_TTL_MS
        ) {
            return null;
        }

        return cached;
    } catch {
        return null;
    }
}

function readUpdateCheckFailureCache(versionInfo) {
    try {
        const raw = window.localStorage.getItem(updateCheckFailureCacheKey(versionInfo));
        if (!raw) {
            return null;
        }

        const cached = JSON.parse(raw);
        if (
            !cached ||
            cached.schemaVersion !== UPDATE_CHECK_CACHE_SCHEMA_VERSION ||
            cached.currentSha !== versionInfo.currentSha ||
            cached.currentBranch !== versionInfo.currentBranch ||
            cached.branchState !== versionInfo.branchState ||
            cached.updateCheckFailed !== true ||
            Date.now() - Number(cached.checkedAt || 0) >= UPDATE_CHECK_FAILURE_RATE_LIMIT_MS
        ) {
            return null;
        }

        return cached;
    } catch {
        return null;
    }
}

function writeUpdateCheckCache(versionInfo, result) {
    try {
        window.localStorage.removeItem(updateCheckFailureCacheKey(versionInfo));
        window.localStorage.setItem(
            updateCheckCacheKey(versionInfo),
            JSON.stringify(Object.assign(
                {
                    schemaVersion: UPDATE_CHECK_CACHE_SCHEMA_VERSION,
                    branchState: versionInfo.branchState || "branch"
                },
                result
            ))
        );
    } catch {
    }
}

function writeUpdateCheckFailureCache(versionInfo, error) {
    const failure = normalizeUpdateCheckFailure(error);

    try {
        window.localStorage.setItem(
            updateCheckFailureCacheKey(versionInfo),
            JSON.stringify(Object.assign(
                {
                    schemaVersion: UPDATE_CHECK_CACHE_SCHEMA_VERSION,
                    checkedAt: Date.now(),
                    currentSha: versionInfo.currentSha,
                    currentBranch: versionInfo.currentBranch,
                    branchState: versionInfo.branchState || "branch"
                },
                failure
            ))
        );
    } catch {
    }

    return failure;
}

async function fetchGithubJson(url, options = {}) {
    let response;
    try {
        response = await fetch(url, {
            headers: {
                Accept: "application/vnd.github+json"
            },
            cache: options.cache || "no-store"
        });
    } catch (error) {
        const networkError = new Error("GitHub network request failed");
        networkError.code = "network";
        networkError.cause = error;
        throw networkError;
    }

    if (!response.ok) {
        const error = new Error(`GitHub returned HTTP ${response.status}`);
        error.status = response.status;
        error.code = response.status === 403 &&
            response.headers.get("x-ratelimit-remaining") === "0"
            ? "rate_limited"
            : "github_http";
        const reset = response.headers.get("x-ratelimit-reset");
        if (reset) {
            error.rateLimitReset = reset;
        }
        throw error;
    }

    try {
        return await response.json();
    } catch (error) {
        const parseError = new Error("GitHub returned malformed JSON");
        parseError.code = "malformed_response";
        parseError.cause = error;
        throw parseError;
    }
}

async function lookupGithubBranch(branch) {
    const branchUrl = `${UPDATE_CHECK_API_BASE}/branches/${encodeURIComponent(branch)}`;
    debugConsole("debug", `Update check branch lookup: ${branchUrl}`);

    let data;
    try {
        data = await fetchGithubJson(branchUrl);
    } catch (error) {
        const status = typeof error?.status === "number" ? `HTTP ${error.status}` : "network error";
        debugConsole("debug", `Update check branch lookup failed for ${branch}: ${status}`);
        if (error?.status === 404) {
            error.code = "branch_missing";
        }
        throw error;
    }

    const sha = typeof data?.commit?.sha === "string" ? data.commit.sha : "";

    if (!sha) {
        const error = new Error(`GitHub branch ${branch} did not include a HEAD SHA`);
        error.code = "malformed_response";
        throw error;
    }

    debugConsole("debug", `Update check branch lookup result for ${branch}: ${sha}`);

    return {
        branch,
        headSha: sha
    };
}

async function isCurrentShaReachableFromBranchHead(currentSha, branchInfo) {
    const normalizedCurrent = typeof currentSha === "string" ? currentSha.trim().toLowerCase() : "";
    const normalizedHead = typeof branchInfo?.headSha === "string" ? branchInfo.headSha.trim().toLowerCase() : "";

    if (normalizedCurrent.length >= 40 && normalizedHead.length >= 40 && normalizedCurrent === normalizedHead) {
        return {
            contained: true,
            status: "identical",
            uncertain: false
        };
    }

    if (
        normalizedCurrent &&
        normalizedCurrent.length < 40 &&
        normalizedHead.startsWith(normalizedCurrent)
    ) {
        return {
            contained: true,
            status: "short_sha_match",
            uncertain: true
        };
    }

    try {
        // GitHub compare direction is base=currentSha, head=branch HEAD.
        // In this direction, status "ahead" means the branch HEAD is ahead of
        // currentSha, so currentSha is reachable from that branch. Status
        // "behind" means currentSha is ahead of the branch and is not contained.
        const data = await fetchGithubJson(
            `${UPDATE_CHECK_API_BASE}/compare/${encodeURIComponent(currentSha)}...${encodeURIComponent(branchInfo.headSha)}`
        );
        const status = typeof data?.status === "string" ? data.status : "";
        if (!status) {
            const error = new Error("GitHub compare response did not include a status");
            error.code = "malformed_response";
            throw error;
        }

        return {
            contained: status === "identical" || status === "ahead",
            status,
            uncertain: false
        };
    } catch {
        return {
            contained: false,
            status: "unavailable",
            uncertain: false
        };
    }
}

function selectedUpdateBranch(branchInfo, reason, fallbackUsed = false) {
    return Object.assign({}, branchInfo, {
        fallbackUsed,
        selectionReason: reason
    });
}

async function selectContainedFallbackBranch(versionInfo, branchInfo, reason) {
    const containment = await isCurrentShaReachableFromBranchHead(versionInfo.currentSha, branchInfo);
    if (!containment.contained) {
        throw buildUpdateCheckFailure(
            "unsafe_target",
            `The running commit is not proven reachable from upstream ${branchInfo.branch} (status ${containment.status || "unknown"}).`
        );
    }

    const certainty = containment.uncertain ? "uncertain short SHA match" : "exact/compare-confirmed match";
    return selectedUpdateBranch(
        branchInfo,
        `${reason}; running commit reachable from upstream ${branchInfo.branch} (${certainty}, status ${containment.status || "unknown"})`,
        true
    );
}

function isDetachedOrUnknownBranchBuild(versionInfo) {
    return versionInfo.branchState === "detached" ||
        versionInfo.branchState === "unknown" ||
        versionInfo.currentBranch === "HEAD" ||
        versionInfo.currentBranch === "unknown";
}

async function selectDetachedOrUnknownUpdateBranch(versionInfo) {
    // Detached HEAD and unknown-branch builds do not have a trustworthy
    // same-name upstream branch. Commit fallback is allowed only after proving
    // the local commit is reachable from a known upstream branch.
    const candidates = ["main", "devel"];
    let lastFailure = null;

    for (const branch of candidates) {
        let branchInfo;
        try {
            branchInfo = await lookupGithubBranch(branch);
        } catch (error) {
            if (error.status !== 404) {
                throw error;
            }
            lastFailure = error;
            debugConsole("debug", `Update check detached/unknown branch target probe skipped missing upstream ${branch}.`);
            continue;
        }

        const containment = await isCurrentShaReachableFromBranchHead(versionInfo.currentSha, branchInfo);
        if (containment.contained) {
            const certainty = containment.uncertain ? "uncertain short SHA match" : "exact/compare-confirmed match";
            debugConsole("debug", `Update check detached/unknown build resolved to upstream ${branch} because current SHA is reachable (${certainty}, status ${containment.status || "unknown"}).`);
            return selectedUpdateBranch(
                branchInfo,
                `detached/unknown branch commit reachable from upstream ${branch} (${certainty}, status ${containment.status || "unknown"})`,
                true
            );
        }

        debugConsole("debug", `Update check detached/unknown build is not reachable from upstream ${branch} (compare status ${containment.status || "unknown"}).`);
    }

    const failure = buildUpdateCheckFailure(
        "detached_target_unknown",
        `Local branch state '${versionInfo.branchState || "unknown"}' with branch '${versionInfo.currentBranch || "unknown"}' is not reachable from upstream main or devel.`
    );
    if (lastFailure?.status === 404) {
        failure.detail += " One or more known upstream branches were missing.";
    }
    throw failure;
}

async function selectGithubUpdateBranch(versionInfo) {
    const currentBranch = versionInfo.currentBranch;

    if (isDetachedOrUnknownBranchBuild(versionInfo)) {
        return selectDetachedOrUnknownUpdateBranch(versionInfo);
    }

    // Rule 1: local main tracks upstream main directly.
    if (currentBranch === "main") {
        return selectedUpdateBranch(await lookupGithubBranch("main"), "local main targets upstream main");
    }

    // Rule 2: local devel tracks upstream devel unless the local commit is
    // proven reachable from upstream main. This prevents devel builds that are
    // ahead of or diverged from main from being compared against the wrong head.
    if (currentBranch === "devel") {
        let develBranch;
        try {
            develBranch = await lookupGithubBranch("devel");
        } catch (error) {
            if (error.status !== 404) {
                throw error;
            }

            debugConsole("debug", "Update check local devel probing upstream main because upstream devel returned HTTP 404.");
            return selectContainedFallbackBranch(
                versionInfo,
                await lookupGithubBranch("main"),
                "upstream devel missing; containment-gated fallback to upstream main"
            );
        }

        try {
            const mainBranch = await lookupGithubBranch("main");
            const mainContainment = await isCurrentShaReachableFromBranchHead(versionInfo.currentSha, mainBranch);
            if (mainContainment.contained) {
                const certainty = mainContainment.uncertain ? "uncertain short SHA match" : "exact/compare-confirmed match";
                debugConsole("debug", `Update check local devel resolved to upstream main because current SHA is reachable from main (${certainty}, status ${mainContainment.status || "unknown"}).`);
                return selectedUpdateBranch(
                    mainBranch,
                    `local devel commit reachable from upstream main (${certainty}, status ${mainContainment.status || "unknown"})`
                );
            }
            debugConsole("debug", `Update check local devel staying on upstream devel because current SHA is not reachable from main (compare status ${mainContainment.status || "unknown"}).`);        } catch (error) {
            const status = typeof error?.status === "number" ? `HTTP ${error.status}` : "network error";
            debugConsole("debug", `Update check local devel staying on upstream devel because main containment probe failed (${status}).`);
        }

        debugConsole("debug", "Update check local devel target remains upstream devel.");
        return selectedUpdateBranch(develBranch, "local devel targets upstream devel");
    }

    // Rule 3: feature and release local branches target the same-name upstream
    // branch first. Detached HEAD and unknown branch-state builds are handled
    // above so they cannot accidentally probe an upstream branch literally
    // named "HEAD" or "unknown".
    try {
        return selectedUpdateBranch(
            await lookupGithubBranch(currentBranch),
            "local branch targets same-name upstream branch"
        );
    } catch (error) {
        if (error.status !== 404) {
            throw error;
        }

        try {
            // Rule 4: if a non-main/non-devel branch is missing upstream,
            // probe devel as a fallback, but select it only after proving that
            // it contains the running commit. Missing branch or differing SHA
            // alone never establishes an update relationship.
            return selectContainedFallbackBranch(
                versionInfo,
                await lookupGithubBranch("devel"),
                `same-name upstream branch '${currentBranch}' missing; containment-gated fallback to upstream devel`
            );
        } catch (fallbackError) {
            if (fallbackError.status === 404) {
                fallbackError.code = "branch_missing";
            }
            throw fallbackError;
        }
    }
}

async function compareGithubCommits(currentSha, headSha) {
    if (updateCheckShaMatches(currentSha, headSha)) {
        return updateCheckCommitComparisonResult(currentSha, headSha, "identical");
    }

    try {
        const data = await fetchGithubJson(
            `${UPDATE_CHECK_API_BASE}/compare/${encodeURIComponent(currentSha)}...${encodeURIComponent(headSha)}`
        );
        const status = typeof data?.status === "string" ? data.status : "";
        if (!status) {
            const error = new Error("GitHub compare response did not include a status");
            error.code = "malformed_response";
            throw error;
        }
        return updateCheckCommitComparisonResult(currentSha, headSha, status);
    } catch (error) {
        if (error.status === 404) {
            throw buildUpdateCheckFailure(
                "comparison_unavailable",
                `GitHub could not compare running commit ${shortSha(currentSha)} with target ${shortSha(headSha)}.`
            );
        }

        throw error;
    }
}

async function resolveReleaseTargetSha(targetCommitish, memo = null) {
    const target = typeof targetCommitish === "string" ? targetCommitish.trim() : "";
    if (!target) {
        return "";
    }

    if (/^[0-9a-f]{40}$/i.test(target)) {
        return target;
    }

    if (memo instanceof Map && memo.has(target)) {
        return memo.get(target);
    }

    function rememberResolvedTarget(resolvedSha) {
        if (memo instanceof Map) {
            memo.set(target, resolvedSha);
        }

        return resolvedSha;
    }

    try {
        return rememberResolvedTarget((await lookupGithubBranch(target)).headSha);
    } catch (error) {
        if (error.status !== 404) {
            throw error;
        }
    }

    try {
        const tagRef = await fetchGithubJson(
            `${UPDATE_CHECK_API_BASE}/git/ref/tags/${encodeURIComponent(target)}`
        );
        const tagObject = tagRef?.object || {};

        if (tagObject.type === "commit" && typeof tagObject.sha === "string") {
            return rememberResolvedTarget(tagObject.sha);
        }

        if (tagObject.type === "tag" && typeof tagObject.url === "string") {
            const tagData = await fetchGithubJson(tagObject.url);
            if (tagData?.object?.type === "commit" && typeof tagData.object.sha === "string") {
                return rememberResolvedTarget(tagData.object.sha);
            }
        }
    } catch (error) {
        if (error.status !== 404) {
            throw error;
        }
    }

    return rememberResolvedTarget("");
}

async function findReleaseForHead(headSha) {
    const releases = await fetchGithubReleases();
    const resolvedTargets = new Map();

    for (const release of releases) {
        const target = typeof release?.target_commitish === "string"
            ? release.target_commitish.trim()
            : "";
        let resolvedSha = target === headSha ? target : "";

        if (!resolvedSha) {
            resolvedSha = await resolveReleaseTargetSha(target, resolvedTargets);
        }

        if (resolvedSha === headSha) {
            return {
                releaseUrl: typeof release.html_url === "string" && release.html_url
                    ? release.html_url
                    : UPDATE_CHECK_RELEASES_URL,
                releaseTitle: release.name || release.tag_name || ""
            };
        }
    }

    return null;
}

function semanticComparisonFallback(reason, localVersion = null) {
    return {
        useCommitFallback: true,
        reason,
        localVersionParsed: localVersion ? formatSemanticVersion(localVersion) : "",
        remoteVersionSelected: ""
    };
}

async function semanticUpdateResultFromCandidate(versionInfo, localVersion, candidate, status) {
    let targetHeadSha = "";
    try {
        targetHeadSha = await resolveReleaseTargetSha(candidate.release?.target_commitish);
    } catch (error) {
        logUpdateCheckWarning(error);
    }

    return {
        useCommitFallback: false,
        checkedAt: Date.now(),
        currentSha: versionInfo.currentSha,
        currentBranch: versionInfo.currentBranch,
        targetBranch: "release",
        targetHeadSha,
        updateAvailable: compareSemanticVersions(localVersion, candidate.version) < 0,
        releaseUrl: candidate.releaseUrl,
        releaseTitle: candidate.releaseTitle,
        fallbackUsed: false,
        selectionReason: `semantic version compared against GitHub release ${candidate.normalized}`,
        versionComparisonUsed: "semver",
        versionComparisonStatus: status,
        localVersionParsed: formatSemanticVersion(localVersion),
        remoteVersionSelected: candidate.normalized
    };
}

async function buildSemanticVersionUpdateResult(versionInfo, options = {}) {
    const localVersion = versionInfo.localVersionParsedObject ||
        parseSemanticVersion(versionInfo.currentModalVersion) ||
        parseSemanticVersion(versionInfo.currentDisplayVersion);
    if (!localVersion) {
        return semanticComparisonFallback("local semantic version could not be parsed");
    }

    if (localVersion.build.length && options.ignoreLocalBuildMetadata !== true) {
        return semanticComparisonFallback("local semantic version has build metadata/commits past tag", localVersion);
    }

    let releases;
    try {
        releases = await fetchGithubReleases();
    } catch (error) {
        logUpdateCheckWarning(error);
        return semanticComparisonFallback("GitHub release data unavailable", localVersion);
    }

    const summary = summarizeSemanticReleases(releases);
    const localIsPrerelease = localVersion.prerelease.length > 0;
    const localVersionParsed = formatSemanticVersion(localVersion);

    // Semantic version flow is primary when the local build is at a parseable
    // tag. Stable builds compare only with latest stable release and never
    // upgrade to a prerelease. Prerelease builds compare with newer stable
    // releases first, then newer prereleases from the same prerelease channel
    // (alpha stays on alpha, beta stays on beta, rc stays on rc). Different
    // prerelease channels are intentionally ignored by default. Commit/branch
    // comparison is fallback only and must not override a valid semantic
    // decision.
    if (!localIsPrerelease) {
        if (!summary.latestStable) {
            return semanticComparisonFallback("no stable semantic GitHub release was available", localVersion);
        }

        const comparison = compareSemanticVersions(localVersion, summary.latestStable.version);
        if (comparison < 0) {
            return semanticUpdateResultFromCandidate(versionInfo, localVersion, summary.latestStable, "update_available");
        }

        return {
            useCommitFallback: false,
            checkedAt: Date.now(),
            currentSha: versionInfo.currentSha,
            currentBranch: versionInfo.currentBranch,
            targetBranch: "release",
            targetHeadSha: "",
            updateAvailable: false,
            releaseUrl: summary.latestStable.releaseUrl,
            releaseTitle: summary.latestStable.releaseTitle,
            fallbackUsed: false,
            selectionReason: "semantic stable version compared against latest stable GitHub release",
            versionComparisonUsed: "semver",
            versionComparisonStatus: comparison === 0 ? "equal" : "local_ahead",
            localVersionParsed,
            remoteVersionSelected: summary.latestStable.normalized
        };
    }

    if (summary.latestStable && compareSemanticVersions(localVersion, summary.latestStable.version) < 0) {
        return semanticUpdateResultFromCandidate(versionInfo, localVersion, summary.latestStable, "update_available");
    }

    const channel = localVersion.prerelease[0];
    const latestSameChannelPrerelease = summary.prereleasesByChannel.get(channel);
    if (latestSameChannelPrerelease) {
        const comparison = compareSemanticVersions(localVersion, latestSameChannelPrerelease.version);
        if (comparison < 0) {
            return semanticUpdateResultFromCandidate(versionInfo, localVersion, latestSameChannelPrerelease, "update_available");
        }

        return {
            useCommitFallback: false,
            checkedAt: Date.now(),
            currentSha: versionInfo.currentSha,
            currentBranch: versionInfo.currentBranch,
            targetBranch: "release",
            targetHeadSha: "",
            updateAvailable: false,
            releaseUrl: latestSameChannelPrerelease.releaseUrl,
            releaseTitle: latestSameChannelPrerelease.releaseTitle,
            fallbackUsed: false,
            selectionReason: "semantic prerelease version compared against same-channel GitHub prerelease",
            versionComparisonUsed: "semver",
            versionComparisonStatus: comparison === 0 ? "equal" : "local_ahead",
            localVersionParsed,
            remoteVersionSelected: latestSameChannelPrerelease.normalized
        };
    }

    const bestRemote = summary.latestStable || summary.latestPrerelease;
    return {
        useCommitFallback: false,
        checkedAt: Date.now(),
        currentSha: versionInfo.currentSha,
        currentBranch: versionInfo.currentBranch,
        targetBranch: "release",
        targetHeadSha: "",
        updateAvailable: false,
        releaseUrl: bestRemote?.releaseUrl || UPDATE_CHECK_RELEASES_URL,
        releaseTitle: bestRemote?.releaseTitle || "",
        fallbackUsed: false,
        selectionReason: "semantic prerelease version has no newer same-channel prerelease or stable release",
        versionComparisonUsed: "semver",
        versionComparisonStatus: "local_ahead",
        localVersionParsed,
        remoteVersionSelected: bestRemote?.normalized || ""
    };
}

async function buildCommitBasedWsprryPiUpdateResult(versionInfo, semanticFallback = null) {
    const selectedBranch = await selectGithubUpdateBranch(versionInfo);
    const comparison = updateCheckShaMatches(versionInfo.currentSha, selectedBranch.headSha)
            ? updateCheckCommitComparisonResult(versionInfo.currentSha, selectedBranch.headSha, "identical")
            : await compareGithubCommits(versionInfo.currentSha, selectedBranch.headSha);
    let releaseUrl = UPDATE_CHECK_RELEASES_URL;
    let releaseTitle = "";

    if (comparison.updateAvailable) {
        try {
            const release = await findReleaseForHead(selectedBranch.headSha);
            if (release) {
                releaseUrl = release.releaseUrl;
                releaseTitle = release.releaseTitle;
            }
        } catch (error) {
            logUpdateCheckWarning(error);
        }
    }

    return {
        checkedAt: Date.now(),
        currentSha: versionInfo.currentSha,
        currentBranch: versionInfo.currentBranch,
        targetBranch: selectedBranch.branch,
        targetHeadSha: selectedBranch.headSha,
        updateAvailable: comparison.updateAvailable,
        releaseUrl,
        releaseTitle,
        fallbackUsed: selectedBranch.fallbackUsed === true,
        selectionReason: selectedBranch.selectionReason || "",
        versionComparisonUsed: "commit",
        versionComparisonStatus: comparison.versionComparisonStatus || semanticFallback?.reason || "commit_fallback",
        localVersionParsed: semanticFallback?.localVersionParsed || "",
        remoteVersionSelected: semanticFallback?.remoteVersionSelected || ""
    };
}

function applyDirtyBuildMetadata(versionInfo, result) {
    if (!versionInfo.buildDirtyKnown) {
        return result;
    }

    const dirtyMetadata = {
        buildDirtyKnown: true,
        buildDirty: versionInfo.buildDirty,
        buildDirtySource: versionInfo.buildDirtySource
    };

    if (!versionInfo.buildDirty) {
        return Object.assign(result, dirtyMetadata);
    }

    debugConsole(
        "debug",
        "Update check local build was dirty at compile time; dirty means local modifications, not a remote update."
    );

    return Object.assign(result, dirtyMetadata, {
        localBuildState: "dirty_build",
        selectionReason: `${result.selectionReason || "update check"}; local build had build-time modifications`,
        versionComparisonStatus: result.updateAvailable ? result.versionComparisonStatus : "local_modified"
    });
}

async function buildWsprryPiUpdateResult(versionInfo) {
    const commitUpdatesAllowed = branchAllowsCommitUpdate(versionInfo.currentBranch);
    const semanticResult = await buildSemanticVersionUpdateResult(versionInfo, {
        ignoreLocalBuildMetadata: !commitUpdatesAllowed
    });
    const branchCommitComparisonHasPriority = versionInfo.branchState === "branch" &&
        !isDetachedOrUnknownBranchBuild(versionInfo);
    if (branchCommitComparisonHasPriority) {
        const commitResult = await buildCommitBasedWsprryPiUpdateResult(
            versionInfo,
            semanticResult.useCommitFallback ? semanticResult : null
        );
        if (commitResult.targetBranch === versionInfo.currentBranch && commitResult.fallbackUsed !== true) {
            if (!commitUpdatesAllowed) {
                const noReleaseUpdateStatus = commitResult.versionComparisonStatus === "equal"
                    ? "equal"
                    : "main_commit_diff_without_release";
                return applyDirtyBuildMetadata(versionInfo, Object.assign(commitResult, {
                    updateAvailable: semanticResult.updateAvailable === true,
                    releaseUrl: semanticResult.releaseUrl || commitResult.releaseUrl,
                    releaseTitle: semanticResult.releaseTitle || "",
                    selectionReason: semanticResult.updateAvailable === true
                        ? semanticResult.selectionReason
                        : `${commitResult.selectionReason || "local main targets upstream main"}; main branch requires a newer tagged release for update notification`,
                    versionComparisonStatus: semanticResult.updateAvailable === true
                        ? semanticResult.versionComparisonStatus
                        : noReleaseUpdateStatus,
                    localVersionParsed: semanticResult.localVersionParsed || commitResult.localVersionParsed,
                    remoteVersionSelected: semanticResult.remoteVersionSelected || commitResult.remoteVersionSelected
                }));
            }

            debugConsole(
                "debug",
                "Update check using same-branch commit comparison priority over semantic version metadata."
            );
            return applyDirtyBuildMetadata(versionInfo, commitResult);
        }

        if (semanticResult.useCommitFallback) {
            debugConsole("debug", `Update check using commit fallback: ${semanticResult.reason}`);
            return applyDirtyBuildMetadata(versionInfo, commitResult);
        }
    }

    if (!semanticResult.useCommitFallback) {
        return applyDirtyBuildMetadata(versionInfo, semanticResult);
    }

    debugConsole("debug", `Update check using commit fallback: ${semanticResult.reason}`);
    const commitResult = await buildCommitBasedWsprryPiUpdateResult(versionInfo, semanticResult);
    return applyDirtyBuildMetadata(versionInfo, commitResult);
}

function logUpdateCheckWarning(error) {
    const failure = normalizeUpdateCheckFailure(error);
    const detail = failure.detail ? ` ${failure.detail}` : "";
    debugConsole("warn", `${failure.message}${detail}`);
}

function normalizeUpdateCheckFailure(error) {
    if (error?.updateCheckFailed === true) {
        return error;
    }

    const code = typeof error?.code === "string" && error.code
        ? error.code
        : typeof error?.status === "number"
            ? "github_http"
            : "unknown";
    let detail = error && typeof error.message === "string" ? error.message : "";

    if (code === "rate_limited" && error?.rateLimitReset) {
        const resetSeconds = Number(error.rateLimitReset);
        if (Number.isFinite(resetSeconds)) {
            detail += ` Resets at ${new Date(resetSeconds * 1000).toLocaleString()}.`;
        }
    }

    if (error?.status === 404 && code === "github_http") {
        detail = detail || "GitHub returned HTTP 404.";
    }

    return buildUpdateCheckFailure(code, detail);
}

function clearWsprryPiUpdateFooter() {
    const versionElement = document.getElementById("versionText");
    const updateLink = document.getElementById("versionUpdateLink");

    if (versionElement) {
        versionElement.classList.remove("update-available");
        versionElement.classList.remove("update-check-failed");
        if (versionElement.textContent && versionElement.textContent !== "---") {
            versionElement.title = versionElement.textContent;
        } else {
            versionElement.removeAttribute("title");
        }
    }

    if (updateLink) {
        updateLink.classList.add("d-none");
        updateLink.href = UPDATE_CHECK_RELEASES_URL;
        updateLink.title = "An update is available";
        updateLink.setAttribute("aria-label", "An update is available");
    }
}

function buildLocalUpdateStateTitle(result) {
    if (result?.versionComparisonStatus === "local_modified" || result?.localBuildState === "dirty_build") {
        return "Local build has modifications. No remote update is being shown.";
    }

    if (result?.versionComparisonStatus === "local_ahead") {
        return "Local build is newer than the selected remote version. No update is available.";
    }

    if (result?.versionComparisonStatus === "diverged") {
        return "Local and selected branch histories have diverged. No safe branch update is available.";
    }

    return "";
}

let fallbackUpdateCheckDisabled = false;
let lastWsprryPiVersionResponse = null;

function isUpdateCheckDisabled() {
    try {
        return window.localStorage.getItem(UPDATE_CHECK_DISABLED_KEY) === "true";
    } catch {
        return fallbackUpdateCheckDisabled;
    }
}

function setUpdateCheckDisabled(disabled) {
    // Site-global localStorage preference. When enabled, checkForWsprryPiUpdate()
    // returns before any GitHub update-check API calls are made.
    fallbackUpdateCheckDisabled = disabled === true;
    try {
        if (fallbackUpdateCheckDisabled) {
            window.localStorage.setItem(UPDATE_CHECK_DISABLED_KEY, "true");
        } else {
            window.localStorage.removeItem(UPDATE_CHECK_DISABLED_KEY);
        }
    } catch {
    }
    syncUpdateCheckToggle();
}

function updateCheckToggleControls() {
    return Array.from(document.querySelectorAll("#updateCheckToggle, #updateCheckToggleBtn"));
}

function syncUpdateCheckToggle() {
    const toggles = updateCheckToggleControls();
    if (!toggles.length) {
        return;
    }

    const disabled = isUpdateCheckDisabled();
    // Footer About is the user-facing re-enable path after "Never check again".
    for (const toggle of toggles) {
        toggle.textContent = disabled ? "Enable update checks" : "Disable update checks";
        toggle.setAttribute("aria-pressed", disabled ? "true" : "false");
    }
}

function markWsprryPiUpdateChecksDisabled() {
    const versionElement = document.getElementById("versionText");
    const updateLink = document.getElementById("versionUpdateLink");
    const title = "Update checks are disabled.";

    if (versionElement) {
        versionElement.classList.remove("update-available");
        versionElement.classList.remove("update-check-failed");
        versionElement.title = title;
    }

    if (updateLink) {
        updateLink.classList.add("d-none");
        updateLink.href = UPDATE_CHECK_RELEASES_URL;
        updateLink.title = title;
        updateLink.setAttribute("aria-label", title);
    }
}

function initUpdateCheckControls() {
    const toggles = updateCheckToggleControls();
    const checkNowButton = document.getElementById("updateCheckNowBtn");
    syncUpdateCheckToggle();

    for (const toggle of toggles) {
        if (toggle.dataset.updateCheckToggleBound === "true") {
            continue;
        }
        toggle.dataset.updateCheckToggleBound = "true";
        toggle.addEventListener("click", () => {
            const disabled = !isUpdateCheckDisabled();
            setUpdateCheckDisabled(disabled);
            if (disabled) {
                markWsprryPiUpdateChecksDisabled();
                renderUpdateCheckPanelDisabled();
            } else {
                updateWsprryPiVersion();
            }
        });
    }

    if (checkNowButton && checkNowButton.dataset.updateCheckNowBound !== "true") {
        checkNowButton.dataset.updateCheckNowBound = "true";
        checkNowButton.addEventListener("click", forceUpdateCheckNow);
    }

    window.addEventListener("storage", handleUpdateCheckStorageEvent);
}

function updateCheckPanelElements() {
    const panel = document.getElementById("updateCheckPanel");
    if (!panel) {
        return null;
    }

    return {
        panel,
        title: document.getElementById("updateCheckPanelTitle"),
        status: document.getElementById("updateCheckStatus"),
        technical: document.getElementById("updateCheckTechnical"),
        technicalSummary: document.getElementById("updateCheckTechnicalSummary"),
        technicalList: document.getElementById("updateCheckTechnicalList"),
        action: document.getElementById("updateCheckAction"),
        checkNowButton: document.getElementById("updateCheckNowBtn")
    };
}

function updateCheckPanelCurrentText(versionInfo = null) {
    if (!versionInfo) {
        return "Unavailable";
    }

    const branch = versionInfo.currentBranch ? ` (${versionInfo.currentBranch})` : "";
    return `${versionInfo.currentModalVersion || versionInfo.currentDisplayVersion || "Unknown"}${branch}`;
}

function appendUpdateCheckCodeText(parent, value) {
    const code = document.createElement("code");
    code.textContent = value;
    parent.appendChild(code);
}

function appendUpdateCheckLinkText(parent, value) {
    const link = document.createElement("a");
    link.href = value;
    link.target = "_blank";
    link.rel = "noopener";
    link.textContent = value;
    parent.appendChild(link);
}

function buildUpdateCheckTargetParts(result = null) {
    const parts = [];
    if (result?.targetBranch) {
        parts.push({ label: "Branch", value: result.targetBranch });
    }
    if (result?.targetHeadSha) {
        parts.push({ label: "Commit", value: shortSha(result.targetHeadSha) });
    }
    return parts;
}

function updateCheckPanelTargetText(result = null) {
    const parts = buildUpdateCheckTargetParts(result);
    if (parts.length) {
        return parts.map((part) => `${part.label}: ${part.value}`).join(" - ");
    }
    if (!result) {
        return "Not checked";
    }
    if (result.remoteVersionSelected) {
        return result.remoteVersionSelected;
    }
    return "No remote target selected";
}

function updateCheckPanelStatus(result = null) {
    if (!result) {
        return {
            state: "clean",
            label: "No update"
        };
    }
    if (result.updateAvailable === true) {
        return {
            state: "available",
            label: result.versionComparisonUsed === "commit" ? "Branch build available" : "Update available"
        };
    }
    if (result.versionComparisonStatus === "local_modified" || result.localBuildState === "dirty_build") {
        return {
            state: "local",
            label: "Local modified"
        };
    }
    if (result.versionComparisonStatus === "local_ahead") {
        return {
            state: "local",
            label: "Local ahead"
        };
    }
    if (result.versionComparisonStatus === "diverged") {
        return {
            state: "local",
            label: "Diverged"
        };
    }
    return {
        state: "clean",
        label: "No update"
    };
}

function updateCheckPanelTitleText(result = null) {
    if (!result) {
        return "You are on the current version";
    }
    if (result.updateAvailable === true) {
        return result.versionComparisonUsed === "commit"
            ? "A newer branch build is available"
            : "An update is available";
    }
    if (result.versionComparisonStatus === "local_modified" || result.localBuildState === "dirty_build") {
        return "Local build has modifications";
    }
    if (result.versionComparisonStatus === "local_ahead") {
        return "Local build is newer than the latest published version";
    }
    if (result.versionComparisonStatus === "diverged") {
        return "Local and selected branch histories have diverged";
    }
    return "You are on the current version";
}

function updateCheckPanelHasReleaseLink(result = null) {
    return Boolean(
        result?.updateAvailable === true &&
        result.remoteVersionSelected &&
        result.releaseUrl &&
        result.versionComparisonUsed === "semver"
    );
}

function renderUpdateCheckPanelTitle(elements, result = null, overrideText = "") {
    if (!elements?.title) {
        return;
    }

    elements.title.textContent = "";
    if (!updateCheckPanelHasReleaseLink(result)) {
        elements.title.textContent = overrideText || updateCheckPanelTitleText(result);
        return;
    }

    elements.title.appendChild(document.createTextNode("An update is available: "));
    const link = document.createElement("a");
    link.href = result.releaseUrl;
    link.target = "_blank";
    link.rel = "noopener";
    link.textContent = result.remoteVersionSelected;
    link.setAttribute("aria-label", `View release ${result.remoteVersionSelected}`);
    elements.title.appendChild(link);
}

function getUserFacingUpdateSummary(result = null) {
    if (!result) {
        return "You are running the latest version.";
    }
    if (result.updateAvailable === true) {
        return result.versionComparisonUsed === "commit"
            ? "A newer branch build is available."
            : "A newer version is available.";
    }
    if (result.versionComparisonStatus === "local_modified" || result.localBuildState === "dirty_build") {
        return "This build includes local modifications.";
    }
    if (result.versionComparisonStatus === "local_ahead") {
        return "This build is newer than the latest published version.";
    }
    if (result.versionComparisonStatus === "diverged") {
        return "This build and the selected branch have diverged.";
    }
    return "You are running the latest version.";
}

function appendUpdateCheckTechnicalDetail(details, label, value, options = {}) {
    if (value === null || value === undefined || value === "") {
        return;
    }
    const text = String(value).trim();
    if (!text) {
        return;
    }
    details.push(Object.assign({ label, value: text }, options));
}

function appendUpdateCheckTechnicalParts(details, label, parts) {
    if (!Array.isArray(parts) || !parts.length) {
        return;
    }
    const value = parts.map((part) => `${part.label}: ${part.value}`).join(" - ");
    details.push({ label, value, parts });
}

function formatUpdateCheckTitleCase(value) {
    return String(value || "")
        .split(/([\s_-]+)/)
        .map((part) => /^[a-z0-9]+$/i.test(part)
            ? part.charAt(0).toUpperCase() + part.slice(1).toLowerCase()
            : part)
        .join("");
}

function formatUpdateCheckSentence(value) {
    const text = String(value || "").trim();
    if (!text) {
        return "";
    }
    const sentence = text.charAt(0).toUpperCase() + text.slice(1);
    return sentence.endsWith(".") ? sentence : `${sentence}.`;
}

function dedupeUpdateCheckTechnicalDetails(details) {
    const seenValues = new Set();
    return details.filter((detail) => {
        const key = detail.value.toLowerCase();
        if (seenValues.has(key)) {
            return false;
        }
        seenValues.add(key);
        return true;
    });
}

function formatUpdateCheckSemver(value) {
    if (!value) {
        return "";
    }
    if (typeof value === "string") {
        return value;
    }
    if (typeof value.normalized === "string") {
        return value.normalized;
    }
    return "";
}

function buildTechnicalDetails(versionInfo = null, result = null, failure = null) {
    const details = [];
    const normalizedFailure = failure ? normalizeUpdateCheckFailure(failure) : null;

    appendUpdateCheckTechnicalDetail(
        details,
        "Current",
        updateCheckPanelCurrentText(versionInfo),
        { code: true }
    );
    const targetParts = buildUpdateCheckTargetParts(result);
    appendUpdateCheckTechnicalParts(details, "Target", targetParts);
    if (!targetParts.length) {
        appendUpdateCheckTechnicalDetail(
            details,
            "Target",
            normalizedFailure ? "Unknown" : updateCheckPanelTargetText(result),
            { code: true }
        );
    }
    appendUpdateCheckTechnicalDetail(
        details,
        "Summary",
        normalizedFailure ? "Unable to check for updates." : getUserFacingUpdateSummary(result)
    );
    appendUpdateCheckTechnicalDetail(
        details,
        "Branch",
        versionInfo?.currentBranch || result?.currentBranch,
        { code: true }
    );
    appendUpdateCheckTechnicalDetail(details, "Branch state", versionInfo?.branchState);
    appendUpdateCheckTechnicalDetail(
        details,
        "Current SHA",
        versionInfo?.currentSha || result?.currentSha,
        { code: true }
    );
    appendUpdateCheckTechnicalDetail(details, "Target branch", result?.targetBranch);
    appendUpdateCheckTechnicalDetail(details, "Target SHA", result?.targetHeadSha);
    appendUpdateCheckTechnicalDetail(details, "Update URL", result?.releaseUrl, { link: true });
    appendUpdateCheckTechnicalDetail(
        details,
        "Comparison method",
        formatUpdateCheckTitleCase(result?.versionComparisonUsed)
    );
    appendUpdateCheckTechnicalDetail(
        details,
        "Comparison status",
        formatUpdateCheckSentence(result?.versionComparisonStatus)
    );
    appendUpdateCheckTechnicalDetail(
        details,
        "Local version",
        result?.localVersionParsed || formatUpdateCheckSemver(versionInfo?.localVersionParsedObject),
        { code: true }
    );
    appendUpdateCheckTechnicalDetail(details, "Remote version", result?.remoteVersionSelected);
    appendUpdateCheckTechnicalDetail(details, "Selection reason", result?.selectionReason);
    appendUpdateCheckTechnicalDetail(details, "Failure reason", normalizedFailure?.message);
    appendUpdateCheckTechnicalDetail(details, "Failure code", normalizedFailure?.code);
    appendUpdateCheckTechnicalDetail(details, "Failure details", normalizedFailure?.detail);

    return dedupeUpdateCheckTechnicalDetails(details);
}

function updateTechnicalDetailsToggleLabel(elements) {
    if (!elements?.technicalSummary || !elements?.technical) {
        return;
    }
    elements.technicalSummary.textContent = elements.technical.open
        ? "Technical details ▲"
        : "Technical details ▼";
}

function renderUpdateCheckTechnicalDetails(elements, details) {
    if (!elements?.technical || !elements?.technicalList) {
        return;
    }

    if (elements.technical.dataset.updateCheckTechnicalBound !== "true") {
        elements.technical.dataset.updateCheckTechnicalBound = "true";
        elements.technical.addEventListener("toggle", () => {
            updateTechnicalDetailsToggleLabel(elements);
        });
    }

    elements.technical.open = false;
    elements.technicalList.textContent = "";
    updateTechnicalDetailsToggleLabel(elements);

    if (!details.length) {
        elements.technical.classList.add("d-none");
        return;
    }

    details.forEach((detail) => {
        const row = document.createElement("div");
        row.className = "maintenance-fact";

        const term = document.createElement("dt");
        term.textContent = detail.label;
        row.appendChild(term);

        const description = document.createElement("dd");
        if (Array.isArray(detail.parts) && detail.parts.length) {
            detail.parts.forEach((part, index) => {
                if (index > 0) {
                    description.appendChild(document.createTextNode(" - "));
                }
                description.appendChild(document.createTextNode(`${part.label}: `));
                appendUpdateCheckCodeText(description, part.value);
            });
        } else if (detail.code === true) {
            appendUpdateCheckCodeText(description, detail.value);
        } else if (detail.link === true) {
            appendUpdateCheckLinkText(description, detail.value);
        } else {
            description.textContent = detail.value;
        }
        row.appendChild(description);

        elements.technicalList.appendChild(row);
    });

    elements.technical.classList.remove("d-none");
}

function setUpdateCheckPanelAction(result = null) {
    const elements = updateCheckPanelElements();
    if (!elements?.action) {
        return;
    }

    elements.action.textContent = "";
    elements.action.classList.add("d-none");

    const url = result?.releaseUrl || "";
    if (!url || result.updateAvailable !== true) {
        return;
    }

    const link = document.createElement("a");
    link.href = url;
    link.target = "_blank";
    link.rel = "noopener";
    link.className = "btn btn-sm btn-outline-warning";
    link.textContent = result.releaseTitle ? "View update" : "View releases";
    elements.action.appendChild(link);
    elements.action.classList.remove("d-none");
}

function renderUpdateCheckPanel(versionInfo = null, result = null) {
    const elements = updateCheckPanelElements();
    if (!elements) {
        return;
    }

    const status = updateCheckPanelStatus(result);
    elements.status.textContent = status.label;
    elements.status.dataset.state = status.state;
    renderUpdateCheckPanelTitle(elements, result);
    renderUpdateCheckTechnicalDetails(elements, buildTechnicalDetails(versionInfo, result));
    setUpdateCheckPanelAction(result);
    syncUpdateCheckToggle();
}

function renderUpdateCheckPanelFailure(error, versionInfo = null) {
    const elements = updateCheckPanelElements();
    if (!elements) {
        return;
    }

    const failure = normalizeUpdateCheckFailure(error);
    elements.status.textContent = "Check failed";
    elements.status.dataset.state = "failed";
    renderUpdateCheckPanelTitle(elements, null, "Unable to check for updates");
    renderUpdateCheckTechnicalDetails(elements, buildTechnicalDetails(versionInfo, null, failure));
    setUpdateCheckPanelAction(null);
    syncUpdateCheckToggle();
}

function renderUpdateCheckPanelDisabled() {
    const elements = updateCheckPanelElements();
    if (!elements) {
        return;
    }

    elements.status.textContent = "Update checks disabled";
    elements.status.dataset.state = "disabled";
    renderUpdateCheckPanelTitle(elements, null, "Update checks are disabled");
    renderUpdateCheckTechnicalDetails(elements, [
        {
            label: "Current",
            value: "Not checked",
            code: true
        },
        {
            label: "Target",
            value: "Disabled",
            code: true
        },
        {
            label: "Summary",
            value: "Update checks are disabled."
        },
        {
            label: "Re-enable",
            value: "Use Enable update checks here or in About to re-enable GitHub update checks."
        }
    ]);
    setUpdateCheckPanelAction(null);
    syncUpdateCheckToggle();
}

function markWsprryPiLocalUpdateState(result) {
    const versionElement = document.getElementById("versionText");
    const updateLink = document.getElementById("versionUpdateLink");
    const title = buildLocalUpdateStateTitle(result);

    if (!title) {
        clearWsprryPiUpdateFooter();
        return;
    }

    // Local modified and local-ahead builds are successful no-update outcomes,
    // but they should not be indistinguishable from a clean remote match.
    if (versionElement) {
        versionElement.classList.remove("update-available");
        versionElement.classList.remove("update-check-failed");
        versionElement.title = title;
    }

    if (updateLink) {
        updateLink.classList.add("d-none");
        updateLink.href = UPDATE_CHECK_RELEASES_URL;
        updateLink.title = title;
        updateLink.setAttribute("aria-label", title);
    }
}

function markWsprryPiUpdateFooter(result) {
    const versionElement = document.getElementById("versionText");
    const updateLink = document.getElementById("versionUpdateLink");
    const releaseUrl = result.releaseUrl || UPDATE_CHECK_RELEASES_URL;
    const title = result.versionComparisonUsed === "commit"
        ? "A newer branch build is available"
        : "An update is available";

    if (versionElement) {
        versionElement.classList.remove("update-check-failed");
        versionElement.classList.add("update-available");
        versionElement.title = title;
    }

    if (updateLink) {
        updateLink.href = releaseUrl;
        updateLink.title = title;
        updateLink.setAttribute("aria-label", title);
        updateLink.classList.remove("d-none");
    }
}

function markWsprryPiUpdateCheckFailed(error) {
    const versionElement = document.getElementById("versionText");
    const updateLink = document.getElementById("versionUpdateLink");
    const failure = normalizeUpdateCheckFailure(error);
    const title = failure.detail ? `${failure.message} ${failure.detail}` : failure.message;

    if (versionElement) {
        versionElement.classList.remove("update-available");
        versionElement.classList.add("update-check-failed");
        versionElement.title = title;
    }

    if (updateLink) {
        updateLink.href = UPDATE_CHECK_RELEASES_URL;
        updateLink.title = title;
        updateLink.setAttribute("aria-label", failure.message);
        updateLink.classList.remove("d-none");
    }
}

let fallbackUpdateModalState = null;
let activeUpdateModalIdentity = null;

function updateModalIdentity(versionInfo, result) {
    // Modal rate limiting is also site-global; the current page path is not
    // part of this identity.
    return {
        branch: result.targetBranch || "",
        currentSha: versionInfo.currentSha || result.currentSha || "",
        targetSha: result.targetHeadSha || result.remoteVersionSelected || "",
        updateUrl: result.releaseUrl || UPDATE_CHECK_RELEASES_URL
    };
}

function updateModalStateMatches(state, identity) {
    return Boolean(
        state &&
        state.branch === identity.branch &&
        state.currentSha === identity.currentSha &&
        state.targetSha === identity.targetSha &&
        state.updateUrl === identity.updateUrl
    );
}

function readUpdateModalState() {
    try {
        const raw = window.localStorage.getItem(UPDATE_MODAL_STATE_KEY);
        if (!raw) {
            return fallbackUpdateModalState;
        }

        return JSON.parse(raw);
    } catch {
        return fallbackUpdateModalState;
    }
}

function parseUpdateModalState(raw) {
    try {
        return raw ? JSON.parse(raw) : null;
    } catch {
        return null;
    }
}

function writeUpdateModalState(versionInfo, result, reason) {
    const state = Object.assign(updateModalIdentity(versionInfo, result), {
        schemaVersion: UPDATE_CHECK_CACHE_SCHEMA_VERSION,
        lastSeenAt: Date.now(),
        reason: reason || "shown"
    });

    fallbackUpdateModalState = state;

    try {
        window.localStorage.setItem(UPDATE_MODAL_STATE_KEY, JSON.stringify(state));
    } catch {
    }
}

function shouldShowUpdateModal(versionInfo, result) {
    const identity = updateModalIdentity(versionInfo, result);
    const state = readUpdateModalState();
    const lastSeenAt = Number(state?.lastSeenAt || 0);

    if (!updateModalStateMatches(state, identity)) {
        return true;
    }

    if (lastSeenAt > Date.now()) {
        return true;
    }

    return Date.now() - lastSeenAt >= UPDATE_MODAL_RATE_LIMIT_MS;
}

function handleUpdateCheckStorageEvent(event) {
    if (event.key === UPDATE_CHECK_DISABLED_KEY) {
        syncUpdateCheckToggle();
        if (isUpdateCheckDisabled()) {
            markWsprryPiUpdateChecksDisabled();
            renderUpdateCheckPanelDisabled();
            const modalEl = document.getElementById("confirmModal");
            if (modalEl?.dataset.updateCheckActive === "true") {
                bootstrap.Modal.getOrCreateInstance(modalEl).hide();
            }
        } else if (lastWsprryPiVersionResponse) {
            checkForWsprryPiUpdate(lastWsprryPiVersionResponse);
        }
        return;
    }

    if (
        lastWsprryPiVersionResponse &&
        (event.key?.startsWith(`${UPDATE_CHECK_CACHE_PREFIX}:`) ||
            event.key?.startsWith(`${UPDATE_CHECK_FAILURE_CACHE_PREFIX}:`))
    ) {
        const versionInfo = parseWsprryPiVersionResponse(lastWsprryPiVersionResponse);
        if (versionInfo?.ok && event.key === updateCheckCacheKey(versionInfo)) {
            renderUpdateCheckPanel(versionInfo, readUpdateCheckCache(versionInfo));
        } else if (versionInfo?.ok && event.key === updateCheckFailureCacheKey(versionInfo)) {
            renderUpdateCheckPanelFailure(readUpdateCheckFailureCache(versionInfo), versionInfo);
        }
        return;
    }

    if (event.key !== UPDATE_MODAL_STATE_KEY || !activeUpdateModalIdentity) {
        return;
    }

    const state = parseUpdateModalState(event.newValue);
    if (
        updateModalStateMatches(state, activeUpdateModalIdentity) &&
        (state.reason === "dismissed" || state.reason === "opened")
    ) {
        const modalEl = document.getElementById("confirmModal");
        if (modalEl?.dataset.updateCheckActive === "true") {
            bootstrap.Modal.getOrCreateInstance(modalEl).hide();
        }
    }
}

function appendUpdateModalBodyLink(body, result, exactRelease) {
    const link = document.createElement("a");
    link.href = result.releaseUrl || UPDATE_CHECK_RELEASES_URL;
    link.target = "_blank";
    link.rel = "noopener";
    link.textContent = exactRelease && result.releaseTitle
        ? result.releaseTitle
        : "GitHub releases";

    body.appendChild(link);
}

function isTaggedReleaseUpdate(result) {
    return result?.fallbackUsed !== true &&
        Boolean(result?.releaseTitle) &&
        Boolean(result?.remoteVersionSelected);
}

function formatUpdateModalBranchTarget(result) {
    const branch = result?.targetBranch || "update";
    const sha = shortSha(result?.targetHeadSha);
    return sha ? `${branch}+${sha}` : branch;
}

function markUpdateCheckModalActive(modalEl) {
    if (modalEl) {
        modalEl.dataset.updateCheckActive = "true";
    }
}

function clearUpdateCheckModalActive(modalEl) {
    if (modalEl) {
        delete modalEl.dataset.updateCheckActive;
    }
    activeUpdateModalIdentity = null;
}

function releaseUpdateCheckModalOwnership(modalEl) {
    if (!modalEl) {
        return;
    }

    clearUpdateCheckModalActive(modalEl);
    $(modalEl).off("hidden.bs.modal.updateCheck");
}

function showWsprryPiUpdateModal(versionInfo, result) {
    const modalEl = document.getElementById("confirmModal");
    if (!modalEl || !shouldShowUpdateModal(versionInfo, result)) {
        return;
    }

    const confirmModal = bootstrap.Modal.getOrCreateInstance(modalEl, {
        backdrop: "static",
        keyboard: false
    });
    const taggedReleaseUpdate = isTaggedReleaseUpdate(result);
    const releaseMessage = taggedReleaseUpdate
        ? "A release is available for this update: "
        : "Review the update channel given to you for this pre-release version.";

    writeUpdateModalState(versionInfo, result, "shown");
    activeUpdateModalIdentity = updateModalIdentity(versionInfo, result);
    markUpdateCheckModalActive(modalEl);
    document.getElementById("confirmModalLabel").textContent = result.versionComparisonUsed === "commit"
        ? "Newer branch build available"
        : "Update available";

    const body = document.getElementById("confirmModalBody");
    body.classList.remove("confirm-modal-body--preformatted");
    body.textContent = "";

    const summaryText = result.versionComparisonUsed === "semver" && result.remoteVersionSelected
        ? `${result.localVersionParsed || versionInfo.currentModalVersion} is behind release ${result.remoteVersionSelected}.`
        : `${versionInfo.currentModalVersion} is behind ${formatUpdateModalBranchTarget(result)}.`;
    body.appendChild(document.createTextNode(summaryText));
    body.appendChild(document.createElement("br"));
    if (result.fallbackUsed === true) {
        debugConsole("DEBUG", "The current branch is not available upstream. Updates are being checked against " + result.targetBranch + ".");
    }
    body.appendChild(document.createTextNode(releaseMessage));
    if (taggedReleaseUpdate) {
        appendUpdateModalBodyLink(body, result, taggedReleaseUpdate);
        body.appendChild(document.createElement("br"));
    }
    const disableLink = document.createElement("button");
    disableLink.type = "button";
    disableLink.className = "btn btn-link btn-sm update-modal__disable-action mt-2";
    disableLink.textContent = "Never check again (re-enable in About)";
    disableLink.addEventListener("click", () => {
        writeUpdateModalState(versionInfo, result, "dismissed");
        setUpdateCheckDisabled(true);
        markWsprryPiUpdateChecksDisabled();
        confirmModal.hide();
    });
    body.appendChild(disableLink);

    const $cancelBtn = $("#confirmCancelBtn");
    const $confirmBtn = $("#confirmActionBtn");
    $cancelBtn
        .text("Dismiss")
        .removeClass("d-none")
        .off("click")
        .on("click", () => {
            writeUpdateModalState(versionInfo, result, "dismissed");
        });
    $confirmBtn
        .attr("class", "btn btn-primary")
        .toggleClass("d-none", !taggedReleaseUpdate)
        .text("View release")
        .off("click")
        .on("click", () => {
            writeUpdateModalState(versionInfo, result, "opened");
            confirmModal.hide();
            window.open(result.releaseUrl || UPDATE_CHECK_RELEASES_URL, "_blank", "noopener");
        });

    $(modalEl)
        .off("hidden.bs.modal.updateCheck")
        .one("hidden.bs.modal.updateCheck", () => {
            if (modalEl.dataset.updateCheckActive !== "true") {
                return;
            }

            writeUpdateModalState(versionInfo, result, "dismissed");
            clearUpdateCheckModalActive(modalEl);
            resetConfirmationDialogState();
        });

    confirmModal.show();
}

function applyWsprryPiUpdateResult(versionInfo, result, options = {}) {
    if (result) {
        const localStateTitle = buildLocalUpdateStateTitle(result);
        debugConsole(
            "debug",
            `Update check selected targetBranch=${result.targetBranch}, fallbackUsed=${result.fallbackUsed === true}, targetHeadSha=${result.targetHeadSha}, status=${result.versionComparisonStatus || "unspecified"}, reason=${result.selectionReason || "unspecified"}${localStateTitle ? `, displayState=${localStateTitle}` : ""}`
        );
    }

    if (!result || result.updateAvailable !== true) {
        markWsprryPiLocalUpdateState(result);
        return;
    }

    markWsprryPiUpdateFooter(result);
    if (options.suppressModal !== true) {
        showWsprryPiUpdateModal(versionInfo, result);
    }
}

function checkForWsprryPiUpdate(response, options = {}) {
    // Disabled update checks are site-global and persisted in localStorage.
    // The footer About toggle can remove this state and re-enable checks.
    if (isUpdateCheckDisabled()) {
        markWsprryPiUpdateChecksDisabled();
        renderUpdateCheckPanelDisabled();
        debugConsole("debug", "Update checks disabled by user preference.");
        return;
    }

    const versionInfo = parseWsprryPiVersionResponse(response);
    if (!versionInfo?.ok) {
        markWsprryPiUpdateCheckFailed(versionInfo || buildUpdateCheckFailure("malformed_response", "The /version response was not usable."));
        renderUpdateCheckPanelFailure(versionInfo || buildUpdateCheckFailure("malformed_response", "The /version response was not usable."));
        logUpdateCheckWarning(versionInfo || buildUpdateCheckFailure("malformed_response", "The /version response was not usable."));
        return;
    }

    debugConsole(
        "debug",
        `Update check parsed displayBranch=${versionInfo.displayBranch || "(none)"}, rawBranch=${versionInfo.currentBranch}, branchState=${versionInfo.branchState}, currentSha=${versionInfo.currentSha}`
    );

    const cached = options.bypassCache === true ? null : readUpdateCheckCache(versionInfo);
    if (cached) {
        applyWsprryPiUpdateResult(versionInfo, cached, options);
        renderUpdateCheckPanel(versionInfo, cached);
        return;
    }

    const cachedFailure = options.bypassCache === true ? null : readUpdateCheckFailureCache(versionInfo);
    if (cachedFailure) {
        debugConsole(
            "debug",
            `Update check failure rate limit active for ${versionInfo.currentBranch} ${shortSha(versionInfo.currentSha)}: ${cachedFailure.code || "unknown"}`
        );
        markWsprryPiUpdateCheckFailed(cachedFailure);
        renderUpdateCheckPanelFailure(cachedFailure, versionInfo);
        logUpdateCheckWarning(cachedFailure);
        return;
    }

    buildWsprryPiUpdateResult(versionInfo)
        .then((result) => {
            writeUpdateCheckCache(versionInfo, result);
            applyWsprryPiUpdateResult(versionInfo, result, options);
            renderUpdateCheckPanel(versionInfo, result);
        })
        .catch((error) => {
            const failure = writeUpdateCheckFailureCache(versionInfo, error);
            markWsprryPiUpdateCheckFailed(failure);
            renderUpdateCheckPanelFailure(failure, versionInfo);
            logUpdateCheckWarning(failure);
        });
}

function normalizeUiBuildId(value) {
    return typeof value === "string" ? value.trim() : "";
}

function sharedConfirmModalIsVisible() {
    const modalEl = document.getElementById("confirmModal");
    return Boolean(modalEl?.classList.contains("show"));
}

function maybePromptForUiRefresh(versionResponse) {
    const loadedBuildId = normalizeUiBuildId(window.WSPRRYPI_INSTALLED_UI_BUILD_ID);
    const installedBuildId = normalizeUiBuildId(
        typeof versionResponse === "object" && versionResponse !== null
            ? versionResponse.installed_ui_build_id
            : versionResponse
    );

    if (uiRefreshPromptActive && !sharedConfirmModalIsVisible()) {
        uiRefreshPromptActive = false;
    }

    if (
        uiRefreshPromptActive ||
        uiConsistencyDiagnosticActive ||
        !loadedBuildId ||
        !installedBuildId ||
        installedBuildId === loadedBuildId ||
        installedBuildId === dismissedUiRefreshBuildId
    ) {
        return;
    }

    const modalEl = document.getElementById("confirmModal");
    if (!modalEl) {
        debugConsole("warn", "UI refresh prompt deferred because the shared confirmation modal is not available.");
        return;
    }

    const promptShown = showConfirmationDialog({
        title: "UI refresh required",
        message: "The WsprryPi web interface has been updated. Refresh this page to load the new web pages, CSS, and JavaScript.",
        confirmLabel: "Refresh",
        confirmClass: "btn-primary",
        cancelLabel: "Cancel",
        onConfirm: () => {
            refreshUiForIdentity(installedBuildId);
        },
        onCancel: () => {
            dismissedUiRefreshBuildId = installedBuildId;
            uiRefreshPromptActive = false;
        },
        onHidden: () => {
            if (uiRefreshPromptActive) {
                dismissedUiRefreshBuildId = installedBuildId;
                uiRefreshPromptActive = false;
            }
        }
    });

    uiRefreshPromptActive = promptShown === true;
}

function refreshUiForIdentity(installedBuildId) {
    const url = new URL(window.location.href);
    const normalizedBuildId = normalizeUiBuildId(installedBuildId);

    if (!normalizedBuildId) {
        return;
    }

    url.searchParams.set(UI_REFRESH_REQUEST_PARAM, normalizedBuildId);

    window.location.replace(url.toString());
}

function showUiConsistencyDiagnostic(expectedBuildId, loadedBuildId) {
    const diagnostic = document.getElementById("uiConsistencyDiagnostic");
    const message = diagnostic?.querySelector(".ui-consistency-diagnostic__message");

    uiConsistencyDiagnosticActive = true;
    if (!diagnostic || !message) {
        debugConsole("error", "UI consistency could not be confirmed after refresh.");
        return;
    }

    const expected = normalizeUiBuildId(expectedBuildId) || "unavailable";
    const loaded = normalizeUiBuildId(loadedBuildId) || "unavailable";
    message.textContent = `The requested UI identity (${expected}) did not match the page that loaded (${loaded}). The installation may still be changing. Wait for it to finish, then reload this page once.`;
    diagnostic.classList.remove("d-none");
}

function checkUiRefreshConvergence() {
    const url = new URL(window.location.href);
    const expectedBuildId = normalizeUiBuildId(
        url.searchParams.get(UI_REFRESH_REQUEST_PARAM)
    );

    if (!expectedBuildId) {
        return true;
    }

    const loadedBuildId = normalizeUiBuildId(window.WSPRRYPI_INSTALLED_UI_BUILD_ID);
    if (!loadedBuildId || loadedBuildId !== expectedBuildId) {
        showUiConsistencyDiagnostic(expectedBuildId, loadedBuildId);
        return false;
    }

    url.searchParams.delete(UI_REFRESH_REQUEST_PARAM);
    if (window.history?.replaceState) {
        window.history.replaceState(null, "", url.toString());
    }
    return true;
}

function updateClocks() {
    const now = new Date();
    // Format HH:MM:SS
    const pad = (n) => String(n).padStart(2, "0");
    const local = [now.getHours(), now.getMinutes(), now.getSeconds()]
        .map(pad)
        .join(":");
    const utc = [now.getUTCHours(), now.getUTCMinutes(), now.getUTCSeconds()]
        .map(pad)
        .join(":");

    // only write the times themselves
    document.getElementById("localTime").textContent = local;
    document.getElementById("utcTime").textContent = utc;

    // schedule next update right after the next full second
    const delay = 1000 - now.getMilliseconds();
    setTimeout(updateClocks, delay);
}

/**
 * toggleButtonLoading
 * -------------------
 * Show just a spinner in the button without changing its width,
 * then restore original text & width when done.
 *
 * @param {HTMLButtonElement} btn
 * @param {boolean} isLoading
 */
function toggleButtonLoading(btn, isLoading) {
    if (isLoading) {
        // first time only: save original content and width
        if (!btn._origNodes) {
            btn._origNodes = Array.from(btn.childNodes).map((node) => node.cloneNode(true));
            btn.dataset.origWidth = btn.offsetWidth;
        }

        // freeze the width so it doesn't collapse
        btn.style.width = btn.dataset.origWidth + "px";
        btn.disabled = true;

        // show only the spinner
        const spinner = document.createElement("span");
        spinner.className = "spinner-border spinner-border-sm";
        spinner.setAttribute("role", "status");
        spinner.setAttribute("aria-hidden", "true");
        btn.replaceChildren(spinner);
    } else {
        // restore text, unfreeze width, re-enable
        if (btn._origNodes) {
            btn.replaceChildren(...btn._origNodes.map((node) => node.cloneNode(true)));
        }
        btn.style.width = ""; // clear the inline width
        btn.disabled = false;

        // clean up our temporary data
        delete btn._origNodes;
        delete btn.dataset.origWidth;
    }
}

function bindTestToneControls() {
    const $modalEl = $("#testToneModal");
    if (!$modalEl.length) {
        return;
    }

    $("#test_tone").off(".testTone").on("click.testTone", clickTestTone);
    $("#testToneStart").off(".testTone").on("click.testTone", onTestToneStart);
    $("#testToneEnd").off(".testTone").on("click.testTone", onTestToneEnd);
    $("#testToneSourceBand, #testToneSourceCustom")
        .off("change.testTone")
        .on("change.testTone", renderTestToneSelection);
    $("#testToneBand").off("change.testTone").on("change.testTone", renderTestToneSelection);
    $("#testToneFrequencyHz").off("input.testTone").on("input.testTone", renderTestToneSelection);
    $modalEl.off("hidden.bs.modal.testTone").on("hidden.bs.modal.testTone", onTestToneEnd);
}

function isTestToneRuntimeActive() {
    return testToneLifecycleState === TEST_TONE_LIFECYCLE.ACTIVE ||
        testToneLifecycleState === TEST_TONE_LIFECYCLE.END_PENDING ||
        testToneLifecycleState === TEST_TONE_LIFECYCLE.UNKNOWN;
}

function hasActiveManagedTransmissionForTestTone() {
    if (!currentRuntimeStatus || typeof currentRuntimeStatus !== "object") {
        return false;
    }

    const txState =
        typeof currentRuntimeStatus.txState === "string"
            ? currentRuntimeStatus.txState
            : "";
    const eventState =
        typeof currentRuntimeStatus.eventState === "string"
            ? currentRuntimeStatus.eventState
            : "";

    if (isTestToneRuntimeActive()) {
        return false;
    }

    return txState === "transmitting" || eventState === "starting";
}

function hasEnabledManagedTransmissionForTestTone() {
    return !!(
        currentRuntimeConfigStatus &&
        currentRuntimeConfigStatus.transmitEnabled === true
    );
}

function clearUnresolvedTestToneStartContext() {
    unresolvedTestToneStartContext = null;
    testToneStartQuarantinedSocket = null;
    testToneStartQuarantinedConnectionGeneration = 0;
}

function clearPendingTestToneEndTimeout() {
    if (pendingTestToneEndTimeoutHandle) {
        window.clearTimeout(pendingTestToneEndTimeoutHandle);
        pendingTestToneEndTimeoutHandle = null;
    }
}

function markTestToneOutcomeUnknown(message) {
    clearPendingTestToneEndTimeout();
    testToneLifecycleState = TEST_TONE_LIFECYCLE.UNKNOWN;
    setTestToneExecutionResult(message, "warning");
    syncTestToneControlState(true);
}

function clearPendingTestToneStartRequest() {
    const discardUnresolvedContext = arguments[0] !== false &&
        retainingTestToneStartContextForResponse !== true;
    const wasPending = pendingTestToneStartRequest;
    pendingTestToneStartRequest = false;
    if (pendingTestToneStartTimeoutHandle) {
        window.clearTimeout(pendingTestToneStartTimeoutHandle);
        pendingTestToneStartTimeoutHandle = null;
    }
    if (discardUnresolvedContext) {
        clearUnresolvedTestToneStartContext();
    }
    if (wasPending) {
        syncTestToneControlState(isTestToneRuntimeActive());
    }
}

function quarantineTimedOutTestToneStartRequest() {
    const context = unresolvedTestToneStartContext;
    if (!context || context.socket !== ws ||
        context.connectionGeneration !== wsprBandCatalogConnectionGeneration) {
        clearUnresolvedTestToneStartContext();
        return;
    }

    // tone_start has no request ID. Once the timeout leaves its outcome unknown,
    // this socket cannot safely issue another Start until it reconnects or replies.
    testToneStartQuarantinedSocket = context.socket;
    testToneStartQuarantinedConnectionGeneration = context.connectionGeneration;
    testToneLifecycleState = TEST_TONE_LIFECYCLE.UNKNOWN;
    setTestToneExecutionResult(
        "Test Tone start timed out and the controller outcome is unknown. Wait for a response or reconnect before trying again.",
        "warning"
    );
    syncTestToneControlState(true);
}

function isTestToneStartQuarantinedForCurrentSocket() {
    return testToneStartQuarantinedSocket === ws &&
        testToneStartQuarantinedConnectionGeneration === wsprBandCatalogConnectionGeneration;
}

function markPendingTestToneStartRequest() {
    const payload = arguments[0] || null;
    clearPendingTestToneStartRequest();
    pendingTestToneStartRequest = true;
    const frequencySource = payload &&
        Object.values(TEST_TONE_SELECTION_MODES).includes(payload.frequency_source)
        ? payload.frequency_source
        : "";
    unresolvedTestToneStartContext = Object.freeze({
        frequencySource,
        request: payload && typeof payload === "object"
            ? Object.freeze({ ...payload })
            : null,
        socket: ws,
        connectionGeneration: wsprBandCatalogConnectionGeneration,
    });
    if (frequencySource) {
        pendingTestToneStartTimeoutHandle = window.setTimeout(() => {
            clearPendingTestToneStartRequest(false);
            quarantineTimedOutTestToneStartRequest();
        }, TEST_TONE_COMMAND_TIMEOUT_MS);
        return;
    }
    pendingTestToneStartTimeoutHandle = window.setTimeout(() => {
        clearPendingTestToneStartRequest();
    }, TEST_TONE_COMMAND_TIMEOUT_MS);
}

function setTestToneModalActionBusy(busy) {
    const action = pendingTestToneStopDisableAction;
    const button = action && action.button;
    if (!button) {
        return;
    }

    if (busy) {
        toggleButtonLoading(button, true);
    } else {
        toggleButtonLoading(button, false);
    }
}

function finishTestToneStopDisableAction(success, message = "") {
    setTestToneModalActionBusy(false);
    if (pendingTestToneStopDisableAction &&
        pendingTestToneStopDisableAction.timeoutHandle) {
        window.clearTimeout(pendingTestToneStopDisableAction.timeoutHandle);
    }
    pendingTestToneStopDisableAction = null;

    if (success) {
        if (typeof setTransmitFromBackend === "function") {
            setTransmitFromBackend(false);
        } else {
            $("#transmit").prop("checked", false);
        }
        updateRuntimeControlConfigStatus(null, false);
        if (typeof getTxState === "function") {
            getTxState();
        }
        return;
    }

    const failureMessage = message || "The controller could not stop or disable scheduled transmissions.";
    if (typeof showMessageDialog === "function") {
        showMessageDialog({
            title: "Unable to disable transmissions",
            message: failureMessage,
            acknowledgeLabel: "Close",
            confirmClass: "btn-danger",
        });
    } else {
        debugConsole("error", failureMessage);
    }
}

function testToneRuntimeConnectionUnavailableMessage() {
    if (navigator.onLine === false) {
        return "This browser is offline, so runtime controls cannot reach the controller.";
    }

    return "The controller connection is unavailable right now, so the action could not be completed.";
}

function testToneTransientRuntimeActionMessage(textStatus = "") {
    const normalizedStatus = typeof textStatus === "string"
        ? textStatus.trim().toLowerCase()
        : "";

    if (navigator.onLine === false) {
        return testToneRuntimeConnectionUnavailableMessage();
    }

    if (normalizedStatus === "timeout") {
        return "The controller did not respond before the action timed out. Check connectivity and try again.";
    }

    return testToneRuntimeConnectionUnavailableMessage();
}

function isTestToneTransientNetworkFailure(xhr, textStatus = "") {
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

function handleTestToneStopDisableResponse(message) {
    if (!pendingTestToneStopDisableAction ||
        pendingTestToneStopDisableAction.reason !== "active") {
        return;
    }

    const response = message && typeof message === "object" ? message : {};
    const stopSucceeded =
        response.transmit_disabled === true || response.stop_performed === true;
    const responseMessage =
        typeof response.message === "string" && response.message.trim()
            ? response.message.trim()
            : "";

    finishTestToneStopDisableAction(stopSucceeded, responseMessage);
}

function requestTestToneTransmitDisable() {
    return ajaxWithEndpointFallback(SETTINGS_ENDPOINT, {
        type: "PATCH",
        contentType: "application/merge-patch+json",
        timeout: TEST_TONE_COMMAND_TIMEOUT_MS,
        data: JSON.stringify({
            Operation: {
                "Transmit": false,
            },
        }),
    });
}

function disableScheduledTransmissionsForTestTone(reason, actionButton = null) {
    if (pendingTestToneStopDisableAction) {
        return;
    }

    pendingTestToneStopDisableAction = {
        reason,
        button: actionButton,
    };
    setTestToneModalActionBusy(true);

    if (reason === "active") {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            finishTestToneStopDisableAction(false, testToneRuntimeConnectionUnavailableMessage());
            return;
        }

        pendingTestToneStopDisableAction.timeoutHandle = window.setTimeout(() => {
            finishTestToneStopDisableAction(
                false,
                "Stop command timed out before the controller confirmed it. Check controller connectivity and runtime state, then try again."
            );
        }, TEST_TONE_COMMAND_TIMEOUT_MS);
        ws.send(
            JSON.stringify({
                command: "stop",
                persist_transmit: true,
            })
        );
        return;
    }

    requestTestToneTransmitDisable()
        .done(function () {
            finishTestToneStopDisableAction(true);
        })
        .fail(function (xhr, textStatus) {
            let message = "Failed to disable scheduled transmissions.";

            if (isTestToneTransientNetworkFailure(xhr, textStatus)) {
                message = testToneTransientRuntimeActionMessage(textStatus);
            } else if (xhr && xhr.responseJSON && typeof xhr.responseJSON.message === "string") {
                message = xhr.responseJSON.message.trim() || message;
            }

            finishTestToneStopDisableAction(false, message);
        });
}

function canStartTestTone() {
    return !!(
        ws &&
        ws.readyState === WebSocket.OPEN &&
        websocketCurrentlyConnected === true &&
        wsprBandCatalogAuthorized === true &&
        wsprBandCatalogPendingRequest === null &&
        pendingTestToneStartRequest === false &&
        !isTestToneStartQuarantinedForCurrentSocket() &&
        currentTestToneSelection &&
        currentTestToneSelection.valid === true &&
        currentTestToneSelection.payload &&
        !hasActiveManagedTransmissionForTestTone() &&
        !hasEnabledManagedTransmissionForTestTone()
    );
}

function syncTestToneControlState(toneActive) {
    const shouldEnableStart = toneActive !== true && canStartTestTone();
    $("#testToneStart").prop("disabled", !shouldEnableStart);
    $("#testToneEnd").prop("disabled", toneActive !== true);
    $("#testToneClose").prop("disabled", false);
}

function showTestToneBlockedModal(reason = "active") {
    const blockedByActive = reason === "active";
    const title = blockedByActive
        ? "Stop and disable transmissions"
        : "Disable transmissions";
    const message =
        "Test tones require scheduled transmissions to be stopped and disabled first.";
    const confirmLabel = blockedByActive ? "Stop and Disable" : "Disable";

    if (typeof showModeChangeGuardModal === "function") {
        showModeChangeGuardModal({
            title,
            message,
            confirmLabel,
            confirmClass: "btn btn-primary",
            cancelLabel: "Cancel",
            onConfirm() {
                const modal = typeof modeChangeGuardModalInstance === "function"
                    ? modeChangeGuardModalInstance()
                    : null;
                if (modal) {
                    modal.hide();
                }
                disableScheduledTransmissionsForTestTone(
                    reason,
                    null
                );
            },
            onCancel() {
            },
        });
        return;
    }

    if (typeof showMessageDialog === "function") {
        showConfirmationDialog({
            title,
            message,
            confirmLabel,
            confirmClass: "btn-primary",
            cancelLabel: "Cancel",
            onConfirm(event) {
                disableScheduledTransmissionsForTestTone(
                    reason,
                    event && event.currentTarget instanceof HTMLElement
                        ? event.currentTarget
                        : null
                );
            },
            onCancel() {
            },
        });
    }
}

function clickTestTone(e) {
    e.preventDefault();
    const btn = this;
    toggleButtonLoading(btn, true);
    setTimeout(() => {
        toggleButtonLoading(btn, false);
    }, 500);
    initializeTestToneSelectionControls();
    updateTestToneFrequencyContext();
    const modalEl = document.getElementById("testToneModal");
    const modal = new bootstrap.Modal(modalEl);
    modal.show();
}

function onTestToneStart(e) {
    e.preventDefault();
    renderTestToneSelection();
    if (!canStartTestTone()) {
        syncTestToneControlState(false);
        return;
    }
    if (hasActiveManagedTransmissionForTestTone()) {
        showTestToneBlockedModal("active");
        syncTestToneControlState(false);
        return;
    }
    if (hasEnabledManagedTransmissionForTestTone()) {
        showTestToneBlockedModal("enabled");
        syncTestToneControlState(false);
        return;
    }

    const btn = this;
    clearTestToneExecutionResult();
    testToneLifecycleState = TEST_TONE_LIFECYCLE.START_PENDING;
    toggleButtonLoading(btn, true);
    syncTestToneControlState(false);
    $("#testToneStart").prop("disabled", true);
    $("#testToneEnd").prop("disabled", true);
    const toneStartPayload = {
        command: "tone_start",
        ...testToneFrequencyOverridePayload()
    };
    markPendingTestToneStartRequest(toneStartPayload);

    // Retain the existing shared-sender false-return path for the narrow race
    // where the socket closes after Start gating but before the local send
    // boundary below. It cannot transmit because the socket is already closed.
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        try {
            if (!sendCommand(toneStartPayload)) {
                testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
                clearPendingTestToneStartRequest();
                toggleButtonLoading(btn, false);
                setTestToneExecutionResult(
                    "Test Tone start could not be sent. Check the controller connection and try again.",
                    "danger"
                );
                syncTestToneControlState(false);
                return;
            }
        } catch (error) {
            testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
            clearPendingTestToneStartRequest();
            toggleButtonLoading(btn, false);
            setTestToneExecutionResult(
                "Test Tone start could not be sent. Check the controller connection and try again.",
                "danger"
            );
            syncTestToneControlState(false);
            return;
        }
    }

    const sent = sendTestToneStartPayload(toneStartPayload);
    if (!sent.accepted) {
        testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
        debugConsole("warn", "Test Tone start could not be sent:", sent.error);
        clearPendingTestToneStartRequest();
        toggleButtonLoading(btn, false);
        setTestToneExecutionResult(
            "Test Tone start could not be sent. Check the controller connection and try again.",
            "danger"
        );
        syncTestToneControlState(false);
        return;
    }

    try {
        debugConsole("debug", "Test tone start.", sent.json);
    } catch (error) {
        // Sending already succeeded. Diagnostic failures must not relabel or
        // clear this pending controller request.
    }
}

/**
 * Send a Test Tone Start request with an explicit acceptance boundary.
 *
 * Serialization and WebSocket.send() are the only operations that can make a
 * Start request definitely unsent. Once send() returns, callers must retain
 * their pending request context even if later diagnostics fail.
 */
function sendTestToneStartPayload(payload) {
    let json;
    try {
        json = JSON.stringify(payload);
    } catch (error) {
        return { accepted: false, error };
    }

    const socket = ws;
    if (!socket || socket.readyState !== WebSocket.OPEN) {
        return { accepted: false };
    }

    try {
        socket.send(json);
    } catch (error) {
        return { accepted: false, error };
    }

    return { accepted: true, json };
}

function onTestToneEnd(e) {
    e.preventDefault();
    if (!isTestToneRuntimeActive()) {
        syncTestToneControlState(false);
        return;
    }

    const btn = this;
    clearPendingTestToneEndTimeout();
    testToneLifecycleState = TEST_TONE_LIFECYCLE.END_PENDING;
    toggleButtonLoading(btn, true);
    $("#testToneStart").prop("disabled", true);
    $("#testToneEnd").prop("disabled", true);
    debugConsole("debug", "Test tone end.");
    if (!sendCommand("tone_end")) {
        testToneLifecycleState = TEST_TONE_LIFECYCLE.ACTIVE;
        toggleButtonLoading(btn, false);
        setTestToneExecutionResult(
            "Test Tone End could not be sent. The tone may still be active; try End again.",
            "danger"
        );
        syncTestToneControlState(true);
        return;
    }
    pendingTestToneEndTimeoutHandle = window.setTimeout(() => {
        toggleButtonLoading(btn, false);
        markTestToneOutcomeUnknown(
            "Test Tone End timed out and the controller outcome is unknown. End remains available for recovery."
        );
    }, TEST_TONE_COMMAND_TIMEOUT_MS);
}

function handleTestToneCommandResponse(message) {
    const responseSocket = arguments[1] || ws;
    if (responseSocket !== ws) {
        return;
    }
    const response = message && typeof message === "object" ? message : {};
    const command = typeof response.command === "string" ? response.command : "";
    const startButton = document.getElementById("testToneStart");
    const endButton = document.getElementById("testToneEnd");

    if (startButton) {
        toggleButtonLoading(startButton, false);
    }
    if (endButton) {
        toggleButtonLoading(endButton, false);
    }

    if (command === "tone_start") {
        const locallyRequested = pendingTestToneStartRequest === true;
        const pendingStartContext = unresolvedTestToneStartContext &&
            unresolvedTestToneStartContext.socket === responseSocket &&
            unresolvedTestToneStartContext.connectionGeneration === wsprBandCatalogConnectionGeneration
            ? unresolvedTestToneStartContext
            : null;
        retainingTestToneStartContextForResponse = true;
        clearPendingTestToneStartRequest();
        if (response.started === true) {
            testToneLifecycleState = TEST_TONE_LIFECYCLE.ACTIVE;
            syncTestToneControlState(true);
            const committed = committedTestToneExecutionText(
                response,
                pendingStartContext?.frequencySource,
                pendingStartContext?.request
            );
            if (committed.applicable) {
                setTestToneExecutionResult(
                    committed.valid
                        ? committed.text
                        : "Test Tone started, but committed execution details were unavailable or invalid.",
                    committed.valid ? "success" : "warning"
                );
            }
        } else {
            testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
            syncTestToneControlState(false);
            const rejectionMessage = typeof response.message === "string" && response.message.trim()
                ? response.message.trim()
                : "Test Tone start was rejected by the controller.";
            setTestToneExecutionResult(rejectionMessage, "danger");
            if (locallyRequested && response.blocked_by_active_transmission === true) {
                showTestToneBlockedModal("active");
            } else if (locallyRequested && response.blocked_by_enabled_transmission === true) {
                showTestToneBlockedModal("enabled");
            } else if (
                !locallyRequested &&
                (
                    response.blocked_by_active_transmission === true ||
                    response.blocked_by_enabled_transmission === true
                )
            ) {
                debugConsole("debug", "Passive test tone start rejection received:", response);
            } else {
                debugConsole("error", "Test tone start rejected:", response);
            }
        }
        clearUnresolvedTestToneStartContext();
        retainingTestToneStartContextForResponse = false;
    } else if (command === "tone_end") {
        clearPendingTestToneEndTimeout();
        if (response.stopped === true) {
            testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
            if (isTestToneStartQuarantinedForCurrentSocket()) {
                clearUnresolvedTestToneStartContext();
            }
            currentRuntimeStatus = null;
            setTestToneExecutionResult("Test Tone ended.", "success");
            syncTestToneControlState(false);
        } else {
            testToneLifecycleState = TEST_TONE_LIFECYCLE.ACTIVE;
            const rejectionMessage = typeof response.message === "string" && response.message.trim()
                ? response.message.trim()
                : "Test Tone End was not confirmed. The tone may still be active.";
            setTestToneExecutionResult(rejectionMessage, "danger");
            syncTestToneControlState(true);
            debugConsole("error", "Test tone stop rejected:", response);
        }
    }

    if (typeof getTxState === "function") {
        getTxState();
    }
}


/**
 * Send a JSON “command” message over the WebSocket.
 *
 * @param {any} payload
 *   Anything serializable — e.g. a string, object, number, etc.
 */
function sendCommand(payload) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const msg =
            payload && typeof payload === "object" && !Array.isArray(payload)
                ? payload
                : { command: payload };
        const json = JSON.stringify(msg);
        ws.send(json);
        debugConsole("debug", "WebSocket ▶️ command sent:", json);
        return true;
    } else {
        debugConsole("warn", "WebSocket not open; cannot send command:", payload);
        return false;
    }
}

// Show the system modal for reboot
function handleRebootClick() {
    showSystemModal("reboot");
}

// Show the system modal for shutdown
function handleShutdownClick() {
    showSystemModal("shutdown");
}

// Reload the page
function handleSystemReload() {
    location.reload();
}

// When the system modal finishes hiding
function handleSystemModalHidden() {
    systemPaused = false;
    connectWebSocket(WEBSOCKET_ENDPOINT, WS_RECONNECT);
    schedulePopulateConfigRetry(null, 10000);
}

/**
 * showSystemModal
 * ----------------
 * Shows the “shutdown” or “reboot” modal.
 * - On shutdown: hides Reload button; Exit/X closes the tab.
 * - On reboot: shows Reload button, disabled until WS reconnects; Exit/X hides modal and restarts services.
 *
 * @param {'shutdown'|'reboot'} action
 * @param {boolean} [pause=true]
 */
function showSystemModal(action, pause = true) {
    const msgs = {
        shutdown: "System shutdown has been initiated.",
        reboot: "System reboot has been initiated.",
    };
    const message = msgs[action] || "Action initiated.";

    if (pause) systemPaused = true;
    $("#systemModalBody").text(message);

    const modalEl = document.getElementById("systemModal");
    const sysModal = bootstrap.Modal.getOrCreateInstance(modalEl, {
        backdrop: "static",
        keyboard: !pause,
    });

    const $reloadBtn = $(modalEl).find(".reload-btn");

    if (action === "shutdown") {
        $reloadBtn.hide();
    } else {
        $reloadBtn
            .show()
            .prop("disabled", true) // start disabled
            .off("click")
            .on("click", (e) => {
                e.preventDefault();
                location.reload();
            });
    }

    // Exit button handler
    $(modalEl)
        .off("click", ".exit-btn")
        .on("click", ".exit-btn", () => {
            if (action === "shutdown") {
                window.close();
            } else {
                sysModal.hide();
            }
        });

    // X (hidden) handler
    $(modalEl)
        .off("hidden.bs.modal")
        .on("hidden.bs.modal", () => {
            if (action === "shutdown") {
                window.close();
            } else {
                systemPaused = false;
                connectWebSocket(WEBSOCKET_ENDPOINT, WS_RECONNECT);
                schedulePopulateConfigRetry(null, 10000);
            }
        });

    sysModal.show();
}

// Show the “Are you sure?” question
function openConfirmModal(action, confirmModal) {
    showConfirmationDialog({
        title: "Please Confirm",
        message: action === "reboot"
            ? "Are you sure you want to reboot the system?"
            : "Are you sure you want to shut down the system?",
        confirmLabel: "Yes, proceed",
        confirmClass: "btn-danger",
        onConfirm: () => {
            pendingSystemAction = action;
            if (action === "reboot") {
                showSystemModal("reboot", false);
            } else {
                showSystemModal("shutdown", true);
            }
            sendCommand(action);
        }
    }, confirmModal);
}

function resetConfirmationDialogState() {
    const label = document.getElementById("confirmModalLabel");
    const body = document.getElementById("confirmModalBody");
    const $cancelBtn = $("#confirmCancelBtn");
    const $confirmBtn = $("#confirmActionBtn");

    if (label) {
        label.textContent = "Please Confirm";
    }

    if (body) {
        body.textContent = "";
        body.classList.remove("confirm-modal-body--preformatted");
    }

    $cancelBtn
        .text("Cancel")
        .removeClass("d-none")
        .off("click");
    $confirmBtn
        .attr("class", "btn btn-danger")
        .text("Yes, proceed")
        .off("click");
}

function showConfirmationDialog(options = {}, modalInstance = null) {
    const modalEl = document.getElementById("confirmModal");
    if (!modalEl) {
        if (typeof options.onConfirm === "function") {
            options.onConfirm();
        }
        return false;
    }

    const confirmModal = modalInstance || bootstrap.Modal.getOrCreateInstance(modalEl, {
        backdrop: "static",
        keyboard: false
    });
    const title = typeof options.title === "string" && options.title.trim()
        ? options.title.trim()
        : "Please Confirm";
    const message = typeof options.message === "string" ? options.message : "";
    const preserveLineBreaks = options.preserveLineBreaks === true;
    const confirmLabel = typeof options.confirmLabel === "string" && options.confirmLabel.trim()
        ? options.confirmLabel.trim()
        : "Continue";
    const cancelLabel = typeof options.cancelLabel === "string" && options.cancelLabel.trim()
        ? options.cancelLabel.trim()
        : "Cancel";
    const confirmClass = typeof options.confirmClass === "string" && options.confirmClass.trim()
        ? options.confirmClass.trim()
        : "btn-primary";
    const showCancel = options.showCancel !== false;

    releaseUpdateCheckModalOwnership(modalEl);
    document.getElementById("confirmModalLabel").textContent = title;
    const confirmModalBody = document.getElementById("confirmModalBody");
    confirmModalBody.textContent = message;
    confirmModalBody.classList.toggle(
        "confirm-modal-body--preformatted",
        preserveLineBreaks
    );

    const $cancelBtn = $("#confirmCancelBtn");
    const $confirmBtn = $("#confirmActionBtn");
    $cancelBtn
        .text(cancelLabel)
        .toggleClass("d-none", !showCancel)
        .off("click")
        .on("click", () => {
            if (typeof options.onCancel === "function") {
                options.onCancel();
            }
        });
    $confirmBtn
        .attr("class", `btn ${confirmClass}`)
        .text(confirmLabel)
        .off("click")
        .on("click", () => {
            confirmModal.hide();
            if (typeof options.onConfirm === "function") {
                options.onConfirm();
            }
        });

    $(modalEl)
        .off("hidden.bs.modal.confirmation")
        .one("hidden.bs.modal.confirmation", () => {
            if (typeof options.onHidden === "function") {
                options.onHidden();
            }
        });

    try {
        confirmModal.show();
        return true;
    } catch (error) {
        debugConsole(
            "warn",
            `Unable to show confirmation modal: ${error && typeof error.message === "string" ? error.message : "unknown error"}`
        );
        return false;
    }
}

function showMessageDialog(options = {}) {
    showConfirmationDialog({
        title: options.title || "Notice",
        message: options.message || "",
        confirmLabel: options.acknowledgeLabel || "OK",
        confirmClass: options.confirmClass || "btn-primary",
        preserveLineBreaks: options.preserveLineBreaks === true,
        showCancel: false,
        onConfirm: options.onConfirm
    });
}

// This bridge is intentionally installed only by the focused Node harness. It
// closes over the live script state so tests exercise the production functions
// without recreating a parallel Test Tone state model.
function installWsprryPiTestHooks() {
    if (typeof globalThis === "undefined") {
        return;
    }

    const hooks = globalThis.WSPRRYPI_TEST_HOOKS;
    if (!hooks || hooks.enabled !== true) {
        return;
    }

    const functions = Object.freeze({
        createOperationConfigSnapshot,
        parseConfiguredWsprFrequencyHz,
        validateWsprBandCatalog,
        createTestToneSelection,
        createTestToneSelectionPreview,
        updateWsprBandCatalog,
        requestWsprBandCatalog,
        connectWebSocket,
        initializeTestToneSelectionControls,
        renderTestToneSelection,
        updateTestToneConfigContext,
        testToneDefaultTransmitFrequencyHz,
        testToneFrequencyContextText,
        clickTestTone,
        onTestToneStart,
        onTestToneEnd,
        handleTestToneCommandResponse,
        bindTestToneControls,
        syncTestToneControlState,
        clearPendingTestToneStartRequest,
        markPendingTestToneStartRequest,
        clearTestToneExecutionResult,
        getTxState,
        setConnectionState,
        syncConnectionAlert,
        armOutageBannerIfReady,
        reloadAllData,
        toggleButtonLoading,
        debugConsole,
        showTestToneBlockedModal,
    });

    const inspect = () => Object.freeze({
        catalog: Object.freeze({
            authorized: wsprBandCatalogAuthorized,
            pending: wsprBandCatalogPendingRequest !== null,
            message: wsprBandCatalogStatusMessage,
            offset: wsprAudioOffsetHz,
            dialFrequenciesHz: Object.freeze({ ...wsprBandDialFrequenciesHz }),
            connectionGeneration: wsprBandCatalogConnectionGeneration,
            requestGeneration: wsprBandCatalogRequestGeneration,
            pendingRequestGeneration: wsprBandCatalogPendingRequest?.requestGeneration ?? null,
            timeoutHandle: wsprBandCatalogPendingRequest?.timeoutHandle ?? null,
        }),
        testToneStart: Object.freeze({
            pending: pendingTestToneStartRequest,
            source: unresolvedTestToneStartContext?.frequencySource || "",
            hasUnresolvedContext: unresolvedTestToneStartContext !== null,
            quarantined: ws !== null && isTestToneStartQuarantinedForCurrentSocket(),
            timeoutHandle: pendingTestToneStartTimeoutHandle,
        }),
        testToneLifecycle: Object.freeze({
            state: testToneLifecycleState,
            endTimeoutHandle: pendingTestToneEndTimeoutHandle,
        }),
        selection: currentTestToneSelection,
    });

    const reset = () => {
        clearPendingTestToneStartRequest();
        clearPendingTestToneEndTimeout();
        clearWebSocketReconnectTimer();
        clearWsprBandCatalogPendingRequest();
        if (pendingTestToneStopDisableAction?.timeoutHandle) {
            window.clearTimeout(pendingTestToneStopDisableAction.timeoutHandle);
        }
        pendingTestToneStopDisableAction = null;
        testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
        CONSOLE_LOG_LEVEL = "log";
        ws = null;
        websocketCurrentlyConnected = false;
        currentRuntimeStatus = null;
        currentRuntimeConfigStatus = { mode: "", transmitEnabled: false };
        currentTestToneConfigContext = {
            mode: "",
            configuredFrequencyHz: 0,
            wsprFrequencyValue: "",
            cwBaseFrequencyHz: 0,
        };
        currentTestToneSelection = invalidTestToneSelection(
            "Select a Test Tone frequency source."
        );
        wsprBandDialFrequenciesHz = Object.create(null);
        wsprAudioOffsetHz = 0;
        wsprBandCatalogConnectionGeneration = 0;
        wsprBandCatalogRequestGeneration = 0;
        wsprBandCatalogRequestedSockets = new WeakSet();
        wsprBandCatalogPendingRequest = null;
        wsprBandCatalogAuthorized = false;
        wsprBandCatalogStatusMessage =
            "WSPR band catalog unavailable. Test Tone Start is disabled.";
        retainingTestToneStartContextForResponse = false;
    };

    Object.defineProperty(hooks, "bridge", {
        value: Object.freeze({
            functions,
            inspect,
            reset,
            setRuntimeInterlocks(active, enabled) {
                currentRuntimeStatus = active ? { txState: "transmitting" } : null;
                currentRuntimeConfigStatus = {
                    mode: "WSPR",
                    transmitEnabled: enabled === true,
                };
            },
            setConfiguration(mode, wsprFrequencyValue, cwBaseFrequencyHz) {
                updateTestToneConfigContext(mode, wsprFrequencyValue, cwBaseFrequencyHz);
            },
            clearConnectionRecoveryState() {
                communicationInterrupted = false;
                reloadAfterReconnectPending = false;
            },
            setConsoleLogLevelForTest(level) {
                CONSOLE_LOG_LEVEL = typeof level === "string" ? level : "log";
            },
            hasCurrentSocket() {
                return ws !== null && ws !== undefined;
            },
            currentSocketReadyState() {
                return ws && typeof ws.readyState === "number"
                    ? ws.readyState
                    : null;
            },
        }),
        enumerable: false,
        configurable: false,
        writable: false,
    });
}

installWsprryPiTestHooks();

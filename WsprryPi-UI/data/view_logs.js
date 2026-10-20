/**
 * view_logs.js
 * ------------
 * Log viewer UI scripting for Server-Sent Events from log_stream.php.
 *
 * NOTE ABOUT EXTERNAL INCLUDES:
 * This file is designed to work when loaded as an external <script src="...">.
 * It does not rely on page-inline globals such as PROTO/HOSTNAME/CURRENT_PATH,
 * and it does not require jQuery. It derives the stream URL from
 * window.location by default.
 *
 * Expects log_stream.php to emit SSE `data:` lines containing JSON objects in the
 * unified schema described in the user's pseudo-OpenAPI summary:
 * - type: "journal" | "internal"
 * - playback: bool
 * - __CURSOR: string|null
 * - __REALTIME_TIMESTAMP: string|null (microseconds since epoch)
 * - PRIORITY: "0".."7" (string) or null
 * - SYSLOG_IDENTIFIER, MESSAGE, _SYSTEMD_UNIT, HOSTNAME, PID, UID, GID
 *
 * Internal adapter events are shown in the "internal" pane.
 * Journal entries render to the "all" pane.
 */

(function () {
    "use strict";

    const MAX_LINES = 8000;
    const MIN_VALID_JOURNAL_TIMESTAMP_US = 946684800000000; // 2000-01-01T00:00:00.000Z
    function setConnectButton(state) {
        const btn = document.getElementById("btn-reconnect");
        if (!btn) return;

        // Connected state shows a manual reconnect action; otherwise start the stream.
        const isConnected = (state === "connected");
        const isConnecting = (state === "reconnecting");
        btn.textContent = isConnected
            ? "Refresh stream"
            : isConnecting
                ? "Connecting…"
                : "Start stream";
        btn.disabled = isConnecting;
        btn.setAttribute("aria-disabled", isConnecting ? "true" : "false");

        const retry = document.getElementById("logsRetryButton");
        if (retry) {
            retry.textContent = isConnecting
                ? "Connecting…"
                : isConnected
                    ? "Refresh stream"
                    : retry.dataset.idleLabel || "Start stream";
            retry.disabled = isConnecting;
            retry.setAttribute("aria-disabled", isConnecting ? "true" : "false");
        }

        // Green when disconnected, blue when connected.
        btn.classList.remove("btn-outline-success", "btn-outline-primary");
        btn.classList.add(isConnected ? "btn-outline-primary" : "btn-outline-success");
    }

// Maximum number of log lines retained per tab/container

    function setSseStatus(state, text, title) {
        const el = document.getElementById("sse-status-badge");
        if (!el) return;

        el.classList.remove("sse-connected", "sse-reconnecting", "sse-disconnected");

        if (state === "connected") el.classList.add("sse-connected");
        else if (state === "reconnecting") el.classList.add("sse-reconnecting");
        else el.classList.add("sse-disconnected");

        if (text && String(text).trim() !== "") el.textContent = text;
        if (title && String(title).trim() !== "") el.title = title;

        const scrollRegion = document.getElementById("log-scroll");
        if (scrollRegion) {
            scrollRegion.setAttribute("aria-busy", state === "reconnecting" ? "true" : "false");
        }

        setConnectButton(state);
    }

    function formatDelay(delayMs) {
        const s = Math.max(0, Math.round(delayMs / 100) / 10);
        return s.toFixed(s < 10 ? 1 : 0) + "s";
    }

    let hiddenBuffer = []; // Buffered log entries while the tab is not visible.
    let hiddenBufferedCount = 0; // Number of buffered entries (for UI hint).

    let autoFollow = true; // True when the user is at (or near) the bottom.
    let softScrollEnabled = true; // Soft-scroll animation when following the tail.
    let inPlayback = false; // Deterministic catch-up mode signaled by SSE events.

    // Hysteresis prevents the follow-state from flapping when we're near the bottom,
    // and scroll UI updates are debounced to avoid flicker during wheel/trackpad
    // scrolls and programmatic smooth-follow animation.
    const FOLLOW_RESUME_PX = 24; // Resume following when within this many pixels of bottom.
    const FOLLOW_BREAK_PX = 120; // Stop following only after the user scrolls up past this.
    const JUMP_HIDE_DELAY_MS = 180; // Delay hiding the jump button to avoid flicker.

    let programmaticScroll = false; // True while our own code is moving scrollTop.
    let scrollUiTimer = null; // Debounce timer for scroll UI updates.
    let jumpHideTimer = null; // Delayed hide timer for the jump button.

    let scrollAnimHandle = null;
    let scrollTarget = 0;

    function cancelScrollAnimation() {
        if (scrollAnimHandle !== null) {
            cancelAnimationFrame(scrollAnimHandle);
            scrollAnimHandle = null;
        }
        programmaticScroll = false;
    }

    function isNearBottom(el, thresholdPx = 24) {
        if (!el) return true;
        const remaining = el.scrollHeight - el.clientHeight - el.scrollTop;
        return remaining <= thresholdPx;
    }

    function remainingToBottomPx(el) {
        if (!el) return 0;
        return Math.max(0, el.scrollHeight - el.clientHeight - el.scrollTop);
    }

    // Update autoFollow using hysteresis so it does not flap near the threshold.
    function updateAutoFollow(el) {
        const remaining = remainingToBottomPx(el);

        if (autoFollow) {
            if (remaining > FOLLOW_BREAK_PX) {
                autoFollow = false;
            }
        } else {
            if (remaining <= FOLLOW_RESUME_PX) {
                autoFollow = true;
            }
        }

        return remaining;
    }

    // Debounce UI changes tied to scroll events so the jump button does not flicker.
    function scheduleScrollUiUpdate(fn) {
        if (scrollUiTimer) {
            clearTimeout(scrollUiTimer);
            scrollUiTimer = null;
        }
        scrollUiTimer = setTimeout(() => {
            scrollUiTimer = null;
            fn();
        }, 120);
    }

    function prefersReducedMotion() {
        return window.matchMedia?.("(prefers-reduced-motion: reduce)").matches === true;
    }

    function animateScrollTo(el, targetTop) {
        if (!el) return;

        scrollTarget = Math.max(0, targetTop);

        // Mark as programmatic so the scroll listener does not flap UI state.
        programmaticScroll = true;

        if (prefersReducedMotion()) {
            el.scrollTop = scrollTarget;
            programmaticScroll = false;
            updateAutoFollow(el);
            if (autoFollow) {
                setJumpButton(0);
            }
            return;
        }

        if (scrollAnimHandle !== null) {
            return; // Animation loop already running; it will converge on the new target.
        }

        const step = () => {
            const current = el.scrollTop;
            const delta = scrollTarget - current;

            // Close enough.
            if (Math.abs(delta) < 1) {
                el.scrollTop = scrollTarget;
                scrollAnimHandle = null;
                programmaticScroll = false;

                // If we ended at the bottom, hide the jump button (with delay).
                updateAutoFollow(el);
                if (autoFollow) {
                    setJumpButton(0);
                }
                return;
            }

            // Ease toward the target.
            el.scrollTop = current + (delta * 0.25);
            scrollAnimHandle = window.requestAnimationFrame(step);
        };

        scrollAnimHandle = window.requestAnimationFrame(step);
    }

    function trimContainer(container) {
        if (!container) return;
        const over = container.childElementCount - MAX_LINES;
        if (over <= 0) return;

        // Remove the oldest nodes first.
        for (let i = 0; i < over; i += 1) {
            if (!container.firstElementChild) break;
            container.removeChild(container.firstElementChild);
        }
    }

    function setJumpButton(count) {
        const btn = document.getElementById("btn-jump-bottom");
        if (!btn) return;

        const show = (label) => {
            if (jumpHideTimer) {
                clearTimeout(jumpHideTimer);
                jumpHideTimer = null;
            }
            btn.hidden = false;
            btn.textContent = label;
        };

        const hideDelayed = () => {
            if (jumpHideTimer) {
                clearTimeout(jumpHideTimer);
            }
            jumpHideTimer = setTimeout(() => {
                btn.hidden = true;
                btn.textContent = "Jump to newest";
                jumpHideTimer = null;
            }, JUMP_HIDE_DELAY_MS);
        };

        // If count is null/undefined, show the button without a count.
        if (count === null || typeof count === "undefined") {
            show("Jump to newest");
            return;
        }

        if (count > 0) {
            show(`Jump to newest (${count})`);
            return;
        }

        // count <= 0: hide, but with a small delay to avoid flicker while scrolling.
        hideDelayed();
    }

    function appendLineToPane(paneId, lineParts) {
        const pane = document.getElementById(paneId);
        if (!pane) return;

        const emptyState = pane.querySelector(".logs-empty-state");
        if (emptyState) {
            emptyState.remove();
        }

        const div = document.createElement("div");
        div.className = "logs-line" + (lineParts.prefix.playback ? " logs-playback" : "");

        const tsSpan = document.createElement("span");
        tsSpan.className = "logs-ts" + (lineParts.prefix.continuation ? " logs-ts-cont" : "");
        tsSpan.textContent = lineParts.prefix.timestamp ? `${lineParts.prefix.timestamp} ` : "";

        const unitSpan = document.createElement("span");
        unitSpan.className = "logs-unit";
        unitSpan.textContent = lineParts.prefix.unit ? `${lineParts.prefix.unit} ` : "";

        const sevSpan = document.createElement("span");
        sevSpan.className = "logs-sev" + (lineParts.prefix.severityClass ? " " + lineParts.prefix.severityClass : "");
        sevSpan.textContent = lineParts.prefix.severity ? `[${lineParts.prefix.severity}] ` : "";

        const msgSpan = document.createElement("span");
        msgSpan.className = "logs-msg";
        msgSpan.textContent = (lineParts.message ?? "").toString();

        div.appendChild(tsSpan);
        div.appendChild(unitSpan);
        div.appendChild(sevSpan);
        div.appendChild(msgSpan);

        pane.appendChild(div);
        trimContainer(pane);
    }

    function flushHiddenBuffer() {
        if (hiddenBuffer.length === 0) {
            setJumpButton(0);
            return;
        }

        const flushedCount = hiddenBufferedCount;

        for (const item of hiddenBuffer) {
            appendLineToPane(item.paneId, item.lineParts);

            if (item.alsoAll && item.paneId !== "all") {
                appendLineToPane("all", item.lineParts);
            }
        }

        hiddenBuffer = [];
        hiddenBufferedCount = 0;

        if (autoFollow || inPlayback) {
            scrollLogsToBottom(true);
            setJumpButton(0);
        } else {
            setJumpButton(flushedCount);
        }
    }

    function debugConsole(level, ...args) {
        // Prefer the site's global debugConsole() if it exists.
        const w = window;
        if (w && typeof w.debugConsole === "function") {
            const method = (level === "info") ? "log" : level;
            try {
                w.debugConsole(method, ...args);
                return;
            } catch (e) {
                // Fall through to console.
            }
        }

        if (level === "debug") {
            // console.debug(...args);
            return;
        }
        if (level === "info") {
            console.log(...args);
            return;
        }
        if (level === "warn") {
            console.warn(...args);
            return;
        }
        if (level === "error") {
            console.error(...args);
            return;
        }
        console.log(...args);
    }

    function onClick(id, fn) {
        const el = document.getElementById(id);
        if (!el) return;
        el.addEventListener("click", fn);
    }

    function clearPanes() {
        const allPane = document.getElementById("all");
        const internalPane = document.getElementById("internal");
        if (allPane) allPane.textContent = "";
        if (internalPane) internalPane.textContent = "";
        renderEmptyState(
            "all",
            "Log output cleared",
            "Live journal entries from wsprrypi.service will appear here when the next line arrives."
        );
        renderEmptyState(
            "internal",
            "Internal messages hidden",
            "Adapter and connection messages will appear here when you switch to the internal view."
        );
    }

    function renderEmptyState(paneId, title, body, action = null) {
        const pane = document.getElementById(paneId);
        if (!pane) return;

        pane.textContent = "";

        const state = document.createElement("div");
        state.className = "logs-empty-state";

        const heading = document.createElement("div");
        heading.className = "logs-empty-state__title";
        heading.textContent = title;

        const copy = document.createElement("p");
        copy.className = "logs-empty-state__body mb-0";
        copy.textContent = body;

        state.appendChild(heading);
        state.appendChild(copy);

        if (action && action.id && action.label) {
            const button = document.createElement("button");
            button.type = "button";
            button.id = action.id;
            button.className = "btn btn-outline-primary logs-empty-state__action";
            button.textContent = action.label;
            button.dataset.idleLabel = action.label;
            state.appendChild(button);
        }

        pane.appendChild(state);
    }


    // ---------------------------------------------------------------------
    // Visible view selection (console controllable).
    //
    // The stream delivers two logical sources:
    // - type: "journal"   (journald via systemd-journal-gatewayd-like output)
    // - type: "internal"  (adapter / UI / connection messages)
    //
    // By default, only the journal view is shown and internal messages are kept
    // in a hidden pane. You can toggle the visible pane at runtime from the
    // browser dev console:
    //
    //   viewLogs.showJournal()
    //   viewLogs.showInternal()
    //   viewLogs.toggle()
    //   viewLogs.set("both")   // optional
    //
    // The selection is persisted in localStorage.
    // ---------------------------------------------------------------------

    const VIEW_STORAGE_KEY = "wsprrypi_log_view";
    let activeView = "journal";

    function normalizeView(view) {
        const v = String(view ?? "").trim().toLowerCase();
        if (v === "internal") return "internal";
        if (v === "both") return "both";
        return "journal";
    }

    function getViewLabel(view) {
        if (view === "internal") return "Internal";
        if (view === "both") return "Journal + Internal";
        return "Journal";
    }

    function applyView(view, persist = true) {
        const v = normalizeView(view);
        activeView = v;

        const allPane = document.getElementById("all");
        const internalPane = document.getElementById("internal");

        // Keep the DOM nodes present for appendLine() to target regardless of view.
        if (allPane) allPane.hidden = (v === "internal");
        if (internalPane) internalPane.hidden = (v === "journal");

        const titleEl = document.getElementById("cardTitle");
        if (titleEl) {
            titleEl.textContent = `Wsprry Pi Log (${getViewLabel(v)})`;
        }

        if (persist) {
            try {
                window.localStorage.setItem(VIEW_STORAGE_KEY, v);
            } catch (e) {
                // Ignore storage failures.
            }
        }

        // If the user is following the tail, keep them on the tail after switching.
        const scrollContainer = document.getElementById("log-scroll");
        if (scrollContainer && autoFollow) {
            scrollLogsToBottom(true);
            setJumpButton(0);
        }
    }

    function loadInitialView() {
        // URL override: ?view=journal|internal|both
        try {
            const u = new URL(window.location.href);
            const qv = u.searchParams.get("view");
            if (qv) return normalizeView(qv);
        } catch (e) {
            // Ignore URL parse errors.
        }

        // localStorage override.
        try {
            const stored = window.localStorage.getItem(VIEW_STORAGE_KEY);
            if (stored) return normalizeView(stored);
        } catch (e) {
            // Ignore storage failures.
        }

        return "journal";
    }

    function registerConsoleApi() {
        // Expose a small helper to the window so operators can switch views at
        // runtime without adding more UI chrome.
        //
        // This is intentionally terse for console use.
        window.viewLogs = {
            set: (v) => applyView(v, true),
            showJournal: () => applyView("journal", true),
            showInternal: () => applyView("internal", true),
            showBoth: () => applyView("both", true),
            toggle: () => applyView(activeView === "internal" ? "journal" : "internal", true),
            get: () => activeView,
            help: () => {
                // eslint-disable-next-line no-console
                console.log(
                    "viewLogs: set('journal'|'internal'|'both'), showJournal(), " +
                    "showInternal(), showBoth(), toggle(), get()"
                );
            }
        };

        // Back-compat / alternative spelling.
        window.view_logs = window.viewLogs;
    }


    function bindLogViewActions() {
        if (logViewActionsBound) {
            return;
        }
        logViewActionsBound = true;

        onClick("btn-clear", () => {
            clearPanes();
        });

        onClick("btn-reconnect", () => {
            restartLogStream();
        });

        document.addEventListener("click", (event) => {
            const target = event.target;
            if (!(target instanceof Element)) {
                return;
            }

            if (target.id === "logsRetryButton") {
                restartLogStream();
            }
        });

        onClick("btn-jump-bottom", () => {
            autoFollow = true;
            scrollLogsToBottom(true);
            setJumpButton(0);
        });

        // Auto-follow is enabled only when the user is at (or near) the bottom.
        // If they scroll up to read history, we stop auto-following until they return.
        const scrollContainer = document.getElementById("log-scroll");
        if (scrollContainer) {
            autoFollow = isNearBottom(scrollContainer);

            // If the user starts interacting with the scroll area, immediately cancel any
            // in-progress programmatic scroll so the view is not pulled back down.
            const cancelOnUserIntent = () => {
                cancelScrollAnimation();
            };
            scrollContainer.addEventListener("wheel", cancelOnUserIntent, { passive: true });
            scrollContainer.addEventListener("touchstart", cancelOnUserIntent, { passive: true });
            scrollContainer.addEventListener("mousedown", cancelOnUserIntent, { passive: true });

            scrollContainer.addEventListener("scroll", () => {
                if (programmaticScroll) return;

                scheduleScrollUiUpdate(() => {
                    updateAutoFollow(scrollContainer);

                    // If the user scrolls up, offer a quick way to jump back to the tail.
                    // Show the buffered count if any accumulated while hidden.
                    if (autoFollow) {
                        setJumpButton(0);
                    } else {
                        setJumpButton(hiddenBufferedCount > 0 ? hiddenBufferedCount : null);
                    }
                });
            }, { passive: true });
        }

        document.addEventListener("visibilitychange", () => {
            if (!document.hidden) flushHiddenBuffer();
        });

        window.addEventListener("focus", () => {
            flushHiddenBuffer();
        });
    }

    function scrollLogsToBottom(force = false) {
        const scrollContainer = document.getElementById("log-scroll");
        if (!scrollContainer) return;

        if (!force && !autoFollow) {
            return;
        }

        const target = Math.max(0, scrollContainer.scrollHeight - scrollContainer.clientHeight);

        // During playback we hard-snap on every append so the initial backlog can
        // never "outrun" the scroll position.
        if (inPlayback) {
            cancelScrollAnimation();

            programmaticScroll = true;
            scrollContainer.scrollTop = target;
            programmaticScroll = false;

            // Ensure UI reflects "at bottom" after the snap.
            updateAutoFollow(scrollContainer);
            setJumpButton(0);
            return;
        }

        if (softScrollEnabled && ("scrollBehavior" in document.documentElement.style)) {
            // Use our own easing loop to avoid repeated native smooth-scroll resets.
            animateScrollTo(scrollContainer, target);
        } else {
            programmaticScroll = true;
            scrollContainer.scrollTop = target;
            programmaticScroll = false;

            updateAutoFollow(scrollContainer);
            if (autoFollow) {
                setJumpButton(0);
            }
        }
    }

    function formatTimestampUTC(payload) {
        // Format: YYYY-MM-DDTHH:MM:SS.mmmZ (always UTC)
        // __REALTIME_TIMESTAMP is microseconds since Unix epoch (journald).
        const raw = payload.__REALTIME_TIMESTAMP;
        if (raw === undefined || raw === null) return "";

        const us = Number(raw);
        if (!Number.isFinite(us)) return "";
        if (!Number.isSafeInteger(us)) return "";
        if (us < MIN_VALID_JOURNAL_TIMESTAMP_US) return "";

        const ms = Math.floor(us / 1000);
        const d = new Date(ms);
        if (Number.isNaN(d.getTime())) return "";

        const yyyy = String(d.getUTCFullYear()).padStart(4, "0");
        const MM = String(d.getUTCMonth() + 1).padStart(2, "0");
        const dd = String(d.getUTCDate()).padStart(2, "0");
        const HH = String(d.getUTCHours()).padStart(2, "0");
        const mm = String(d.getUTCMinutes()).padStart(2, "0");
        const ss = String(d.getUTCSeconds()).padStart(2, "0");
        const mmm = String(d.getUTCMilliseconds()).padStart(3, "0");

        return `${yyyy}-${MM}-${dd}T${HH}:${mm}:${ss}.${mmm}Z`;
    }

    function formatUnitFixed16(unit) {
        const s = (unit ?? "").toString();
        if (!s) return "";
        return s;
    }

    function priorityToLabel(priorityStr) {
        // Syslog priority: Emerg (0), Alert (1), Crit (2), 3 err, Warn (4),
        // Notice (5), Info (6), Debug (7).
        switch (String(priorityStr)) {
            case "0":
                return "EMERG";
            case "1":
                return "ALERT";
            case "2":
                return "CRIT";
            case "3":
                return "ERROR";
            case "4":
                return "WARN";
            case "5":
                return "NOTICE";
            case "7":
                return "DEBUG";
            case "6":
            default:
                return "INFO";
        }
    }

    function normalizeSeverityLabel(label) {
        const s = (label ?? "").toString().toUpperCase();
        // Keep exactly 5 characters, padded on the right.
        if (s.length > 5) return s.slice(0, 5);
        return s.padEnd(5, " ");
    }

    function severityClassFromLabel(label5) {
        const s = (label5 ?? "").toString().trim().toUpperCase();
        switch (s) {
            case "EMERG":
                return "log-emerg";
            case "ALERT":
                return "log-alert";
            case "CRIT":
                return "log-crit";
            case "ERROR":
                return "log-error";
            case "WARN":
                return "log-warn";
            case "NOTIC":
            case "NOTICE":
                return "log-notic";
            case "INFO":
                return "log-info";
            case "DEBUG":
                return "log-debug";
            default:
                return "";
        }
    }

    function extractSeverityAndMessage(payload) {
        // Prefer PRIORITY if present. Otherwise try to parse leading [LEVEL] from MESSAGE.
        const rawMsg = (payload.MESSAGE ?? "").toString();

        let label = "";
        if (payload.PRIORITY !== undefined && payload.PRIORITY !== null) {
            label = priorityToLabel(payload.PRIORITY);
        } else {
            const m = rawMsg.match(/^\s*\[\s*([A-Za-z]+)\s*\]\s*-?\s*/);
            if (m && m[1]) {
                label = String(m[1]).toUpperCase();
                if (label === "WARNING") label = "WARN";
            } else {
                label = "INFO";
            }
        }

        // Do not strip or alter MESSAGE. The producer may already include its own
        // severity prefix and formatting.
        return {
            label: normalizeSeverityLabel(label),
            message: rawMsg
        };
    }

    let evt = null;
    let lastEventAtMs = 0;
    let watchdogTimer = null;
    let reconnectAttempts = 0;
    let reconnectPending = false;
    let reconnectTimer = null;
    let isReloading = false;
    let unloadHookInstalled = false;
    let logViewActionsBound = false;
    let logStreamInitialized = false;

    const CURSOR_STORAGE_KEY = "log_stream_last_cursor";

    function getStoredCursor() {
        try {
            const v = localStorage.getItem(CURSOR_STORAGE_KEY);
            return (v && v.trim() !== "") ? v.trim() : null;
        } catch (e) {
            return null;
        }
    }

    function storeCursor(cursor) {
        if (!cursor || cursor.trim() === "") return;
        try {
            localStorage.setItem(CURSOR_STORAGE_KEY, cursor.trim());
        } catch (e) {
            // Ignore storage failures (private mode, quota, etc.)
        }
    }

    /*
     * Compute a jittered exponential backoff delay.
     * Uses "equal jitter" to avoid thundering herds while keeping bounded growth.
     */
    function computeReconnectDelayMs(attempts) {
        const baseDelayMs = 1000;
        const maxDelayMs = 30000;
        const exp = Math.min(maxDelayMs, baseDelayMs * Math.pow(2, attempts));
        const half = Math.floor(exp / 2);
        const jitter = Math.floor(Math.random() * (half + 1));
        return half + jitter;
    }

    function scheduleManualReconnect(reason) {
        if (reconnectPending) return;
        reconnectPending = true;

        const delayMs = computeReconnectDelayMs(reconnectAttempts);
        reconnectAttempts += 1;

        debugConsole("warn", "Reconnect scheduled in", delayMs + "ms", "Reason:", reason);

        setSseStatus(
            "reconnecting",
            "Reconnecting (" + reconnectAttempts + ")",
            "Next attempt in " + formatDelay(delayMs) + ". " + reason
        );

        reconnectTimer = window.setTimeout(() => {
            reconnectTimer = null;
            reconnectPending = false;
            restartLogStream();
        }, delayMs);
    }

    function resolveStreamBaseUrl() {
        // Allow the page to override where to connect (optional):
        //   window.WSPRRYPI_LOG_STREAM_URL = "https://example/path/log_stream.php";
        // or a DOM hint:
        //   <body data-log-stream-url="...">
        const w = window;

        if (typeof w.WSPRRYPI_LOG_STREAM_URL === "string" && w.WSPRRYPI_LOG_STREAM_URL.trim() !== "") {
            return w.WSPRRYPI_LOG_STREAM_URL.trim();
        }

        const body = document.body;
        if (body) {
            const attr = body.getAttribute("data-log-stream-url");
            if (attr && attr.trim() !== "") return attr.trim();
        }

        // Prefer the centralized UI path map when available.
        if (w.WSPRRYPI_PATHS && typeof w.WSPRRYPI_PATHS.logStreamPath === "string") {
            return new URL(w.WSPRRYPI_PATHS.logStreamPath, w.location.origin).toString();
        }

        // Fallback: log_stream.php next to the current document.
        const url = new URL("log_stream.php", w.location.href);
        return url.toString();
    }

    function buildStreamUrl() {
        const url = new URL(resolveStreamBaseUrl());

        // Fixed settings (UI filtering removed).
        // Keep playback/backlog enabled for a quick initial tail, and lock the unit to
        // wsprrypi.service by default.
        url.searchParams.set("playback", "1");
        url.searchParams.set("backlog", "200");
        url.searchParams.set("unit", "wsprrypi.service");

        const cursor = getStoredCursor();
        if (cursor) url.searchParams.set("cursor", cursor);

        // Hint the server for SSE retry cadence (ms).
        url.searchParams.set("retry_ms", "2000");

        return url.toString();
    }

    function installUnloadHookOnce() {
        if (unloadHookInstalled) return;
        unloadHookInstalled = true;

        window.addEventListener("beforeunload", () => {
            isReloading = true;
            if (evt) evt.close();
        });
    }

    function startLogStream() {
        if (!("EventSource" in window)) {
            setSseStatus("disconnected", "Unavailable", "This browser does not support server-sent events");
            renderEmptyState(
                "all",
                "Live stream unavailable",
                "This browser cannot open the live log stream. Use a current browser with server-sent event support to view live journal output."
            );
            return;
        }

        if (evt) {
            evt.close();
            evt = null;
        }

        const url = buildStreamUrl();
        debugConsole("info", "Connecting to", url);

        setSseStatus("reconnecting", "Connecting", "Connecting to log stream");
        evt = new EventSource(url);

        lastEventAtMs = Date.now();
        reconnectPending = false;
        isReloading = false;

        installUnloadHookOnce();

        evt.onopen = () => {
            setSseStatus("connected", "Connected", "Connected to log stream");
            debugConsole("debug", "Connected to log stream");
            reconnectAttempts = 0;
            // Do not assume playback is running. We enter/exit playback deterministically
            // via explicit SSE boundary events from log_stream.php.
            inPlayback = false;
        };

        const handler = (e) => {
            lastEventAtMs = Date.now();
            reconnectPending = false;

            try {
                const raw = (e.data ?? "").toString().trim();

                // Some servers send plain-text heartbeat/status lines or accidentally include
                // a leading "data:" prefix inside the payload. Normalize before parsing.
                let normalized = raw;
                if (normalized.toLowerCase().startsWith("data:")) {
                    normalized = normalized.slice(5).trim();
                }

                if (normalized === "") {
                    return;
                }

                let payload;

                // If it's not JSON, treat it as a plain message.
                const first = normalized[0];
                if (first !== "{" && first !== "[") {
                    payload = {
                        type: (e.type && e.type !== "message") ? e.type : "internal",
                        playback: false,
                        __REALTIME_TIMESTAMP: null,
                        PRIORITY: null,
                        SYSLOG_IDENTIFIER: "log_stream.php",
                        MESSAGE: normalized,
                        _SYSTEMD_UNIT: null,
                        HOSTNAME: null,
                        PID: null,
                        UID: null,
                        GID: null
                    };
                } else {
                    payload = JSON.parse(normalized);
                }

                // Some implementations may send named SSE events (event: journal/internal/etc).
                // Prefer payload.type; otherwise infer from the event name.
                const inferredType =
                    (payload.type ?? ((e.type && e.type !== "message") ? e.type : null));

                // Persist cursor for resume safety (Last-Event-ID / cursor).
                if (typeof e.lastEventId === "string" && e.lastEventId.trim() !== "") {
                    storeCursor(decodeURIComponent(e.lastEventId));
                } else if (payload.__CURSOR && typeof payload.__CURSOR === "string") {
                    storeCursor(payload.__CURSOR);
                }

                // If this is the initial SSE connection status and playback is enabled,
                // treat it as part of the preload so it renders dim.
                if (payload.MESSAGE &&
                    payload.MESSAGE.startsWith("SSE connected") &&
                    payload.playback !== true) {
                    try {
                        const u = new URL(url);
                        if (u.searchParams.get("playback") === "1") {
                            payload.playback = true;
                        }
                    } catch (e2) {
                        // Ignore URL parsing errors
                    }
                }

                // Determine target pane (UI filtering removed).
                // Journal entries render only to the "all" pane.
                // Internal adapter entries render to the hidden "internal" pane.
                let paneId = (inferredType === "internal") ? "internal" : "all";
                const alsoAll = false;

                // Extract severity and a cleaned message (removes any leading [LEVEL]).
                const sev = extractSeverityAndMessage(payload);

                // Split into lines so continuations can render with a bold Z timestamp.
                const msgLines = sev.message.split(/\r?\n/);

                const ts = formatTimestampUTC(payload);
                const unit = payload._SYSTEMD_UNIT ? formatUnitFixed16(String(payload._SYSTEMD_UNIT)) : "";
                const severity = sev.label;

                // When the page is not visible, browsers may throttle animation frames
                // and delay layout/paint. Buffer incoming lines and flush on focus.
                if (document.hidden) {
                    for (let i = 0; i < msgLines.length; i++) {
                        const lineParts = {
                            prefix: {
                                timestamp: ts,
                                unit: unit,
                                severity: severity,
                                severityClass: severityClassFromLabel(severity),
                                playback: !!payload.playback,
                                continuation: (i > 0)
                            },
                            message: msgLines[i]
                        };

                        hiddenBuffer.push({
                            paneId: paneId,
                            alsoAll: alsoAll,
                            lineParts: lineParts
                        });
                        hiddenBufferedCount += 1;
                    }
                    return;
                }

                // Visible: append immediately.
                for (let i = 0; i < msgLines.length; i++) {
                    const lineParts = {
                        prefix: {
                            timestamp: ts,
                            unit: unit,
                            severity: severity,
                            severityClass: severityClassFromLabel(severity),
                            playback: !!payload.playback,
                            continuation: (i > 0)
                        },
                        message: msgLines[i]
                    };

                    appendLineToPane(paneId, lineParts);
                }
                scrollLogsToBottom();
            } catch (err) {
                debugConsole("error", "Parse error", err, (e.data ?? "").toString().slice(0, 200));
            }
        };

        // Default (unnamed) messages.
        evt.onmessage = handler;

        // Named event support (event: journal/internal/etc).
        // If the backend emits `event: journal`, onmessage will NOT fire.
        evt.addEventListener("journal", handler);
        evt.addEventListener("internal", handler);

        // Deterministic replay boundaries.
        evt.addEventListener("playback_start", () => {
            lastEventAtMs = Date.now();
            inPlayback = true;
            autoFollow = true;
            scrollLogsToBottom(true);
        });
        evt.addEventListener("playback_end", () => {
            lastEventAtMs = Date.now();
            inPlayback = false;
            autoFollow = true;
            scrollLogsToBottom(true);
        });

        evt.addEventListener("status", handler);
        evt.addEventListener("debug", handler);
        evt.addEventListener("error", handler);
        evt.addEventListener("heartbeat", handler);

        evt.onerror = () => {
            if (!evt) return;

            // If the browser gives up (CLOSED), we handle reconnect with backoff.
            if (evt.readyState === EventSource.CLOSED && !isReloading) {
                debugConsole("warn", "SSE connection closed. Forcing reconnect");
                setSseStatus("disconnected", "Disconnected", "Stream closed. Reconnecting");
                renderEmptyState(
                    "all",
                    navigator.onLine === false ? "Offline" : "Log stream disconnected",
                    navigator.onLine === false
                        ? "The browser is offline, so the live log stream is paused. Reconnect to the network and start the stream again."
                        : "The live log stream closed before new entries arrived. The UI will retry automatically, or you can reconnect now.",
                    { id: "logsRetryButton", label: "Reconnect now" }
                );
                scheduleManualReconnect("eventsource closed");
                return;
            }

            // Otherwise: browser will auto-reconnect.
            setSseStatus("reconnecting", "Reconnecting", "Transient SSE error. Browser retrying");
        };
    }

    function stopLogStream() {
        setSseStatus("disconnected", "Disconnected", "Disconnected");
        reconnectPending = false;
        if (reconnectTimer !== null) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
        if (evt) {
            evt.close();
            evt = null;
        }
    }

    function restartLogStream() {
        stopLogStream();
        startLogStream();
    }

    function handleBrowserOffline() {
        stopLogStream();
        renderEmptyState(
            "all",
            "Offline",
            "The browser is offline, so the live log stream is paused. Reconnect to the network and start the stream again.",
            { id: "logsRetryButton", label: "Reconnect now" }
        );
    }

    function handleBrowserOnline() {
        renderEmptyState(
            "all",
            "Reconnecting log stream",
            "Network connectivity returned. The viewer is reconnecting to the local log feed now."
        );
        restartLogStream();
    }

    function startWatchdog() {
        if (watchdogTimer) return;

        // If no events arrive for this long, force a reconnect.
        // This should never trigger during normal operation, since log_stream.php emits a heartbeat.
        const stallThresholdMs = 60000;

        watchdogTimer = window.setInterval(() => {
            if (!evt) return;
            if (isReloading) return;

            const ageMs = Date.now() - (lastEventAtMs || 0);
            if (evt.readyState === EventSource.OPEN && lastEventAtMs && ageMs > stallThresholdMs) {
                scheduleManualReconnect("watchdog: No SSE events for " + Math.floor(ageMs / 1000) + "s");
            }
        }, 5000);
    }

    function init() {
        if (logStreamInitialized) {
            if (!evt) {
                startLogStream();
            }
            return;
        }
        logStreamInitialized = true;

        setSseStatus("disconnected", "Disconnected", "Disconnected");
        registerConsoleApi();
        applyView(loadInitialView(), false);
        renderEmptyState(
            "all",
            "Waiting for log stream",
            "The viewer will connect automatically. If nothing appears, use Start stream to reconnect to the local log feed.",
            { id: "logsRetryButton", label: "Start stream" }
        );
        renderEmptyState(
            "internal",
            "Internal messages hidden",
            "Switch to the internal view from the browser console if you need adapter and connection diagnostics."
        );

// This mirrors the old inline behavior where site.js calls initLogStream()
        // from loadPage(). We intentionally do not auto-start here to avoid
        // double-connecting if the page framework controls init order.
        bindLogViewActions();
        window.addEventListener("offline", handleBrowserOffline);
        window.addEventListener("online", handleBrowserOnline);
        startWatchdog();
        startLogStream();
    }

    // Export entry points for site.js.
    // - loadPage() calls initLogStream() when present.
    // - bindActions() calls bindLogViewActions() when present.
    if (window.WSPRRYPI_ENABLE_LOG_TEST_HOOKS === true) {
        window.WSPRRYPI_LOG_TEST_HOOKS = {
            formatTimestampUTC,
            extractSeverityAndMessage,
            formatUnitFixed16,
            priorityToLabel
        };
    }

    window.initLogStream = init;
    window.bindLogViewActions = bindLogViewActions;
})();

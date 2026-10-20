<?php

// IMPECCABLE: DO NOT MODIFY

/**
 * Standalone diagnostic log viewer.
 * This page is intentionally self-contained and does not depend on header.php
 * or the shared UI bootstrap used by the main WsprryPi interface.
 */

/**
 * view_diag_logs.php
 * ------------
 * Log viewer UI for Server-Sent Events from log_stream.php.
 *
 * Expects log_stream.php to emit SSE `data:` lines containing JSON objects in the
 * unified schema described in the user's pseudo-OpenAPI summary:
 * - type: "journal" | "internal"
 * - playback: bool
 * - __CURSOR: string|null
 * - __REALTIME_TIMESTAMP: int (microseconds since epoch)
 * - PRIORITY: "0".."7" (string) or null
 * - SYSLOG_IDENTIFIER, MESSAGE, _SYSTEMD_UNIT, HOSTNAME, PID, UID, GID
 *
 * This page maps journald PRIORITY to panes:
 * Emerg (0), Alert (1), Crit (2), Error (3), Warn (4), Notice (5), Info (6), Debug (7).
 * Internal adapter events are shown in the "internal" pane.
 */

declare(strict_types=1);

require_once __DIR__ . '/html_cache_headers.php';

$scriptName = $_SERVER['SCRIPT_NAME'] ?? '/';
$basePath = rtrim(str_replace('\\', '/', dirname($scriptName)), '/');
if ($basePath === '/' || $basePath === '.') {
    $basePath = '';
}

$pathConfig = [
    'basePath' => $basePath,
    'logStreamPath' => $basePath . '/log_stream.php',
];
?>
<!doctype html>
<html lang="en">

<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Logs</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link
        rel="stylesheet"
        href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@500;600;700&family=Source+Sans+3:wght@400;500;600;700&display=swap">

    <script>
        window.WSPRRYPI_PATHS = <?= json_encode($pathConfig, JSON_UNESCAPED_SLASHES) ?>;
    </script>

    <!-- Bootstrap + jQuery (CDN). -->
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
    <script src="https://cdn.jsdelivr.net/npm/jquery@3.7.1/dist/jquery.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js"></script>

    <style>
        :root {
            --font-body: "Source Sans 3", "Segoe UI", sans-serif;
            --font-display: "Barlow Semi Condensed", "Segoe UI", sans-serif;
            --text-kicker: 0.75rem;
            --text-caption: 0.875rem;
            --text-body: 1rem;
            --text-title: 1.5rem;
        }

        body {
            font-family: var(--font-body);
            line-height: 1.55;
        }

        .logs-card .card-body {
            position: relative;
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
            font-size: 1rem;
            line-height: 1.5;
            background: #0b0f14;
            color: #d9e1ea;
            border-radius: 0 0 .5rem .5rem;
        }

        /* SSE status badge (top-right overlay) */
        #sse-status-badge {
            position: absolute;
            top: 12px;
            right: 12px;
            z-index: 12;
            padding: 2px 10px;
            border-radius: 999px;
            font-size: var(--text-kicker);
            font-weight: 600;
            letter-spacing: 0.06em;
            text-transform: uppercase;
            line-height: 1.4;
            border: 1px solid rgba(255, 255, 255, 0.15);
            background: rgba(255, 255, 255, 0.06);
            color: #e7edf5;
            user-select: none;
            pointer-events: none;
        }

        #sse-status-badge.sse-connected {
            background: rgba(46, 204, 113, 0.18);
            border-color: rgba(46, 204, 113, 0.40);
        }

        #sse-status-badge.sse-reconnecting {
            background: rgba(241, 196, 15, 0.18);
            border-color: rgba(241, 196, 15, 0.40);
        }

        #sse-status-badge.sse-disconnected {
            background: rgba(231, 76, 60, 0.18);
            border-color: rgba(231, 76, 60, 0.40);
        }

        /*
         * Keep the scrollable area separate from overlays (like the jump button).
         * If an overlay lives inside the element that scrolls, it will scroll out
         * of view with the content.
         */
        #log-scroll {
            height: 65vh;
            overflow-y: auto;
        }

        .logs-line {
            white-space: pre-wrap;
            word-break: break-word;
        }

        .logs-ts-cont {
            font-weight: 700;
            color: #ffffff;
        }

        .logs-playback {
            opacity: 0.45;
        }

        .logs-header {
            font-size: var(--text-body);
            line-height: 1.45;
        }

        .logs-toolbar .form-select,
        .logs-toolbar .form-control,
        .logs-toolbar .btn {
            font-size: var(--text-caption);
        }

        .logs-toolbar .form-label {
            font-family: var(--font-display);
            font-size: var(--text-kicker);
            font-weight: 600;
            letter-spacing: 0.06em;
            text-transform: uppercase;
        }

        .log-emerg {
            color: #ff3b3b;
            font-weight: bold;
        }

        .log-alert {
            color: #bf5af2;
        }

        .log-crit {
            color: #ff3b3b;
        }

        .log-error {
            color: #ff9500;
        }

        .log-warn {
            color: #ffd60a;
        }

        .log-notic {
            color: #0a84ff;
        }

        .log-info {
            color: #30d158;
        }

        .log-debug {
            color: #ffffff;
        }

        .severity {
            display: inline-block;
            width: 7ch;
            /* Includes brackets: [ + 5 chars + ] */
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas,
                "Liberation Mono", "Courier New", monospace;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: clip;
        }
    </style>
</head>

<body class="bg-body-tertiary">
    <div class="container py-3">

        <div class="d-flex align-items-center justify-content-between mb-3">
            <div>
                <div class="mb-0" style="font-family: var(--font-display); font-size: var(--text-title); font-weight: 600; letter-spacing: 0.012em; line-height: 1.15;">Log Stream</div>
                <div class="text-body-secondary logs-header">systemd-journald via SSE</div>
            </div>
            <div class="d-flex gap-2">
                <button id="btn-clear" class="btn btn-outline-secondary btn-sm" type="button">Clear</button>
                <button id="btn-reconnect" class="btn btn-outline-primary btn-sm" type="button">Reconnect</button>
            </div>
        </div>

        <div class="card logs-card shadow-sm">
            <div class="card-header">
                <div class="row g-2 align-items-center logs-toolbar">
                    <div class="col-12 col-lg-3">
                        <label class="form-label mb-1" for="unitFilter">Unit filter</label>
                        <input id="unitFilter" class="form-control form-control-sm" type="text"
                            value="wsprrypi.service"
                            placeholder="wsprrypi.service,ssh.service or *">
                    </div>
                    <div class="col-6 col-lg-2">
                        <label class="form-label mb-1" for="prioMin">Priority Max (Emerg = 0)</label>
                        <select id="prioMin" class="form-select form-select-sm">
                            <option value="0" selected>Emerg (0)</option>
                            <option value="1">Alert (1)</option>
                            <option value="2">Crit (2)</option>
                            <option value="3">Error (3)</option>
                            <option value="4">Warn (4)</option>
                            <option value="5">Notice (5)</option>
                            <option value="6">Info (6)</option>
                            <option value="7">Debug (7)</option>
                        </select>
                    </div>
                    <div class="col-6 col-lg-2">
                        <label class="form-label mb-1" for="prioMax">Priority Min (Debug = 7)</label>
                        <select id="prioMax" class="form-select form-select-sm">
                            <option value="0">Emerg (0)</option>
                            <option value="1">Alert (1)</option>
                            <option value="2">Crit (2)</option>
                            <option value="3">Error (3)</option>
                            <option value="4">Warn (4)</option>
                            <option value="5">Notice (5)</option>
                            <option value="6">Info (6)</option>
                            <option value="7" selected>Debug (7)</option>
                        </select>
                    </div>
                    <div class="col-6 col-lg-2">
                        <label class="form-label mb-1" for="backlog">Backlog</label>
                        <input id="backlog" class="form-control form-control-sm" type="number" min="0" value="200">
                    </div>
                    <div class="col-6 col-lg-2">
                        <label class="form-label mb-1" for="playback">Playback</label>
                        <select id="playback" class="form-select form-select-sm">
                            <option value="1" selected>On</option>
                            <option value="0">Off</option>
                        </select>
                    </div>
                    <div class="col-12 col-lg-1 d-grid">
                        <label class="form-label mb-1">&nbsp;</label>
                        <button id="btn-apply" class="btn btn-sm btn-primary" type="button">Apply</button>
                    </div>
                </div>

                <ul class="nav nav-tabs mt-3" id="logsTabs" role="tablist" data-persist-tab-query-param="tab">
                    <?php
                    $tabs = [
                        ['id' => 'all',     'label' => 'All'],
                        ['id' => 'emerg',   'label' => 'Emerg'],
                        ['id' => 'alert',   'label' => 'Alert'],
                        ['id' => 'crit',    'label' => 'Crit'],
                        ['id' => 'error',   'label' => 'Error'],
                        ['id' => 'warn',    'label' => 'Warn'],
                        ['id' => 'notice',  'label' => 'Notice'],
                        ['id' => 'info',    'label' => 'Info'],
                        ['id' => 'debug',   'label' => 'Debug'],
                        ['id' => 'internal', 'label' => 'Internal'],
                    ];
                    foreach ($tabs as $idx => $t) {
                        $active = ($t['id'] === 'all') ? 'active' : '';
                        $selected = ($t['id'] === 'all') ? 'true' : 'false';
                        printf(
                            '<li class="nav-item" role="presentation">
                            <button class="nav-link %s" id="%s-tab" data-bs-toggle="tab" data-bs-target="#%s"
                                    type="button" role="tab" aria-controls="%s" aria-selected="%s">%s</button>
                        </li>',
                            $active,
                            htmlspecialchars($t['id']),
                            htmlspecialchars($t['id']),
                            htmlspecialchars($t['id']),
                            $selected,
                            htmlspecialchars($t['label'])
                        );
                    }
                    ?>
                </ul>
            </div>

            <div class="card-body" style="position: relative;">
                <div id="sse-status-badge" class="sse-disconnected" title="Disconnected">Disconnected</div>

                <button id="btn-jump-bottom" type="button" class="btn btn-sm btn-primary" style="display:none; position:absolute; right:12px; bottom:12px; z-index:10;">Jump to bottom</button>
                <div id="log-scroll">
                    <div class="tab-content" id="logsTabContent">
                        <?php
                        foreach ($tabs as $t) {
                            $active = ($t['id'] === 'all') ? 'show active' : '';
                            printf(
                                '<div class="tab-pane fade %s" id="%s" role="tabpanel" aria-labelledby="%s-tab"></div>',
                                $active,
                                htmlspecialchars($t['id']),
                                htmlspecialchars($t['id'])
                            );
                        }
                        ?>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        (function() {
            const MAX_LINES = 8000; // Maximum number of log lines retained per tab/container
            const LOG_TAB_QUERY_PARAM = "tab";

            function getLogTabSelector(trigger) {
                if (!(trigger instanceof Element)) {
                    return "";
                }

                const target = trigger.getAttribute("data-bs-target");
                if (typeof target === "string" && target.trim().startsWith("#")) {
                    return target.trim();
                }

                return "";
            }

            function persistLogTabInUrl(selector) {
                try {
                    const url = new URL(window.location.href);
                    const normalizedSelector =
                        typeof selector === "string" && selector.startsWith("#")
                            ? selector.slice(1)
                            : "";

                    if (!normalizedSelector || normalizedSelector === "all") {
                        url.searchParams.delete(LOG_TAB_QUERY_PARAM);
                    } else {
                        url.searchParams.set(LOG_TAB_QUERY_PARAM, normalizedSelector);
                    }

                    window.history.replaceState(window.history.state, "", url.toString());
                } catch {
                }
            }

            function restoreLogTabFromUrl() {
                let paneId = "";
                try {
                    const url = new URL(window.location.href);
                    paneId = url.searchParams.get(LOG_TAB_QUERY_PARAM) || "";
                } catch {
                    paneId = "";
                }

                if (!paneId || paneId === "all") {
                    return;
                }

                const trigger = document.querySelector(
                    `#logsTabs [data-bs-toggle="tab"][data-bs-target="#${CSS.escape(paneId)}"]`
                );

                if (!trigger) {
                    try {
                        const url = new URL(window.location.href);
                        url.searchParams.delete(LOG_TAB_QUERY_PARAM);
                        window.history.replaceState(window.history.state, "", url.toString());
                    } catch {
                    }
                    return;
                }

                bootstrap.Tab.getOrCreateInstance(trigger).show();
            }

            function setSseStatus(state, text, title) {
                const el = document.getElementById("sse-status-badge");
                if (!el) return;

                el.classList.remove("sse-connected", "sse-reconnecting", "sse-disconnected");

                if (state === "connected") el.classList.add("sse-connected");
                else if (state === "reconnecting") el.classList.add("sse-reconnecting");
                else el.classList.add("sse-disconnected");

                if (text && String(text).trim() !== "") el.textContent = text;
                if (title && String(title).trim() !== "") el.title = title;
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

            function animateScrollTo(el, targetTop) {
                if (!el) return;

                scrollTarget = Math.max(0, targetTop);

                // Mark as programmatic so the scroll listener does not flap UI state.
                programmaticScroll = true;

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
                    btn.style.display = "inline-block";
                    btn.textContent = label;
                };

                const hideDelayed = () => {
                    if (jumpHideTimer) {
                        clearTimeout(jumpHideTimer);
                    }
                    jumpHideTimer = setTimeout(() => {
                        btn.style.display = "none";
                        btn.textContent = "Jump to bottom";
                        jumpHideTimer = null;
                    }, JUMP_HIDE_DELAY_MS);
                };

                // If count is null/undefined, show the button without a count.
                if (count === null || typeof count === "undefined") {
                    show("Jump to bottom");
                    return;
                }

                if (count > 0) {
                    show(`Jump to bottom (${count})`);
                    return;
                }

                // count <= 0: hide, but with a small delay to avoid flicker while scrolling.
                hideDelayed();
            }

            function appendLineToPane(paneId, lineParts) {
                const pane = document.getElementById(paneId);
                if (!pane) return;

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

            /* Centralized path configuration for local endpoints. */
            const WSPRRYPI_PATHS = <?php echo json_encode($pathConfig, JSON_UNESCAPED_SLASHES); ?>;

            function debugConsole(level, ...args) {
                // Keep this simple; integrate with your logger if needed.
                if (level === "debug") {
                    // Comment this out if you want to reduce console noise.
                    console.debug(...args);
                    return;
                }
                if (level === "info") {
                    console.info(...args);
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

            function bindLogViewActions() {
                $(document).on("shown.bs.tab", '#logsTabs button[data-bs-toggle="tab"]', (event) => {
                    persistLogTabInUrl(getLogTabSelector(event.target));
                    // When switching tabs, force the view to the bottom.
                    scrollLogsToBottom(true);
                });

                $("#btn-clear").on("click", () => {
                    $("#logsTabContent .tab-pane").empty();
                });

                $("#btn-reconnect").on("click", () => {
                    restartLogStream();
                });

                $("#btn-jump-bottom").on("click", () => {
                    autoFollow = true;
                    scrollLogsToBottom(true);
                    setJumpButton(0);
                });

                $("#btn-apply").on("click", () => {
                    restartLogStream();
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
                    scrollContainer.addEventListener("wheel", cancelOnUserIntent, {
                        passive: true
                    });
                    scrollContainer.addEventListener("touchstart", cancelOnUserIntent, {
                        passive: true
                    });
                    scrollContainer.addEventListener("mousedown", cancelOnUserIntent, {
                        passive: true
                    });

                    scrollContainer.addEventListener("scroll", () => {
                        if (programmaticScroll) {
                            return;
                        }

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
                    }, {
                        passive: true
                    });
                }

                document.addEventListener("visibilitychange", () => {
                    if (!document.hidden) {
                        flushHiddenBuffer();
                    }
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

            function priorityToPane(priorityStr) {
                // Syslog priority: Emerg (0), Alert (1), Crit (2), 3 err, Warn (4), Notice (5), Info (6), Debug (7).
                switch (String(priorityStr)) {
                    case "0":
                        return "emerg";
                    case "1":
                        return "alert";
                    case "2":
                        return "crit";
                    case "3":
                        return "error";
                    case "4":
                        return "warn";
                    case "5":
                        return "notice";
                    case "7":
                        return "debug";
                    case "6":
                    default:
                        return "info";
                }
            }

            function pad2(n) {
                return String(n).padStart(2, "0");
            }

            function pad3(n) {
                return String(n).padStart(3, "0");
            }

            function formatTimestampUTC(payload) {
                // Format: YYYY-MM-DDTHH:MM:SS.mmmZ (always UTC)
                // __REALTIME_TIMESTAMP is microseconds since Unix epoch (journald).
                const raw = payload.__REALTIME_TIMESTAMP;
                if (raw === undefined || raw === null) return "";

                const us = Number(raw);
                if (!Number.isFinite(us)) return "";

                const ms = Math.floor(us / 1000);
                const d = new Date(ms);

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
                // Fixed-width unit column: 16 characters.
                // - If shorter, pad spaces on the right.
                // - If longer, truncate to 15 chars and use an ellipsis as the 16th char.
                const s = (unit ?? "").toString();
                if (!s) return "";
                if (s.length > 16) return s.slice(0, 15) + "…";
                return s.padEnd(16, " ");
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

            function buildLineParts(payload, isContinuation) {
                const ts = formatTimestampUTC(payload);
                const unit = payload._SYSTEMD_UNIT ? formatUnitFixed16(String(payload._SYSTEMD_UNIT)) : "";

                const sev = extractSeverityAndMessage(payload);
                const prefix = {
                    timestamp: ts,
                    unit: unit,
                    severity: sev.label,
                    severityClass: severityClassFromLabel(sev.label),
                    playback: !!payload.playback,
                    continuation: !!isContinuation
                };

                return {
                    prefix: prefix,
                    message: sev.message
                };
            }

            let evt = null;
            let lastEventAtMs = 0;
            let watchdogTimer = null;
            let reconnectAttempts = 0;
            let reconnectPending = false;

            let isReloading = false;

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

            function clearStoredCursor() {
                try {
                    localStorage.removeItem(CURSOR_STORAGE_KEY);
                } catch (e) {
                    // Ignore storage failures
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
                setTimeout(() => {
                    reconnectPending = false;
                    restartLogStream();
                }, delayMs);
            }

            function buildStreamUrl() {
                const url = new URL(WSPRRYPI_PATHS.logStreamPath, window.location.origin);

                // Apply filters from controls.
                const playback = $("#playback").val();
                const backlog = $("#backlog").val();
                const prioMin = $("#prioMin").val();
                const prioMax = $("#prioMax").val();
                const unit = $("#unitFilter").val();

                if (playback !== "" && playback !== null) url.searchParams.set("playback", playback);
                if (backlog !== "" && backlog !== null) url.searchParams.set("backlog", backlog);
                if (prioMin !== "" && prioMin !== null) url.searchParams.set("priority_min", prioMin);
                if (prioMax !== "" && prioMax !== null) url.searchParams.set("priority_max", prioMax);
                if (unit !== "" && unit !== null) url.searchParams.set("unit", unit);

                const cursor = getStoredCursor();
                if (cursor) url.searchParams.set("cursor", cursor);
                // Hint the server for SSE retry cadence (ms).
                url.searchParams.set("retry_ms", "2000");

                return url.toString();
            }

            function startLogStream() {
                const url = buildStreamUrl();
                debugConsole("info", "Connecting to", url);

                setSseStatus("reconnecting", "Connecting", "Connecting to log stream");
                evt = new EventSource(url);

                lastEventAtMs = Date.now();
                reconnectPending = false;
                isReloading = false;

                window.addEventListener("beforeunload", () => {
                    isReloading = true;
                    if (evt) evt.close();
                });

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

                        // Some implementations may send named SSE events (event: journal/internal).
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
                            } catch (e) {
                                // Ignore URL parsing errors
                            }
                        }

                        // Determine target pane.
                        let paneId = "info";
                        if (inferredType === "internal") {
                            paneId = "internal";
                        } else if (payload.PRIORITY !== undefined && payload.PRIORITY !== null) {
                            paneId = priorityToPane(payload.PRIORITY);
                        }

                        const isInternal = (inferredType === "internal");
                        const alsoAll = !isInternal;

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

                            if (alsoAll && paneId !== "all") {
                                appendLineToPane("all", lineParts);
                            }
                        }
                        scrollLogsToBottom();
                    } catch (err) {
                        debugConsole("error", "Parse error", err, (e.data ?? "").toString().slice(0, 200));
                    }
                };

                // Default (unnamed) messages.
                evt.onmessage = handler;

                // Named event support (event: journal/internal/etc.).
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
                        scheduleManualReconnect("eventsource closed");
                        return;
                    }

                    // Otherwise: browser will auto-reconnect.
                    setSseStatus("reconnecting", "Reconnecting", "Transient SSE error. Browser retrying");
                };
            }

            function stopLogStream() {
                setSseStatus("disconnected", "Disconnected", "Disconnected");
                if (evt) {
                    evt.close();
                    evt = null;
                }
            }

            function restartLogStream() {
                stopLogStream();
                startLogStream();
            }

            function scheduleWatchdogReconnect(reason) {
                scheduleManualReconnect("watchdog: " + reason);
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
                        scheduleWatchdogReconnect("No SSE events for " + Math.floor(ageMs / 1000) + "s");
                    }
                }, 5000);
            }

            $(document).ready(() => {
                bindLogViewActions();
                restoreLogTabFromUrl();
                startWatchdog();
                startLogStream();
            });
        })();
    </script>

</body>

</html>

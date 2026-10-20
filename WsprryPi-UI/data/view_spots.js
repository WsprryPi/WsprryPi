// view_spots.js
(function ($) {
    "use strict";

    const MINUTES = 60;
    const TTL_MS = 2 * 60 * 1000;
    const REFRESH_MS = 5 * 60 * 1000;

    const COLUMNS = [
        "time",
        "tx_sign",
        "frequency",
        "snr",
        "drift",
        "tx_loc",
        "power",
        "rx_sign",
        "rx_loc",
        "distance",
        "code",
        "version",
    ];

    const CODE_MAP = {
        1: "WSPR-2",
        2: "WSPR-15",
        3: "FST4W-120",
        4: "FST4W-300",
        5: "FST4W-900",
        8: "FST4W-1800",
        16: "FST4W-900",
    };

    const HEADERS = {
        time: "Timestamp (UTC)",
        tx_sign: "Transmitter",
        frequency: "Freq (Hz)",
        snr: "SNR (dB)",
        drift: "Drift (Hz)",
        tx_loc: "TX Grid",
        power: "Power (dBm)",
        rx_sign: "Receiver",
        rx_loc: "RX Grid",
        distance: "Distance (km)",
        code: "Type",
        version: "Decoder Ver.",
    };

    const SOURCE_LABELS = {
        auto: "Automatic failover",
        wspr_live_downloader: "wspr.live Downloader API",
        wspr_live_clickhouse: "wspr.live ClickHouse direct",
    };

    const DEMO_CALLSIGN = "AA0NT";

    let _cacheEntry = null,
        _cacheKey = "",
        _cacheTS = 0,
        _refreshTimer = null,
        _activeRequest = null,
        _requestSequence = 0;

    function clearRefreshTimer() {
        if (_refreshTimer !== null) {
            clearTimeout(_refreshTimer);
            _refreshTimer = null;
        }
    }

    function clearActiveRequest() {
        if (_activeRequest && typeof _activeRequest.abort === "function") {
            _activeRequest.abort();
        }
        _activeRequest = null;
    }

    function getSpotsBody() {
        return document.querySelector(".card-body.tab-content");
    }

    function setSpotsBusy(isBusy) {
        const container = getSpotsBody();
        if (container) {
            container.setAttribute("aria-busy", isBusy ? "true" : "false");
        }
    }

    function getSourceSelect() {
        return document.getElementById("spotsSource");
    }

    function getSourceStatus() {
        return document.getElementById("spotsSourceStatus");
    }

    function getSelectedSource() {
        return getSourceSelect()?.value || "auto";
    }

    function isDemoMode() {
        return new URLSearchParams(window.location.search).get("demo") === "1";
    }

    function sourceLabel(source) {
        return SOURCE_LABELS[source] || source || "Unknown";
    }

    function cacheKeyFor(callSign, source) {
        return `${callSign}::${source}`;
    }

    function setSourceStatus(message, tone = "") {
        const status = getSourceStatus();
        if (!status) {
            return;
        }

        status.className = "spots-toolbar__status";
        if (tone) {
            status.classList.add(`spots-toolbar__status--${tone}`);
        } else {
            status.classList.add("text-body-secondary");
        }
        status.textContent = message;
    }

    function describeSourceStatus(meta, selectedSource) {
        const selectedLabel = sourceLabel(selectedSource);
        const actualSource = meta?.source_used || selectedSource;
        const actualLabel = sourceLabel(actualSource);

        if (selectedSource === "auto" && meta?.fallback_used) {
            return {
                message: `${selectedLabel}: downloader failed, showing ${actualLabel}.`,
                tone: "warning",
            };
        }

        if (selectedSource === "auto") {
            return {
                message: `${selectedLabel}: showing ${actualLabel}.`,
                tone: "success",
            };
        }

        return {
            message: `Source: ${actualLabel}.`,
            tone: "success",
        };
    }

    function getConfigPageUrl() {
        const url = new URL(window.location.href);
        url.searchParams.set("page", "config");
        return url.toString();
    }

    function renderState(title, body, action = null) {
        const container = getSpotsBody();
        if (!container) {
            return;
        }

        container.replaceChildren();
        setSpotsBusy(false);

        const state = document.createElement("div");
        state.className = "spots-state py-5 px-3";
        state.setAttribute("role", "status");
        state.setAttribute("aria-live", "polite");

        const titleElement = document.createElement("div");
        titleElement.className = "spots-state__title fw-semibold mb-2";
        titleElement.textContent = title;

        const bodyElement = document.createElement("p");
        bodyElement.className = "spots-state__body text-body-secondary mb-0";
        bodyElement.textContent = body;

        state.append(titleElement, bodyElement);

        if (action && action.label) {
            let actionElement;
            if (action.href) {
                actionElement = document.createElement("a");
                actionElement.href = action.href;
            } else {
                actionElement = document.createElement("button");
                actionElement.type = "button";
                actionElement.id = action.id || "spotsRetryButton";
            }

            actionElement.className = "btn btn-outline-primary spots-state__action mt-3";
            actionElement.textContent = action.label;
            state.appendChild(actionElement);
        }

        container.appendChild(state);
    }

    function renderLoading(selectedSource) {
        const container = getSpotsBody();
        if (!container) {
            return;
        }

        container.replaceChildren();
        setSpotsBusy(true);

        const state = document.createElement("div");
        state.className = "spots-state py-5 px-3";
        state.setAttribute("role", "status");
        state.setAttribute("aria-live", "polite");

        const spinner = document.createElement("div");
        spinner.className = "spinner-border text-primary mb-3";
        spinner.setAttribute("role", "status");

        const hiddenLabel = document.createElement("span");
        hiddenLabel.className = "visually-hidden";
        hiddenLabel.textContent = "Loading…";
        spinner.appendChild(hiddenLabel);

        const title = document.createElement("div");
        title.className = "spots-state__title fw-semibold";
        title.textContent = "Loading recent spots";

        const body = document.createElement("p");
        body.className = "spots-state__body text-body-secondary mb-0";
        body.textContent = `Checking the last hour of spot reports for this transmitter using ${sourceLabel(selectedSource)}.`;

        state.append(spinner, title, body);
        container.appendChild(state);
        setSourceStatus(`Loading spots via ${sourceLabel(selectedSource)}…`);
    }

    function renderError(msg, selectedSource) {
        renderState(
            "Unable to load spots",
            `${msg} Check the configured callsign and selected source, then load the spots list again.`,
            { id: "spotsRetryButton", label: "Load spots again" }
        );
        setSourceStatus(`${sourceLabel(selectedSource)} failed.`, "danger");
    }

    function normalizeSpotResponse(response) {
        if (Array.isArray(response)) {
            return { data: response, meta: {} };
        }

        if (response && Array.isArray(response.data)) {
            return {
                data: response.data,
                meta: {
                    source_used: response.source_used || "",
                    fallback_used: Boolean(response.fallback_used),
                    warning: typeof response.warning === "string" ? response.warning : "",
                }
            };
        }

        return null;
    }

    function resolveSpotsErrorMessage(xhr, status, selectedSource) {
        if (navigator.onLine === false) {
            return "This browser is offline, so the local spots proxy could not be reached.";
        }

        if (status === "timeout") {
            return `${sourceLabel(selectedSource)} did not respond before the request timed out.`;
        }

        if (status === "parsererror") {
            return "The spots proxy returned an unreadable response.";
        }

        if (!xhr || typeof xhr.status !== "number") {
            return "The spots request did not complete.";
        }

        if (xhr.status === 404) {
            return "The local spots proxy is not available on this installation.";
        }

        if (xhr.status === 429) {
            return "The upstream service rate-limited the request. Wait a moment, then try again.";
        }

        if (xhr.status >= 500) {
            return "The local spots proxy reported a server error while fetching spot data.";
        }

        if (xhr.status >= 400) {
            return "The spots request was rejected before any data could be returned.";
        }

        return "The spots request did not complete.";
    }

    function utcString(d) {
        return d.toISOString().slice(0, 19).replace("T", " ");
    }

    function buildDemoSpotsFixture() {
        const now = new Date();
        const minutesAgo = [8, 18, 28, 38, 48, 58];

        return [
            {
                tx_sign: DEMO_CALLSIGN,
                frequency: 7040219,
                tx_loc: "EM18ig",
                power: 20,
                rx_sign: "AE5AU",
                rx_loc: "EM13qc",
                distance: 512,
                code: 1,
                version: "SparkSDR",
                snr: -9,
                drift: 0,
            },
            {
                tx_sign: DEMO_CALLSIGN,
                frequency: 7040218,
                tx_loc: "EM18ig",
                power: 20,
                rx_sign: "K6VZK",
                rx_loc: "CM98pi",
                distance: 2328,
                code: 1,
                version: "WD_3.1.7",
                snr: -24,
                drift: -1,
            },
            {
                tx_sign: DEMO_CALLSIGN,
                frequency: 7040220,
                tx_loc: "EM18ig",
                power: 20,
                rx_sign: "KD7EFG-1",
                rx_loc: "CN87tl",
                distance: 2671,
                code: 1,
                version: "WD_3.2.3-1",
                snr: -21,
                drift: 1,
            },
            {
                tx_sign: DEMO_CALLSIGN,
                frequency: 7040219,
                tx_loc: "EM18ig",
                power: 20,
                rx_sign: "W3MLM",
                rx_loc: "FM18lw",
                distance: 1936,
                code: 1,
                version: "1.3 Kiwi",
                snr: -17,
                drift: 0,
            },
            {
                tx_sign: DEMO_CALLSIGN,
                frequency: 7040219,
                tx_loc: "EM18ig",
                power: 20,
                rx_sign: "KC0KVR",
                rx_loc: "EN26oi",
                distance: 1087,
                code: 1,
                version: "WD_3.2.3-1",
                snr: -12,
                drift: 0,
            },
            {
                tx_sign: DEMO_CALLSIGN,
                frequency: 7040218,
                tx_loc: "EM18ig",
                power: 20,
                rx_sign: "KA7OEI-9",
                rx_loc: "DN40xa",
                distance: 1598,
                code: 1,
                version: "SparkSDR",
                snr: -19,
                drift: -2,
            },
        ].map((spot, index) => {
            const timestamp = new Date(now.getTime() - minutesAgo[index] * 60 * 1000);
            return {
                time: utcString(timestamp),
                ...spot,
            };
        });
    }

    function refreshSpotsHeader() {
        const cs = isDemoMode() ? DEMO_CALLSIGN : ($("#callsign").val() || "");
        const now = new Date();
        const header = document.getElementById("spotsFor");
        if (!header) return;

        const formatter = new Intl.DateTimeFormat(undefined, {
            dateStyle: "medium",
            timeStyle: "medium",
            timeZone: "UTC"
        });

        header.textContent = "";

        const title = document.createElement("span");
        title.className = "spots-header-text";
        if (isDemoMode()) {
            title.textContent = `Recent demo spots for: ${cs}`;
        } else {
            title.textContent = cs ? `Recent spots for ${cs}` : "Recent spots";
        }

        const stamp = document.createElement("small");
        stamp.className = "spots-header-stamp text-body-secondary";
        stamp.textContent = `Updated ${formatter.format(now)} UTC`;

        header.appendChild(title);
        header.appendChild(stamp);
    }

    function renderTable(spots) {
        const $c = $(".card-body.tab-content").empty();
        setSpotsBusy(false);
        if (!Array.isArray(spots) || spots.length === 0) {
            return renderState(
                "No recent spots",
                "No spot reports were found in the last hour. This can be normal if the station has not transmitted recently or if propagation is poor."
            );
        }

        const $wrap = $("<div>").addClass("table-responsive");
        const $tbl = $("<table>").addClass("table table-hover table-sm align-middle");
        const $thead = $("<thead>").addClass("table-light");
        const $hrow = $("<tr>");
        COLUMNS.forEach(col => {
            $("<th>").attr("scope", "col").text(HEADERS[col] || col).appendTo($hrow);
        });
        $thead.append($hrow);
        $tbl.append($thead);

        const $tbody = $("<tbody>");
        spots.forEach(spot => {
            const $tr = $("<tr>");
            COLUMNS.forEach(col => {
                let val = spot[col];
                if (col === "code") val = CODE_MAP[val] || val;
                $("<td>").text(val ?? "").appendTo($tr);
            });
            $tbody.append($tr);
        });
        $tbl.append($tbody);
        $wrap.append($tbl);
        $c.append($wrap);

        const $pane = $(".spots-card .table-responsive");
        window.requestAnimationFrame(() => {
            $pane.scrollTop($pane.prop("scrollHeight"));
        });
    }

    function scheduleNext() {
        if (_refreshTimer !== null) {
            clearTimeout(_refreshTimer);
        }
        _refreshTimer = setTimeout(() => {
            _refreshTimer = null;
            fetchSpots();
        }, REFRESH_MS);
    }

    function fetchSpots() {
        const now = Date.now();
        const requestId = ++_requestSequence;
        clearRefreshTimer();
        clearActiveRequest();
        const selectedSource = getSelectedSource();
        const demoMode = isDemoMode();
        const rawCallSign = $("#callsign").val();
        const callSign = demoMode
            ? DEMO_CALLSIGN
            : (typeof rawCallSign === "string"
            ? rawCallSign.toUpperCase().trim()
            : "");

        if (!callSign) {
            renderState(
                "Finish station setup",
                "Save a station callsign on the Setup page first. The spots view uses that callsign to query the last hour of reports and confirm the station is being heard.",
                { href: getConfigPageUrl(), label: "Open Setup" }
            );
            setSourceStatus("A station callsign is required before spot lookup can run.");
            return scheduleNext();
        }

        const cacheKey = cacheKeyFor(callSign, selectedSource);
        if (_cacheKey !== cacheKey) {
            _cacheEntry = null;
            _cacheKey = cacheKey;
            _cacheTS = 0;
        }

        if (_cacheEntry && (now - _cacheTS) < TTL_MS) {
            renderTable(_cacheEntry.spots);
            refreshSpotsHeader();
            if (demoMode) {
                setSourceStatus("Demo data is active. Live spot lookup is bypassed.", "warning");
            } else {
                const status = describeSourceStatus(_cacheEntry.meta, selectedSource);
                setSourceStatus(status.message, status.tone);
            }
            return scheduleNext();
        }

        if (demoMode) {
            const spots = buildDemoSpotsFixture();
            _cacheEntry = {
                spots,
                meta: {
                    source_used: "demo",
                    fallback_used: false,
                },
            };
            _cacheKey = cacheKey;
            _cacheTS = now;
            renderTable(spots);
            refreshSpotsHeader();
            setSourceStatus("Demo data is active. Live spot lookup is bypassed.", "warning");
            return scheduleNext();
        }

        if (!_cacheEntry) renderLoading(selectedSource);
        const endDate = new Date(now);
        const startDate = new Date(now - MINUTES * 60 * 1000);

        _activeRequest = $.ajax({
            url: "fetch_spots.php",
            dataType: "json",
            cache: false,
            timeout: 15000,
            data: {
                tx_sign: callSign,
                start: utcString(startDate),
                end: utcString(endDate),
                format: "JSON",
                source: selectedSource,
            }
        })
            .done((response) => {
                if (requestId !== _requestSequence) {
                    return;
                }

                const proxyError =
                    response && typeof response.error === "string"
                        ? response.error.trim()
                        : "";
                if (proxyError) {
                    renderError(proxyError, selectedSource);
                    return;
                }

                const normalized = normalizeSpotResponse(response);
                if (!normalized || !Array.isArray(normalized.data)) {
                    renderError("The spots service returned an unexpected response.", selectedSource);
                    return;
                }

                const cutoff = now - 2 * 3600 * 1000;
                const spots = normalized.data.filter(s => {
                    const ts = Date.parse(`${s.time || ""}Z`);
                    return !isNaN(ts) && ts >= cutoff;
                });

                _cacheEntry = {
                    spots,
                    meta: normalized.meta,
                };
                _cacheKey = cacheKey;
                _cacheTS = now;

                renderTable(spots);
                refreshSpotsHeader();

                const status = describeSourceStatus(normalized.meta, selectedSource);
                setSourceStatus(status.message, status.tone);
            })
            .fail((xhr, status) => {
                if (status === "abort" || requestId !== _requestSequence) {
                    return;
                }

                let message = resolveSpotsErrorMessage(xhr, status, selectedSource);
                const proxyError =
                    xhr?.responseJSON && typeof xhr.responseJSON.error === "string"
                        ? xhr.responseJSON.error.trim()
                        : "";
                if (proxyError) {
                    message = proxyError;
                }

                renderError(message, selectedSource);
            })
            .always(() => {
                if (requestId === _requestSequence) {
                    _activeRequest = null;
                    setSpotsBusy(false);
                    scheduleNext();
                }
            });
    }

    window.fetchSpots = fetchSpots;
    window.refreshSpotsHeader = refreshSpotsHeader;

    $(document).on("click", "#spotsRetryButton", fetchSpots);
    $(document).on("change", "#spotsSource", () => {
        _cacheEntry = null;
        _cacheKey = "";
        _cacheTS = 0;
        fetchSpots();
    });

    window.addEventListener("offline", () => {
        _requestSequence++;
        clearRefreshTimer();
        clearActiveRequest();
        renderError("This browser went offline before the spots list could finish loading.", getSelectedSource());
    });
    window.addEventListener("online", fetchSpots);
    window.addEventListener("beforeunload", () => {
        clearRefreshTimer();
        clearActiveRequest();
    });

})(jQuery);

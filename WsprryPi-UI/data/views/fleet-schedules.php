<?php // SPDX-License-Identifier: MIT ?>
<section id="fleet-schedules" class="mb-4" aria-labelledby="fleet-schedules-heading" data-independent-wtp>
    <h4 id="fleet-schedules-heading" class="cw-control-section__title">Output schedules</h4>
    <p class="form-text">Assign a schedule to each remote output. This Pi keeps its own schedule and Enable setting. Up to eight remote outputs can run independently.</p>
    <div class="d-flex flex-wrap gap-2 mb-2">
        <button type="button" id="fleet-schedule-new" class="btn btn-primary">Assign a schedule</button>
        <button type="button" id="fleet-schedule-refresh" class="btn btn-outline-secondary">Refresh schedules</button>
    </div>
    <p id="fleet-schedule-feedback" class="form-text" role="status" aria-live="polite"></p>
    <div id="fleet-output-list" aria-busy="true"><p class="form-text">Open Fleet to load output schedules.</p></div>
    <div id="fleet-schedule-editor" class="fleet-editor mt-3" hidden>
        <h5 id="fleet-schedule-editor-heading" class="cw-control-section__title">Assign a schedule</h5>
        <div class="row gx-3 gy-3">
            <div class="col-12 config-stacked-field">
                <label for="fleet-schedule-device" class="form-label">Saved output</label>
                <select id="fleet-schedule-device" class="form-select" form="wtp-independent"></select>
                <div class="form-text">Add and identify a device above first. Assigning it here does not replace this Pi's local output.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field">
                <label for="fleet-schedule-mode" class="form-label">Mode</label>
                <select id="fleet-schedule-mode" class="form-select" form="wtp-independent"><option value="tone">Finite tone</option><option value="wspr">WSPR</option><option value="qrss">QRSS</option><option value="fskcw">FSKCW</option><option value="dfcw">DFCW</option></select>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field">
                <label for="fleet-schedule-frequency" class="form-label">RF base frequency (Hz)</label>
                <input id="fleet-schedule-frequency" class="form-control" type="number" min="1" max="200000000" step="any" value="14097100" form="wtp-independent">
                <div class="form-text">Lowest tone for WSPR; space for FSKCW; dot for DFCW. The target must support this frequency.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-schedule-modes="tone">
                <label for="fleet-schedule-duration" class="form-label">Tone duration (seconds)</label>
                <input id="fleet-schedule-duration" class="form-control" type="number" min="0.001" max="600" step="0.001" value="3" form="wtp-independent">
                <div class="form-text">The selected output must support the duration, frequency, and mode.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-schedule-modes="wspr">
                <label for="fleet-schedule-callsign" class="form-label">Callsign</label>
                <input id="fleet-schedule-callsign" class="form-control" maxlength="16" form="wtp-independent">
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-schedule-modes="wspr">
                <label for="fleet-schedule-locator" class="form-label">Grid locator</label>
                <input id="fleet-schedule-locator" class="form-control" maxlength="6" form="wtp-independent">
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-schedule-modes="wspr">
                <label for="fleet-schedule-power" class="form-label">Reported power (dBm)</label>
                <input id="fleet-schedule-power" class="form-control" type="number" min="0" max="60" value="10" form="wtp-independent">
            </div>
            <div class="col-12 config-stacked-field" data-schedule-modes="qrss fskcw dfcw">
                <label for="fleet-schedule-message" class="form-label">Message</label>
                <input id="fleet-schedule-message" class="form-control" maxlength="80" form="wtp-independent">
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-schedule-modes="qrss fskcw dfcw">
                <label for="fleet-schedule-dot" class="form-label">Dot length (seconds)</label>
                <input id="fleet-schedule-dot" class="form-control" type="number" min="0.1" max="60" step="0.001" value="3" form="wtp-independent">
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-schedule-modes="fskcw dfcw">
                <label for="fleet-schedule-shift" class="form-label">Shift (Hz)</label>
                <input id="fleet-schedule-shift" class="form-control" type="number" min="0.001" max="1000" step="any" value="4" form="wtp-independent">
            </div>
            <div class="col-12 col-lg-6 config-stacked-field">
                <label for="fleet-schedule-period" class="form-label">Repeat every (seconds)</label>
                <input id="fleet-schedule-period" class="form-control" type="number" min="60" max="86400" value="120" form="wtp-independent">
            </div>
            <div class="col-12 col-lg-6 config-stacked-field">
                <label for="fleet-schedule-phase" class="form-label">UTC phase (seconds)</label>
                <input id="fleet-schedule-phase" class="form-control" type="number" min="0" max="86399" value="0" form="wtp-independent">
                <div class="form-text">Offset within the repeat period. WSPR uses two-minute boundaries and starts at second 1.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" id="fleet-schedule-management-field">
                <label for="fleet-schedule-management" class="form-label">Pi status HTTP port</label>
                <input id="fleet-schedule-management" class="form-control" type="number" min="1" max="65535" value="31415" form="wtp-independent">
                <div class="form-text">Used to reconcile local takeovers on Pi targets. Pico targets use WTP status.</div>
            </div>
            <div class="col-12" id="fleet-schedule-plain-field">
                <div class="form-check"><input id="fleet-schedule-plain" class="form-check-input" type="checkbox" form="wtp-independent"><label for="fleet-schedule-plain" class="form-check-label">Use Plain LAN for this assignment. Device identity is unauthenticated.</label></div>
            </div>
            <div class="col-12" id="fleet-schedule-enable-field">
                <div class="form-check"><input id="fleet-schedule-enabled" class="form-check-input" type="checkbox" form="wtp-independent"><label for="fleet-schedule-enabled" class="form-check-label">Start this output's schedule after saving</label></div>
            </div>
        </div>
        <div class="d-flex flex-wrap gap-2 mt-3">
            <button type="button" id="fleet-schedule-save" class="btn btn-primary">Save assignment</button>
            <button type="button" id="fleet-schedule-cancel" class="btn btn-outline-secondary">Cancel</button>
        </div>
        <p id="fleet-schedule-editor-feedback" class="form-text mt-2" role="status" aria-live="polite"></p>
    </div>
</section>
<details id="fleet-listener" class="mb-4" data-independent-wtp>
    <summary>This Pi's WTP listener</summary>
    <p class="form-text mt-2">Allows other controllers to inspect this Pi and claim its output when local Enable is off. This setting does not enable local output or outbound schedules.</p>
    <div class="form-check form-switch mb-3"><input id="fleet-listener-enabled" class="form-check-input" type="checkbox" form="wtp-independent"><label for="fleet-listener-enabled" class="form-check-label">Accept Plain LAN connections</label></div>
    <div class="row gx-3 gy-3">
        <div class="col-12 col-lg-6 config-stacked-field"><label for="fleet-listener-port" class="form-label">TCP port</label><input id="fleet-listener-port" class="form-control" type="number" min="1" max="65535" value="31417" form="wtp-independent"></div>
        <div class="col-12 col-lg-6 config-stacked-field"><label for="fleet-listener-interface" class="form-label">LAN interface</label><input id="fleet-listener-interface" class="form-control" value="auto" maxlength="15" form="wtp-independent"><div class="form-text">Auto needs one eligible LAN address. With multiple interfaces, enter one name, such as wlan0.</div></div>
    </div>
    <button type="button" id="fleet-listener-save" class="btn btn-outline-primary mt-3">Save listener settings</button>
    <p id="fleet-listener-feedback" class="form-text mt-2" role="status" aria-live="polite"></p>
</details>
<div class="modal fade" id="fleet-action-confirmation" tabindex="-1" aria-labelledby="fleet-action-heading" aria-describedby="fleet-action-target fleet-action-consequence" aria-hidden="true" data-independent-wtp>
    <div class="modal-dialog modal-dialog-centered">
        <div class="modal-content">
            <div class="modal-header">
                <h3 class="modal-title h5" id="fleet-action-heading">Pause and stop?</h3>
                <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Cancel"></button>
            </div>
            <div class="modal-body">
                <p id="fleet-action-target" class="fw-semibold text-break"></p>
                <p id="fleet-action-consequence" class="mb-0"></p>
            </div>
            <div class="modal-footer">
                <button type="button" id="fleet-action-cancel" class="btn btn-outline-secondary" data-bs-dismiss="modal">Cancel</button>
                <button type="button" id="fleet-action-confirm" class="btn btn-danger">Pause and stop</button>
            </div>
        </div>
    </div>
</div>

<?php // SPDX-License-Identifier: MIT ?>
<section id="setup-scheduling" class="mt-3" hidden role="tabpanel" tabindex="0" aria-labelledby="scheduling-tab" data-independent-wtp>
    <h3 class="cw-control-section__title">Device-owned scheduling</h3>
    <p id="schedule-identity" class="form-text text-break"></p>
    <p id="schedule-feedback" role="status" aria-live="polite" class="form-text">Select a verified Pi to read its scheduling settings.</p>
    <p class="form-text">The selected device runs its schedule without this browser or controller. Schedule permission, solar policy, Transmit, boot behavior and immediate Stop are separate controls.</p>
    <dl id="schedule-summary" class="scheduling-summary text-break"></dl>
    <p id="schedule-hold" class="form-text"></p>
    <details class="mb-3"><summary>Target observations and forecasts</summary><dl id="schedule-observation" class="text-break mt-3"></dl></details>
    <div class="form-check form-switch my-3"><input class="form-check-input" type="checkbox" id="schedule-enabled" disabled aria-describedby="schedule-enable-hint"><label class="form-check-label" for="schedule-enabled">Enable scheduling</label></div>
    <p id="schedule-enable-hint" class="form-text">This switch prepares a browser choice only. Save policy while scheduling is disabled, read back, then explicitly enable the confirmed saved settings. Disable admits no new work; accepted work may finish.</p>
    <div id="schedule-fields"></div>
    <div class="d-flex flex-wrap gap-2 mt-3">
        <button type="button" id="schedule-read" class="btn btn-outline-secondary">Read saved scheduling</button>
        <button type="button" id="schedule-preview" class="btn btn-outline-secondary" disabled>Review draft</button>
        <button type="button" id="schedule-save" class="btn btn-outline-primary" disabled>Save disabled policy</button>
        <button type="button" id="schedule-enable" class="btn btn-outline-primary" disabled>Enable saved schedule</button>
        <button type="button" id="schedule-disable" class="btn btn-outline-secondary" disabled>Disable saved schedule</button>
        <button type="button" id="schedule-check" class="btn btn-outline-secondary" hidden>Check earlier scheduling request</button>
        <button type="button" id="schedule-discard" class="btn btn-outline-secondary" disabled>Discard draft and read again</button>
    </div>
    <p id="schedule-action-feedback" class="form-text" role="status" aria-live="polite"></p>
    <p id="schedule-preview-result" class="form-text text-break" role="status"></p>
    <div id="schedule-recovery" class="mt-3" hidden>
        <p class="form-text">The earlier result is unknown. Checking or reviewing never resends it. Read current settings and compare your retained draft before explicitly reviewing.</p>
        <button type="button" id="schedule-read-review" class="btn btn-outline-secondary">Read current scheduling for review</button>
        <pre id="schedule-review-values" class="form-text text-break scheduling-review"></pre>
        <button type="button" id="schedule-confirm-review" class="btn btn-outline-secondary" disabled>I reviewed these settings</button>
    </div>
</section>

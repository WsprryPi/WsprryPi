<?php // SPDX-License-Identifier: MIT ?>
<section id="setup-pi-workspace" class="mt-3" data-independent-wtp hidden role="tabpanel" tabindex="0" aria-labelledby="radio-tab">
    <h4 id="pi-target-heading" class="cw-control-section__title" tabindex="-1">Pi settings</h4>
    <p id="pi-target-identity" class="form-text text-break"></p>
    <p id="pi-target-feedback" class="form-text" role="status" aria-live="polite">Choose a paired Pi in Device to read its saved settings.</p>
    <p class="form-text">Changes stay in this browser until you save. Saving affects only the selected Pi and keeps its saved scheduling settings.</p>
    <div class="d-flex flex-wrap gap-2 mb-3">
        <button id="pi-target-save" type="button" class="btn btn-outline-primary" disabled>Save this Pi’s settings</button>
        <button id="pi-target-refresh" type="button" class="btn btn-outline-secondary">Read saved settings</button>
        <button id="pi-target-check" type="button" class="btn btn-outline-secondary" hidden>Check earlier request</button>
        <button id="pi-target-discard" type="button" class="btn btn-outline-secondary" hidden>Discard draft and read again</button>
    </div>
    <div id="pi-target-discard-confirmation" class="mb-3" hidden>
        <p>Discard this Pi’s unsaved draft and read its current settings?</p>
        <button id="pi-target-discard-yes" type="button" class="btn btn-outline-danger">Discard draft</button>
        <button id="pi-target-discard-no" type="button" class="btn btn-outline-secondary">Keep draft</button>
    </div>
    <div id="pi-target-review" class="mb-3" hidden>
        <p class="form-text">The earlier result is unconfirmed. Read and review current settings before another settings change. Stop remains available separately. Review does not repeat the request or claim it succeeded.</p>
        <button id="pi-target-read-review" type="button" class="btn btn-outline-secondary">Read current settings for review</button>
        <dl id="pi-target-review-values" class="text-break mt-2"></dl>
        <button id="pi-target-confirm-review" type="button" class="btn btn-outline-primary" disabled>I reviewed these settings</button>
    </div>
    <div id="pi-target-fields"></div>
    <details class="mt-4">
        <summary>Stop this Pi’s output</summary>
        <p class="form-text">Read its current output first, then confirm Stop. This turns off its saved Transmit permission; it does not change schedule enablement or pairing.</p>
        <button id="pi-target-read-output" type="button" class="btn btn-outline-secondary">Read current output</button>
        <p id="pi-target-output" class="form-text" role="status" aria-live="polite"></p>
        <button id="pi-target-stop" type="button" class="btn btn-outline-danger" disabled>Stop the output shown above</button>
    </details>
</section>

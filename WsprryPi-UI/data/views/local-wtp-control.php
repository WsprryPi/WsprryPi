<?php // SPDX-License-Identifier: MIT ?>
<section id="local-wtp-control" class="local-wtp-control mt-2" aria-label="Local output ownership" hidden>
    <p id="local-wtp-status" class="form-text mb-2" role="status" aria-live="polite"></p>
    <button type="button" id="local-wtp-recover" class="btn btn-outline-secondary btn-sm" hidden>Reconcile local output</button>
    <div id="local-wtp-confirmation" class="mt-2" role="group" aria-labelledby="local-wtp-confirm-title" hidden>
        <h3 id="local-wtp-confirm-title" class="h6">Take local control</h3>
        <p id="local-wtp-confirm-detail" class="text-break mb-2"></p>
        <p class="form-text">This Pi takes priority. Future assignments saved on its controller will be removed when the controller reconnects.</p>
        <div class="d-flex flex-wrap gap-2">
            <button type="button" id="local-wtp-end-now" class="btn btn-danger">End now</button>
            <button type="button" id="local-wtp-finish" class="btn btn-outline-primary">Let it finish</button>
            <button type="button" id="local-wtp-decline" class="btn btn-outline-secondary">Cancel</button>
        </div>
    </div>
</section>

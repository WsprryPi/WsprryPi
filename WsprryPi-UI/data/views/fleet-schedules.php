<?php // SPDX-License-Identifier: MIT ?>
<section id="fleet-schedules" class="mb-4" aria-labelledby="fleet-schedules-heading" data-independent-wtp>
    <h4 id="fleet-schedules-heading" class="cw-control-section__title">Legacy controller schedules</h4>
    <p class="form-text">These saved entries are read-only. This Pi cannot dispatch remote transmissions. Complete outstanding ownership, cleanup, and retirement with WTP-capable software before replacement. Manage each device’s native schedules separately.</p>
    <button type="button" id="fleet-schedule-refresh" class="btn btn-outline-secondary">Read saved state</button>
    <p id="fleet-schedule-feedback" class="form-text" role="status" aria-live="polite"></p>
    <div id="fleet-output-list" aria-busy="true"></div>
    <section id="controllerRetirement" class="mt-3" aria-labelledby="controllerRetirementHeading" hidden>
        <h5 id="controllerRetirementHeading" class="cw-control-section__title">Controller retirement</h5>
        <button type="button" id="controllerRetirementRefresh" class="btn btn-outline-secondary">Read retirement receipt</button>
        <p id="controllerRetirementFeedback" class="form-text text-break" role="status" aria-live="polite"></p>
    </section>
</section>

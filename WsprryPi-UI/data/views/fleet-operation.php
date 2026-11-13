<div id="operationFleetView" hidden>
    <section class="operation-hero" aria-labelledby="operationFleetName">
        <div class="operation-hero__status">
            <div class="operation-hero__headline" role="status" aria-live="polite" aria-atomic="true">
                <div class="operation-hero__state">
                    <span id="operationFleetName" class="operation-hero__state-label">Selected output</span>
                    <span id="operationFleetState" class="operation-hero__state-value">Device state unknown</span>
                </div>
                <p id="operationFleetDetail" class="operation-hero__detail"></p>
            </div>
            <p id="operationFleetIdentity" class="form-text text-break"></p>
            <p id="operationFleetAge" class="form-text" aria-live="off"></p>
            <p id="operationFleetOutput" class="form-text"></p>
        </div>
        <div class="operation-hero__controls" aria-label="Selected output controls">
            <p id="operationFleetScheduleState" class="mb-2"></p>
            <button type="button" class="btn btn-primary" id="operationFleetEnable" disabled>Resume schedule</button>
            <button type="button" class="btn btn-danger" id="operationFleetPause" disabled>Pause and stop</button>
            <div class="d-flex flex-wrap gap-2 mt-2">
                <button type="button" class="btn btn-outline-secondary" id="operationFleetRecover" disabled>Reconcile</button>
                <button type="button" class="btn btn-outline-secondary" id="operationFleetEdit" disabled>Edit schedule</button>
                <button type="button" class="btn btn-outline-secondary" id="operationFleetRefresh">Read status</button>
                <button type="button" class="btn btn-outline-secondary" id="operationFleetReview" hidden>Review current state</button>
                <button type="button" class="btn btn-outline-secondary" id="operationFleetReturn" hidden>Return to This Pi</button>
            </div>
            <p id="operationFleetFeedback" class="form-text" role="status" aria-live="polite" aria-atomic="true"></p>
        </div>
    </section>
    <section class="operation-summary-grid" aria-label="Selected output summary">
        <article class="operation-panel operation-panel--primary">
            <div class="operation-panel__label">Assigned signal</div><div id="operationFleetMode" class="operation-panel__value">Unavailable</div>
        </article>
        <article class="operation-panel">
            <div id="operationFleetFrequencyLabel" class="operation-panel__label">Assigned frequency</div>
            <div id="operationFleetFrequency" class="operation-panel__value operation-panel__value--wrap">Unavailable</div>
            <div id="operationFleetRealized" class="form-text"></div>
        </article>
        <article class="operation-panel operation-panel--wide">
            <div class="operation-panel__label">Assigned schedule</div><div id="operationFleetSchedule" class="operation-panel__value operation-panel__value--wrap">Unavailable</div>
            <p id="operationFleetStart" class="form-text mb-0" aria-live="off"></p>
        </article>
        <article class="operation-panel operation-panel--wide">
            <div class="operation-panel__label">Last execution / reconciliation</div><div id="operationFleetHistory" class="operation-panel__value operation-panel__value--wrap">No historical report</div>
        </article>
    </section>
    <p class="form-text mt-3">Clocks in the header belong to the controller. Device status describes software observations; it does not establish measured RF output.</p>
</div>
<div class="modal fade" id="operationFleetConfirmation" tabindex="-1" aria-labelledby="operationFleetConfirmationTitle" aria-describedby="operationFleetConfirmationText" aria-hidden="true">
    <div class="modal-dialog modal-dialog-centered"><div class="modal-content">
        <div class="modal-header"><h2 class="modal-title fs-5" id="operationFleetConfirmationTitle">Pause and stop?</h2><button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Cancel"></button></div>
        <div class="modal-body"><p id="operationFleetConfirmationText" class="text-break mb-0"></p></div>
        <div class="modal-footer"><button type="button" class="btn btn-secondary" id="operationFleetCancel" data-bs-dismiss="modal">Cancel</button><button type="button" class="btn btn-danger" id="operationFleetConfirm">Pause and stop</button></div>
    </div></div>
</div>

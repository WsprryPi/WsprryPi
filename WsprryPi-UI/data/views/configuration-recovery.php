                <section id="configurationRecoveryPanel" aria-labelledby="configurationRecoveryTitle" hidden>
                    <h2 id="configurationRecoveryTitle" class="h4">Review saved settings</h2>
                    <p id="configurationRecoveryExplanation">These settings are an unconfirmed draft. Reviewing or editing them does not save anything.</p>
                    <p class="form-text" id="configurationRecoveryStartupHint">After a valid save, normal startup resumes with the saved enable-on-boot policy. Transmission controls remain unavailable until startup is verified.</p>
                    <div class="d-flex flex-wrap gap-2 mt-4">
                        <button type="button" class="btn btn-primary" id="configurationRecoverySave">Save corrected settings</button>
                        <button type="button" class="btn btn-outline-primary" id="configurationRecoveryRepair">Repair missing settings</button>
                        <button type="button" class="btn btn-outline-secondary" id="configurationRecoveryRetry" hidden>Check saved result</button>
                        <button type="button" class="btn btn-outline-secondary" id="configurationRecoveryReturn" hidden>Return to Setup</button>
                    </div>
                    <p class="form-text" id="configurationRecoveryRepairHint">Repair retains compatible saved values and fills missing settings from the installed defaults.</p>
                    <p id="configurationRecoveryFeedback" class="mt-3 mb-0" role="status" aria-live="polite" aria-atomic="true"></p>
                    <div id="configurationRecoveryFields" class="mt-3 col-lg-8 col-xl-7"></div>
                    <div class="mt-4" id="configurationRecoveryBottomActions">
                        <button type="button" class="btn btn-primary" id="configurationRecoverySaveBottom" aria-describedby="configurationRecoveryFeedbackBottom">Save corrected settings</button>
                        <p id="configurationRecoveryFeedbackBottom" class="mt-2 mb-0"></p>
                    </div>
                    <details class="mt-4 pt-3 border-top" id="configurationRecoveryResetSection">
                        <summary>Restore default settings</summary>
                        <p class="mt-2">This replaces the current configuration and your unsaved recovery edits with the installed defaults. It cannot clear unverifiable saved history.</p>
                        <button type="button" class="btn btn-outline-danger" id="configurationRecoveryReset">Confirm restore defaults</button>
                    </details>
                </section>

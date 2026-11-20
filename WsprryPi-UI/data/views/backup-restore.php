<?php // SPDX-License-Identifier: MIT ?>
                <section id="backupRestore" class="maintenance-pane mb-4" aria-labelledby="backupRestoreTitle" aria-busy="false">
                    <h2 id="backupRestoreTitle" class="maintenance-pane__title h5 mb-0">Backup and restore</h2>
                    <p class="maintenance-pane__body mb-0">Protect this installation’s saved settings and recovery information. Remote devices need their own backups. Keep the backup and its passphrase safe; a lost passphrase cannot be recovered.</p>
                    <form id="backupCreateForm" class="pt-3 border-top" aria-labelledby="backupCreateTitle">
                        <h3 id="backupCreateTitle" class="h6">Create an encrypted backup</h3>
                        <div class="row g-3 mb-3">
                            <div class="col-md-6"><label for="backupPassphrase" class="form-label">Backup passphrase</label><input id="backupPassphrase" class="form-control" type="password" autocomplete="new-password" required aria-describedby="backupPassphraseHint"></div>
                            <div class="col-md-6"><label for="backupConfirmation" class="form-label">Confirm passphrase</label><input id="backupConfirmation" class="form-control" type="password" autocomplete="new-password" required></div>
                        </div>
                        <p id="backupPassphraseHint" class="form-text">Use a long, unique passphrase. If this installation transmits locally, stop transmission before creating a backup.</p>
                        <button id="backupCreate" class="btn btn-primary" type="submit">Create backup</button>
                        <p id="backupFeedback" class="mt-2 mb-0" role="status" aria-live="polite" aria-atomic="true"></p>
                    </form>
                    <form id="restoreUploadForm" class="pt-3 border-top" aria-labelledby="restoreUploadTitle">
                        <h3 id="restoreUploadTitle" class="h6">Restore from a backup</h3>
                        <label for="restoreFile" class="form-label">Encrypted backup file</label>
                        <input id="restoreFile" class="form-control mb-3" type="file" accept=".age,application/octet-stream" required aria-describedby="restoreUploadHint">
                        <p id="restoreUploadHint" class="form-text">Upload a WsprryPi backup, up to 2 GiB. Uploading and validating do not replace current settings.</p>
                        <button id="restoreUpload" class="btn btn-outline-primary" type="submit">Upload backup</button>
                        <p id="restoreUploadFeedback" class="mt-2 mb-0" role="status" aria-live="polite" aria-atomic="true"></p>
                    </form>
                    <div class="pt-3 border-top">
                        <label for="recoveryJob" class="form-label">Recent backup or restore</label>
                        <div class="d-flex flex-wrap gap-2"><select id="recoveryJob" class="form-select pairing-device" disabled><option value="">Checking saved results…</option></select><button id="recoveryRefresh" class="btn btn-outline-secondary" type="button">Refresh results</button></div>
                        <p id="recoveryStatus" class="mt-3 mb-0 text-break" role="status" aria-live="polite" aria-atomic="true" tabindex="-1">Checking maintenance access…</p>
                        <p id="recoveryActionFeedback" class="mt-2 mb-0 text-break" role="status" aria-live="polite" aria-atomic="true" tabindex="-1"></p>
                        <a id="backupDownload" class="btn btn-outline-primary mt-3" hidden download>Download backup</a>
                        <form id="restoreValidateForm" class="mt-3" hidden aria-labelledby="restoreValidateTitle">
                            <h3 id="restoreValidateTitle" class="h6">Validate and review</h3>
                            <label for="restorePurpose" class="form-label">Restore purpose</label>
                            <select id="restorePurpose" class="form-select mb-3"><option value="recover">Recover this installation</option><option value="move">Move settings to another computer</option></select>
                            <p class="form-text">Moving keeps the destination’s hardware settings and identity. Retire the source installation before using relocated credentials.</p>
                            <label for="restorePassphrase" class="form-label">Backup passphrase</label>
                            <input id="restorePassphrase" class="form-control mb-3" type="password" autocomplete="off" required>
                            <button id="restoreValidate" class="btn btn-outline-primary" type="submit">Validate backup</button>
                        </form>
                        <section id="restorePreview" class="mt-3" hidden aria-labelledby="restorePreviewTitle">
                            <h3 id="restorePreviewTitle" class="h6" tabindex="-1">Review the proposed restore</h3>
                            <p id="restoreSource" class="text-break"></p>
                            <div class="table-responsive"><table class="table table-sm"><caption class="caption-top">Settings that will change</caption><thead><tr><th scope="col">Setting</th><th scope="col">Current</th><th scope="col">Restored</th></tr></thead><tbody id="restoreChanges"></tbody></table></div>
                            <p id="restoreRetained" class="text-break"></p>
                            <p id="restoreDefinitions" class="text-break"></p>
                            <p id="restoreListeners" class="text-break"></p>
                            <p>Restoring stops the local backend and retains a recovery copy. Local transmission and remote authority remain held for destination review. Existing remote schedules continue; they are not adopted or replayed.</p>
                            <div class="form-check mb-3"><input id="restoreConsent" class="form-check-input" type="checkbox"><label for="restoreConsent" class="form-check-label">I reviewed this destination and the proposed changes.</label></div>
                            <button id="restoreConfirm" class="btn btn-danger" type="button" disabled>Confirm restore</button>
                        </section>
                        <div class="d-flex flex-wrap gap-2 mt-3"><button id="restoreCancel" class="btn btn-outline-secondary" type="button" hidden>Cancel staged restore</button><button id="restoreResume" class="btn btn-outline-primary" type="button" hidden>Recover this restore</button></div>
                    </div>
                    <section class="pt-3 border-top" aria-labelledby="destinationReviewTitle">
                        <h3 id="destinationReviewTitle" class="h6">Destination review after restore</h3>
                        <button id="destinationRead" class="btn btn-outline-secondary" type="button">Read current destination</button>
                        <div id="destinationReview" class="mt-3" hidden>
                            <p id="destinationHoldState" class="text-break"></p>
                            <details><summary>Current saved settings</summary><div class="table-responsive"><table class="table table-sm"><caption class="visually-hidden">Current destination settings</caption><thead><tr><th scope="col">Setting</th><th scope="col">Saved value</th></tr></thead><tbody id="destinationSettings"></tbody></table></div></details>
                            <p id="destinationReviewPolicy" class="mt-3">Review the destination’s station, frequencies, timing, hardware, and startup settings. Releasing the local hold leaves transmission stopped; use Start when ready. Releasing the remote hold permits existing pairing recovery and authority checks; it does not establish authority or adopt remote jobs.</p>
                            <div id="destinationLocalRow" class="form-check mb-2"><input id="destinationLocal" class="form-check-input" type="checkbox"><label for="destinationLocal" class="form-check-label">I reviewed this destination for local operation.</label></div>
                            <div class="form-check mb-3"><input id="destinationRemote" class="form-check-input" type="checkbox"><label for="destinationRemote" class="form-check-label">I reviewed the destination’s controller access and will recover pairing where required.</label></div>
                            <button id="destinationRelease" class="btn btn-primary" type="button" disabled>Release selected holds</button>
                        </div>
                        <p id="destinationFeedback" class="mt-2 mb-0" role="status" aria-live="polite" aria-atomic="true" tabindex="-1"></p>
                    </section>
                </section>

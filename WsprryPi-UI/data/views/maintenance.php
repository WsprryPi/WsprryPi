<?php
$cardTitleId = 'cardTitle';
$cardTitleText = 'Wsprry Pi Maintenance';
require __DIR__ . '/../card_header.php';
?>

            <div
                id="globalToastContainer"
                class="toast-container position-fixed start-50 translate-middle-x p-3">
            </div>

            <div class="card-body tab-content bg-body">
                <section
                    id="maintenanceResult"
                    class="maintenance-result d-none mb-4"
                    role="status"
                    aria-live="polite"
                    aria-atomic="true">
                    <div class="maintenance-result__label" id="maintenanceResultLabel">Maintenance update</div>
                    <div class="maintenance-result__title" id="maintenanceResultTitle"></div>
                    <p class="maintenance-result__body mb-0" id="maintenanceResultBody"></p>
                </section>

                <section class="maintenance-recovery" aria-label="Recovery actions">
                    <div class="maintenance-recovery__grid">
                        <section class="maintenance-pane maintenance-pane--primary">
                            <h2 class="maintenance-pane__title h5 mb-0">Repair configuration</h2>
                            <p class="maintenance-pane__body mb-0">
                                Check the current configuration for missing or invalid values and repair what can be repaired while preserving usable settings.
                            </p>
                            <dl class="maintenance-fact-list">
                                <div class="maintenance-fact">
                                    <dt>Keeps</dt>
                                    <dd>Existing values whenever they are still valid.</dd>
                                </div>
                                <div class="maintenance-fact">
                                    <dt>Use when</dt>
                                    <dd>The transmitter worked before and only needs cleanup.</dd>
                                </div>
                                <div class="maintenance-fact">
                                    <dt>Next</dt>
                                    <dd>Setup reloads so you can confirm repaired values before transmitting.</dd>
                                </div>
                            </dl>
                            <div class="maintenance-action maintenance-action--start">
                                <button
                                    id="repairConfigButton"
                                    type="button"
                                    class="btn btn-warning">
                                    Repair current configuration
                                </button>
                            </div>
                        </section>

                        <section class="maintenance-pane maintenance-pane--danger">
                            <h2 class="maintenance-pane__title h5 mb-0">Reset to stock defaults</h2>
                            <p class="maintenance-pane__body mb-0">
                                Replace the current configuration with the stock baseline when the existing settings are no longer trustworthy.
                            </p>
                            <dl class="maintenance-fact-list maintenance-fact-list--danger">
                                <div class="maintenance-fact">
                                    <dt>Replaces</dt>
                                    <dd>The current configuration with stock defaults.</dd>
                                </div>
                                <div class="maintenance-fact">
                                    <dt>Use when</dt>
                                    <dd>You need a clean baseline instead of trying to salvage the current values.</dd>
                                </div>
                                <div class="maintenance-fact">
                                    <dt>Next</dt>
                                    <dd>Review and re-save station, mode, and hardware settings in Setup.</dd>
                                </div>
                            </dl>
                            <div class="maintenance-action maintenance-action--start">
                                <button
                                    id="restoreConfigButton"
                                    type="button"
                                    class="btn btn-danger">
                                    Reset to defaults
                                </button>
                            </div>
                        </section>
                    </div>
                </section>

                <section
                    id="networkSafetyPanel"
                    class="maintenance-pane maintenance-pane--network-safety"
                    aria-labelledby="networkSafetyTitle">
                    <div class="maintenance-network-safety__heading">
                        <div>
                            <h2 id="networkSafetyTitle" class="maintenance-section-title h5 mb-0">Privileged network safety</h2>
                            <p class="maintenance-pane__body mb-0">
                                Limit configuration changes, device controls, support bundles, and the WebSocket endpoint to this Pi's directly connected LAN.
                            </p>
                        </div>
                        <div
                            id="networkSafetyBanner"
                            class="maintenance-network-safety__banner"
                            data-state="loading"
                            role="status"
                            aria-live="polite"
                            aria-atomic="true">
                            Checking network safety…
                        </div>
                    </div>

                    <dl class="maintenance-network-safety__states" aria-label="Network safety state">
                        <div class="maintenance-fact">
                            <dt>Requested</dt>
                            <dd id="networkSafetyRequested">No change selected</dd>
                        </div>
                        <div class="maintenance-fact">
                            <dt>Configured</dt>
                            <dd id="networkSafetyConfigured">Checking…</dd>
                        </div>
                        <div class="maintenance-fact">
                            <dt>Active</dt>
                            <dd id="networkSafetyActive">Checking…</dd>
                        </div>
                    </dl>

                    <fieldset class="maintenance-network-safety__choices">
                        <legend class="form-label fw-semibold mb-2">Choose the requested state</legend>
                        <div class="form-check">
                            <input class="form-check-input" type="radio" name="networkSafetyMode" id="networkSafetyEnforced" value="enforced">
                            <label class="form-check-label" for="networkSafetyEnforced">
                                <strong>Enforced</strong> <span class="text-body-secondary">— recommended; privileged browser operation requires Apache and a directly connected LAN client.</span>
                            </label>
                        </div>
                        <div class="form-check mt-2">
                            <input class="form-check-input" type="radio" name="networkSafetyMode" id="networkSafetyDisabled" value="insecure-disabled">
                            <label class="form-check-label" for="networkSafetyDisabled">
                                <strong>Insecure disabled</strong> <span class="text-body-secondary">— permits protected operations outside the directly connected LAN.</span>
                            </label>
                        </div>
                    </fieldset>

                    <section id="networkSafetyDisableConfirmation" class="maintenance-network-safety__confirmation d-none" aria-labelledby="networkSafetyDisableTitle">
                        <h3 id="networkSafetyDisableTitle" class="h6 mb-0">Confirm the insecure override</h3>
                        <p class="mb-0">
                            This removes only the peer/subnet restriction. Host, Origin, CORS, malformed-request, and forwarded-header protections remain active.
                        </p>
                        <label class="form-label mb-0" for="networkSafetyDisablePhrase">
                            Type <code>DISABLE LOCAL-LAN SAFETY</code> exactly to continue.
                        </label>
                        <input
                            id="networkSafetyDisablePhrase"
                            class="form-control"
                            type="text"
                            autocomplete="off"
                            spellcheck="false"
                            aria-describedby="networkSafetyDisableHelp networkSafetyPhraseStatus">
                        <p id="networkSafetyDisableHelp" class="small mb-0">The phrase is case-sensitive. It is cleared after a confirmed successful change or when you cancel this draft.</p>
                        <p id="networkSafetyPhraseStatus" class="small fw-semibold mb-0" role="status" aria-live="polite">Enter the exact phrase to unlock Apply.</p>
                    </section>

                    <div id="networkSafetyResult" class="maintenance-network-safety__result d-none" role="alert" aria-live="assertive" aria-atomic="true"></div>

                    <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                        <button id="applyNetworkSafetyButton" type="button" class="btn btn-primary" disabled>
                            Apply requested state
                        </button>
                        <button id="cancelNetworkSafetyButton" type="button" class="btn btn-outline-secondary d-none">
                            Cancel change
                        </button>
                    </div>
                </section>

                <section class="maintenance-utility" aria-label="Maintenance utilities">
                    <div class="maintenance-utility__grid">
                        <section class="maintenance-pane maintenance-pane--utility" aria-labelledby="maintenanceUtilityTitle">
                            <h2 id="maintenanceUtilityTitle" class="maintenance-section-title h5 mb-0">Transmit test tone</h2>
                            <p class="maintenance-pane__body mb-0">
                                Test tone opens the manual tone dialog so you can start or stop a quick output-path check and then return to normal scheduling.
                            </p>
                            <div class="maintenance-action maintenance-action--start">
                                <button
                                    id="test_tone"
                                    type="button"
                                    class="btn btn-outline-warning"
                                    data-bs-toggle="tooltip"
                                    title="Click to generate a test tone">
                                    Test tone
                                </button>
                            </div>
                        </section>

                        <section
                            id="updateCheckPanel"
                            class="maintenance-pane maintenance-pane--update"
                            aria-labelledby="updateCheckPanelTitle">
                            <h2 id="updateCheckPanelTitle" class="maintenance-section-title h5 mb-0">Checking update status</h2>
                            <div
                                id="updateCheckStatus"
                                class="maintenance-update-status visually-hidden"
                                role="status"
                                aria-live="polite"
                                aria-atomic="true">
                                Waiting for version data.
                            </div>
                            <details id="updateCheckTechnical" class="maintenance-update-technical d-none">
                                <summary id="updateCheckTechnicalSummary">Technical details ▼</summary>
                                <dl id="updateCheckTechnicalList" class="maintenance-update-technical-list"></dl>
                            </details>
                            <div id="updateCheckAction" class="maintenance-update-action d-none"></div>
                            <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                <button
                                    id="updateCheckNowBtn"
                                    type="button"
                                    class="btn btn-outline-primary">
                                    Check now
                                </button>
                                <button
                                    id="updateCheckToggleBtn"
                                    type="button"
                                    class="btn btn-outline-secondary">
                                    Disable update checks
                                </button>
                            </div>
                        </section>

                        <section
                            id="supportBundlePanel"
                            class="maintenance-pane maintenance-pane--support"
                            aria-labelledby="supportBundlePanelTitle">
                            <h2 id="supportBundlePanelTitle" class="maintenance-section-title h5 mb-0">Support Bundle</h2>
                            <p class="maintenance-pane__body mb-0">
                                Create a private diagnostic candidate, download the readable archive, and review it before approving the exact bytes for encryption.
                            </p>
                            <div
                                id="supportBundleStatus"
                                class="maintenance-support-status visually-hidden"
                                role="status"
                                aria-live="polite"
                                aria-atomic="true">
                            </div>
                            <div
                                id="supportBundleAlert"
                                class="alert alert-danger d-none mb-0"
                                role="alert"
                                aria-live="assertive"
                                aria-atomic="true">
                            </div>
                            <section id="supportBundleSetup" class="maintenance-support-setup d-none" aria-labelledby="supportBundleSetupTitle">
                                <h3 id="supportBundleSetupTitle" class="h6 mb-0">Describe the support request</h3>
                                <fieldset class="maintenance-support-context">
                                    <legend class="form-label mb-2">Issue correlation</legend>
                                    <div class="form-check">
                                        <input class="form-check-input" type="radio" name="supportBundleContextKind" id="supportBundleExistingIssue" value="existing_github_issue" checked>
                                        <label class="form-check-label" for="supportBundleExistingIssue">I already have a WsprryPi GitHub issue</label>
                                    </div>
                                    <div class="form-check">
                                        <input class="form-check-input" type="radio" name="supportBundleContextKind" id="supportBundleNewIssue" value="new_github_issue">
                                        <label class="form-check-label" for="supportBundleNewIssue">I will create a GitHub issue after collection</label>
                                    </div>
                                    <div class="form-check">
                                        <input class="form-check-input" type="radio" name="supportBundleContextKind" id="supportBundleNoGithub" value="no_github">
                                        <label class="form-check-label" for="supportBundleNoGithub">I am not using GitHub</label>
                                    </div>
                                </fieldset>
                                <div id="supportBundleExistingIssueFields">
                                    <label class="form-label" for="supportBundleIssueNumber">Existing issue number</label>
                                    <div class="input-group">
                                        <span class="input-group-text">#</span>
                                        <input id="supportBundleIssueNumber" class="form-control" inputmode="numeric" pattern="[1-9][0-9]*" maxlength="10" aria-describedby="supportBundleIssueNumberHelp supportBundleIssueNumberError">
                                    </div>
                                    <div id="supportBundleIssueNumberHelp" class="form-text">Enter the number from github.com/WsprryPi/WsprryPi/issues.</div>
                                    <div id="supportBundleIssueNumberError" class="invalid-feedback d-block" role="alert"></div>
                                </div>
                                <div id="supportBundleDescriptionFields" class="d-none">
                                    <label class="form-label" for="supportBundleProblemDescription">Problem description</label>
                                    <textarea id="supportBundleProblemDescription" class="form-control" rows="3" maxlength="4096" aria-describedby="supportBundleProblemDescriptionHelp supportBundleProblemDescriptionError"></textarea>
                                    <div id="supportBundleProblemDescriptionHelp" class="form-text">Describe what happened, what you expected, and when it occurred.</div>
                                    <div id="supportBundleProblemDescriptionError" class="invalid-feedback d-block" role="alert"></div>
                                    <label class="form-label mt-3" for="supportBundleContact">Contact information</label>
                                    <input id="supportBundleContact" class="form-control" maxlength="512" autocomplete="email" aria-describedby="supportBundleContactHelp supportBundleContactError">
                                    <div id="supportBundleContactHelp" class="form-text">Provide a method the maintainer can use to follow up. Identity is not verified.</div>
                                    <div id="supportBundleContactError" class="invalid-feedback d-block" role="alert"></div>
                                </div>
                                <div class="form-check">
                                    <input id="supportBundleProbeI2c" class="form-check-input" type="checkbox" aria-describedby="supportBundleProbeI2cHelp">
                                    <label class="form-check-label" for="supportBundleProbeI2c">Actively probe I²C bus 1</label>
                                    <div id="supportBundleProbeI2cHelp" class="form-text">Optional: runs <code>i2cdetect -y 1</code> against attached I²C devices.</div>
                                </div>
                                <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                    <button id="confirmCreateSupportBundleButton" type="button" class="btn btn-primary">Create readable candidate</button>
                                    <button id="cancelCreateSupportBundleButton" type="button" class="btn btn-outline-secondary">Cancel</button>
                                </div>
                            </section>
                            <section id="supportBundleReview" class="maintenance-support-review d-none" aria-labelledby="supportBundleReviewTitle">
                                <div>
                                    <h3 id="supportBundleReviewTitle" class="h6 mb-1">Review candidate</h3>
                                    <p class="mb-0">Case ID: <strong id="supportBundleCaseId" class="font-monospace"></strong></p>
                                </div>
                                <ol class="maintenance-support-steps mb-0">
                                    <li>Download the readable <code>.tar.gz</code> archive.</li>
                                    <li>Open it locally and inspect the manifest and diagnostic files.</li>
                                    <li>Delete and recollect if anything should not be shared.</li>
                                </ol>
                                <div id="supportBundleReviewConsent" class="form-check d-none">
                                    <input id="supportBundleReviewed" class="form-check-input" type="checkbox">
                                    <label class="form-check-label" for="supportBundleReviewed">I reviewed this candidate and approve these exact bytes for encryption.</label>
                                </div>
                            </section>
                            <section
                                id="supportIntakePanel"
                                class="maintenance-support-intake d-none"
                                aria-labelledby="supportIntakeTitle">
                                <div>
                                    <h3 id="supportIntakeTitle" class="h6 mb-1">Private upload availability</h3>
                                    <p id="supportIntakeMessage" class="mb-0" role="status" aria-live="polite" aria-atomic="true">
                                        Check whether this version can use the current private support channel.
                                    </p>
                                </div>
                                <p id="supportIntakeSignedMessage" class="maintenance-support-intake__signed-message d-none mb-0"></p>
                                <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                    <button id="checkSupportIntakeButton" type="button" class="btn btn-outline-primary">
                                        Check private upload availability
                                    </button>
                                    <a
                                        id="supportIntakeUpgradeLink"
                                        class="btn btn-outline-primary d-none"
                                        target="_blank"
                                        rel="noopener noreferrer">
                                        Download latest version
                                    </a>
                                </div>
                            </section>
                            <section id="supportEncryptionPanel" class="maintenance-support-intake d-none" aria-labelledby="supportEncryptionTitle">
                                <div>
                                    <h3 id="supportEncryptionTitle" class="h6 mb-1">Encrypt reviewed candidate</h3>
                                    <p id="supportEncryptionMessage" class="mb-0" role="status" aria-live="polite" aria-atomic="true">
                                        Encryption runs locally on this Pi. The readable archive remains available and no file is uploaded.
                                    </p>
                                </div>
                                <div class="form-check">
                                    <input id="supportEncryptionConsent" class="form-check-input" type="checkbox">
                                    <label class="form-check-label" for="supportEncryptionConsent">Encrypt the exact candidate I reviewed for the WsprryPi maintainer.</label>
                                </div>
                                <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                    <button id="encryptSupportBundleButton" type="button" class="btn btn-primary" disabled>Encrypt reviewed candidate</button>
                                    <button id="downloadEncryptedSupportBundleButton" type="button" class="btn btn-outline-primary d-none">Download encrypted bundle</button>
                                    <button id="downloadSupportReceiptButton" type="button" class="btn btn-outline-secondary d-none">Download receipt</button>
                                </div>
                            </section>
                            <section id="supportDropboxHandoffPanel" class="maintenance-support-intake d-none" aria-labelledby="supportDropboxHandoffTitle">
                                <div>
                                    <h3 id="supportDropboxHandoffTitle" class="h6 mb-1">Continue to private Dropbox upload</h3>
                                    <p class="mb-0">The encrypted support bundle will be uploaded using Dropbox.</p>
                                </div>
                                <ul class="maintenance-support-steps mb-0">
                                    <li>Dropbox will ask for your name and a valid email address. Dropbox and the WsprryPi maintainer may receive this information as upload metadata. A Dropbox account is not required.</li>
                                    <li>Dropbox cannot read the encrypted bundle contents, but it can observe the filename, file size, upload time, network information, and the name and email address entered on its upload form.</li>
                                    <li>On the Dropbox page, select the downloaded <code>.age</code> file. Opening the page does not confirm an upload.</li>
                                </ul>
                                <div class="form-check">
                                    <input id="supportDropboxHandoffConsent" class="form-check-input" type="checkbox">
                                    <label class="form-check-label" for="supportDropboxHandoffConsent">I understand what Dropbox and the maintainer can receive and want to open the private upload page.</label>
                                </div>
                                <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                    <a id="openSupportDropboxButton" class="btn btn-primary disabled" target="_blank" rel="noopener noreferrer" aria-disabled="true" tabindex="-1">Open private Dropbox upload</a>
                                </div>
                            </section>
                            <section id="supportUploadReportPanel" class="maintenance-support-intake d-none" aria-labelledby="supportUploadReportTitle">
                                <div>
                                    <h3 id="supportUploadReportTitle" class="h6 mb-1">Report the Dropbox result</h3>
                                    <p id="supportUploadReportMessage" class="mb-0" role="status" aria-live="polite" aria-atomic="true">The Dropbox page was requested. Opening it is not upload success or maintainer confirmation.</p>
                                </div>
                                <div class="form-check">
                                    <input id="supportUploadReportedComplete" class="form-check-input" type="checkbox">
                                    <label class="form-check-label" for="supportUploadReportedComplete">Dropbox displayed “Finished uploading” after I selected and submitted the downloaded <code>.age</code> file.</label>
                                </div>
                                <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                    <button id="reportSupportUploadButton" type="button" class="btn btn-primary" disabled>Record my upload report</button>
                                </div>
                            </section>
                            <section id="supportGithubContinuationPanel" class="maintenance-support-intake d-none" aria-labelledby="supportGithubContinuationTitle">
                                <div>
                                    <h3 id="supportGithubContinuationTitle" class="h6 mb-1">Continue your support request</h3>
                                    <p id="supportGithubContinuationMessage" class="mb-0" role="status" aria-live="polite" aria-atomic="true"></p>
                                </div>
                                <div id="supportGithubExistingIssueActions" class="d-none">
                                    <label class="form-label" for="supportGithubComment">Public issue comment</label>
                                    <textarea id="supportGithubComment" class="form-control" rows="5" readonly aria-describedby="supportGithubCommentHelp"></textarea>
                                    <div id="supportGithubCommentHelp" class="form-text">Review this text, copy it, open the issue, and post it yourself. GitHub sign-in is required.</div>
                                    <div class="maintenance-action maintenance-action--start maintenance-action--wrap mt-3">
                                        <button id="copySupportGithubCommentButton" type="button" class="btn btn-outline-primary">Copy public comment</button>
                                        <a id="openSupportGithubIssueButton" class="btn btn-primary" target="_blank" rel="noopener noreferrer">Open existing GitHub issue</a>
                                    </div>
                                </div>
                                <div id="supportGithubNewIssueActions" class="d-none">
                                    <p class="mb-0">The prefilled issue contains only the case ID and safe instructions. Review it and add a public problem summary that does not include diagnostics, transfer links, email addresses, callsigns, locators, or network details.</p>
                                    <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                        <a id="createSupportGithubIssueButton" class="btn btn-primary" target="_blank" rel="noopener noreferrer">Create prefilled GitHub issue</a>
                                    </div>
                                </div>
                                <p id="supportGithubCopyStatus" class="form-text mb-0" role="status" aria-live="polite" aria-atomic="true"></p>
                            </section>
                            <div class="maintenance-action maintenance-action--start maintenance-action--wrap">
                                <button id="createSupportBundleButton" type="button" class="btn btn-primary">
                                    Start support bundle
                                </button>
                                <button id="downloadSupportBundleButton" type="button" class="btn btn-outline-primary d-none" disabled>
                                    Download readable candidate
                                </button>
                                <button id="finalizeSupportBundleButton" type="button" class="btn btn-primary d-none" disabled>
                                    Approve reviewed candidate
                                </button>
                                <button id="deleteSupportBundleButton" type="button" class="btn btn-outline-danger d-none" disabled>
                                    Delete from Pi
                                </button>
                            </div>
                        </section>
                    </div>
                </section>
            </div>

            <div id="maintenanceOverlay" class="maintenance-overlay d-none"></div>

            <div
                class="modal fade"
                id="testToneModal"
                tabindex="-1"
                aria-labelledby="testToneModalLabel"
                aria-hidden="true">
                <div class="modal-dialog modal-dialog-centered">
                    <div class="modal-content">
                        <div class="modal-header">
                            <h3 class="modal-title h5" id="testToneModalLabel">Test Tone</h3>
                            <button
                                type="button"
                                class="btn-close"
                                data-bs-dismiss="modal"
                                aria-label="Close"></button>
                        </div>
                        <div class="modal-body">
                            <p class="mb-2">Use the controls below to start or stop the test tone.</p>
                            <p id="testToneFrequencyContext" class="mb-0">
                                Configured frequency: unavailable.
                            </p>
                            <fieldset class="mt-3 mb-0">
                                <legend class="form-label mb-2">Frequency source</legend>
                                <div class="d-flex flex-wrap gap-3">
                                    <div class="form-check">
                                        <input
                                            class="form-check-input"
                                            type="radio"
                                            name="testToneFrequencySource"
                                            id="testToneSourceBand"
                                            value="wspr_band">
                                        <label class="form-check-label" for="testToneSourceBand">
                                            WSPR band
                                        </label>
                                    </div>
                                    <div class="form-check">
                                        <input
                                            class="form-check-input"
                                            type="radio"
                                            name="testToneFrequencySource"
                                            id="testToneSourceCustom"
                                            value="custom_rf">
                                        <label class="form-check-label" for="testToneSourceCustom">
                                            Custom RF frequency
                                        </label>
                                    </div>
                                </div>
                            </fieldset>
                            <div class="mt-3">
                                <label for="testToneBand" class="form-label">WSPR band</label>
                                <select id="testToneBand" class="form-select" disabled>
                                    <option value="">Select a WSPR band</option>
                                </select>
                            </div>
                            <div class="mt-3">
                                <label for="testToneFrequencyHz" class="form-label">
                                    Custom RF frequency, Hz
                                    <span class="visually-hidden">Test tone transmit frequency, Hz.</span>
                                </label>
                                <input
                                    type="number"
                                    id="testToneFrequencyHz"
                                    class="form-control"
                                    inputmode="numeric"
                                    min="1"
                                    step="1">
                            </div>
                            <p id="testToneSelectionPreview" class="small mb-0 mt-3" aria-live="polite">
                                Select a frequency source.
                            </p>
                            <p id="testToneSelectionError" class="small text-danger mb-0 mt-2" role="alert"></p>
                            <p
                                id="testToneExecutionResult"
                                class="small mb-0 mt-2"
                                role="status"
                                aria-live="polite"
                                aria-atomic="true"></p>
                        </div>
                        <div class="modal-footer">
                            <button
                                type="button"
                                id="testToneStart"
                                class="btn btn-primary">
                                Start
                            </button>
                            <button
                                type="button"
                                id="testToneEnd"
                                class="btn btn-danger">
                                End
                            </button>
                            <button
                                type="button"
                                id="testToneClose"
                                class="btn btn-secondary"
                                data-bs-dismiss="modal">
                                Close
                            </button>
                        </div>
                    </div>
                </div>
            </div>

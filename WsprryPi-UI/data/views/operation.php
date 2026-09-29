<?php
$cardTitleId = 'operationPageTitle';
$cardTitleText = 'Wsprry Pi Operation';
require __DIR__ . '/../card_header.php';
?>

            <div class="card-body operation-card-body">
                <section class="operation-hero" aria-labelledby="operationPageTitle">
                    <div class="operation-hero__status">
                        <div
                            id="operationStatusAnnouncement"
                            class="operation-hero__headline"
                            role="status"
                            aria-live="polite"
                            aria-atomic="true"
                            aria-relevant="text">
                            <div class="operation-hero__state">
                                <span class="operation-hero__state-label">Current state</span>
                                <span id="operationCurrentState" class="operation-hero__state-value">Loading runtime state</span>
                            </div>
                            <p id="operationStateDetail" class="operation-hero__detail">
                                Connecting to the controller and loading the latest operating values.
                            </p>
                        </div>
                    </div>

                    <div class="operation-hero__reboot" aria-labelledby="operationRebootBehaviorTitle">
                        <div class="operation-hero__state-label" id="operationRebootBehaviorTitle">Reboot behavior</div>
                        <div class="operation-reboot-options" role="radiogroup" aria-labelledby="operationRebootBehaviorTitle">
                            <div class="form-check">
                                <input class="form-check-input" type="radio" name="operation_reboot_behavior" id="operation_reboot_disable" value="disable" checked>
                                <label class="form-check-label" for="operation_reboot_disable">Disable transmission on (re)boot</label>
                            </div>
                            <div class="form-check">
                                <input class="form-check-input" type="radio" name="operation_reboot_behavior" id="operation_reboot_follow_last" value="follow_last">
                                <label class="form-check-label" for="operation_reboot_follow_last">Follow the last transmit setting</label>
                            </div>
                            <div class="form-check">
                                <input class="form-check-input" type="radio" name="operation_reboot_behavior" id="operation_reboot_restart" value="restart">
                                <label class="form-check-label" for="operation_reboot_restart">Restart transmissions on (re)boot</label>
                            </div>
                        </div>
                    </div>

                    <div class="operation-hero__controls" aria-label="Runtime controls">
                        <div class="operation-hero__state-label">Transmit control</div>

                        <div class="operation-runtime-toggle">
                            <div class="form-check form-switch">
                                <input class="form-check-input" type="checkbox" role="switch" id="transmit" aria-describedby="transmitAvailabilityHint operationControlHint">
                                <label class="form-check-label" for="transmit">Transmit enabled</label>
                            </div>
                            <?php require __DIR__ . '/local-wtp-control.php'; ?>
                            <div id="transmitAvailabilityHint" class="form-text mt-2" aria-live="polite" aria-atomic="true" hidden></div>
                        </div>

                        <button
                            type="button"
                            class="btn btn-danger operation-stop-button"
                            id="stop_transmit"
                            aria-describedby="operationControlHint"
                            disabled>
                            Stop transmission
                        </button>

                        <div id="operationRecoveryActions" class="operation-recovery" hidden>
                            <div class="operation-recovery__actions">
                                <button type="button" class="btn btn-outline-primary operation-recovery__action" id="operationRetryButton">
                                    Retry now
                                </button>
                                <a class="btn btn-outline-secondary operation-recovery__action" id="operationSetupButton" href="index.php?page=config" hidden>
                                    Open Setup
                                </a>
                            </div>
                            <div id="operationRecoveryHint" class="operation-recovery__hint" aria-live="polite" aria-atomic="true"></div>
                        </div>
                    </div>
                </section>

                <section class="operation-summary-grid" aria-label="Runtime summary">
                    <article class="operation-panel operation-panel--primary">
                        <div class="operation-panel__label">Current mode</div>
                        <div class="operation-panel__value">
                            <span id="runtime_mode_value" aria-live="polite" aria-atomic="true">Unknown</span>
                        </div>
                    </article>

                    <article class="operation-panel">
                        <div class="operation-panel__label operation-panel__label--split">
                            <span id="runtime_frequency_primary_label">Frequency</span>
                            <span id="runtime_frequency_secondary_label" hidden>Offset</span>
                        </div>
                        <div class="operation-panel__stack" id="runtime_frequency_value" aria-live="polite" aria-atomic="true">
                            <div class="operation-panel__value">Not available</div>
                        </div>
                    </article>

                    <article class="operation-panel operation-panel--wide">
                        <div class="operation-panel__label" id="runtime_plan_label">Current WSPR plan</div>
                        <div class="operation-panel__value operation-panel__value--wrap" id="runtime_wspr_plan_value" aria-live="polite" aria-atomic="true">
                            Not available
                        </div>
                    </article>

                    <article class="operation-panel operation-panel--wide" id="gpio_frequency_correction_panel">
                        <div class="operation-panel__label">GPIO frequency correction</div>
                        <div class="operation-panel__value operation-panel__value--wrap" id="gpio_frequency_correction_value" aria-live="polite" aria-atomic="true">
                            Runtime correction status is not available yet.
                        </div>
                        <div class="form-text" id="gpio_frequency_correction_detail">
                            Candidate and committed execution values are reported separately.
                        </div>
                    </article>
                </section>

                <div id="backendStatus" class="alert operation-backend-status" role="alert" aria-live="assertive" aria-atomic="true" hidden></div>
            </div>

<?php // SPDX-License-Identifier: MIT ?>
        <section id="pico-target-settings" data-independent-wtp class="mt-3" aria-labelledby="pico-target-heading">
            <h4 id="pico-target-heading" class="cw-control-section__title" tabindex="-1">Pico settings</h4>
            <p id="pico-target-identity" class="visually-hidden">Choose a saved Pico with the Device selector. This does not select a transmitter backend.</p>
            <p id="pico-target-feedback" class="form-text" role="status" aria-live="polite"></p>
            <section class="mb-3" aria-labelledby="pico-native-stop-heading">
                <h5 id="pico-native-stop-heading" class="cw-control-section__title">Native schedule safety</h5>
                <p class="form-text">Stop this Pico’s WSPR and keyed-message schedules. A confirmed stop disables both saved schedules and confirms inactive, unowned output. Enable a schedule explicitly to run it again.</p>
                <div class="d-flex flex-wrap gap-2">
                    <button id="pico-native-stop" type="button" class="btn btn-outline-danger" disabled>Stop native schedules</button>
                    <button id="pico-native-status-read" type="button" class="btn btn-outline-secondary" disabled>Read device status</button>
                    <button id="pico-native-stop-read" type="button" class="btn btn-outline-secondary" disabled>Read stop status</button>
                </div>
                <p id="pico-native-status" class="form-text" role="status" aria-live="polite"></p>
                <p id="pico-native-stop-status" class="form-text" role="status" aria-live="polite">Connect to the intended Pico first.</p>
            </section>
            <div id="pico-target-recovery" hidden>
                <p id="pico-target-recovery-state" class="form-text" role="status" aria-live="polite"></p>
                <button id="pico-target-read-recovery" type="button" class="btn btn-outline-primary">Read current settings</button>
                <dl id="pico-target-recovery-observation" class="text-break mt-2"></dl>
                <button id="pico-target-review-recovery" type="button" class="btn btn-outline-primary mt-2" disabled>I reviewed these settings</button>
            </div>
            <details class="mt-3"><summary>Earlier settings requests</summary><ul id="pico-target-operations" class="text-break"></ul></details>
            <details id="pico-target-retained" class="mt-3" hidden>
                <summary>Retained drafts</summary>
                <p class="form-text">Entries from other editors remain available for review. They cannot save through an old connection. Password contents are not shown.</p>
                <div id="pico-target-retained-list"></div>
            </details>
            <div id="pico-target-panel"></div>
        </section>
        <template id="pico-target-template">
        <div id="wtp-management" class="wtp-status mt-3" data-independent-wtp>
            <p id="wtp-management-availability" class="form-text mt-2" role="status" aria-live="polite" hidden></p>
            <details class="mb-4"><summary>UDP logging on this Pico</summary><div id="member-udp-logging-controls"></div></details>
            <section id="wtp-usb-settings" class="mb-4" aria-labelledby="wtp-usb-settings-heading" hidden>
                <h5 id="wtp-usb-settings-heading" class="h6">USB name and Wi-Fi</h5>
                <p class="form-text">The common name is the Pico's Wi-Fi hostname, including .local. Saved names or network changes may need a restart. Removing Wi-Fi keeps USB control and transmitter settings.</p>
                <button type="button" class="btn btn-outline-primary mb-3" id="wtp-usb-load">Read name and Wi-Fi</button>
                <details class="mb-3" id="wtp-usb-logging-controls">
                    <summary>USB logging on this Pico</summary>
                    <button type="button" class="btn btn-outline-primary mt-2" id="wtp-usb-logging-read">Read logging settings</button>
                    <label class="form-check mt-2"><input type="checkbox" class="form-check-input" id="wtp-usb-logging-enabled" disabled> Enable USB log export</label>
                    <label for="wtp-usb-logging-severity" class="form-label">Maximum severity</label>
                    <select class="form-select" id="wtp-usb-logging-severity" disabled><option value="0">Emergency only</option><option value="1">Alert and higher</option><option value="2">Critical and higher</option><option value="3">Errors only</option><option value="4">Warnings and errors</option><option value="5">Notice and higher</option><option value="6">Info and higher</option><option value="7">All levels</option></select>
                    <button type="button" class="btn btn-outline-primary mt-2" id="wtp-usb-logging-save" disabled>Save Pico logging</button>
                    <p id="wtp-usb-logging-status" class="form-text" role="status" aria-live="polite">Read settings to see saved and active export state. Select collection separately on Logs.</p>
                </details>
                <p id="wtp-usb-identity" class="form-text text-break"></p>
                <div class="row gx-3 gy-3">
                    <div class="col-12 col-lg-6">
                        <label class="form-label" for="wtp-usb-hostname">Pico common name</label>
                        <input class="form-control" id="wtp-usb-hostname" type="text" maxlength="63" autocomplete="off" spellcheck="false">
                        <div class="form-text" id="wtp-usb-name-state">Read the saved and active name first. Leave blank to select the device default.</div>
                        <button type="button" class="btn btn-outline-primary mt-2" id="wtp-usb-name-save" disabled>Save common name</button>
                    </div>
                    <div class="col-12 col-lg-6">
                        <label class="form-label" for="wtp-usb-ssid">Pico Wi-Fi network</label>
                        <input class="form-control" id="wtp-usb-ssid" type="text" maxlength="32" autocomplete="off" spellcheck="false">
                        <label class="form-label mt-2" for="wtp-usb-password">Wi-Fi password</label>
                        <input class="form-control" id="wtp-usb-password" type="password" maxlength="63" autocomplete="off" aria-describedby="wtp-usb-password-hint">
                        <div class="form-text" id="wtp-usb-password-hint">Leave blank to preserve the password for the same network. A new or renamed connection needs its password. Credentials are never read back.</div>
                        <label class="form-label mt-2" for="wtp-usb-time-server">Wi-Fi time server</label>
                        <input class="form-control" id="wtp-usb-time-server" type="text" maxlength="253" autocomplete="off" spellcheck="false">
                        <div class="d-flex flex-wrap gap-2 mt-2">
                            <button type="button" class="btn btn-outline-primary" id="wtp-usb-wifi-save" disabled>Save Wi-Fi connection</button>
                            <button type="button" class="btn btn-outline-danger" id="wtp-usb-wifi-remove" disabled>Remove saved Wi-Fi</button>
                        </div>
                    </div>
                </div>
                <div class="d-flex flex-wrap gap-2 mt-3">
                    <button type="button" class="btn btn-outline-secondary" id="wtp-usb-restart" disabled>Restart Pico to apply</button>
                    <button type="button" class="btn btn-outline-secondary" id="wtp-usb-ap" disabled>Open setup Wi-Fi for ten minutes</button>
                </div>
                <p id="wtp-usb-feedback" class="form-text mt-2" role="status" aria-live="polite">Read name and Wi-Fi before editing.</p>
            </section>
            <div class="row gx-3 gy-3 mb-3">
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-management-carrier">Management connection</label>
                    <select class="form-select" id="wtp-management-carrier" aria-describedby="wtp-management-carrier-hint">
                        <option value="usb_physical">USB cable</option>
                        <option value="engineering_mtls">Engineering HTTPS (certificates)</option>
                        <option value="consumer_station">Consumer Wi-Fi</option>
                        <option value="consumer_ap">Consumer setup Wi-Fi</option>
                    </select>
                    <div class="form-text" id="wtp-management-carrier-hint">USB uses the selected Pico cable and needs no Wi-Fi credentials. Consumer Wi-Fi uses encrypted authenticated requests on HTTP port 80. Engineering HTTPS uses the explicit saved certificate identity and port.</div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field" id="wtp-management-host-field" hidden>
                    <label class="form-label" for="wtp-management-host">Pico IP address or local hostname</label>
                    <input class="form-control" id="wtp-management-host" type="text" maxlength="253" autocomplete="off" spellcheck="false" aria-describedby="wtp-management-host-hint">
                    <div class="form-text" id="wtp-management-host-hint">Use the exact reachable authority. Setup Wi-Fi uses 192.168.4.1; connect this Pi to that network first. This control does not change the Pi's network.</div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field" id="wtp-management-password-field" hidden>
                    <label class="form-label" for="wtp-management-password">Saved Pico Wi-Fi password</label>
                    <input class="form-control" id="wtp-management-password" type="password" maxlength="63" autocomplete="off" aria-describedby="wtp-management-password-hint">
                    <div class="form-text" id="wtp-management-password-hint">Used once for login, then cleared. Session keys expire in memory. Anyone who knows this shared password has the same access.</div>
                </div>
                <div class="col-12 d-flex flex-wrap gap-2">
                    <button type="button" class="btn btn-outline-primary" id="wtp-management-connect">Use this management connection</button>
                    <button type="button" class="btn btn-outline-secondary" id="wtp-management-state">Read connection state</button>
                    <button type="button" class="btn btn-outline-secondary" id="wtp-management-disconnect">Disconnect management</button>
                </div>
                <details class="col-12" id="wtp-management-recovery">
                    <summary>Recover an occupied Pico login</summary>
                    <p class="form-text">First read connection state. A working native Pi session can survive browser connection loss. Retirement may end another operator's login; it does not repeat a save, alter saved settings or stop a radio job.</p>
                    <button type="button" class="btn btn-outline-secondary" id="wtp-management-inspect">Inspect occupied logins</button>
                    <label class="form-label" for="wtp-management-target">Login to retire</label>
                    <select class="form-select" id="wtp-management-target"><option value="">Inspect logins first</option></select>
                    <p class="form-text" id="wtp-management-retirement-status" role="status" aria-live="polite"></p>
                    <button type="button" class="btn btn-outline-danger" id="wtp-management-retire" disabled>Authenticate and retire selected login</button>
                </details>
                <p class="col-12 form-text mb-0" id="wtp-management-connection-feedback" role="status" aria-live="polite">Engineering HTTPS uses the selected certificate connection. Consumer login requires the configured full Pico device ID. No automatic fallback is used.</p>
            </div>
            <section class="mt-4 mb-4" aria-labelledby="wtp-member-heading">
                <h4 class="cw-control-section__title" id="wtp-member-heading">Member application, station and pins</h4>
                <p class="form-text">Inspect this Pico's standalone defaults and saved hardware. These actions do not change Pi station settings or prepared jobs.</p>
                <button type="button" class="btn btn-outline-primary" id="wtp-member-load">Load member resources</button>
                <p class="form-text mt-2" id="wtp-member-feedback" role="status" aria-live="polite">Load resources using the selected management connection. Older firmware may support only the settings form below.</p>
                <dl class="wtp-observations">
                    <div><dt>Member / boot / connection</dt><dd id="wtp-member-identity">Unavailable until loaded</dd></div>
                    <div><dt>Member recurrence</dt><dd id="wtp-member-recurrence">Unknown</dd></div>
                    <div><dt>Storage / application</dt><dd id="wtp-member-health">Unknown</dd></div>
                    <div><dt>Saved hardware</dt><dd id="wtp-member-saved">Unknown</dd></div>
                    <div><dt>Active at boot</dt><dd id="wtp-member-active">Unknown</dd></div>
                    <div><dt>Restart required</dt><dd id="wtp-member-restart">Unknown</dd></div>
                </dl>
                <h5 class="cw-control-section__title">Standalone station defaults</h5>
                <p class="form-text" id="wtp-member-station-hint">Read saved progress, save valid station fields, then apply the saved progress separately. Complete station save is an alternative below. The Pico automatically selects WSPR Type 1, 2 or 3; reported power is metadata, not measured RF output.</p>
                <div class="row gx-3 gy-3">
                    <div class="col-12 col-lg-4 config-stacked-field">
                        <label class="form-label" for="wtp-member-callsign">Callsign</label>
                        <input class="form-control" id="wtp-member-callsign" type="text" maxlength="12" autocomplete="off" spellcheck="false" data-wtp-resource="station" aria-describedby="wtp-member-station-hint wtp-member-callsign-progress" disabled>
                        <p class="form-text mb-0" id="wtp-member-callsign-progress" role="status" aria-live="polite"></p>
                    </div>
                    <div class="col-12 col-lg-4 config-stacked-field">
                        <label class="form-label" for="wtp-member-locator">Locator</label>
                        <input class="form-control" id="wtp-member-locator" type="text" maxlength="6" autocomplete="off" spellcheck="false" data-wtp-resource="station" aria-describedby="wtp-member-station-hint wtp-member-locator-progress" disabled>
                        <p class="form-text mb-0" id="wtp-member-locator-progress" role="status" aria-live="polite"></p>
                    </div>
                    <div class="col-12 col-lg-4 config-stacked-field">
                        <label class="form-label" for="wtp-member-power">Reported WSPR power (dBm)</label>
                        <select class="form-select" id="wtp-member-power" data-wtp-resource="station" aria-describedby="wtp-member-station-hint wtp-member-power-progress" disabled>
                            <option value="">Unset</option>
                            <?php foreach ([0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60] as $power): ?>
                            <option value="<?= $power ?>"><?= $power ?></option>
                            <?php endforeach; ?>
                        </select>
                        <p class="form-text mb-0" id="wtp-member-power-progress" role="status" aria-live="polite"></p>
                    </div>
                </div>

                <section class="mt-3" aria-labelledby="wtp-usb-progress-heading">
                    <h6 id="wtp-usb-progress-heading">Save station progress</h6>
                    <p class="form-text">Read saved progress, then edit the station fields above to save valid fields independently. Applying saved station progress is a separate action. Neither action transmits, resumes STOP or changes clock readiness. Older firmware retains the complete station save.</p>
                    <div class="d-flex flex-wrap gap-2">
                        <button type="button" class="btn btn-outline-secondary" id="wtp-usb-progress-load">Read saved progress</button>
                        <button type="button" class="btn btn-outline-primary" id="wtp-usb-progress-save" disabled>Save valid station fields</button>
                        <button type="button" class="btn btn-outline-primary" id="wtp-usb-progress-apply" disabled>Apply saved station progress</button>
                        <button type="button" class="btn btn-outline-primary" id="wtp-usb-progress-discard" disabled>Discard station drafts and load progress</button>
                    </div>
                    <p id="wtp-usb-progress-state" class="form-text mt-2" role="status" aria-live="polite">Saved progress and applied configuration have not been read.</p>
                </section>
                <details class="mt-3" id="wtp-complete-station">
                    <summary>Complete station save</summary>
                    <p class="form-text mt-2">Alternative to saving and applying station progress: read member resources, complete all three station fields above, then save them together. Use this path when saved progress is unsupported. It does not start a transmission.</p>
                    <button type="button" class="btn btn-outline-primary" id="wtp-member-station-save" disabled>Save member station</button>
                </details>
                <p class="form-text mt-2" id="wtp-member-station-feedback" role="status" aria-live="polite"></p>
                <div class="mt-3">
                    <div id="wtp-gray-line-controls"></div>
                    <div class="row g-3" id="wtp-member-beacon-controls"></div>
                    <p class="form-text mt-0" id="wtp-member-beacon-saved">Load member resources to read the saved schedule.</p>
                    <button type="button" class="btn btn-outline-primary" id="wtp-member-beacon-save" disabled>Save beacon schedule</button>
                    <p class="form-text mt-2" id="wtp-member-beacon-feedback" role="status"></p>
                </div>
                <details id="wtp-hardware-settings" class="mt-4">
                <summary class="cw-control-section__title">RF output, GPS, and pins</summary>
                <p class="form-text" id="wtp-member-pin-hint">Pico GP numbering. Direct PIO output, active-low button and indicator are supported. Button actions require the member's button adapter to be enabled. Changes are saved for restart; saving does not change physical routing or enable RF. Pin selection does not establish RF qualification.</p>
                <div class="row gx-3 gy-3">
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="wtp-member-engine">RF output engine</label>
                        <select class="form-select" id="wtp-member-engine" data-wtp-resource="hardware" disabled><option value="direct">Direct GPIO / PIO</option><option value="i2c" disabled>Si5351 (engineering)</option></select>
                    </div>
                    <div id="wtp-member-direct-rf" class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="wtp-member-rf">Direct RF output GPIO</label>
                        <select class="form-select" id="wtp-member-rf" data-wtp-resource="hardware" aria-describedby="wtp-member-pin-hint" disabled></select>
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="wtp-member-button">Active-low button</label>
                        <select class="form-select" id="wtp-member-button" data-wtp-resource="hardware" aria-describedby="wtp-member-pin-hint" disabled></select>
                    </div>
                    <div class="col-12 col-lg-4 config-stacked-field">
                        <label class="form-label" for="wtp-member-indicator">TX indicator</label>
                        <select class="form-select" id="wtp-member-indicator" data-wtp-resource="hardware" disabled>
                            <option value="">Select indicator</option>
                            <option value="onboard_led">Onboard LED</option><option value="external">External GP</option><option value="disabled">Disabled</option>
                        </select>
                    </div>
                    <div class="col-12 col-lg-4 config-stacked-field">
                        <label class="form-label" for="wtp-member-indicator-gp">External indicator GP</label>
                        <select class="form-select" id="wtp-member-indicator-gp" data-wtp-resource="hardware" disabled></select>
                    </div>
                    <div class="col-12 col-lg-4 config-stacked-field">
                        <label class="form-label" for="wtp-member-polarity">External indicator polarity</label>
                        <select class="form-select" id="wtp-member-polarity" data-wtp-resource="hardware" disabled><option value="true">Active high</option><option value="false">Active low</option></select>
                    </div>
                </div>
                <div class="row g-3 mb-3">
                    <div class="col-12 col-md-6"><label class="form-label" for="wtp-member-amplifier">Amplifier enable pin</label><select class="form-select" id="wtp-member-amplifier" data-wtp-resource="hardware" aria-describedby="wtp-member-amplifier-hint" disabled></select></div>
                    <div class="col-12 col-md-6"><label class="form-label" for="wtp-member-amplifier-polarity">Amplifier enable polarity</label><select class="form-select" id="wtp-member-amplifier-polarity" data-wtp-resource="hardware" disabled><option value="true">Active high</option><option value="false">Active low</option></select></div>
                </div>
                <p class="form-text" id="wtp-member-amplifier-hint">Disabled by default. The pin must be unused by RF, the button, indicator or filter selectors. Save the hardware plan, then restart the Pico to activate it. Saving does not enable RF.</p>
                <div id="wtp-gps-controls" class="mt-3"></div>
                <div id="wtp-si5351-controls" class="mt-3" hidden></div>
                <p class="form-text mt-3">Validate and save the complete pin, GPS, and Si5351 plan together. Restart the Pico explicitly to activate saved hardware changes.</p>
                <div class="d-flex flex-wrap gap-2 mt-3">
                    <button type="button" class="btn btn-outline-primary" id="wtp-member-validate" disabled>Validate hardware plan</button>
                    <button type="button" class="btn btn-primary" id="wtp-member-hardware-save" disabled>Save RF output settings</button>
                </div>
                <p class="form-text mt-2" id="wtp-member-pin-feedback" role="status" aria-live="polite">Validation is a preview. It never saves or activates pins.</p>
                <p class="form-text" id="wtp-member-hardware-feedback" role="status" aria-live="polite"></p><fieldset class="mt-3" id="amplifier-timing-settings"><legend>Amplifier lead and tail</legend><div class="row g-3"><label class="form-label col-12 col-lg-6" for="amplifier-lead-ms">Enable before RF (ms)<input id="amplifier-lead-ms" class="form-control" type="number" min="0" max="1000" step="1" disabled aria-describedby="amplifier-timing-help amplifier-timing-status"></label><label class="form-label col-12 col-lg-6" for="amplifier-tail-ms">Disable after RF (ms)<input id="amplifier-tail-ms" class="form-control" type="number" min="0" max="1000" step="1" disabled aria-describedby="amplifier-timing-help amplifier-timing-status"></label></div><small class="form-text d-block" id="amplifier-timing-help">Default 200 ms each; range 0–1000 ms in whole milliseconds. Lead allows the amplifier to settle before the timed RF start; tail follows RF completion. Neither changes transmission duration. Zero removes that interval. A disabled amplifier pin uses neither interval. Save, then restart to activate.</small><p class="form-text text-break" id="amplifier-timing-status" role="status" aria-live="polite">Read saved amplifier timing before editing.</p><div class="d-flex flex-wrap gap-2 mt-3"><button class="btn btn-outline-primary" id="amplifier-timing-read" type="button">Read saved timing</button><button id="amplifier-timing-save" type="button" class="btn btn-primary" disabled>Save amplifier timing</button><button class="btn btn-outline-primary" id="amplifier-timing-discard" type="button" disabled>Discard timing entries</button><button class="btn btn-outline-primary" id="amplifier-timing-adopt" type="button" hidden disabled>Use entries for this Pico</button></div></fieldset><div id="filter-selector-controls" class="mt-3"></div><div id="gpio-drive-form"><fieldset class="mt-3" aria-labelledby="gpio-drive-label"><label id="gpio-drive-label" class="form-label" for="gpio-drive-select">GPIO drive strength</label><select class="form-select" id="gpio-drive-select" aria-describedby="gpio-drive-help gpio-drive-status" disabled><option value="2">2 mA</option><option value="4">4 mA (default)</option><option value="8">8 mA</option><option value="12">12 mA</option></select><small class="form-text d-block" id="gpio-drive-help">Electrical RF-pin drive; actual RF power depends on load and frequency. Save, then explicitly restart to activate.</small><p class="form-text text-break" id="gpio-drive-status" role="status" aria-live="polite">GPIO drive has not been read.</p><div class="d-flex flex-wrap gap-2"><button class="btn btn-outline-primary" id="gpio-drive-read" type="button">Read GPIO drive</button><button class="btn btn-primary" id="gpio-drive-save" type="button" disabled>Save GPIO drive</button><button class="btn btn-outline-primary" id="gpio-drive-discard" type="button" disabled>Discard drive draft</button></div><details class="mt-3"><summary>Saved drive progress</summary><p>Save drive independently of station identity. Apply saved drive separately, then restart to activate changed hardware.</p><button class="btn btn-outline-primary" id="gpio-drive-save-draft" type="button" disabled>Save drive progress</button><button class="btn btn-outline-primary" id="gpio-drive-apply-draft" type="button" disabled>Apply saved drive</button><p class="form-text text-break" id="gpio-drive-draft-status" role="status" aria-live="polite">Drive progress has not been read.</p></details></fieldset></div>
                <details class="mt-3" id="wtp-platform-limits"><summary>Platform limits and fixed I2C choices</summary>
                    <p class="form-text">Read member resources to check this Pico's supported controls. Si5351 settings appear when its supported RF engine is selected; automatic filter selectors appear when supported by the current firmware and connection. Amplifier timing and GPIO drive have separate save actions. Pico GP numbers are GPIOs, not Pi BCM or physical header numbers. Available choices do not establish physical or RF qualification.</p>
                    <p class="form-text" id="wtp-member-pairs">Choices unavailable until loaded.</p>
                </details>
                </details>
            </section>
            <details id="wtp-combined-settings" class="mt-4">
            <summary>Standalone schedules and combined settings</summary>
            <p class="form-text mt-2">For station edits, use saved progress in Signal Setup; USB name and Wi-Fi controls are in Device I/O. This form also supports network management and firmware without those separate editors. Save schedules only changes recurrence; Save combined settings can also change station, Wi-Fi, expiry, and WSPR recurrence.</p>
            <button type="button" class="btn btn-outline-primary" id="wtp-remote-load">Load saved Pico settings</button>
            <p class="form-text">Loading replaces the standalone draft below. After a conflicting or unconfirmed save, loading reads saved values and keeps your draft for review. Status polling preserves it.</p>
            <div class="row gx-3 gy-3" id="wtp-remote-fields">
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-callsign">Standalone callsign</label>
                    <input class="form-control" id="wtp-remote-callsign" type="text" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-callsign-hint">
                    <div class="form-text" id="wtp-remote-callsign-hint"></div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-locator">Standalone locator</label>
                    <input class="form-control" id="wtp-remote-locator" type="text" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-locator-hint">
                    <div class="form-text" id="wtp-remote-locator-hint"></div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-power">WSPR power metadata (dBm)</label>
                    <input class="form-control" id="wtp-remote-power" type="number" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-power-hint">
                    <div class="form-text" id="wtp-remote-power-hint"></div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-ssid">Wi-Fi network name</label>
                    <input class="form-control" id="wtp-remote-ssid" type="text" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-ssid-hint">
                    <div class="form-text" id="wtp-remote-ssid-hint"></div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-password">New Wi-Fi password</label>
                    <input class="form-control" id="wtp-remote-password" type="password" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-password-hint">
                    <div class="form-text" id="wtp-remote-password-hint">Leave blank to preserve the saved password. It is never returned by Pico.</div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-ntp">Time server</label>
                    <input class="form-control" id="wtp-remote-ntp" type="text" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-ntp-hint">
                    <div class="form-text" id="wtp-remote-ntp-hint"></div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-expiry">Schedule expiry (UTC epoch seconds)</label>
                    <input class="form-control" id="wtp-remote-expiry" type="number" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-expiry-hint">
                    <div class="form-text" id="wtp-remote-expiry-hint">0 means no expiry. Existing values are preserved until you change them.</div>
                </div>
                <div class="col-12">
                    <div class="form-check"><input class="form-check-input" type="checkbox" id="wtp-remote-enabled" data-wtp-remote disabled><label class="form-check-label" for="wtp-remote-enabled">Enable Pico WSPR recurrence</label></div>
                    <div class="form-text" id="wtp-remote-enabled-hint"></div>
                </div>
                <div class="col-12 config-stacked-field">
                    <label class="form-label" for="wtp-remote-schedules">WSPR schedule periods and phases (seconds)</label>
                    <textarea class="form-control" id="wtp-remote-schedules" rows="3" data-wtp-remote disabled aria-describedby="wtp-remote-schedules-hint"></textarea>
                    <div class="form-text" id="wtp-remote-schedules-hint">One period and phase per line, separated by a space. Example: 120 0. Up to eight nonoverlapping schedules; values must be multiples of 120 seconds.</div>
                </div>
            </div>
            <div class="d-flex flex-wrap gap-2 mt-3">
                <button type="button" class="btn btn-primary" id="wtp-remote-save" disabled>Save combined settings</button>
                <button type="button" class="btn btn-outline-primary" id="wtp-schedules-save" disabled>Save schedules only</button>
                <button type="button" class="btn btn-outline-primary" id="wtp-network-load">Refresh Wi-Fi status</button>
                <button type="button" class="btn btn-outline-danger" id="wtp-network-disable" disabled>Disable Pico Wi-Fi</button>
            </div>
            <p id="wtp-network-feedback" class="form-text mt-2" role="status" aria-live="polite">Wi-Fi status loads when the Pico connection is confirmed.</p>
            <p class="form-text mt-2">Disabling Wi-Fi disconnects network control. Re-enable it using the physical USB Console or a restart. This is a temporary switch, separate from saved standalone settings.</p>
            <p id="wtp-management-feedback" class="form-text" role="status" aria-live="polite"></p>
            </details>

        </div>
        </template>

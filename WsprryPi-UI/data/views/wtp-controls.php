<?php // SPDX-License-Identifier: MIT ?>

<div id="wtp-fleet">
    <section id="wtp-controls" class="transmitter-backend-fields" aria-labelledby="wtp-heading">
        <h3 id="wtp-heading" class="transmitter-backend-fields__title">WTP outputs</h3>
        <section class="fleet-catalog mb-4" data-independent-wtp aria-labelledby="fleet-catalog-heading">
            <h4 id="fleet-catalog-heading" class="cw-control-section__title">Known and nearby devices</h4>
            <label class="form-label" for="fleet-device">Saved output or nearby device</label>
            <select class="form-select" id="fleet-device" aria-describedby="fleet-device-detail" disabled>
                <option value="">Loading devices…</option>
            </select>
            <p class="form-text" id="fleet-device-detail">Exploring devices does not change the active connection.</p>
            <p class="form-text" id="fleet-discovery-status" role="status"></p>
            <div class="d-flex flex-wrap gap-2">
                <button type="button" class="btn btn-primary" id="fleet-use" disabled>Use this device</button>
                <button type="button" class="btn btn-outline-primary" id="fleet-manage" disabled>Manage settings</button>
                <button type="button" class="btn btn-outline-primary" id="fleet-add">Add device</button>
                <button type="button" class="btn btn-outline-secondary" id="fleet-edit" disabled>Edit saved device</button>
                <button type="button" class="btn btn-outline-danger" id="fleet-remove" disabled>Remove saved device</button>
                <button type="button" class="btn btn-outline-secondary" id="fleet-refresh">Refresh list</button>
            </div>
            <p id="fleet-feedback" class="form-text mt-2" role="status" aria-live="polite"></p>
            <div id="fleet-editor" class="fleet-editor mt-3" hidden>
                <h5 id="fleet-editor-heading" class="cw-control-section__title">Add device</h5>
                <div class="row gx-3 gy-3">
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="fleet-name">Display name</label>
                        <input class="form-control" id="fleet-name" maxlength="80" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="fleet-method">Connection method</label>
                        <select class="form-select" id="fleet-method"><option value="manual">Manual</option><option value="dns_sd">Nearby DNS-SD</option></select>
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="fleet-binding">Connection</label>
                        <select class="form-select" id="fleet-binding"><option value="usb">USB</option><option value="network_plain">Plain LAN</option><option value="network">Network (TLS)</option></select>
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="fleet-identity">Expected WTP device ID</label>
                        <input class="form-control font-monospace" id="fleet-identity" maxlength="32" autocomplete="off" spellcheck="false" aria-describedby="fleet-identity-hint">
                        <div class="form-text" id="fleet-identity-hint">Full 32 character ID. Plain LAN ID is an observation, not cryptographic authentication.</div>
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-network>
                        <label class="form-label" for="fleet-host">Hostname or IP address</label>
                        <input class="form-control" id="fleet-host" maxlength="254" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-network>
                        <label class="form-label" for="fleet-port">SRV or manual TCP port</label>
                        <input class="form-control" id="fleet-port" type="number" min="1" max="65535">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-usb>
                        <label class="form-label" for="fleet-path">Dedicated USB path</label>
                        <input class="form-control" id="fleet-path" maxlength="512" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-usb>
                        <label class="form-label" for="fleet-serial">USB serial</label>
                        <input class="form-control" id="fleet-serial" maxlength="128" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-usb>
                        <label class="form-label" for="fleet-vendor">USB vendor ID (decimal)</label>
                        <input class="form-control" id="fleet-vendor" type="number" min="1" max="65535">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-usb>
                        <label class="form-label" for="fleet-product">USB product ID (decimal)</label>
                        <input class="form-control" id="fleet-product" type="number" min="1" max="65535">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-tls>
                        <label class="form-label" for="fleet-tls-identity">Expected certificate identity</label>
                        <input class="form-control" id="fleet-tls-identity" maxlength="254" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-tls>
                        <label class="form-label" for="fleet-ca">Trusted CA file on host</label>
                        <input class="form-control" id="fleet-ca" maxlength="512" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-tls>
                        <label class="form-label" for="fleet-cert">Client certificate file on host</label>
                        <input class="form-control" id="fleet-cert" maxlength="512" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" data-fleet-tls>
                        <label class="form-label" for="fleet-key">Client key file on host</label>
                        <input class="form-control" id="fleet-key" maxlength="512" autocomplete="off">
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field" hidden aria-hidden="true">
                        <label class="form-label" for="fleet-uncertainty">Maximum UTC uncertainty (ns)</label>
                        <input class="form-control" id="fleet-uncertainty" type="number" min="1" max="1000000000" value="100000000" aria-describedby="fleet-uncertainty-hint">
                        <div class="form-text" id="fleet-uncertainty-hint">Recommended: 100,000,000 ns = 100 ms. Existing saved limits are kept.</div>
                        <button type="button" class="btn btn-outline-secondary btn-sm mt-2" id="fleet-uncertainty-default" aria-describedby="fleet-uncertainty-hint">Use 100 ms</button>
                    </div>
                    <div class="col-12 d-flex flex-wrap gap-3 align-items-center">
                        <div class="form-check"><input class="form-check-input" type="checkbox" id="fleet-adjust"><label class="form-check-label" for="fleet-adjust">Allow device frequency rounding</label></div>
                        <div class="form-check"><input class="form-check-input" type="checkbox" id="fleet-plain-consent"><label class="form-check-label" for="fleet-plain-consent">I choose Plain LAN on this network</label></div>
                    </div>
                </div>
                <p class="form-text mt-2" id="fleet-editor-hint">Adding saves a profile only. Use this device applies it to the host.</p>
                <div class="d-flex flex-wrap gap-2 mt-2">
                    <button type="button" class="btn btn-outline-primary" id="fleet-identify">Identify selected nearby device</button>
                    <button type="button" class="btn btn-primary" id="fleet-save">Save device</button>
                    <button type="button" class="btn btn-outline-secondary" id="fleet-editor-cancel">Cancel</button>
                </div>
                <p id="fleet-editor-feedback" class="form-text mt-2" role="status" aria-live="polite"></p>
            </div>
        </section>
        <?php require __DIR__ . '/fleet-schedules.php'; ?>
        <h4 class="cw-control-section__title mt-4">Controller output and connection</h4>
        <p class="form-text">These controls belong to this Pi's output. Choose This Pi with Device to edit them.</p>
        <div id="setup-controller-output-fields">
        <div class="form-check form-switch mb-3">
            <input class="form-check-input" type="checkbox" role="switch" id="wtp_use" aria-controls="wtp-settings" aria-describedby="wtp-selection-hint">
            <label class="form-check-label" for="wtp_use">Use Pico</label>
        </div>
        <p class="form-text" id="wtp-selection-hint">Uses the selected WTP connection. Plain LAN allows other clients on the same network to control the Pico. Disable TX LED, amplifier, shutdown-button and band GPIO controls before selecting Pico. Host GPIO calibration and CW fades do not apply.</p>
        <div id="wtp-settings" class="row gx-3 gy-3">
            <div class="col-12 config-stacked-field">
                <label class="form-label" for="wtp_transport">Connection</label>
                <select class="form-select" id="wtp_transport" data-wtp-key="Transport" disabled>
                    <option value="usb">USB</option><option value="network_plain">Plain LAN</option><option value="network">Network (TLS, advanced)</option>
                </select>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-transport="usb">
                <label class="form-label" for="wtp_endpoint">Endpoint path</label>
                <input class="form-control" id="wtp_endpoint" type="text" maxlength="512" placeholder="/dev/serial/by-id/…-if02" data-wtp-key="Endpoint" aria-describedby="wtp-endpoint-hint" disabled>
                <div class="form-text" id="wtp-endpoint-hint"></div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-transport="usb">
                <label class="form-label" for="wtp_serial">USB serial</label>
                <input class="form-control" id="wtp_serial" type="text" maxlength="128" data-wtp-key="USB Serial" aria-describedby="wtp-serial-hint" disabled>
                <div class="form-text" id="wtp-serial-hint"></div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-transport="network" hidden>
                <label class="form-label" for="wtp_hostname">Hostname or IP address</label>
                <input class="form-control" id="wtp_hostname" type="text" maxlength="254" placeholder="wsprrypico-&lt;device-id&gt;.local" data-wtp-key="Hostname" aria-describedby="wtp_hostname-hint" disabled>
                <div class="form-text" id="wtp_hostname-hint">Use the Pico hostname or its current local IP address. The host must support .local resolution to use a .local name.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-transport="network" hidden>
                <label class="form-label" for="wtp_tcp_port">WTP port</label>
                <input class="form-control" id="wtp_tcp_port" type="number" min="1" max="65535" step="1" placeholder="" data-wtp-key="TCP Port" aria-describedby="wtp_tcp_port-hint" disabled>
                <div class="form-text" id="wtp_tcp_port-hint">Plain LAN uses 31417 on the standard Pico image. TLS uses the configured TLS port.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field">
                <label class="form-label" for="wtp_device" id="wtp-device-label">Device identity</label>
                <input class="form-control" id="wtp_device" type="text" maxlength="32" pattern="[0-9a-f]{32}" data-wtp-key="Device ID" aria-describedby="wtp-device-hint" disabled>
                <div class="form-text" id="wtp-device-hint">32 lowercase hexadecimal characters from Pico HELLO.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-transport="usb">
                <label class="form-label" for="wtp_vendor">USB vendor ID</label>
                <input class="form-control" id="wtp_vendor" type="number" min="1" max="65535" step="1" data-wtp-key="USB Vendor ID" aria-describedby="wtp-vendor-hint" disabled>
                <div class="form-text" id="wtp-vendor-hint">Decimal USB identifier.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-transport="usb">
                <label class="form-label" for="wtp_product">USB product ID</label>
                <input class="form-control" id="wtp_product" type="number" min="1" max="65535" step="1" data-wtp-key="USB Product ID" aria-describedby="wtp-product-hint" disabled>
                <div class="form-text" id="wtp-product-hint">Decimal USB identifier.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" hidden aria-hidden="true">
                <label class="form-label" for="wtp_uncertainty">Maximum UTC uncertainty (ns)</label>
                <input class="form-control" id="wtp_uncertainty" type="number" min="1" max="1000000000" step="1" value="100000000" data-wtp-key="Start Uncertainty ns" aria-describedby="wtp-uncertainty-hint" disabled>
                <div class="form-text" id="wtp-uncertainty-hint">Recommended: 100,000,000 ns = 100 ms. Existing saved limits are kept. Enter a new value to change it; valid edits save automatically. Pico checks the limit at ARM and launch.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-security="tls" hidden>
                <label class="form-label" for="wtp_tls_identity">Expected certificate identity</label>
                <input class="form-control" id="wtp_tls_identity" type="text" maxlength="254" placeholder="" data-wtp-key="TLS Server Identity" aria-describedby="wtp_tls_identity-hint" disabled>
                <div class="form-text" id="wtp_tls_identity-hint">Leave blank to verify the hostname or IP above. For a direct-IP connection, enter its certified hostname here. A literal-IP identity requires a matching IP certificate entry.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-security="tls" hidden>
                <label class="form-label" for="wtp_ca">Trusted CA file</label>
                <input class="form-control" id="wtp_ca" type="text" maxlength="512" placeholder="/etc/wsprrypi/pico/ca.crt" data-wtp-key="TLS CA File" aria-describedby="wtp_ca-hint" disabled>
                <div class="form-text" id="wtp_ca-hint">Administrator-managed file on the WsprryPi host.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-security="tls" hidden>
                <label class="form-label" for="wtp_certificate">Client certificate file</label>
                <input class="form-control" id="wtp_certificate" type="text" maxlength="512" placeholder="/etc/wsprrypi/pico/client.crt" data-wtp-key="TLS Client Certificate" aria-describedby="wtp_certificate-hint" disabled>
                <div class="form-text" id="wtp_certificate-hint">Administrator-managed certificate for this controller.</div>
            </div>
            <div class="col-12 col-lg-6 config-stacked-field" data-wtp-security="tls" hidden>
                <label class="form-label" for="wtp_key">Client private-key file</label>
                <input class="form-control" id="wtp_key" type="text" maxlength="512" placeholder="/etc/wsprrypi/pico/client.key" data-wtp-key="TLS Client Key" aria-describedby="wtp_key-hint" disabled>
                <div class="form-text" id="wtp_key-hint">Path only; never paste key material. The key must have owner-only permissions.</div>
            </div>
            <div class="col-12">
                <div class="form-check">
                    <input class="form-check-input" type="checkbox" id="wtp_adjust" data-wtp-key="Allow Frequency Adjustment" disabled>
                    <label class="form-check-label" for="wtp_adjust">Allow device frequency rounding</label>
                </div>
                <p class="form-text mb-0">Accepts the device's reported realizable frequencies. This does not establish RF accuracy.</p>
            </div>
        </div>
        <p class="form-text mt-3">The host needs synchronized UTC. The Pico needs its own valid UTC clock with reported uncertainty; USB automatically supplies qualified host UTC on supported firmware. Network WTP does not set that clock. GPSDO frequency discipline alone is not UTC.</p>
        <div class="wtp-status" aria-labelledby="wtp-status-heading">
            <h4 id="wtp-status-heading" class="cw-control-section__title">Pico status</h4>
            <p id="wtp-status-text" class="mb-2" role="status" aria-live="polite">Status has not been checked.</p>
            <dl class="wtp-observations">
                <div><dt>Connection</dt><dd id="wtp-connection-method">Unconfirmed</dd></div>
                <div><dt>Pico UTC over USB</dt><dd id="wtp-usb-time">Unconfirmed</dd></div>
                <div><dt>Output observed</dt><dd id="wtp-output">Unknown</dd></div>
                <div><dt>Network connection</dt><dd id="wtp-network-state">No network connection has been observed.</dd></div>
                <div><dt>Host UTC</dt><dd id="wtp-clock">Unknown</dd></div>
                <div><dt>Device / boot</dt><dd id="wtp-identity">Unknown</dd></div>
                <div><dt>Last job</dt><dd id="wtp-history">None</dd></div>
            </dl>
            <button type="button" class="btn btn-outline-danger me-2" id="wtp-cancel" disabled>Cancel current Pico job</button>
            <button type="button" class="btn btn-primary" id="wtp-recover" aria-describedby="wtp-recover-hint" disabled>Reconcile Pico</button>
            <p class="form-text mt-2 mb-0" id="wtp-recover-hint">Checks an idle device, or reconnects the current session to resolve its owned job. Recovery may stop that job. It never resumes a transmission or takes another owner's job.</p>
            <p id="wtp-feedback" class="form-text mt-2 mb-0" role="status" aria-live="polite"></p>
        </div>
        </div>
        <details id="wtp-firmware" class="wtp-status mt-3" hidden>
            <summary class="cw-control-section__title">Pico firmware over USB (controller output)</summary>
            <p class="form-text">Updates the Pico connected to this Pi by USB and preserves its saved settings. The Pico must be idle and unowned. Older firmware needs a one-time BOOTSEL reconnect when prompted; its saved standalone schedule must be disabled first. Keep USB connected during programming and verification.</p>
            <p id="wtp-firmware-identity" class="text-break">Read the selected Pico first.</p>
            <div class="row gx-3 gy-3">
                <div class="col-12 col-lg-4">
                    <label class="form-label" for="wtp-firmware-branch">Firmware branch</label>
                    <select id="wtp-firmware-branch" class="form-select"><option value="main">main</option><option value="devel">devel</option></select>
                </div>
                <div class="col-12 col-lg-4">
                    <label class="form-label" for="wtp-firmware-source">Image selection</label>
                    <select id="wtp-firmware-source" class="form-select" aria-describedby="wtp-firmware-selection-help"><option value="" selected disabled>Choose an image source</option><option value="latest">Latest verified CI image</option><option value="version">Specific version</option><option value="stable">Stable release</option></select>
                    <small class="form-text" id="wtp-firmware-selection-help">The image check requires firmware that matches this WsprryPi build’s settings contract.</small>
                </div>
                <div id="wtp-firmware-version-field" class="col-12 col-lg-4">
                    <label class="form-label" for="wtp-firmware-version">Firmware version</label>
                    <input id="wtp-firmware-version" class="form-control" type="text" maxlength="80" autocomplete="off" spellcheck="false">
                </div>
            </div>
            <div class="d-flex flex-wrap gap-2 mt-3">
                <button id="wtp-firmware-read" class="btn btn-outline-primary" type="button">Read firmware status</button>
                <button id="wtp-firmware-plan" class="btn btn-outline-primary" type="button" disabled>Download and check image</button>
                <button id="wtp-firmware-upgrade" class="btn btn-primary" type="button" disabled>Install checked image</button>
                <button id="wtp-firmware-recover" class="btn btn-outline-danger" type="button" hidden disabled>Recover this update</button>
            </div>
            <details class="mt-3">
                <summary>Erase this Pico</summary>
                <p class="form-text mt-2">Full erase removes the firmware, Wi-Fi passwords, certificates, Bluetooth bonds and every saved setting from all 4 MiB of flash. It creates no backup. Disable the Pico's saved standalone schedule first. After a BOOTSEL reconnect, the entire flash is erased and checked. The Pico stays blank until firmware is installed again. Existing backups on this Pi are retained.</p>
                <button id="wtp-firmware-erase" class="btn btn-outline-danger" type="button" disabled>Erase firmware and all data…</button>
            </details>
            <div id="wtp-firmware-activity" class="mt-3" hidden>
                <div class="d-flex align-items-center gap-2">
                    <span class="spinner-border spinner-border-sm flex-shrink-0" aria-hidden="true"></span>
                    <p id="wtp-firmware-phase" class="form-text mb-0" role="status" aria-live="polite"></p>
                </div>
                <p id="wtp-firmware-timing" class="form-text mt-1 mb-0" aria-live="off"></p>
            </div>
            <p id="wtp-firmware-read-status" class="form-text mt-2 mb-0" role="status" aria-live="polite"></p>
            <p id="wtp-firmware-feedback" class="form-text mt-3 mb-0 text-break" role="status" aria-live="polite">main is selected by default. No firmware download or device change has been requested.</p>
            <p id="wtp-firmware-retry" class="form-text mt-2 mb-0" aria-live="off" hidden></p>
            <p id="wtp-firmware-installer-update" class="form-text mt-2 mb-0 text-break" hidden></p>
            <div id="wtp-firmware-migration" class="mt-3" hidden>
                <p class="mb-2">Keep the Pico idle and do not edit its settings while completing these steps.</p>
                <ol class="mb-0">
                    <li>Unplug this Pico's USB cable from the Pi.</li>
                    <li>Hold BOOTSEL on the Pico while reconnecting to the same Pi USB port, then release BOOTSEL.</li>
                    <li>Choose <strong id="wtp-firmware-migration-action">Continue after BOOTSEL</strong>. <span id="wtp-firmware-migration-tail">Keep USB connected until installation and verification finish.</span></li>
                </ol>
            </div>
        </details>
    </section>
</div>

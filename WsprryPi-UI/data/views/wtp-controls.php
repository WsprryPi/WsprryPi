<?php // SPDX-License-Identifier: MIT ?>
<div id="wtp-development" class="wtp-development" hidden>
    <section id="wtp-controls" class="transmitter-backend-fields" aria-labelledby="wtp-heading" hidden>
        <h3 id="wtp-heading" class="transmitter-backend-fields__title">Pico output</h3>
        <section class="fleet-catalog mb-4" aria-labelledby="fleet-catalog-heading">
            <h4 id="fleet-catalog-heading" class="cw-control-section__title">Known and nearby devices</h4>
            <label class="form-label" for="fleet-device">Device</label>
            <select class="form-select" id="fleet-device" aria-describedby="fleet-device-detail" disabled>
                <option value="">Loading devices…</option>
            </select>
            <p class="form-text" id="fleet-device-detail">Exploring devices does not change the active connection.</p>
            <p class="form-text" id="fleet-discovery-status" role="status"></p>
            <div class="d-flex flex-wrap gap-2">
                <button type="button" class="btn btn-primary" id="fleet-use" disabled>Use this device</button>
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
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="fleet-uncertainty">Maximum start uncertainty (ns)</label>
                        <input class="form-control" id="fleet-uncertainty" type="number" min="1" max="1000000000" value="1000000">
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
            <div class="col-12 col-lg-6 config-stacked-field">
                <label class="form-label" for="wtp_uncertainty">Maximum start uncertainty (ns)</label>
                <input class="form-control" id="wtp_uncertainty" type="number" min="1" max="1000000000" step="1" value="1000000" data-wtp-key="Start Uncertainty ns" aria-describedby="wtp-uncertainty-hint" disabled>
                <div class="form-text" id="wtp-uncertainty-hint">1,000,000 ns = 1 ms. Device clock evidence must meet this limit.</div>
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
        <p class="form-text mt-3">The host needs synchronized UTC. The Pico needs its own valid UTC clock with bounded uncertainty; WTP does not set that clock. GPSDO frequency discipline alone is not UTC.</p>
        <div class="wtp-status" aria-labelledby="wtp-status-heading">
            <h4 id="wtp-status-heading" class="cw-control-section__title">Pico status</h4>
            <p id="wtp-status-text" class="mb-2" role="status" aria-live="polite">Status has not been checked.</p>
            <dl class="wtp-observations">
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
        <details id="wtp-management" class="wtp-status mt-3" hidden>
            <summary class="cw-control-section__title">Pico standalone settings and Wi-Fi</summary>
            <p class="form-text">These settings belong to Pico's standalone scheduler, separate from WsprryPi's schedule. Management is available only while the host has no pending, owned or unresolved job. Saving standalone settings requires a Pico restart to apply.</p>
            <button type="button" class="btn btn-outline-primary" id="wtp-remote-load">Load saved Pico settings</button>
            <p class="form-text">Loading replaces the standalone draft below. Status polling preserves it.</p>
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
                    <label class="form-label" for="wtp-remote-ntp">Time server IPv4 address</label>
                    <input class="form-control" id="wtp-remote-ntp" type="text" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-ntp-hint">
                    <div class="form-text" id="wtp-remote-ntp-hint"></div>
                </div>
                <div class="col-12 col-lg-6 config-stacked-field">
                    <label class="form-label" for="wtp-remote-expiry">Schedule expiry (UTC epoch seconds)</label>
                    <input class="form-control" id="wtp-remote-expiry" type="number" autocomplete="off" data-wtp-remote disabled aria-describedby="wtp-remote-expiry-hint">
                    <div class="form-text" id="wtp-remote-expiry-hint">0 means no expiry. Existing values are preserved until you change them.</div>
                </div>
                <div class="col-12">
                    <div class="form-check"><input class="form-check-input" type="checkbox" id="wtp-remote-enabled" data-wtp-remote disabled><label class="form-check-label" for="wtp-remote-enabled">Enable Pico standalone schedule after restart</label></div>
                </div>
                <div class="col-12 config-stacked-field">
                    <label class="form-label" for="wtp-remote-schedules">Standalone schedule periods and phases (seconds)</label>
                    <textarea class="form-control" id="wtp-remote-schedules" rows="3" data-wtp-remote disabled aria-describedby="wtp-remote-schedules-hint"></textarea>
                    <div class="form-text" id="wtp-remote-schedules-hint">One period and phase per line, separated by a space. Example: 120 0. Up to eight nonoverlapping schedules; values must be multiples of 120 seconds.</div>
                </div>
            </div>
            <div class="d-flex flex-wrap gap-2 mt-3">
                <button type="button" class="btn btn-primary" id="wtp-remote-save" disabled>Save Pico settings</button>
                <button type="button" class="btn btn-outline-primary" id="wtp-schedules-save" disabled>Save schedules only</button>
                <button type="button" class="btn btn-outline-primary" id="wtp-network-load">Read Pico Wi-Fi status</button>
                <button type="button" class="btn btn-outline-danger" id="wtp-network-disable" disabled>Disable Pico Wi-Fi</button>
            </div>
            <p class="form-text mt-2">Disabling Wi-Fi disconnects network control. Re-enable it using the physical USB Console or a restart. This is a temporary switch, separate from saved standalone settings.</p>
            <p id="wtp-management-feedback" class="form-text" role="status" aria-live="polite"></p>
        </details>
    </section>
</div>

<?php // SPDX-License-Identifier: MIT ?>

<div id="wtp-fleet">
    <section id="wtp-controls" class="transmitter-backend-fields" aria-labelledby="wtp-heading">
        <h3 id="wtp-heading" class="transmitter-backend-fields__title">Device management</h3>
        <section class="fleet-catalog mb-4" data-independent-wtp aria-labelledby="fleet-catalog-heading">
            <h4 id="fleet-catalog-heading" class="cw-control-section__title">Known and nearby devices</h4>
            <label class="form-label" for="fleet-device">Saved output or nearby device</label>
            <select class="form-select" id="fleet-device" aria-describedby="fleet-device-detail" disabled>
                <option value="">Loading devices…</option>
            </select>
            <p class="form-text" id="fleet-device-detail">Exploring devices does not change the active connection.</p>
            <p class="form-text" id="fleet-discovery-status" role="status"></p>
            <div class="d-flex flex-wrap gap-2">

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
                        <select class="form-select" id="fleet-binding"><option value="usb">USB</option><option value="network_plain">Consumer Wi-Fi (encrypted)</option><option value="network">Engineering HTTPS</option></select>
                    </div>
                    <div class="col-12 col-lg-6 config-stacked-field">
                        <label class="form-label" for="fleet-identity">Expected device ID</label>
                        <input class="form-control font-monospace" id="fleet-identity" maxlength="32" autocomplete="off" spellcheck="false" aria-describedby="fleet-identity-hint">
                        <div class="form-text" id="fleet-identity-hint">Full 32-character immutable ID from the Pico. A display name or advertisement alone is not identity.</div>
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

                    <div class="col-12 d-flex flex-wrap gap-3 align-items-center">
                        <div class="form-check"><input class="form-check-input" type="checkbox" id="fleet-plain-consent"><label class="form-check-label" for="fleet-plain-consent">I choose authenticated Consumer Wi-Fi</label></div>
                    </div>
                </div>
                <p class="form-text mt-2" id="fleet-editor-hint">Adding saves a connection profile only. Choose Manage settings to connect; saving does not enable a schedule.</p>
                <div class="d-flex flex-wrap gap-2 mt-2">
                    <button type="button" class="btn btn-outline-primary" id="fleet-identify">Identify selected nearby device</button>
                    <button type="button" class="btn btn-primary" id="fleet-save">Save device</button>
                    <button type="button" class="btn btn-outline-secondary" id="fleet-editor-cancel">Cancel</button>
                </div>
                <p id="fleet-editor-feedback" class="form-text mt-2" role="status" aria-live="polite"></p>
            </div>
        </section>
        <?php require __DIR__ . '/fleet-schedules.php'; ?>


    </section>
</div>

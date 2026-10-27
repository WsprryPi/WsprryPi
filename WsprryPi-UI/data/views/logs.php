<?php
$cardTitleId = 'cardTitle';
$cardTitleText = 'Wsprry Pi Log';
require __DIR__ . '/../card_header.php';
?>

            <div class="card-body logs-card-body">
                <div class="logs-control-row">
                    <div
                        id="sse-status-badge"
                        class="sse-disconnected logs-overlay"
                        role="status"
                        aria-live="polite"
                        aria-atomic="true"
                        title="Log stream disconnected">Disconnected</div>

                    <div id="logs-overlay-controls" role="toolbar" aria-label="Log controls" class="logs-overlay">
                        <button id="btn-clear"
                            class="btn btn-outline-warning btn-sm logs-toolbar-btn"
                            type="button">Clear</button>

                        <button id="btn-reconnect"
                            class="btn btn-outline-primary btn-sm logs-toolbar-btn"
                            type="button">Start stream</button>
                    </div>
                    <div class="logs-source-control">
                        <label for="logs-source" class="form-label mb-0">Source</label>
                        <select id="logs-source" class="form-select" aria-describedby="logs-source-status">
                            <option value="all">All sources</option>
                            <option value="local">Local Pi</option>
                        </select>
                    </div>
                </div>

                <p id="logs-source-status" class="logs-source-status" role="status" aria-live="polite"></p>
                <details class="logs-options">
                    <summary>Log options</summary>
                    <div class="logs-options-controls">
                        <label for="logs-severity">Show severity</label>
                        <select id="logs-severity" class="form-select">
                            <option value="7">All levels</option><option value="6">Info and higher</option>
                            <option value="4">Warnings and errors</option><option value="3">Errors only</option>
                        </select>
                        <button id="logs-refresh-sources" class="btn btn-outline-secondary" type="button">Refresh sources</button>
                        <button id="logs-pause" class="btn btn-outline-secondary" type="button">Pause view</button>
                        <button id="logs-export" class="btn btn-outline-secondary" type="button">Export visible logs</button>
                        <label><input id="logs-collect" type="checkbox" disabled> Collect this Pico's USB logs</label>
                        <p class="mb-0">Collection continues when this page is closed. The Pico Console remains available while logs are collected. Enable USB logging on the Pico separately.</p>
                    </div>
                </details>
                <button id="btn-jump-bottom" type="button" class="btn btn-sm btn-primary logs-jump-bottom" hidden>Jump to newest</button>
                <div id="log-scroll">
                    <div id="logsTabContent">
                        <div id="all"></div>
                        <div id="internal" hidden></div>
                    </div>
                </div>
            </div>

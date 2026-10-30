<?php
$cardTitleId = 'cardTitle';
$cardTitleText = 'Wsprry Pi Log';
require __DIR__ . '/../card_header.php';
?>

            <div class="card-body logs-card-body">
                <div class="logs-control-row">
                    <div id="logs-overlay-controls" role="toolbar" aria-label="Log controls" class="logs-overlay">
                        <button id="btn-clear"
                            class="btn btn-outline-warning btn-sm logs-toolbar-btn"
                            type="button">Clear</button>

                        <button id="btn-reconnect"
                            class="btn btn-outline-primary btn-sm logs-toolbar-btn"
                            type="button">Start stream</button>

                        <button id="logs-pause" class="btn btn-outline-secondary btn-sm logs-toolbar-btn" type="button">Pause view</button>
                        <button class="btn btn-outline-secondary btn-sm logs-toolbar-btn" type="button"
                            data-bs-toggle="modal" data-bs-target="#logs-options-modal">Log options</button>
                        <button class="btn btn-outline-secondary btn-sm logs-toolbar-btn" type="button"
                            data-bs-toggle="modal" data-bs-target="#host-logging-modal">Local logging</button>
                    </div>
                    <div class="logs-source-control">
                        <label for="logs-source" class="form-label mb-0">Source</label>
                        <select id="logs-source" class="form-select" aria-describedby="logs-source-status">
                            <option value="all">All sources</option>
                            <option value="local">Local Pi</option>
                        </select>
                    </div>
                    <div
                        id="sse-status-badge"
                        class="btn btn-outline-secondary btn-sm sse-disconnected logs-overlay"
                        role="status"
                        aria-live="polite"
                        aria-atomic="true"
                        title="Log stream disconnected">Disconnected</div>
                </div>

                <p id="logs-source-status" class="logs-source-status" role="status" aria-live="polite"></p>
                <button id="btn-jump-bottom" type="button" class="btn btn-sm btn-primary logs-jump-bottom" hidden>Jump to newest</button>
                <div id="log-scroll">
                    <div id="logsTabContent">
                        <div id="all"></div>
                        <div id="internal" hidden></div>
                    </div>
                </div>
            </div>

            <div class="modal fade logs-modal" id="logs-options-modal" tabindex="-1" aria-labelledby="logs-options-title" aria-hidden="true">
                <div class="modal-dialog modal-dialog-centered modal-dialog-scrollable">
                    <div class="modal-content">
                        <div class="modal-header">
                            <h3 class="modal-title h5" id="logs-options-title">Log options</h3>
                            <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
                        </div>
                        <div class="modal-body logs-options-controls">
                            <label for="logs-severity">Show severity</label>
                            <select id="logs-severity" class="form-select">
                                <option value="7">All levels</option><option value="6">Info and higher</option>
                                <option value="4">Warnings and errors</option><option value="3">Errors only</option>
                            </select>
                            <button id="logs-refresh-sources" class="btn btn-outline-secondary" type="button">Refresh sources</button>
                            <button id="logs-export" class="btn btn-outline-secondary" type="button">Export visible logs</button>
                            <label><input id="logs-collect" type="checkbox" disabled aria-describedby="logs-options-status"> Collect this Pico's USB logs</label>
                            <p id="logs-options-status" class="logs-source-status mb-0" role="status" aria-live="polite"></p>
                            <p class="mb-0">Collection continues when this page is closed. The Pico Console remains available while logs are collected. Enable USB logging on the Pico separately.</p>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-outline-secondary" data-bs-dismiss="modal">Close</button>
                        </div>
                    </div>
                </div>
            </div>

            <div class="modal fade logs-modal" id="host-logging-modal" tabindex="-1" aria-labelledby="host-logging-title" aria-hidden="true">
                <div class="modal-dialog modal-lg modal-dialog-centered modal-dialog-scrollable">
                    <div class="modal-content">
                        <div class="modal-header">
                            <h3 class="modal-title h5" id="host-logging-title">Logging on this Pi</h3>
                            <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
                        </div>
                        <div class="modal-body">
                            <div id="host-udp-logging-controls"></div>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-outline-secondary" data-bs-dismiss="modal">Close</button>
                        </div>
                    </div>
                </div>
            </div>

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
                </div>

                <button id="btn-jump-bottom" type="button" class="btn btn-sm btn-primary logs-jump-bottom" hidden>Jump to newest</button>
                <div id="log-scroll">
                    <div id="logsTabContent">
                        <div id="all"></div>
                        <div id="internal" hidden></div>
                    </div>
                </div>
            </div>

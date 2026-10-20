<?php
$cardTitleId = 'spotsFor';
$isDemoMode = isset($_GET['demo']) && $_GET['demo'] === '1';
$cardTitleText = $isDemoMode ? 'Recent demo spots for: AA0NT' : 'Recent spots';
require __DIR__ . '/../card_header.php';
?>

            <div class="spots-toolbar px-3 py-2 border-bottom bg-body-tertiary">
                <label class="spots-toolbar__label" for="spotsSource">Source</label>
                <select id="spotsSource" class="form-select form-select-sm spots-toolbar__select" aria-label="Spot data source">
                    <option value="auto" selected>Automatic failover</option>
                    <option value="wspr_live_downloader">wspr.live downloader</option>
                    <option value="wspr_live_clickhouse">wspr.live direct</option>
                </select>
                <div id="spotsSourceStatus" class="spots-toolbar__status text-body-secondary" aria-live="polite">
                    Automatic failover is selected.
                </div>
            </div>

            <div class="card-body tab-content bg-body">
            </div>

            <div id="server-settings" class="d-none">
                <input type="text" id="callsign" name="callsign" value="" />
            </div>

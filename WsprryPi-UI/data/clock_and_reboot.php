<!-- Break after title on XS only -->
<div class="w-100 d-sm-none"></div>

<!-- Group wrapper: Icons + Clocks -->
<div class="d-flex flex-wrap align-items-center group-wrapper">
    <!-- icons -->
    <div class="icons-wrapper d-flex align-items-center mb-2 mb-sm-0 me-sm-3">
        <!-- Reboot button with extra right margin -->
        <button
            type="button"
            id="rebootButton"
            class="btn btn-link text-body p-0 custom-tooltip system-action-button me-2"
            data-bs-toggle="tooltip"
            aria-label="Reboot system"
            title="Reboot">
            <i class="fa-solid fa-rotate-right fa-lg" aria-hidden="true"></i>
        </button>

        <!-- Shutdown button -->
        <button
            type="button"
            id="shutdownButton"
            class="btn btn-link text-body p-0 custom-tooltip system-action-button"
            data-bs-toggle="tooltip"
            aria-label="Shutdown system"
            title="Shutdown">
            <i class="fa-solid fa-power-off fa-lg" aria-hidden="true"></i>
        </button>
    </div>

    <!-- Break between icons and times on XS only -->
    <div class="w-100 d-sm-none"></div>

    <!-- Local and UTC Times -->
    <div class="times-wrapper small mb-2 mb-sm-0">
        <!-- Local Time Line -->
        <div class="time-line d-flex align-items-center">
            <span class="time-label">Local Time:</span>
            <span class="time-value" id="localTime">--:--:--</span>
        </div>
        <!-- UTC Time Line -->
        <div class="time-line d-flex align-items-center">
            <span class="time-label">UTC Time:</span>
            <span class="time-value" id="utcTime">--:--:--</span>
        </div>
    </div>
</div>

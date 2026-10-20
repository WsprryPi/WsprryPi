<!-- System action modal -->
<div class="modal fade" id="systemModal" tabindex="-1" aria-labelledby="systemModalLabel" aria-hidden="true">
    <div class="modal-dialog modal-dialog-centered">
        <div class="modal-content">

            <div class="modal-header">
                <h3 class="modal-title h5" id="systemModalLabel">System Event</h3>
                <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
            </div>

            <div id="systemModalBody" class="modal-body">
                <!-- JS will fill this in -->
            </div>

            <!-- Use Bootstrap’s modal-footer class for proper spacing & alignment -->
            <div class="modal-footer">
                <button type="button" class="btn btn-secondary exit-btn" data-bs-dismiss="modal">Exit</button>
                <button type="button" class="btn btn-primary reload-btn" disabled>Reload Page</button>
            </div>

        </div>
    </div>
</div>

<!-- Confirm before reboot/shutdown -->
<div class="modal fade" id="confirmModal" tabindex="-1" aria-labelledby="confirmModalLabel" aria-hidden="true">
    <div class="modal-dialog modal-dialog-centered">
        <div class="modal-content">
            <div class="modal-header">
                <h3 class="modal-title h5" id="confirmModalLabel">Please Confirm</h3>
                <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
            </div>
            <div class="modal-body">
                <!-- We'll fill this via JS -->
                <p id="confirmModalBody" class="mb-0"></p>
            </div>
            <div class="modal-footer">
                <button type="button" id="confirmCancelBtn" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                <button type="button" id="confirmActionBtn" class="btn btn-danger">Yes, proceed</button>
            </div>
        </div>
    </div>
</div>

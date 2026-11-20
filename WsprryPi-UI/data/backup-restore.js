// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    document.addEventListener('DOMContentLoaded', () => {
        const pane = document.getElementById('backupRestore');
        if (!pane) return;
        const element = id => document.getElementById(id);
        const base = root.WSPRRYPI_PATHS?.recoveryPath || '/api/recovery';
        const control = root.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1';
        if (!/^\/(?:[\w.~-]+\/)*api\/recovery$/.test(base)) return;
        let csrf = '', busy = false, known = false, active = null, jobs = [], selected = '', review = null, timer = null, disposed = false, previewDigest = '';
        let readError = '', pendingFocus = null;
        const storageKey = 'wsprrypi.recovery.csrf:' + base;
        try { csrf = sessionStorage.getItem(storageKey) || ''; } catch (_) { /* Browser storage is optional. */ }
        const current = () => jobs.find(job => job.job_id === selected);
        const jobPath = id => { if (!/^[0-9a-f]{32}$/.test(id)) throw Error('invalid_response'); return base + '/jobs/' + id; };
        const requestId = () => Array.from(crypto.getRandomValues(new Uint8Array(16)), value => value.toString(16).padStart(2, '0')).join('');
        const valueText = value => typeof value === 'string' ? value : JSON.stringify(value);
        const errors = {
            maintenance_admission_required: 'Maintenance access expired. Refresh results to establish access again.',
            busy: 'Another operation is active. Refresh results before trying again.',
            capture_busy: 'The local transmitter or settings owner is busy. Stop transmission, then create another backup.',
            archive_invalid: 'The backup or passphrase could not be validated. Check the file and try validation again.',
            destination_recovery_required: 'The destination’s scheduling authority needs recovery before these settings can be restored.',
            destination_changed: 'Destination state changed after preview. Validate a new staged restore before confirming.',
            validation_failed: 'The backup could not be accepted for this destination. Check its role, identity, and settings, then validate again.',
            backend_unavailable: 'The backend is unavailable. Refresh the saved result; do not assume the operation failed.',
            outcome_unknown: 'The result is unknown. Refresh the saved result before another action.',
            restore_failed: 'Restore needs recovery. Its saved state and output holds have been retained.',
            storage_unavailable: 'Maintenance storage is unavailable or full. Check the saved result before retrying.',
            interrupted: 'The operation was interrupted. Current settings were not automatically replaced.',
            invalid_request: 'The request was refused. Refresh the current result or destination review before trying again.',
            cancelled: 'The staged restore was cancelled.'
        };
        const message = error => errors[error?.message] || 'The result could not be confirmed. Refresh results to check saved state.';
        async function fetchJson(url, body, extra = {}) {
            const response = await fetch(url, { credentials: 'same-origin', cache: 'no-store', redirect: 'error',
                signal: AbortSignal.timeout(30000), ...(body === undefined ? {} : { method: 'POST', body: JSON.stringify(body),
                    headers: { 'Content-Type': 'application/json', 'X-WsprryPico-Request': '1', 'X-WsprryPi-Recovery-CSRF': csrf, ...extra } }) });
            const text = await response.text();
            if (text.length > 1024 * 1024) throw Error('invalid_response');
            let value;
            try { value = JSON.parse(text); } catch (_) { throw Error('backend_unavailable'); }
            if (!response.ok) throw Error(typeof value.error === 'string' ? value.error : 'backend_unavailable');
            return { value, response };
        }
        async function admit() {
            const session = await fetchJson(control + '/host/management-session', { contract: 'Wsprry-Control/1', schema: 'controller-browser-session' });
            const token = session.response.headers.get('X-WsprryPi-CSRF');
            if (!/^[0-9a-f]{64}$/.test(token || '')) throw Error('maintenance_admission_required');
            const admitted = await fetchJson(base + '/session', {}, { 'X-WsprryPi-CSRF': token });
            if (admitted.value.schema !== 'recovery-browser-admission/1' || !/^[0-9a-f]{64}$/.test(admitted.value.csrf || '')) throw Error('invalid_response');
            csrf = admitted.value.csrf;
            try { sessionStorage.setItem(storageKey, csrf); } catch (_) { /* No passphrases or archive contents are stored. */ }
        }
        function rows(target, values) {
            target.replaceChildren();
            for (const cells of values) {
                const row = document.createElement('tr');
                cells.forEach((value, index) => { const cell = document.createElement(index ? 'td' : 'th');
                    if (!index) cell.scope = 'row'; cell.className = 'text-break'; cell.textContent = valueText(value); row.append(cell); });
                target.append(row);
            }
        }
        function render() {
            const job = current(), confirmed = job?.confirmed === true;
            pane.setAttribute('aria-busy', String(busy));
            element('backupCreate').disabled = busy || !known || active !== null;
            element('restoreUpload').disabled = busy || !known || (active !== null && !(active === job?.job_id && job.state === 'uploading'));
            element('recoveryRefresh').disabled = busy;
            element('recoveryJob').disabled = busy || !jobs.length;
            element('backupDownload').hidden = job?.kind !== 'backup' || job?.state !== 'ready';
            if (job?.kind === 'backup' && job.state === 'ready') element('backupDownload').href = jobPath(job.job_id) + '/download';
            const validate = job?.kind === 'restore' && !confirmed && ['uploaded', 'preview', 'failed'].includes(job.state) && job.archive?.sha256 && job.error !== 'cancelled';
            element('restoreValidateForm').hidden = !validate;
            element('restoreValidate').disabled = busy || !known;
            element('restorePreview').hidden = job?.state !== 'preview';
            element('restoreCancel').hidden = job?.kind !== 'restore' || confirmed || !['created', 'uploading', 'uploaded', 'preview', 'failed'].includes(job.state);
            element('restoreCancel').disabled = busy || !known;
            element('restoreResume').hidden = !confirmed || job.state !== 'failed' || job.error === 'destination_changed';
            element('restoreResume').disabled = busy || !known;
            const nextDigest = job?.state === 'preview' ? job.preview?.digest || '' : '';
            if (nextDigest !== previewDigest) { element('restoreConsent').checked = false; previewDigest = nextDigest; }
            element('restoreConfirm').disabled = busy || !known || job?.state !== 'preview' || !element('restoreConsent').checked;
            element('destinationRead').disabled = busy || !known || active !== null;
            element('destinationRelease').disabled = busy || !known || !review || (!element('destinationLocal').checked && !element('destinationRemote').checked);
            if (job) {
                const labels = { capturing: 'Capturing a consistent backup…', encrypting: 'Encrypting backup…', ready: 'Backup ready. Download and keep a copy.',
                    uploading: 'Upload admitted. Select the same file and Upload backup to continue an interrupted transfer.', upload_checking: 'Checking the uploaded file…',
                    uploaded: 'Upload complete. Enter its passphrase and validate before restoring.', validating: 'Validating backup and current destination…',
                    preview: 'Backup validated. Review the proposed changes before confirming.', applying: 'Restoring. Local output and remote authority are held.',
                    awaiting_backend: 'Waiting for the restored backend. Refresh reads the saved result.', completed: 'Restore verified. Review the destination before releasing its output and authority holds.' };
                element('recoveryStatus').textContent = job.state === 'failed' ? message(Error(job.error)) : (labels[job.state] || 'Operation admitted. Refresh to check progress.');
                if (job.state === 'preview' && job.preview) {
                    const preview = job.preview;
                    element('restoreSource').textContent = `${preview.purpose === 'move' ? 'Move settings' : 'Recover this installation'} · ${preview.source.role} · ${preview.source.application_version} · ${new Date(preview.created_utc * 1000).toLocaleString()} · ${preview.source.identity}`;
                    rows(element('restoreChanges'), preview.changes.map(item => [item.setting, item.before, item.after]));
                    if (!preview.changes.length) rows(element('restoreChanges'), [['No saved setting changes', '—', '—']]);
                    element('restoreRetained').textContent = preview.retained_destination_settings.length ? 'Destination settings retained: ' + preview.retained_destination_settings.map(item => item.setting).join(', ') + '.' : 'No additional destination-specific settings differ.';
                    const connectionLabels = { preserved_destination_connection: 'destination connection retained', source_usb_connection: 'USB definition retained; authority checks apply',
                        destination_connection_required: 'set up the destination connection', destination_transport_required: 'select the destination USB path', unverified_definition: 'unverified; retained as recovery information' };
                    element('restoreDefinitions').textContent = 'Source device definitions: ' + (preview.fleet_definitions.map(item => item.name + ' (' + (connectionLabels[item.destination_status] || 'review destination connection') + ')').join('; ') || 'none') + '. Destination authority is preserved.';
                    element('restoreListeners').textContent = `Web port: ${preview.listeners.current_web_port} → ${preview.listeners.restored_web_port}. WebSocket port: ${preview.listeners.current_socket_port} → ${preview.listeners.restored_socket_port}. This admitted maintenance endpoint remains available for the result.`;
                    element('restoreListeners').textContent += ' Startup overrides can keep different active ports; the result reports the verified active addresses.';
                }
                if (job.result) element('recoveryStatus').textContent += ` Restored HTTP address: http://${location.hostname.includes(':') ? '[' + location.hostname.replace(/^\[|\]$/g, '') + ']' : location.hostname}:${job.result.web_port}; WebSocket port: ${job.result.socket_port}.`;
                if (job.result && (job.result.saved_web_port !== job.result.web_port || job.result.saved_socket_port !== job.result.socket_port)) element('recoveryStatus').textContent += ` Saved ports: ${job.result.saved_web_port} and ${job.result.saved_socket_port}; startup overrides remain active.`;
                if (job.result?.unresolved_definitions?.length) element('recoveryStatus').textContent += ' Destination connection setup is still required for: ' + job.result.unresolved_definitions.map(item => item.name).join(', ') + '.';
            } else element('recoveryStatus').textContent = known ? 'No saved backup or restore results.' : 'Checking maintenance access…';
            if (readError) element('recoveryStatus').textContent = readError + (job ? ' Last observed: ' + element('recoveryStatus').textContent : '');
            if (pendingFocus && !busy && known && (pendingFocus.kind !== 'preview' || job?.state === 'preview' || job?.state === 'failed')) {
                const target = element(pendingFocus.kind === 'preview' && job?.state === 'preview' ? 'restorePreviewTitle' : pendingFocus.kind === 'destination' ? 'destinationFeedback' : 'recoveryStatus');
                const focused = document.activeElement;
                if (focused === pendingFocus.from || focused === document.body || focused?.disabled || focused?.closest('[hidden]')) target.focus();
                pendingFocus = null;
            }
        }
        async function refresh(allowAdmission = true) {
            let result;
            try { result = await fetchJson(base + '/jobs'); }
            catch (error) { if (!allowAdmission || error.message !== 'maintenance_admission_required') throw error; await admit(); result = await fetchJson(base + '/jobs'); }
            const value = result.value;
            if (value.schema !== 'recovery-job-list/1' || !Array.isArray(value.jobs) || value.jobs.length > 4 || value.jobs.some(job => job.schema !== 'recovery-job/1' || !/^[0-9a-f]{32}$/.test(job.job_id))) throw Error('invalid_response');
            jobs = value.jobs; active = value.active; known = true; readError = '';
            if (!jobs.some(job => job.job_id === selected)) selected = active || jobs[0]?.job_id || '';
            const select = element('recoveryJob'); select.replaceChildren();
            for (const job of jobs) { const option = document.createElement('option'); option.value = job.job_id;
                option.textContent = `${job.kind === 'backup' ? 'Backup' : 'Restore'} · ${new Date(job.created_utc * 1000).toLocaleString()} · ${job.state}`; select.append(option); }
            if (!jobs.length) { const option = document.createElement('option'); option.textContent = 'No saved results'; option.value = ''; select.append(option); }
            select.value = selected; render();
            clearTimeout(timer);
            if (active && !disposed) timer = setTimeout(() => poll(), 3000);
        }
        async function poll() {
            if (busy) { if (!disposed) timer = setTimeout(poll, 3000); return; }
            try { await refresh(false); } catch (error) { known = false; readError = message(error); render(); if (!disposed) timer = setTimeout(poll, 3000); }
        }
        async function action(feedback, operation) {
            if (busy || disposed) return;
            busy = true; render(); feedback.textContent = 'Working…';
            try { if (!/^[0-9a-f]{64}$/.test(csrf)) await admit(); await operation();
                if (feedback.textContent === 'Working…') feedback.textContent = 'Request admitted. Check the saved result below.'; }
            catch (error) { feedback.textContent = message(error); }
            finally { busy = false; try { await refresh(false); } catch (error) { known = false; readError = message(error); } render(); }
        }
        element('backupCreateForm').addEventListener('submit', event => {
            event.preventDefault();
            const phrase = element('backupPassphrase'), confirmation = element('backupConfirmation');
            if (phrase.value !== confirmation.value || !phrase.value || new TextEncoder().encode(phrase.value).length > 1024) {
                element('backupFeedback').textContent = 'Enter matching passphrases of at most 1,024 UTF-8 bytes.'; confirmation.focus(); return;
            }
            action(element('backupFeedback'), async () => {
                const payload = { kind: 'backup', request_id: requestId(), passphrase: phrase.value, confirmation: confirmation.value };
                phrase.value = ''; confirmation.value = '';
                const result = await fetchJson(base + '/jobs', payload); payload.passphrase = ''; payload.confirmation = '';
                selected = result.value.job_id; element('backupFeedback').textContent = 'Backup admitted. Progress and download appear below.';
            });
        });
        element('restoreUploadForm').addEventListener('submit', event => {
            event.preventDefault(); const file = element('restoreFile').files[0];
            if (!file || file.size <= 0 || file.size > 2 * 1024 * 1024 * 1024) { element('restoreUploadFeedback').textContent = 'Select an encrypted WsprryPi backup no larger than 2 GiB.'; return; }
            action(element('restoreUploadFeedback'), async () => {
                let job = current();
                if (job?.kind !== 'restore' || job.state !== 'uploading') job = (await fetchJson(base + '/jobs', { kind: 'restore', request_id: requestId(), size: String(file.size) })).value;
                if (job.archive.size !== String(file.size)) throw Error('invalid_request');
                selected = job.job_id;
                for (let offset = 0; offset < file.size; offset += 65536) {
                    const bytes = new Uint8Array(await file.slice(offset, offset + 65536).arrayBuffer());
                    let binary = ''; for (const byte of bytes) binary += String.fromCharCode(byte);
                    await fetchJson(jobPath(job.job_id) + '/chunk', { offset: String(offset), bytes: btoa(binary) });
                    element('restoreUploadFeedback').textContent = `Uploaded ${Math.floor(Math.min(offset + bytes.length, file.size) / file.size * 100)}%.`;
                }
                await fetchJson(jobPath(job.job_id) + '/finish', {});
                element('restoreUploadFeedback').textContent = 'Upload complete. Validate the backup below.';
            });
        });
        element('restoreValidateForm').addEventListener('submit', event => {
            event.preventDefault(); const job = current(), input = element('restorePassphrase'); if (!job) return;
            const phrase = input.value;
            if (!phrase || new TextEncoder().encode(phrase).length > 1024) {
                element('recoveryActionFeedback').textContent = 'Enter the backup passphrase, up to 1,024 UTF-8 bytes.'; input.focus(); return;
            }
            pendingFocus = { kind: 'preview', from: document.activeElement };
            action(element('recoveryActionFeedback'), async () => {
                input.value = ''; element('restoreConsent').checked = false; review = null;
                await fetchJson(jobPath(job.job_id) + '/validate', { purpose: element('restorePurpose').value, passphrase: phrase });
            });
        });
        element('restoreConsent').addEventListener('change', render);
        element('restoreConfirm').addEventListener('click', () => {
            const job = current(); if (job?.state !== 'preview' || !element('restoreConsent').checked) return;
            pendingFocus = { kind: 'status', from: document.activeElement };
            action(element('recoveryActionFeedback'), async () => { element('restoreConsent').checked = false; review = null;
                await fetchJson(jobPath(job.job_id) + '/confirm', { preview_digest: job.preview.digest }); });
        });
        for (const [id, operation] of [['restoreCancel', 'cancel'], ['restoreResume', 'resume']]) element(id).addEventListener('click', () => {
            const job = current(); if (job) { pendingFocus = { kind: 'status', from: document.activeElement };
                action(element('recoveryActionFeedback'), async () => { await fetchJson(jobPath(job.job_id) + '/' + operation, {}); }); }
        });
        element('recoveryRefresh').addEventListener('click', () => action(element('recoveryActionFeedback'), async () => { await refresh(); element('recoveryActionFeedback').textContent = 'Saved results refreshed.'; }));
        element('recoveryJob').addEventListener('change', () => { selected = element('recoveryJob').value; element('restoreConsent').checked = false; render(); });
        element('destinationRead').addEventListener('click', () => action(element('destinationFeedback'), async () => {
            review = (await fetchJson(base + '/review')).value;
            if (review.schema !== 'recovery-destination-review/1' || !/^[0-9a-f]{64}$/.test(review.digest)) { review = null; throw Error('invalid_response'); }
            element('destinationLocal').checked = false; element('destinationRemote').checked = false;
            const controllerOnly = review.role === 'controller';
            element('destinationLocalRow').hidden = controllerOnly;
            element('destinationLocal').disabled = controllerOnly || !review.hold.local_output_held;
            element('destinationRemote').disabled = !review.hold.remote_authority_held;
            element('destinationHoldState').textContent = `Local output: ${controllerOnly ? 'unavailable in controller-only mode' : review.hold.local_output_held ? 'held' : 'not held'}. Remote authority: ${review.hold.remote_authority_held ? 'held' : 'not held'}.`;
            element('destinationReviewPolicy').textContent = controllerOnly
                ? 'Review this controller’s saved settings and connections. Releasing the remote hold permits existing pairing recovery and authority checks; it does not establish authority or adopt remote jobs. This role has no local transmitter.'
                : 'Review the destination’s station, frequencies, timing, hardware, and startup settings. Releasing the local hold leaves transmission stopped; use Start when ready. Releasing the remote hold permits existing pairing recovery and authority checks; it does not establish authority or adopt remote jobs.';
            rows(element('destinationSettings'), Object.entries(review.settings).flatMap(([section, fields]) => Object.entries(fields).map(([key, value]) => [section + '.' + key, value])));
            element('destinationReview').hidden = false; element('destinationFeedback').textContent = 'Current destination read. Review the settings before selecting holds to release.';
        }));
        for (const id of ['destinationLocal', 'destinationRemote']) element(id).addEventListener('change', render);
        element('destinationRelease').addEventListener('click', () => {
            if (!review) return;
            const captured = review;
            pendingFocus = { kind: 'destination', from: document.activeElement };
            action(element('destinationFeedback'), async () => {
                const result = await fetchJson(base + '/review', { action: 'release', digest: captured.digest,
                    release_local: element('destinationLocal').checked, release_remote: element('destinationRemote').checked });
                review = null; element('destinationReview').hidden = true;
                element('destinationFeedback').textContent = `Selected holds released. Local output: ${captured.role === 'controller' ? 'unavailable in controller-only mode' : result.value.hold.local_output_held ? 'held' : 'stopped; use Start when ready'}. Remote authority: ${result.value.hold.remote_authority_held ? 'held' : 'existing pairing and recovery checks apply'}.`;
            });
        });
        root.addEventListener('pagehide', () => { disposed = true; clearTimeout(timer); });
        refresh().catch(error => { known = false; readError = message(error); render(); });
    });
})(window);

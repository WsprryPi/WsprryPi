// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    if (!root.document) return;
    const control = root.WsprryControl, client = root.ConfigurationRecoveryClient.create({base:root.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1'});
    const node = id => root.document.getElementById(id);
    let active = false, built = false, busy = false, ready = false, blocked = false, polling = false, observer = null, serial = 0, actionRevision = 0;
    const unknown = value => !!value && typeof value === 'object' && value.state === 'unavailable';
    const element = (tag,className,text) => {const item = root.document.createElement(tag);if (className) item.className = className;if (text !== undefined) item.textContent = text;return item;};
    function savedHint(value) {
        if(!unknown(value) || typeof value.saved_text !== 'string') return null;
        let text = value.saved_text;try {const parsed = JSON.parse(text);text = typeof parsed === 'string' ? parsed : JSON.stringify(parsed);} catch (_) {}
        return element('p','form-text text-break','Saved value: ' + text);
    }
    function feedback(text) { for(const id of ['configurationRecoveryFeedback','configurationRecoveryFeedbackBottom']) if(node(id)) node(id).textContent = text; }
    function lockOrdinaryControls() {
        if (!active) return;
        root.suspendConfigAutosave?.(true);
        root.document.querySelectorAll('#main-content input,#main-content select,#main-content textarea,#main-content button').forEach(item => {
            if (!item.closest('#configurationRecoveryPanel') && !item.closest('#backupRestore') && !item.disabled) item.disabled = true;
        });
        for (const id of ['wsprform','configTabs','operationLocalView']) {
            const item = node(id);if (item) {item.hidden = true;item.inert = true;}
        }
        root.document.querySelectorAll('.config-header-actions').forEach(item => {item.hidden = true;});
    }
    function buttons() {
        const writable = !!client.current?.writable && !ready && !blocked, pending = !!client.operation;
        for (const id of ['configurationRecoverySave','configurationRecoverySaveBottom','configurationRecoveryRepair','configurationRecoveryReset']) {
            const item = node(id);if (item) item.disabled = !writable || busy || pending;
        }
        const retry = node('configurationRecoveryRetry');
        if (retry) {retry.hidden = !pending;retry.disabled = busy;}
        if(node('configurationRecoveryReturn')) node('configurationRecoveryReturn').disabled = busy;
        const fields = node('configurationRecoveryFields');
        if (fields) fields.querySelectorAll('input,select,textarea,button').forEach(item => {
            if (item.tagName === 'INPUT' || item.tagName === 'TEXTAREA') {item.readOnly = !writable;item.disabled = false;}
            else item.disabled = !writable;
        });
        node('configurationRecoveryPanel')?.setAttribute('aria-busy',String(busy));
    }
    function field(section,key,rule,value,parent,onChange) {
        const variants = rule.anyOf || [rule];
        const objectRule = variants.find(item => item.type === 'object');
        const invalid = unknown(value), actual = invalid ? undefined : value;
        if (objectRule && (actual === undefined || actual && typeof actual === 'object' && !Array.isArray(actual))) {
            const group = element('fieldset','mt-3'), legend = element('legend','h6',key);
            group.append(legend);parent.append(group);
            const original = savedHint(value);if(original) group.append(original);
            const keys = objectRule.required?.length ? objectRule.required : Object.keys(actual || {}).filter(name => Object.hasOwn(objectRule.properties || {},name));
            if (!keys.length) {
                group.append(element('p','form-text',invalid ? 'The saved value is unavailable. Repair it or explicitly remove its overrides.' : 'No saved overrides.'));
                if (invalid && !(objectRule.required || []).length) {
                    const empty = element('button','btn btn-outline-secondary btn-sm','Use no overrides');empty.type = 'button';
                    empty.addEventListener('click',() => {onChange({});empty.disabled = true;});group.append(empty);
                }
                return;
            }
            const values = actual ? structuredClone(actual) : {};
            for (const child of keys) field(section,child,objectRule.properties[child],actual?.[child],group,next => {
                values[child] = next;onChange(structuredClone(values));
                group.querySelectorAll('[aria-invalid="true"]').forEach(item => {
                    item.setAttribute('aria-invalid','false');
                    const message = node(item.getAttribute('aria-describedby').split(/\s+/)[0]);if (message) message.textContent = 'Unsaved recovery edit.';
                });
            });
            return;
        }
        let choice = variants.find(item => item.type === typeof actual) || variants.find(item => item.type === 'string') ||
            variants.find(item => ['integer','number','boolean'].includes(item.type)) || rule;
        let labels = null;
        if(client.current.validation === 'initialization') {
            const options = section === 'Operation' && key === 'Transmit Backend'
                ? {gpio:'Raspberry Pi GPIO','rp1-gpclk':'Raspberry Pi 5 GPIO (RP1)',si5351:'Si5351'}
                : section === 'Operation' && key === 'Mode' ? {WSPR:'WSPR',QRSS:'QRSS',FSKCW:'FSKCW',DFCW:'DFCW',TONE:'Tone'}
                : null;
            if(options) {labels=options;choice={...choice,enum:Object.keys(options)};}
        }
        const row = element('div','mb-3'), id = 'recovery-field-' + (++serial), label = element('label','form-label',key);
        label.htmlFor = id;row.append(label);parent.append(row);
        const original = savedHint(value);if(original) {original.id = id + '-saved';row.append(original);}
        let input;
        if (choice.enum || choice.type === 'boolean') {
            input = element('select','form-select');
            const values = choice.enum || [true,false];
            const placeholder = element('option','',invalid || actual === undefined ? 'Choose a value' : 'Choose another value');placeholder.value = '';input.append(placeholder);
            values.forEach((option,index) => {const item = element('option','',typeof option === 'boolean' ? option ? 'Yes' : 'No' : labels?.[option] || String(option));item.value = String(index);input.append(item);});
            input.value = values.findIndex(option => option === actual) < 0 ? '' : String(values.findIndex(option => option === actual));
            input.addEventListener('change',() => {if (input.value !== '') commit(values[Number(input.value)]);});
        } else {
            input = element('input','form-control');input.type = ['integer','number'].includes(choice.type) ? 'number' : 'text';
            if (input.type === 'number') {input.step = choice.type === 'integer' ? '1' : 'any';if (choice.minimum !== undefined) input.min = choice.minimum;if (choice.maximum !== undefined) input.max = choice.maximum;}
        if (choice.maxLength) input.maxLength = choice.maxLength;
            input.value = actual !== undefined && ['string','number'].includes(typeof actual) ? String(actual) : '';
            if (invalid && typeof value.saved_text === 'string') {
                try {const saved = JSON.parse(value.saved_text);if (['string','number'].includes(typeof saved)) input.value = String(saved);} catch (_) {}
            }
            input.addEventListener('input',() => {
                if (input.type === 'number' && (input.value === '' || !input.checkValidity())) {mark('Enter a supported number.');return;}
                commit(input.type === 'number' ? Number(input.value) : input.value);
            });
            if (Array.isArray(actual)) row.append(element('p','form-text','The saved list is retained until you enter a replacement. Enter bands or frequencies separated by spaces.'));
        }
        input.id = id;input.autocomplete = 'off';input.dataset.recoverySection = section;input.dataset.recoveryKey = key;
        row.insertBefore(input,row.children[1] || null);
        const message = element('div','form-text');message.id = id + '-feedback';input.setAttribute('aria-describedby',message.id + (original ? ' ' + original.id : ''));row.append(message);
        if (invalid || actual === undefined) mark('Saved value unavailable. Enter or choose a value explicitly.');
        function mark(text) {message.textContent = text;input.setAttribute('aria-invalid','true');}
        function commit(next) {
            try {onChange(next);message.textContent = 'Unsaved recovery edit.';input.setAttribute('aria-invalid','false');}
            catch (_) {mark('This value is not supported. Your edit is retained.');}
        }
    }
    function build() {
        const container = node('configurationRecoveryFields');if (!container || built) return;
        built = true;
        if(client.current.validation === 'storage_unavailable') {
            container.append(element('p','form-text','No verified saved settings are available. The saved history has been retained.'));return;
        }
        const initializing = client.current.validation === 'initialization';
        const sections = Object.entries(client.draft);
        if(initializing) {
            const order = ['WSPR','Operation'];
            sections.sort(([left],[right]) => (order.includes(left) ? order.indexOf(left) : 2) - (order.includes(right) ? order.indexOf(right) : 2));
        }
        for (const [section,values] of sections) {
            const titleText = initializing && section === 'WSPR' ? 'Station' : section;
            const group = element('details','py-3 border-bottom'), title = element('summary','h5 mb-0',titleText), fields = element('div','mt-3');
            group.open = initializing && ['WSPR','Operation'].includes(section) || Object.values(values).some(unknown);group.append(title,fields);container.append(group);
            for (const [key,value] of Object.entries(values)) {
                if (client.current.validation === 'initialization' && section === 'Operation' && ['Transmit','Enable on Boot'].includes(key)) continue;
                const rule = control.controllerPiConfigurationField(section,key);
                if (!rule) throw new Error('unavailable_configuration_shape');
                field(section,key,rule,value,fields,next => {client.set(section,key,next);buttons();});
            }
        }
    }
    async function pollReady() {
        if (polling) return;polling = true;
        const observedAction = actionRevision;
        const mayReport = () => observedAction === actionRevision || client.operation?.receipt?.state === 'committed';
        try {
            for (let attempt = 0; attempt < 15; ++attempt) {
                let status;
                try {status = await client.readiness();}
                catch(error) {
                    if(['capacity','rate_limited'].includes(error.code)) {await new Promise(resolve => root.setTimeout(resolve,2000));continue;}
                    throw error;
                }
                if (status.state === 'ready') {
                    ready = true;buttons();
                    if(client.operation && client.operation.receipt?.state !== 'committed') {
                        feedback('Startup is ready, but this save is not confirmed. Check the saved result before leaving this draft.');return;
                    }
                    if (client.operation ? !client.laterEdits : !client.dirty) {root.location.reload();return;}
                    feedback('Startup is ready. Your later edits are retained here and have not been saved.');
                    const back = node('configurationRecoveryReturn');if (back) {back.hidden = false;back.textContent = 'Return to Setup and discard later edits';}return;
                }
                if (['storage_recovery','output_recovery'].includes(status.state)) {
                    blocked = true;
                    feedback('Startup remains blocked. Settings are read-only until saved history and output can be verified.');return;
                }
                await new Promise(resolve => root.setTimeout(resolve,2000));
            }
            if(mayReport()) feedback(client.operation?.receipt?.state === 'committed'
                ? 'Settings were saved. Startup is taking longer than expected; check the saved result again.'
                : client.operation ? 'This save is unconfirmed. Check the saved result before another action.'
                    : 'Startup remains blocked. Your recovery edits have not been saved.');
        } catch (_) {if(mayReport()) feedback('The connection could not be verified. Your draft is retained; check again before another action.');}
        finally {polling = false;buttons();}
    }
    async function submit(action) {
        if (busy) return;busy = true;++actionRevision;buttons();feedback('Saving settings. Your draft is retained until the result is confirmed.');
        try {
            const result = action === 'retry' ? await client.retry() : action ? await client.action(action) : await client.save();
            if (result.state === 'committed') {feedback('Settings saved. Checking normal startup…');await pollReady();}
            else feedback('The save result is unconfirmed. Check the saved result before another action.');
        } catch (error) {
            const messages = {candidate_invalid:'Some settings are still invalid. Correct the draft, use Repair, or restore defaults.',
                revision_conflict:'Saved settings changed. Your draft is retained; reload only when you are ready to discard it.',
                authority_pending:'Settings cannot be changed while startup history or output is unverified.',
                boot_changed:'The device restarted. Your draft is retained; reload to review its current settings.'};
            feedback(messages[error.code] || 'The save result is unconfirmed. Your draft is retained; check the saved result before another action.');
            if (['revision_conflict','boot_changed'].includes(error.code)) {
                const back = node('configurationRecoveryReturn');if (back) {back.hidden = false;back.textContent = 'Reload and discard this draft';}
            }
        } finally {busy = false;buttons();}
    }
    function accept(value) {
        if (!control.validControllerDocument(value,'configuration-recovery')) return false;
        const installing = value.validation === 'initialization';
        active = true;
        try {client.accept(value);} catch (_) {feedback('Saved settings changed. Your existing draft is retained.');}
        const notice = node('configurationRecoveryNotice');if (notice) notice.hidden = false;
        const panel = node('configurationRecoveryPanel');if (panel) panel.hidden = false;
        if(panel && notice?.querySelector('a')) notice.querySelector('a').setAttribute('href','#configurationRecoveryPanel');
        if (installing) {
            const link = notice?.querySelector('a');
            const title = notice?.querySelector('h2');if(title) title.textContent = 'Initial setup required';
            if (link) {link.textContent = 'Review initial settings';link.href = node('installationSetup') ? '#configurationRecoveryPanel' :
                root.WSPRRYPI_PATHS?.basePath ? root.WSPRRYPI_PATHS.basePath + '/index.php?page=maintenance' : 'index.php?page=maintenance';}
            if (panel && !node('installationSetup')) panel.hidden = true;
            if (node('configurationRecoveryTitle')) node('configurationRecoveryTitle').textContent = 'Set up this transmitter';
            if (node('configurationRecoveryExplanation')) node('configurationRecoveryExplanation').textContent = 'Review these initial settings, then save them. Nothing transmits during setup.';
            if (node('configurationRecoveryStartupHint')) node('configurationRecoveryStartupHint').textContent = 'Setup saves transmission and scheduling disabled. Enabling a schedule is a separate step.';
            for (const id of ['configurationRecoverySave','configurationRecoverySaveBottom']) if(node(id)) node(id).textContent = 'Save initial settings';
            for (const id of ['configurationRecoveryRepair','configurationRecoveryRepairHint','configurationRecoveryResetSection']) if(node(id)) node(id).hidden = true;
        }
        if (node('configPageTitle')) node('configPageTitle').textContent = 'Recover saved settings';
        if (node('configSaveStatusHint')) node('configSaveStatusHint').textContent = 'Reviewing settings does not save them. Recovery changes require Save.';
        if (node('configSaveStatus')) node('configSaveStatus').textContent = 'Unconfirmed draft';
        lockOrdinaryControls();
        try {if(!installing || node('installationSetup')) build();} catch (_) {blocked = true;feedback('The recovery editor is unavailable. Reload to verify the page before making changes.');}
        buttons();
        root.PicoFirmwareSafety?.render?.();
        if (value.validation === 'migration_required') {
            if (node('configurationRecoveryExplanation')) node('configurationRecoveryExplanation').textContent = 'Migration required. Use WTP-capable software to disable legacy recurrence, confirm inactive and unowned output, and complete controller retirement before replacing it. Saved settings remain read-only.';
        } else if (!value.writable) {
            if (node('configurationRecoveryExplanation')) node('configurationRecoveryExplanation').textContent = 'Saved history or startup could not be verified. These settings are read-only; Reset cannot erase that history.';
            if (node('configSaveStatus')) node('configSaveStatus').textContent = 'Read-only recovery';
            if (node('configSaveStatusHint')) node('configSaveStatusHint').textContent = 'Saved history and startup must be verified before settings can change.';
            for(const id of ['configurationRecoveryStartupHint','configurationRecoveryRepairHint','configurationRecoveryBottomActions'])
                if(node(id)) node(id).hidden = true;
        }
        if (!observer && node('main-content')) {
            observer = new MutationObserver(lockOrdinaryControls);observer.observe(node('main-content'),{subtree:true,childList:true,attributes:true,attributeFilter:['disabled']});
        }
        if(value.writable && !blocked && !polling) void pollReady();
        return true;
    }
    for (const [id,action] of [['configurationRecoverySave',undefined],['configurationRecoverySaveBottom',undefined],['configurationRecoveryRepair','repair'],['configurationRecoveryReset','reset'],['configurationRecoveryRetry','retry']])
        node(id)?.addEventListener('click',() => {void submit(action);});
    node('configurationRecoveryReturn')?.addEventListener('click',() => root.location.reload());
    root.ConfigurationRecovery = Object.freeze({get active(){return active;},accept,render:lockOrdinaryControls,
        normalResponse(){if (!active) return false;void pollReady();return true;}});
})(typeof window === 'undefined' ? globalThis : window);

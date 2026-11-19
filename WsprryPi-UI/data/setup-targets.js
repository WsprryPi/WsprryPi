// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    if (!root.document) return;
    const node = id => root.document.getElementById(id);
    const tabs = ['radio-tab','scheduling-tab','transmitter-hardware-tab','pi-hardware-tab'];
    const localPanes = ['radio-pane','setup-scheduling','transmitter-hardware-pane','pi-hardware-pane'];
    const picoPanes = ['setup-pico-signal','setup-pico-scheduling','setup-pico-transmitter','setup-pico-io'];
    let schedulingKey=null;
    const identity = value => typeof value === 'string' && /^[0-9a-f]{32}$/.test(value);
    let choice = 'local', profiles = [], managedProfiles = [], managedPis = [], candidates = [], current = null, local = null;
    let inventoryReading = false, inventoryTimer = null, closed = false;
    const selectionValue = profile => (profile.managed ? 'managed:' : 'known:')+profile.id;
    const availableProfiles = () => [...profiles,...managedProfiles];
    let localDraft = false, sequence = 0, lastTab = tabs[0], loading = false, initialized = false, localReady = false;
    function activeTab() {return tabs.find(id => node(id)?.classList.contains('active'));}
    function retainedPis() {return [...(root.PiTargetEditors?.retainedTargets?.()||[]),...(root.SchedulingEditor?.retainedTargets?.({remoteOnly:true})||[])];}
    function options() {
        const select = node('setup-device');if (!select) return;
        const focused = root.document.activeElement === select;
        const values = [['local',root.ControllerMode?.active ? 'Controller · no local transmitter' : 'This Pi · local settings']];
        for (const profile of profiles) if (identity(profile.id) && identity(profile.settings?.['Device ID']))
            values.push(['known:'+profile.id,`${profile.name} · ${profile.settings.Transport} · ${profile.legacy_unverified ? 'unverified' : 'saved device'}`]);
        for (const profile of managedProfiles) values.push([selectionValue(profile),`${profile.name} · through paired Pi`]);
        for (const profile of root.PicoTargetEditors?.retainedProfiles?.() || [])
            if (!values.some(([id]) => id === selectionValue(profile))) values.push([selectionValue(profile),`${profile.name} · unavailable source · retained draft`]);
        for (const target of managedPis) values.push(['pi:'+target.device_id,`${target.name} · paired Pi`]);
        for (const target of retainedPis())
            if (!values.some(([id]) => id === 'pi:'+target.device_id)) values.push(['pi:'+target.device_id,`${target.name} · unavailable Pi · retained draft`]);
        for (const candidate of candidates) if (typeof candidate.id === 'string')
            values.push(['nearby:'+candidate.id,`${candidate.instance || 'Nearby device'} · ${candidate.binding || 'USB'} · not enrolled`]);
        const signature = JSON.stringify(values);
        if (select.dataset.options !== signature) {
            select.replaceChildren(...values.map(([value,label]) => {const option = root.document.createElement('option');option.value = value;option.textContent = label;return option;}));
            select.dataset.options = signature;
        }
        if (values.some(([id]) => id === choice)) select.value = choice;
        else if (choice !== 'local') {
            const option = root.document.createElement('option');option.value = choice;option.textContent = `${current?.name || 'Selected device'} · unavailable`;select.append(option);select.value = choice;
        }
        if (focused && root.document.activeElement !== select) select.focus({preventScroll:true});
    }
    function render() {
        if (!initialized || !node('setup-device')) return;
        options();
        const recovery = root.ConfigurationRecovery?.active === true, locked = root.PicoFirmwareSafety?.locked === true;
        const pico = choice.startsWith('known:') || choice.startsWith('managed:'), pi = choice.startsWith('pi:'), own = choice === 'local' && !root.ControllerMode?.active;
        const fleet = node('fleet-tab')?.classList.contains('active'), scheduling=node('scheduling-tab')?.classList.contains('active');
        const state = pi ? root.PiTargetEditors?.state : root.PicoTargetEditors?.state;
        node('setup-device').disabled = recovery || locked || !root.WtpUi?.catalogReady && !managedPis.length && !managedProfiles.length;
        node('wsprform').hidden = !own || scheduling;node('wsprform').inert = !own || !localReady || recovery || locked;
        node('setup-pico-workspace').hidden = !pico || fleet || recovery;
        node('setup-pico-workspace').inert = locked || recovery;
        if(node('setup-pi-workspace')){node('setup-pi-workspace').hidden=!pi||scheduling||fleet||recovery;node('setup-pi-workspace').inert=locked||recovery;}
        if (node('setup-local-output')) {node('setup-local-output').hidden = !own;node('setup-local-output').inert = !own || locked || recovery;}
        if (node('setup-controller-output-fields')) node('setup-controller-output-fields').inert = !own || !localReady || locked || recovery;
        node('setup-local-draft').hidden = !own || !localDraft || scheduling || recovery;
        node('setup-local-resume').disabled = locked || recovery;
        node('setup-device-reconnect').hidden = !(pico || pi) || loading || !!state && !state.paused;
        node('setup-device-reconnect').disabled = locked || recovery || !!state?.pending || !(pi?managedPis.some(item=>item.device_id===current?.device_id):availableProfiles().some(item => item.id === current?.id && Boolean(item.managed) === Boolean(current?.managed)));
        for (let i=0;i<tabs.length;++i) {
            const tab = node(tabs[i]);if (!tab) continue;
            const pane = pi ? (i===1?'setup-scheduling':'setup-pi-workspace') : pico ? picoPanes[i] : localPanes[i];
            tab.setAttribute('data-bs-target','#'+pane);tab.setAttribute('aria-controls',pane);
            const unavailable = recovery || !(own || pi || pico && state?.profile === current?.id);
            tab.disabled = unavailable;tab.setAttribute('aria-disabled',String(unavailable));
            for (const id of [localPanes[i],picoPanes[i]]) {
                const panel = node(id);if (!panel) continue;
                const visible = !fleet && (pico ? id === picoPanes[i] : id === localPanes[i]) && !pi && tab.classList.contains('active');
                panel.classList.toggle('active',visible);panel.classList.toggle('show',visible);
            }
        }
        const detail = node('setup-device-detail'),status = node('setup-device-state');
        if (pico) {
            detail.textContent = `${current?.name || 'Saved device'} · ${state?.connected ? 'Pico' : 'Pico management not verified'} · ${current?.managed ? 'through paired Pi' : current?.settings?.Transport || 'unavailable'} · device ${current?.settings?.['Device ID'] || 'unavailable'}`;
            status.textContent = loading ? 'Checking the selected device. No configuration or output change was requested.'
                : state?.unconfirmed ? 'Unconfirmed change on this Pico. Read and review its saved settings before another change.'
                : state?.paused ? 'Connection or profile changed. Draft retained; reconnect before editing.'
                : state?.pending ? 'A request is finishing on this Pico. Its result stays with this device.'
                : !state ? (node('pico-target-feedback')?.textContent || 'This device has no verified Pico management context. No settings were changed.')
                : state?.connected ? 'Connected. Each resource shows its saved, draft, and restart state beside its own actions.'
                : 'Settings are unavailable until this Pico is authenticated. Open Device I/O to check its connection. Paired Pis are available from the Device selector.';
        } else if(pi) {
            detail.textContent=(current?.name||'Paired Pi')+' · '+(current?.device_id||'unavailable');
            status.textContent=loading?'Reading the selected Pi. No settings or output change was requested.':state?.pending?'A request on this Pi is unconfirmed. Check its receipt or review current settings.':state?.paused?'Draft retained. Read saved settings before another change.':'Connected through pairing. Edits require an explicit Save on this Pi.';
        } else if (choice.startsWith('nearby:')) {
            detail.textContent = 'Nearby device · identity and management permission unverified';
            status.textContent = 'Identify and add this device in Fleet before managing settings. Selection does not enroll it.';
        } else {
            detail.textContent = root.ControllerMode?.active ? `Controller · ${root.ControllerMode.ready ? 'ready' : 'not ready'} · no local transmitter`
                : `This Pi · local settings${local ? ' · device '+local.device_id : ''}`;
            status.textContent = localDraft ? 'Local draft retained. Review it and choose Save retained local draft to resume saving.'
                : root.ControllerMode?.active ? 'Choose a saved Pico to manage its settings. Paired Pis are available from the Device selector.'
                : !localReady ? 'Reading this Pi’s saved settings. Editing remains unavailable until they load.' : 'Local settings keep their existing save behavior. Selecting another device never changes this Pi’s output.';
        }
        if(pi&&!scheduling){root.PiTargetEditors?.showTab?.([0,-1,1,2][Math.max(0,tabs.indexOf(activeTab()))]);node('setup-pi-workspace')?.setAttribute('aria-labelledby',activeTab()||tabs[0]);}
        const schedulePanel=node('setup-scheduling');
        if(schedulePanel){
            schedulePanel.hidden=!scheduling||!(own||pi)||fleet||recovery;
            schedulePanel.classList.toggle('active',!schedulePanel.hidden);schedulePanel.classList.toggle('show',!schedulePanel.hidden);
            schedulePanel.inert=locked||recovery;
            const target=own?local:pi?current:null,key=!schedulePanel.hidden&&target?(own?'local:':'pi:')+target.device_id:null;
            if(key!==schedulingKey){schedulingKey=key;root.SchedulingEditor?.leave?.();
                if(key){if(own)retainLocal();void root.SchedulingEditor?.select?.(target,own);}
                else if(own&&!localDraft)root.suspendConfigAutosave?.(false);
            }
        }
        const title = node('configPageTitle');
        if (title && !recovery) title.textContent = fleet ? 'Fleet' : (node(activeTab())?.textContent.trim() || 'Setup');
        for (const id of ['configSaveStatus','configSaveStatusHint','configSaveStatusDetail']) {
            const item = node(id);if (!item || recovery) continue;
            if (id !== 'configSaveStatusDetail') item.hidden = !own || scheduling || fleet;
            if (id === 'configSaveStatusHint' && own) item.textContent = localDraft
                ? 'Autosave is paused. Review the retained local draft before saving it.'
                : !localReady ? 'Reading saved settings before allowing local edits.'
                : 'Changes save automatically. Invalid entries stay in this browser until corrected.';
        }
    }
    function retainLocal() {
        if (choice !== 'local' || root.ControllerMode?.active) return;
        if (localReady && root.hasUnsavedLocalConfigChanges?.()) localDraft = true;
        root.suspendConfigAutosave?.(true);
    }
    function showTab(id) {
        const tab = node(id);if (tab && !tab.disabled) root.bootstrap.Tab.getOrCreateInstance(tab).show();
        render();
    }
    async function selectProfile(profile) {
        if (!node('setup-device') || root.ConfigurationRecovery?.active || root.PicoFirmwareSafety?.locked) return;
        if (!profile || !identity(profile.id) || !identity(profile.settings?.['Device ID'])) return;
        root.PiTargetEditors?.leave?.();
        retainLocal();current = structuredClone(profile);choice = selectionValue(profile);
        const trigger = root.document.activeElement;
        const captured = ++sequence;loading = true;render();
        if (profile.legacy_unverified || !availableProfiles().some(item => item.id === profile.id && Boolean(item.managed) === Boolean(profile.managed))) {
            root.PicoTargetEditors.leave();loading = false;render();
            if (profile.legacy_unverified) {root.WtpUi.explore?.(choice);showTab('fleet-tab');}
            else {node('pico-target-retained').open = true;node('pico-target-feedback').textContent = 'This profile is unavailable. Retained drafts are read-only; no connection or save was attempted.';}
            return;
        }
        try {await root.PicoTargetEditors.select(profile);}
        finally {
            if (captured === sequence) {
                loading = false;render();
                if (node('fleet-tab')?.classList.contains('active')) showTab(lastTab);
                if (root.document.activeElement === trigger && trigger?.id === 'fleet-manage') node('setup-device').focus({preventScroll:true});
            }
        }
    }
    async function select(value) {
        if (root.ConfigurationRecovery?.active || root.PicoFirmwareSafety?.locked) {render();return;}
        if(value.startsWith('pi:')) {
            const id=value.slice(3),target=managedPis.find(item=>item.device_id===id)||retainedPis().find(item=>item.device_id===id);
            if(!target)return;retainLocal();root.PicoTargetEditors.leave();current=structuredClone(target);choice=value;const captured=++sequence;loading=true;render();
            try{await root.PiTargetEditors.select(target);}finally{if(captured===sequence){loading=false;render();if(node('fleet-tab')?.classList.contains('active'))showTab(lastTab);}}return;
        }
        if (value.startsWith('known:') || value.startsWith('managed:')) {
            const managed = value.startsWith('managed:'), id = value.slice(managed?8:6),profile = availableProfiles().find(item => item.id === id && Boolean(item.managed)===managed) || root.PicoTargetEditors.retainedProfiles().find(item => item.id === id && Boolean(item.managed)===managed);
            if (profile) await selectProfile(profile);
            return;
        }
        retainLocal();root.PiTargetEditors?.leave?.();++sequence;loading = false;choice = value;current = null;root.PicoTargetEditors.leave();
        if (value === 'local') {
            if (!localDraft && !root.ControllerMode?.active) root.suspendConfigAutosave?.(false);
            render();if (root.ControllerMode?.active) showTab('fleet-tab');
        } else {
            root.WtpUi.explore?.(value);showTab('fleet-tab');
        }
    }
    async function readLocalIdentity() {
        if (inventoryReading || closed) return;inventoryReading = true;
        try {
            const base=root.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1';
            const response=await root.fetch(base+'/targets',{cache:'no-store',credentials:'same-origin',redirect:'error',signal:AbortSignal.timeout(10000)});
            const value=await response.json();
            if(!response.ok||!root.WsprryControl.validControllerDocument(value,'targets'))throw Error('target_inventory_unavailable');
            const ready=await root.fetch(base+'/host/readiness',{cache:'no-store',credentials:'same-origin',redirect:'error',signal:AbortSignal.timeout(10000)});
            const host=await ready.json();
            if(!ready.ok||!root.WsprryControl.validControllerDocument(host,'host-readiness'))throw Error('host_identity_unavailable');
            if(closed)return;
            local=value.targets.find(target=>target.device_id===host.device_id&&target.product==='WsprryPi')||null;
            managedPis=value.targets.filter(target=>target.product==='WsprryPi'&&target.device_id!==host.device_id);
            // Native-editor rendering can select local Scheduling immediately.
            // Publish its availability first so that selection uses this snapshot.
            root.SchedulingEditor?.targetsChanged?.([...(local?[local]:[]),...managedPis]);
            root.PiTargetEditors?.targetsChanged?.(managedPis);
            managedProfiles=value.targets.filter(target=>target.product==='WsprryPico'&&!profiles.some(profile=>profile.settings['Device ID']===target.device_id))
                .map(target=>({id:target.device_id,name:target.name,managed:true,settings:{'Device ID':target.device_id,Transport:'usb'}}));
            root.PicoTargetEditors?.managedTargetsChanged?.(managedProfiles);
        } catch (_) {
            // Discovery/absence cannot authorize an alternate owner. Retain
            // drafts, withdraw these paired observations, and await fresh data.
            managedPis=[];root.PiTargetEditors?.targetsChanged?.([]);root.SchedulingEditor?.targetsChanged?.([]);
            managedProfiles=[];root.PicoTargetEditors?.managedTargetsChanged?.([]);
        } finally {
            inventoryReading=false;render();root.clearTimeout(inventoryTimer);
            if(!closed)inventoryTimer=root.setTimeout(readLocalIdentity,3000);
        }
    }
    root.SetupTargets = Object.freeze({
        get localSelected() {return choice === 'local' && !root.ControllerMode?.active;},
        get localWritable() {return localReady && choice === 'local' && !localDraft && !root.ControllerMode?.active && !root.ConfigurationRecovery?.active && !root.PicoFirmwareSafety?.locked;},
        get retainingLocalDraft() {return localDraft;},
        get managingPi() {return choice.startsWith('pi:');},
        get managingPico() {return choice.startsWith('known:') || choice.startsWith('managed:');},
        selectProfile,render,editorChanged:render,localLoaded() {localReady = true;render();},
        catalogChanged(next,nearby) {
            profiles = Array.isArray(next) ? structuredClone(next.filter(item => item && identity(item.id) && identity(item.settings?.['Device ID']))) : [];
            candidates = Array.isArray(nearby) ? structuredClone(nearby.filter(item => item && typeof item.id === 'string')) : [];render();
        }
    });
    function init() {
        if (!node('setup-device')) return;initialized = true;
        node('setup-device').addEventListener('change',event => {void select(event.target.value);});
        node('setup-device-reconnect').addEventListener('click',() => {if(choice.startsWith('pi:')){void select(choice);return;}const profile = availableProfiles().find(item => item.id === current?.id && Boolean(item.managed)===Boolean(current?.managed));if (profile) {node('setup-device').focus({preventScroll:true});void selectProfile(profile);}});
        node('setup-local-resume').addEventListener('click',() => {
            if (choice !== 'local' || root.ControllerMode?.active || root.ConfigurationRecovery?.active || root.PicoFirmwareSafety?.locked) return;
            localDraft = false;root.suspendConfigAutosave?.(false);render();root.flushAutosave?.();
        });
        for (const id of [...tabs,'fleet-tab']) node(id)?.addEventListener('shown.bs.tab',() => {if (id !== 'fleet-tab') lastTab = id;render();});
        if (root.WtpUi?.catalogReady) root.SetupTargets.catalogChanged(root.WtpUi.savedProfiles,root.WtpUi.nearbyDevices);
        root.addEventListener('pagehide',()=>{closed=true;root.clearTimeout(inventoryTimer);});
        root.addEventListener('pageshow',()=>{if(closed){closed=false;void readLocalIdentity();}});
        render();void readLocalIdentity();
    }
    if (root.document.readyState === 'loading') root.document.addEventListener('DOMContentLoaded',init);else init();
})(typeof window === 'undefined' ? globalThis : window);

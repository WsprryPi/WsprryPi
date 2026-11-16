// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    const identity = value => typeof value === 'string' && /^[0-9a-f]{32}$/.test(value);
    function create(host) {
        const client = host.PicoTargetClient.create({base:host.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1'});
        const entries = new Map();
        let active = null, selection = 0, template;
        const node = id => host.document.getElementById(id);
        const fingerprint = profile => JSON.stringify([profile.id,profile.name,profile.settings]);
        const busy = entry => entry.pending > 0 || entry.panel.querySelector('[aria-busy="true"]') !== null;
        function retained() {
            const disclosure = node('pico-target-retained'),list = node('pico-target-retained-list');
            const drafts = Array.from(entries.values()).filter(entry => entry !== active);
            disclosure.hidden = drafts.length === 0;
            list.replaceChildren();
            for (const entry of drafts) {
                const row = host.document.createElement('details'),title = host.document.createElement('summary');
                row.className = 'mt-2';title.className = 'text-break';row.dataset.retainedDevice = entry.profile.settings['Device ID'];
                title.textContent = `${entry.profile.name} · saved profile ${entry.generation || 'not connected'}`;
                row.append(title);list.append(row);
                row.addEventListener('toggle',() => {
                    if (!row.open || row.childElementCount > 1) return;
                    const identity = host.document.createElement('p');identity.className = 'form-text text-break';
                    identity.textContent = `Device ${entry.profile.settings['Device ID']}. Read-only retained entries; saved values may have changed.`;
                    const values = host.document.createElement('dl');values.className = 'text-break';
                    for (const field of entry.panel.querySelectorAll('input,select,textarea')) {
                        if (['button','submit','hidden'].includes(field.type) || field.type === 'radio' && !field.checked) continue;
                        const label = (field.id ? entry.panel.querySelector(`label[for="${host.CSS.escape(field.id)}"]`) : null) || field.closest('label');
                        const term = host.document.createElement('dt'),detail = host.document.createElement('dd');
                        term.textContent = label?.textContent.trim().slice(0,160) || field.getAttribute('aria-label') ||
                            field.id.replace(/^wtp-(?:remote-|member-|usb-)?/,'').replaceAll('-',' ') || field.name || 'Setting';
                        detail.textContent = field.type === 'password' ? field.value ? 'A password entry is retained but is not displayed. Enter it again in a current editor if needed.' : 'No password entry.'
                            : field.type === 'checkbox' ? field.checked ? 'Yes' : 'No'
                            : field.tagName === 'SELECT' ? Array.from(field.selectedOptions).map(option => option.textContent).join(', ') || 'No selection'
                            : field.value || 'Empty';
                        values.append(term,detail);
                    }
                    row.append(identity,values);
                });
            }
        }
        function ledger() {
            const list = node('pico-target-operations');
            if (!list) return;
            list.replaceChildren();
            for (const record of client.operations().slice().reverse()) {
                const row = host.document.createElement('li');
                row.textContent = `${record.device} · ${record.resource} · ${record.state}`;
                list.append(row);
            }
        }
        function recovery(entry) {
            if (active !== entry) return;
            entry.panel.inert = Boolean(entry.hold) || entry.paused;
            node('pico-target-recovery').hidden = !entry.hold;
            if (!entry.hold) return;
            node('pico-target-recovery-state').textContent = entry.recoveryMessage || 'An earlier change is unconfirmed on this Pico. Your draft is preserved. Read its current settings and review them before another change.';
            node('pico-target-review-recovery').disabled = !entry.observed || entry.recovering || entry.paused;
            node('pico-target-read-recovery').disabled = entry.recovering || entry.paused;
        }
        function hold(entry, hint) {
            entry.hold = hint; entry.observed = null;
            entry.nativeHold = true;
            entry.recoveryMessage = '';
            if (active === entry) node('pico-target-recovery-observation').replaceChildren();
            recovery(entry);
        }
        async function generation(entry) {
            const value = await entry.handle.generation();
            const key = `${entry.profile.settings['Device ID']}:${entry.profile.id}:${value}`;
            if (!entries.has(key) || entries.get(key) === entry) {
                if (entry.key && entries.get(entry.key) === entry) entries.delete(entry.key);
                entry.key = key; entries.set(key,entry);
            }
            entry.generation = value;
            if (entries.get(key) !== entry && !entry.restoreObserver) {
                entry.restoreObserver = new host.MutationObserver(() => restoreConnected(entry,key));
                entry.restoreObserver.observe(entry.panel,{attributes:true,attributeFilter:['aria-busy']});
                host.queueMicrotask(() => restoreConnected(entry,key));
            }
            return key;
        }
        function restoreConnected(entry,key) {
            if (active !== entry || entry.paused) {entry.restoreObserver?.disconnect();entry.restoreObserver = null;return;}
            if (busy(entry)) return;
            const saved = entries.get(key);if (!saved || saved === entry) return;
            entry.restoreObserver.disconnect();entry.restoreObserver = null;
            entry.paused = true;++entry.epoch;entry.panel.remove();
            if (entries.get(entry.key) === entry) entries.delete(entry.key);
            saved.handle = entry.handle;saved.paused = false;++saved.epoch;
            saved.management.invalidate();saved.usb.invalidate();saved.udp.invalidate();
            active = saved;node('pico-target-panel').append(saved.panel);
            saved.management.setAvailability(saved.handle.state.connected,true);
            saved.usb.setAvailability(saved.handle.state.carrier === 'usb_physical');saved.udp.setAvailability(saved.handle.state.connected);
            node('pico-target-feedback').textContent = 'Connected to this Pico. Its retained draft is restored; read current saved settings before another change.';
            recovery(saved);ledger();retained();
        }
        function response(result) {
            const headers = new Headers(result.response.headers);
            headers.set('Content-Type','application/json');
            return new Response(JSON.stringify(result.data),{status:result.response.status,headers});
        }
        async function exchange(entry, url, options = {}) {
            if (entry.paused || !entry.handle) throw Error('This editor needs a fresh connection. Its draft is preserved.');
            const base = host.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1';
            if (typeof url !== 'string' || !url.startsWith(base + '/')) throw Error('Unsupported Pico settings path.');
            const path = url.slice(base.length + 1);
            const resource = path.startsWith('member/') ? path.slice(7) : path;
            const method = options.method || 'GET', mutation = method !== 'GET';
            if (!host.WsprryControl.controllerAdapter.pico_routes[method]?.includes(resource)) throw Error('Unsupported Pico setting.');
            if (entry.hold && mutation && resource !== 'management') throw Error('Review the unconfirmed change before another save.');
            const handle = entry.handle, epoch = entry.epoch;
            const body = options.body ? JSON.parse(options.body) : undefined;
            const headers = new Headers(options.headers || {});
            ++entry.pending;
            try {
                let result = await handle.request(resource,{method,body,revision:headers.get('If-Match') || '',memberContext:headers.get('X-WsprryPi-Member-Context') || ''});
                const tracked = result.operation && client.operations().find(record => record.device === handle.device && record.operation === result.operation);
                const uncertain = mutation && resource !== 'management' && resource !== 'pins/validate' &&
                    (result.data?.schema === 'target-operation' && ['pending','unknown'].includes(result.data.state) ||
                        tracked?.state === 'unconfirmed' || result.response.status >= 500);
                if (uncertain || result.recovery && !result.response.ok)
                    hold(entry,result.recovery || {device:handle.device,operation:result.operation,resource,read:host.WsprryControl.controllerRecoveryRead(resource)});
                if (result.data?.schema === 'target-error' && ['profile_changed','connection_changed','capability_changed','unauthenticated','unauthorized'].includes(result.data.error?.code)) {
                    entry.paused = true;++entry.epoch;handle.retire();entry.panel.inert = true;
                    if (active === entry) node('pico-target-feedback').textContent = 'This Pico connection changed or permission was withdrawn. Its draft is preserved. Manage the intended saved Pico again before editing.';
                }
                if (entry.paused || epoch !== entry.epoch) throw Error('The response belongs to the previous editor. Its request remains tracked on that Pico.');
                if (resource === 'management' && method === 'POST' && result.response.ok &&
                    !['inspect_logins','retire_login','reconcile'].includes(body?.action)) {
                    if (body?.action === 'disconnect') {
                        handle.retire(); entry.handle = await client.open(entry.profile);
                        result = await entry.handle.request('management');
                    } else {
                        result = await handle.request('management');
                        if (result.response.ok && result.data.connected) await generation(entry);
                    }
                }
                if (entry.paused || epoch !== entry.epoch) throw Error('This editor was superseded. Its draft is preserved.');
                entry.usb?.setAvailability(entry.handle.state.connected && entry.handle.state.carrier === 'usb_physical');
                entry.udp?.setAvailability(entry.handle.state.connected);
                return response(result);
            } catch (error) {
                if (error.code === 'request_unconfirmed' && error.operation)
                    hold(entry,{device:handle.device,operation:error.operation,resource,read:host.WsprryControl.controllerRecoveryRead(resource)});
                throw error;
            } finally {--entry.pending;ledger();recovery(entry);}
        }
        function mount(profile, handle, key) {
            const panel = template.content.firstElementChild.cloneNode(true);
            const entry = {profile,signature:fingerprint(profile),panel,handle,key,pending:0,epoch:0,paused:false,hold:null,observed:null,recovering:false};
            const scoped = new Proxy(host.document,{get(document,property) {
                if (property === 'getElementById') return id => id === panel.id ? panel : panel.querySelector('#' + host.CSS.escape(id));
                if (property === 'querySelector') return selector => panel.querySelector(selector);
                if (property === 'querySelectorAll') return selector => panel.querySelectorAll(selector);
                if (property === 'readyState') return 'complete';
                const value = document[property];return typeof value === 'function' ? value.bind(document) : value;
            }});
            const environment = {document:scoped,crypto:host.crypto,confirm:host.confirm.bind(host),targetManagement:true,
                WSPRRYPI_PATHS:host.WSPRRYPI_PATHS,WtpUi:{read:() => profile.settings,selected:() => true},
                fetch:(...args) => exchange(entry,...args)};
            for (const name of ['WsprryControl','WsprryAmplifierTiming','WsprryFilters','WsprryGpioDrive','WsprryGps','WsprryGrayLine','WsprrySi5351']) environment[name] = host[name];
            entry.management = host.WtpManagementEditor.mount(environment);
            entry.usb = host.WtpUsbSettingsEditor.mount(environment);
            entry.udp = host.WsprryUdp.mount(scoped.getElementById('member-udp-logging-controls'),{
                inputClass:'form-control',buttonClass:'btn btn-outline-secondary',checkClass:'form-check',
                context:() => entry.handle?.context,
                exchange:async (path,method = 'GET',body,revision) => {
                    const reply = await exchange(entry,(host.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1')+'/member/'+path,
                        {method,...(body ? {body:JSON.stringify(body),headers:{'If-Match':revision}} : {})});
                    const value = host.WsprryUdp.parseJson(await reply.text());
                    if (!reply.ok) throw Error('Logging request refused or unconfirmed. Read this Pico and review the preserved draft.');
                    return {value,revision:reply.headers.get('ETag') || ''};
                }
            });
            environment.WsprryMemberUdp = entry.udp;
            return entry;
        }
        async function select(profile) {
            if (!template) return;
            if (!profile || profile.legacy_unverified || !identity(profile.id) || !identity(profile.settings?.['Device ID'])) {
                node('pico-target-feedback').textContent = 'Save and verify this Pico profile before managing settings.';return;
            }
            profile = structuredClone(profile);Object.freeze(profile.settings);Object.freeze(profile);
            const working = Array.from(entries.values()).find(entry => entry.profile.settings['Device ID'] === profile.settings['Device ID'] && busy(entry));
            if (working) {node('pico-target-feedback').textContent = 'A request is still finishing on this Pico. Its result is tracked here; wait before reopening its editor.';return;}
            const epoch = ++selection;
            if (active) {active.paused = true;++active.epoch;active.restoreObserver?.disconnect();active.restoreObserver = null;active.handle.retire();active.panel.remove();}
            active = null;
            retained();
            node('pico-target-panel').replaceChildren();node('pico-target-recovery').hidden = true;
            node('pico-target-identity').textContent = `${profile.name} · ${profile.settings.Transport} · device ${profile.settings['Device ID']}`;
            node('pico-target-feedback').textContent = 'Checking this Pico. The transmitter backend and accepted jobs remain unchanged.';
            let handle;
            try {
                handle = await client.open(profile);
                if (epoch !== selection) {handle.retire();return;}
                if (!handle.state.connected && ['usb','network'].includes(profile.settings.Transport)) {
                    const connected = await handle.request('management',{method:'POST',body:{action:profile.settings.Transport === 'usb' ? 'usb_physical' : 'engineering_mtls'}});
                    if (epoch !== selection) {handle.retire();return;}
                    if (!connected.response.ok) throw Error('This Pico could not be connected. Check its saved profile and try Manage settings again.');
                    const checked = await handle.request('management');if (!checked.response.ok || !checked.data.connected) throw Error('Pico connection is unconfirmed.');
                }
                const generation = handle.state.connected ? await handle.generation() : 'unconnected';
                if (epoch !== selection) {handle.retire();return;}
                const key = `${handle.device}:${profile.id}:${generation}`;
                let entry = entries.get(key);
                if (!entry) {
                    if (entries.size >= 64) throw Error('The browser is retaining 64 Pico drafts. Open a new page to manage another device; existing drafts stay on this page.');
                    entry = mount(profile,handle,key);entries.set(key,entry);
                } else {
                    entry.handle = handle;entry.paused = false;++entry.epoch;
                    entry.management.invalidate();entry.usb.invalidate();entry.udp.invalidate();
                }
                active = entry;entry.generation = generation;
                node('pico-target-panel').append(entry.panel);entry.panel.open = true;
                node('pico-target-feedback').textContent = 'Settings are bound to this Pico. Read saved settings before editing. Drafts stay with their device and saved profile.';
                entry.management.setAvailability(handle.state.connected,true);
                entry.usb.setAvailability(handle.state.connected && handle.state.carrier === 'usb_physical');
                entry.udp.setAvailability(handle.state.connected);
                recovery(entry);ledger();retained();
            } catch (error) {
                handle?.retire();
                if (epoch === selection) node('pico-target-feedback').textContent = error.message || 'Pico management is unavailable. Saved drafts are retained.';
            }
        }
        async function readRecovery() {
            const entry = active;
            if (!entry?.hold || entry.paused || entry.recovering) return;
            entry.recovering = true;entry.observed = null;entry.recoveryMessage = 'Reading current settings from this Pico…';recovery(entry);
            try {
                try {
                    const prior = await entry.handle.lookup(entry.hold.operation);
                    entry.hold = prior.recovery;entry.nativeHold = ['pending','unknown'].includes(prior.data.state);
                } catch (error) {
                    if (error.status !== 404) throw error;
                    entry.nativeHold = false;
                }
                const result = await entry.handle.request(entry.hold.read);
                if (active !== entry || entry.paused) return;
                if (!result.response.ok || !result.readback) throw Error('Authoritative settings could not be read. Reconnect this Pico; no change was repeated.');
                entry.observed = result.readback;
                const list = node('pico-target-recovery-observation');list.replaceChildren();
                const show = (names,item) => {
                    const term = host.document.createElement('dt'),detail = host.document.createElement('dd');
                    term.textContent = names.join(' · ').replaceAll('_',' ');
                    detail.textContent = item === null ? 'Unavailable' : typeof item === 'object' ? JSON.stringify(item) : typeof item === 'boolean' ? item ? 'Yes' : 'No' : String(item);
                    list.append(term,detail);
                };
                const inspect = (value,names = []) => {
                    if (value !== null && typeof value === 'object' && !Array.isArray(value) && names.length < 5) {
                        for (const [name,item] of Object.entries(value)) {
                            if (['contract','schema','member_context','control_capabilities','capabilities','pin_choices'].includes(name) || /password|secret|private_key|psk|pairing_code/.test(name)) continue;
                            if (!names.length && entry.hold.read === 'network' && !['enabled','requested_enabled','withdrawal_pending','ipv4','configured_hostname','advertised_hostname','link_status'].includes(name)) continue;
                            inspect(item,name === 'config' && !names.length ? names : [...names,name]);
                        }
                    } else show(names,value);
                };
                const observed = entry.hold.read === 'configuration'
                    ? {target:result.data.target,saved:result.data.config,restart_required:result.data.reboot_required}
                    : result.data;
                inspect(observed);
                entry.recoveryMessage = 'Review this Pico’s current settings beside your preserved draft. Matching values do not prove that the earlier request committed.';
            } catch (error) {entry.recoveryMessage = error.message;}
            finally {entry.recovering = false;recovery(entry);}
        }
        async function reviewRecovery() {
            const entry = active;
            if (!entry?.hold || entry.paused || !entry.observed || entry.recovering) return;
            entry.recovering = true;recovery(entry);
            try {
                if (entry.nativeHold) {
                    const result = await entry.handle.request('management',{method:'POST',body:{action:'reconcile',operation_id:entry.hold.operation,readback_revision:entry.observed}});
                    if (active !== entry || entry.paused) return;
                    if (!result.response.ok) throw Error('Review was not accepted. Read this Pico again before another change.');
                }
                client.reviewed(entry.hold.operation,entry.handle.device);
                entry.hold = null;entry.observed = null;entry.management.invalidate();entry.usb.invalidate();entry.udp.invalidate();
                node('pico-target-feedback').textContent = 'Current settings reviewed. The earlier outcome remains historical. Your draft is preserved; read saved settings before a new explicit change.';
            } catch (error) {if (active === entry) node('pico-target-feedback').textContent = error.message;}
            finally {entry.recovering = false;recovery(entry);ledger();}
        }
        function catalogChanged(profiles) {
            if (!active || !Array.isArray(profiles)) return;
            const current = profiles.find(profile => profile.id === active.profile.id);
            if (current && fingerprint(current) === active.signature) return;
            active.paused = true;++active.epoch;active.handle.retire();active.panel.inert = true;
            recovery(active);
            node('pico-target-feedback').textContent = 'This saved profile changed or was removed. Its draft is retained separately. Choose the intended saved Pico and Manage settings again.';
        }
        function init() {
            template = node('pico-target-template');if (!template) return;
            node('pico-target-read-recovery').addEventListener('click',readRecovery);
            node('pico-target-review-recovery').addEventListener('click',reviewRecovery);
            host.addEventListener('pagehide',() => {++selection;for (const entry of entries.values()) {entry.paused = true;++entry.epoch;entry.handle.retire();}});
        }
        return Object.freeze({init,select,catalogChanged,operations:() => client.operations()});
    }
    if (typeof module !== 'undefined' && module.exports) module.exports = {create};
    if (!root.document) return;
    root.PicoTargetEditors = create(root);
    if (root.document.readyState === 'loading') root.document.addEventListener('DOMContentLoaded',root.PicoTargetEditors.init);
    else root.PicoTargetEditors.init();
})(typeof window === 'undefined' ? globalThis : window);

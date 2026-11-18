// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    const registry = root.WsprryControl || (typeof require === 'function' ? require('./control-contract.js') : null);
    const documents = root.ConfigurationRecoveryClient || (typeof require === 'function' ? require('./configuration-recovery-client.js') : null);
    const identity = value => typeof value === 'string' && /^[0-9a-f]{32}$/.test(value);
    const generation = value => typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value) && BigInt(value) <= 18446744073709551615n;
    const session = (value,device) => value === null || value !== null && typeof value === 'object' && !Array.isArray(value) &&
        Object.keys(value).sort().join(',') === 'boot_id,device_id,generation,selector' && value.device_id === device &&
        identity(value.boot_id) && identity(value.selector) && generation(value.generation);
    const management = (value,context,device) => value !== null && typeof value === 'object' && !Array.isArray(value) &&
        Object.keys(value).sort().join(',') === 'carrier,connected,contract,host,selection_id,session' &&
        value.contract === registry.contract && typeof value.connected === 'boolean' && value.selection_id === context && identity(context) &&
        ['engineering_mtls','consumer_station','consumer_ap','usb_physical','disconnected','unsupported'].includes(value.carrier) &&
        typeof value.host === 'string' && value.host.length <= 254 && !/[\u0000-\u001f\u007f]/.test(value.host) && session(value.session,device) &&
        (value.carrier.startsWith('consumer_') ? !value.connected || value.session !== null : value.session === null) &&
        (!['disconnected','unsupported'].includes(value.carrier) || !value.connected);
    class TargetRequestError extends Error {
        constructor(code, detail = {}) { super(code); this.code = code; Object.assign(this, detail); }
    }
    // Each handle owns its captured target. Choosing another editor never moves
    // a request or its eventual receipt to the newly displayed target.
    function create({fetch = root.fetch.bind(root), crypto = root.crypto, base = '/api/control/v1'} = {}) {
        if (!registry || typeof base !== 'string' || base.length > 96 || !/^\/(?:[a-zA-Z0-9_.~-]+\/)*api\/control\/v1$/.test(base) || base.split('/').some(part => part === '.' || part === '..')) throw new TargetRequestError('invalid_base');
        let csrf = '', bootstrap = null;
        const operations = new Map();
        const terminal = value => ['committed','rejected','refused'].includes(value.state) || value.state === 'unknown' && value.reviewed === true;
        function remember(key,value) {
            if (!operations.has(key) && operations.size >= 256) {
                const expired = Array.from(operations).find(([,item]) => terminal(item));
                if (!expired) throw new TargetRequestError('operation_tracking_capacity');
                operations.delete(expired[0]);
            }
            operations.set(key,Object.freeze(value));
        }
        const id = () => Array.from(crypto.getRandomValues(new Uint8Array(16)), value => value.toString(16).padStart(2, '0')).join('');
        async function browserSession() {
            if (csrf) return csrf;
            if (!bootstrap) bootstrap = (async () => {
                const response = await fetch(base + '/host/management-session', {
                    method:'POST', credentials:'same-origin', cache:'no-store', redirect:'error', signal:AbortSignal.timeout(30000),
                    headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1'},
                    body:JSON.stringify({contract:registry.contract,schema:'controller-browser-session'})
                });
                const value = await response.json(), token = response.headers.get('X-WsprryPi-CSRF');
                if (!response.ok || !registry.validControllerDocument(value,'controller-browser-session') || !/^[0-9a-f]{64}$/.test(token || '')) throw new TargetRequestError('browser_session_unavailable');
                csrf = token; return token;
            })().finally(() => {bootstrap = null;});
            return bootstrap;
        }
        function recovery(response, device) {
            const resource = response.headers.get('X-WsprryPi-Recovery-Resource');
            if (resource === null) return null;
            const read = registry.controllerRecoveryRead(resource), location = response.headers.get('Location');
            const prefix = '/api/control/v1/targets/' + device + '/operations/';
            if (!read || !location?.startsWith(prefix) || !identity(location.slice(prefix.length))) throw new TargetRequestError('invalid_recovery_metadata');
            return Object.freeze({device,resource,read,operation:location.slice(prefix.length)});
        }
        async function open(profile) {
            if (!profile || !identity(profile.id) || !identity(profile.settings?.['Device ID'])) throw new TargetRequestError('saved_profile_required');
            const device = profile.settings['Device ID'], profileId = profile.id, managed = profile.managed === true;
            const prefix = base + '/targets/' + device;
            const profileHeaders = managed ? {} : {'X-WsprryPi-Profile-ID':profileId};
            let binding = null, scope = profileId, parentDevice = null;
            const initialHeaders = {...profileHeaders};
            if (managed) {
                const reply = await fetch(prefix + '/context',{credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000)});
                const context = await documents.readControllerDocument(reply,'target-context');
                const token = reply.headers.get('X-WsprryPi-Target-Context');
                if (!identity(token) || context.target.product !== 'WsprryPico' || context.binding.device_id !== device ||
                    context.authorization === null || context.gateway === null || context.capabilities.length !== 0) throw new TargetRequestError('target_context_unavailable');
                binding = structuredClone(context.binding);parentDevice = context.gateway.parent.device_id;
                scope = JSON.stringify([context.gateway.parent.device_id,context.authorization.pair_id,context.authorization.authorization_generation]);
                initialHeaders['X-WsprryPi-Target-Context'] = token;
            }
            const response = await fetch(prefix + '/pico/management', {credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000),headers:initialHeaders});
            let state = await response.json();
            const initial = response.headers.get('X-WsprryPi-Target-Context');
            if (!response.ok || !management(state,initial,device) || managed && (!state.connected || state.carrier !== 'usb_physical')) throw new TargetRequestError('target_context_unavailable');
            let context = initial, retired = false;
            const handle = {
                device, profileId, managed, scope, parentDevice,
                get state() {return state;},
                get context() {return context;},
                retire() {retired = true;},
                async request(resource, {method = 'GET',body,revision = '',memberContext = ''} = {}) {
                    if (retired) throw new TargetRequestError('connection_changed');
                    if (!registry.controllerAdapter.pico_routes[method]?.includes(resource)) throw new TargetRequestError('unsupported_resource');
                    if (managed && resource === 'management' && method !== 'GET') throw new TargetRequestError('unsupported_resource');
                    const mutation = method !== 'GET', settingsWrite = mutation && !['management','pins/validate'].includes(resource);
                    if (settingsWrite && !revision) throw new TargetRequestError('revision_required');
                    const captured = context, operation = settingsWrite ? id() : '';
                    const headers = {...profileHeaders,'X-WsprryPi-Target-Context':captured};
                    if (mutation) {
                        headers['X-WsprryPi-CSRF'] = await browserSession();
                        headers['Content-Type'] = 'application/json'; headers['X-WsprryPico-Request'] = '1';
                        if (revision) headers['If-Match'] = revision;
                        if (memberContext) headers['X-WsprryPi-Member-Context'] = memberContext;
                        if (operation) headers['X-WsprryPi-Operation-ID'] = operation;
                    }
                    if (retired || context !== captured) throw new TargetRequestError('connection_changed');
                    const key = device + ':' + operation;
                    if (settingsWrite) {
                        remember(key,{device,profileId,operation,resource,state:'unconfirmed'});
                    }
                    try {
                        const reply = await fetch(prefix + '/pico/' + resource, {
                            method,headers,credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000),
                            ...(mutation ? {body:JSON.stringify({...body,contract:registry.contract})} : {})
                        });
                        const value = await reply.json(), hint = recovery(reply,device);
                        if (settingsWrite && reply.ok && reply.headers.get('X-WsprryPi-Operation-ID') !== operation) throw new TargetRequestError('invalid_operation_metadata');
                        const next = reply.headers.get('X-WsprryPi-Target-Context');
                        if (next !== null && !identity(next)) throw new TargetRequestError('invalid_context_metadata');
                        // A superseded in-flight reply can still resolve its own
                        // receipt, but cannot replace another request's context.
                        if (!retired && context === captured && next) context = next;
                        if (resource === 'management' && method === 'GET' && reply.ok) {
                            if (!management(value,next || captured,device)) throw new TargetRequestError('invalid_management_metadata');
                            if (!retired) state = value;
                        }
                        if (settingsWrite) {
                            const receipt = registry.validControllerDocument(value,'target-operation');
                            const error = registry.validControllerDocument(value,'target-error');
                            if(error && value.error.code === 'csrf_failed') csrf = '';
                            // fetch() can hide an HTTP PUT retry after an
                            // accepted reply is lost. Busy/auth/context errors
                            // cannot prove that no earlier copy committed.
                            const refusal = error && (
                                ['invalid_shape','invalid_json','candidate_invalid','payload_too_large'].includes(value.error.code) ||
                                value.error.code === 'recovery_required' && hint && hint.operation !== operation);
                            const state = receipt ? value.state : reply.ok ? 'committed' : refusal ? 'refused' : 'unconfirmed';
                            operations.set(key,Object.freeze({device,profileId,operation,resource,state,...(hint ? {recovery:hint} : {})}));
                        }
                        return {response:reply,data:value,recovery:hint,operation,context:captured,readback:reply.headers.get('X-WsprryPi-Readback-Revision') || ''};
                    } catch (error) {
                        // The record deliberately contains no request body,
                        // station draft, password, credential, or private key.
                        throw new TargetRequestError('request_unconfirmed',{device,operation,cause:error});
                    }
                },
                async generation() {
                    if (retired) throw new TargetRequestError('connection_changed');
                    const captured = context;
                    const reply = await fetch(base + '/targets',{credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000)});
                    const value = await reply.json();
                    if (!reply.ok || !registry.validControllerDocument(value,'targets')) throw new TargetRequestError('target_context_unavailable');
                    const target = value.targets.find(item => item.device_id === device);
                    if (!target) throw new TargetRequestError('target_context_unavailable');
                    const checked = await handle.request('management');
                    if (retired || captured !== context || !checked.response.ok || !state.connected) throw new TargetRequestError('connection_changed');
                    return target.profile_generation;
                },
                async review(operation,readback) {
                    if (!managed || retired || !binding || !identity(operation) || !/^[0-9a-f]{64}$/.test(readback)) throw new TargetRequestError('connection_changed');
                    const captured = context, token = await browserSession();
                    if (retired || context !== captured) throw new TargetRequestError('connection_changed');
                    const reply = await fetch(prefix + '/operations/' + operation + '/review',{
                        method:'POST',credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000),
                        headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','X-WsprryPi-CSRF':token,'X-WsprryPi-Target-Context':captured},
                        body:JSON.stringify({contract:registry.contract,schema:'target-operation-review',binding,operation_id:operation,readback_revision:readback})
                    });
                    let data;
                    try {data = await documents.readControllerDocument(reply,'target-operation-reviewed');}
                    catch (error) {if (error.code === 'csrf_failed') csrf = '';throw error;}
                    if (data.operation_id !== operation || data.readback_revision !== readback || data.binding.device_id !== device ||
                        data.binding.boot_id !== binding.boot_id || data.binding.profile_generation !== binding.profile_generation ||
                        data.binding.connection_generation === binding.connection_generation) throw new TargetRequestError('invalid_response');
                    retired = true;return {response:reply,data};
                },
                async lookup(operation) {
                    if (retired || !identity(operation)) throw new TargetRequestError('connection_changed');
                    const reply = await fetch(prefix + '/operations/' + operation,{credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000),headers:{...profileHeaders,'X-WsprryPi-Target-Context':context}});
                    const data = await reply.json();
                    if (!reply.ok || !registry.validControllerDocument(data,'target-operation') || data.operation_id !== operation || data.binding.device_id !== device) throw new TargetRequestError('receipt_unavailable',{status:reply.status});
                    const hint = recovery(reply,device), prior = operations.get(device+':'+operation);
                    if (!hint) throw new TargetRequestError('invalid_recovery_metadata');
                    remember(device+':'+operation,{...prior,device,profileId,operation,resource:hint.resource,state:data.state,recovery:hint});
                    return {data,recovery:hint};
                }
            };
            return Object.freeze(handle);
        }
        return Object.freeze({open,operations:() => Object.freeze(Array.from(operations.values())),
            reviewed(operation,device) {const key=device+':'+operation,value=operations.get(key);if(value) remember(key,{...value,state:['pending','unconfirmed'].includes(value.state)?'unknown':value.state,reviewed:true});},
            forget(operation,device) {const key=device+':'+operation;const value=operations.get(key);if(value && terminal(value)) operations.delete(key);}
        });
    }
    const api = Object.freeze({create,TargetRequestError});
    root.PicoTargetClient = api;
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window === 'undefined' ? globalThis : window);

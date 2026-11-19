// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    const control = root.WsprryControl || (typeof require === 'function' ? require('./control-contract.js') : null);
    const clone = value => JSON.parse(JSON.stringify(value));
    const same = (left, right) => {
        if (left === right) return true;
        if (!left || !right || typeof left !== 'object' || typeof right !== 'object' || Array.isArray(left) !== Array.isArray(right)) return false;
        const keys = Object.keys(left).sort(), other = Object.keys(right).sort();
        return keys.length === other.length && keys.every((key,index) => key === other[index] && same(left[key],right[key]));
    };
    const own = (value, key) => Object.prototype.hasOwnProperty.call(value, key);
    class RecoveryError extends Error {
        constructor(code, status = 0) { super(code); this.code = code; this.status = status; }
    }
    async function document(response, family) {
        let text = '';
        if (response.body?.getReader) {
            const reader = response.body.getReader(), decoder = new TextDecoder('utf-8', {fatal:true});
            let size = 0;
            try {
                while (true) {
                    const part = await reader.read();
                    if (part.done) break;
                    size += part.value.byteLength;
                    if (size > 32768) throw new RecoveryError('invalid_response');
                    text += decoder.decode(part.value, {stream:true});
                }
                text += decoder.decode();
            } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
        } else {
            text = await response.text();
            if (new TextEncoder().encode(text).byteLength > 32768) throw new RecoveryError('invalid_response');
        }
        let value;
        try { value = JSON.parse(text); } catch (_) { throw new RecoveryError('invalid_response'); }
        // JSON.parse alone accepts duplicate keys. Scan valid JSON strings to
        // reject repeated decoded object keys before inspecting any fields.
        const levels = [];
        for (let index = 0; index < text.length; ++index) {
            const character = text[index];
            if (character === '{' || character === '[') {
                levels.push(character === '{' ? new Set() : null);
                if (levels.length > 16) throw new RecoveryError('invalid_response');
            } else if (character === '}' || character === ']') levels.pop();
            else if (character === '"') {
                const start = index++;
                while (text[index] !== '"') { if (text[index] === '\\') ++index; ++index; }
                let next = index + 1; while (/\s/.test(text[next] || '') && next < text.length) ++next;
                if (text[next] === ':') {
                    const key = JSON.parse(text.slice(start,index + 1)), keys = levels.at(-1);
                    if (!keys || keys.has(key)) throw new RecoveryError('invalid_response');
                    keys.add(key);
                }
            }
        }
        if (!control.validControllerDocument(value, response.ok ? family : 'target-error')) throw new RecoveryError('invalid_response');
        if (!response.ok) throw new RecoveryError(value.error.code,response.status);
        return value;
    }
    function create({fetch = root.fetch.bind(root), crypto = root.crypto, base = '/api/control/v1'} = {}) {
        if (!control || typeof base !== 'string' || base.length > 96 ||
            !/^\/(?:[a-zA-Z0-9_.~-]+\/)*api\/control\/v1$/.test(base) || base.split('/').some(part => part === '.' || part === '..'))
            throw new RecoveryError('invalid_base');
        const path = base + '/host/configuration-recovery';
        let current = null, draft = null, edited = false, revision = 0, token = '', pending = null, sending = false;
        const request = (url, options = {}) => fetch(url, {credentials:'same-origin',cache:'no-store',redirect:'error',
            signal:AbortSignal.timeout(30000),...options});
        const id = () => Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2,'0')).join('');
        async function session() {
            if (token) return token;
            const response = await request(base + '/host/management-session', {method:'POST',
                headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1'},
                body:JSON.stringify({contract:control.contract,schema:'controller-browser-session'})});
            await document(response, 'controller-browser-session');
            const next = response.headers.get('X-WsprryPi-CSRF');
            if (!/^[0-9a-f]{64}$/.test(next || '')) throw new RecoveryError('browser_session_unavailable');
            token = next; return token;
        }
        function accept(value) {
            if (!control.validControllerDocument(value, 'configuration-recovery')) throw new RecoveryError('invalid_response');
            if (edited && current && (!same(value.binding,current.binding) || value.revision !== current.revision))
                throw new RecoveryError('revision_conflict');
            current = clone(value);
            if (!edited) draft = clone(value.draft);
            return clone(current);
        }
        function command(action) {
            if (!current?.writable || !current.binding) throw new RecoveryError('authority_pending');
            const value = {contract:control.contract,schema:action ? 'configuration-recovery-action' : 'configuration-recovery-save',
                binding:clone(current.binding),capability_revision:current.capability_revision,
                expected_revision:current.revision,operation_id:id()};
            if (action) value.action = action;
            else {
                value.configuration = clone(draft);
            }
            if (!control.validControllerDocument(value,value.schema)) throw new RecoveryError('candidate_invalid');
            if (new TextEncoder().encode(JSON.stringify(value)).byteLength > 32768) throw new RecoveryError('payload_too_large');
            return value;
        }
        async function dispatch() {
            if (!pending || sending) throw new RecoveryError('management_busy');
            if (pending.receipt?.state === 'committed') return clone(pending.receipt);
            sending = true;
            const captured = pending;
            try {
                const csrf = await session();
                const response = await request(path, {method:captured.method,
                    headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','X-WsprryPi-CSRF':csrf,
                        'If-Match':'"' + captured.request.expected_revision + '"'},body:captured.body});
                const receipt = await document(response,'target-operation');
                if (receipt.operation_id !== captured.request.operation_id || !same(receipt.binding,captured.request.binding))
                    throw new RecoveryError('invalid_response');
                captured.receipt = clone(receipt); captured.state = receipt.state;
                if (['rejected','refused'].includes(receipt.state)) pending = null;
                return clone(receipt);
            } catch (error) {
                // A documented refusal is evidence of no accepted mutation.
                // Malformed/lost replies retain the identical operation, never
                // silently resubmit a changed draft or invent a new operation.
                if (error instanceof RecoveryError && error.code === 'csrf_failed') token = '';
                // Browsers can retry an idempotent HTTP PUT below fetch(). A
                // later busy/auth/context refusal can therefore follow an
                // already committed copy even on our first visible response.
                // Only immutable request/candidate validation can prove this
                // first command was never accepted; retain every other result.
                const known = error instanceof RecoveryError &&
                    (error.code === 'candidate_invalid' && [400,422].includes(error.status) ||
                     ['invalid_shape','invalid_json'].includes(error.code) && error.status === 400 ||
                     error.code === 'payload_too_large' && error.status === 413);
                if (known && !captured.receipt && captured.state !== 'unconfirmed') { captured.state = 'refused'; pending = null; }
                else captured.state = 'unconfirmed';
                throw error instanceof RecoveryError ? error : new RecoveryError('request_unconfirmed');
            } finally { sending = false; }
        }
        return Object.freeze({
            get draft() { return draft && clone(draft); },
            get current() { return current && clone(current); },
            get dirty() { return edited; },
            get laterEdits() { return !!pending && revision !== pending.draftRevision; },
            get operation() { return pending && clone({request:pending.request,state:pending.state,receipt:pending.receipt || null}); },
            accept,
            async read() {
                const captured = revision;
                const value = await document(await request(path),'configuration-recovery');
                if (captured !== revision) throw new RecoveryError('draft_changed');
                return accept(value);
            },
            async targets() {return document(await request(base+'/targets'),'targets');},
            async readiness() {
                const value = await document(await request(base + '/host/readiness'),'host-readiness');
                if (current?.binding && (value.device_id !== current.binding.device_id || value.boot_id !== current.binding.boot_id))
                    throw new RecoveryError('boot_changed');
                return value;
            },
            async retirements(body=null,revision=null) {
                const method=body?'POST':'GET',headers={};
                if(body){
                    if(!control.validControllerDocument(body,'controller-retirement')||!/^"[0-9a-f]{64}"$/.test(revision||''))throw new RecoveryError('candidate_invalid');
                    headers['Content-Type']='application/json';headers['X-WsprryPico-Request']='1';
                    headers['X-WsprryPi-CSRF']=await session();headers['If-Match']=revision;
                }
                const response=await request(base+'/host/retirements',{method,headers,...(body?{body:JSON.stringify(body)}:{})});
                try{return {data:await document(response,body?'controller-retirement-operation':'controller-retirements'),etag:response.headers.get('ETag'),status:response.status};}
                catch(error){if(error.code==='csrf_failed')token='';throw error;}
            },
            // Current native-target resources only. Closed routes and families
            // share the same bounded parser and protected browser session.
            async targetRequest(device,resource,{method='GET',body=null} = {}) {
                if(!/^[0-9a-f]{32}$/.test(device)) throw new RecoveryError('target_mismatch');
                const read = {context:'target-context',settings:'target-settings',status:'target-status',migration:'target-migration-read'};
                const operation = /^operations\/[0-9a-f]{32}$/.test(resource);
                const review = /^operations\/[0-9a-f]{32}\/review$/.test(resource) && method === 'POST';
                const write = review || method === 'POST' && ['migration','migration/rollback','stop','disable','enable'].includes(resource) || method === 'PUT' && resource === 'settings';
                if(!(method === 'GET' && (own(read,resource) || operation)) && !write) throw new RecoveryError('unsupported_resource');
                const family = review ? 'target-operation-review' : write ? ({settings:'target-save',stop:'target-stop',disable:'target-action',enable:'target-enable',migration:'target-migration','migration/rollback':'target-migration-rollback'})[resource] : operation ? 'target-operation' : read[resource];
                const headers = {};
                if(write) {
                    if(!control.validControllerDocument(body,family) || body.binding.device_id !== device || review && resource !== 'operations/'+body.operation_id+'/review') throw new RecoveryError('candidate_invalid');
                    headers['Content-Type']='application/json';headers['X-WsprryPico-Request']='1';
                    token=''; // Read a fresh protected lease after automatic key/session retirement.
                    headers['X-WsprryPi-CSRF']=await session();if(!review)headers['If-Match']='"'+body.expected_revision+'"';
                }
                const response=await request(base+'/targets/'+device+'/'+resource,{method,headers,...(write?{body:JSON.stringify(body)}:{})});
                try {return {data:await document(response,review?'target-operation-reviewed':write?'target-operation':family),status:response.status,etag:response.headers.get('ETag'),readbackRevision:response.headers.get('X-WsprryPi-Readback-Revision')};}
                catch(error) {
                    if(error.code==='csrf_failed')token='';
                    const location=response.headers.get('Location'),start=base+'/targets/'+device+'/operations/';
                    if(error.code==='recovery_required'&&location?.startsWith(start)&&/^[0-9a-f]{32}$/.test(location.slice(start.length))) {
                        error.operation=location.slice(start.length);
                        const recovery=response.headers.get('X-WsprryPi-Recovery-Resource');
                        if(['settings','status','migration'].includes(recovery))error.resource=recovery;
                    }
                    throw error;
                }
            },
            set(section,key,value) {
                if (!draft || !own(draft,section) || !own(draft[section],key))
                    throw new RecoveryError('read_only_field');
                const changed = clone(current); changed.draft = clone(draft); changed.draft[section][key] = value;
                if (!control.validControllerDocument(changed,'configuration-recovery') ||
                    value && typeof value === 'object' && own(value,'state')) throw new RecoveryError('candidate_invalid');
                draft = clone(changed.draft); edited = !same(draft,current.draft); ++revision;
            },
            async save() {
                if (pending || sending) throw new RecoveryError('operation_pending');
                const value = command(); pending = {request:value,body:JSON.stringify(value),method:'PUT',state:'pending',draftRevision:revision};
                return dispatch();
            },
            async action(action) {
                if (pending || sending) throw new RecoveryError('operation_pending');
                if (!['repair','reset'].includes(action)) throw new RecoveryError('unsupported_resource');
                const value = command(action); pending = {request:value,body:JSON.stringify(value),method:'POST',state:'pending',draftRevision:revision};
                return dispatch();
            },
            retry:dispatch
        });
    }
    const api = Object.freeze({create,RecoveryError,readControllerDocument:document});
    root.ConfigurationRecoveryClient = api;
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window === 'undefined' ? globalThis : window);

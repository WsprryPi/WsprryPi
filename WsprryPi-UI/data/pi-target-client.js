// SPDX-License-Identifier: MIT
(function (root) {
    'use strict';
    const control=root.WsprryControl || (typeof require==='function'?require('./control-contract.js'):null);
    const transport=root.ConfigurationRecoveryClient || (typeof require==='function'?require('./configuration-recovery-client.js'):null);
    const clone=value=>structuredClone(value);
    const equal=(a,b)=>{
        if(a===b)return true;
        if(!a||!b||typeof a!=='object'||typeof b!=='object'||Array.isArray(a)!==Array.isArray(b))return false;
        const keys=Object.keys(a).sort(),other=Object.keys(b).sort();
        return keys.length===other.length&&keys.every((key,index)=>key===other[index]&&equal(a[key],b[key]));
    };
    const failure=code=>new transport.RecoveryError(code);
    const privateKeys=new Set(['TLS CA File','TLS Client Certificate','TLS Client Key','TLS Server Identity']);
    function create({device,local=false,fetch=root.fetch?.bind(root),crypto=root.crypto,base='/api/control/v1'}={}) {
        if(!/^[0-9a-f]{32}$/.test(device||''))throw failure('target_mismatch');
        const api=transport.create({fetch,crypto,base});
        let context=null,saved=null,draft=null,pending=null,suspended=null,stopRead=null,confirmed=null,evidence=null,reviewed=null,paused=true,busy=false,edit=0;
        const id=()=>Array.from(crypto.getRandomValues(new Uint8Array(16)),byte=>byte.toString(16).padStart(2,'0')).join('');
        const authorization=value=>value?{pair_id:value.pair_id,authorization_generation:value.authorization_generation}:null;
        const scope=value=>({binding:value.binding,authorization:authorization(value.authorization),capability_revision:value.capability_revision});
        const capability=resource=>context?.capabilities.find(item=>item.resource===resource);
        function checkContext(value) {
            if(!control.validControllerDocument(value,'target-context')||value.target.product!=='WsprryPi'||value.target.device_id!==device||
                value.binding.device_id!==device||value.gateway!==null||(local?value.authorization!==null:!value.authorization)||!value.capabilities.every(control.supportedPiResource))throw failure('target_context_unavailable');
        }
        function checkSettings(value,current) {
            if(!control.validControllerDocument(value,'target-settings')||!equal(value.binding,current.binding)||
                value.capability_revision!==current.capability_revision||value.revision!==current.configuration_revision)throw failure('connection_changed');
        }
        function intent(schema) {
            if(paused||!context||!saved)throw failure('connection_changed');
            return {contract:control.contract,schema,binding:clone(context.binding),capability_revision:context.capability_revision,
                expected_revision:saved.revision,operation_id:id()};
        }
        function receiptShape(value) {
            return control.validControllerDocument(value,'target-operation')&&
                (value.state==='committed'?!!value.revision&&value.error===null:value.revision===null);
        }
        function checkReceipt(value,operation) {
            if(!receiptShape(value)||value.operation_id!==operation.body.operation_id||
                !equal(value.binding,operation.body.binding)||value.state==='committed'&&(!value.revision||value.error!==null)||
                ['pending','unknown'].includes(value.state)&&value.revision!==null)throw failure('invalid_response');
            if(operation.receipt&&value.request_digest!==operation.receipt.request_digest)throw failure('invalid_response');
        }
        function releasePending(){pending=suspended;suspended=null;}
        async function dispatch(resource,method,body) {
            const independentStop=resource==='stop'&&pending&&pending.resource!=='stop'&&!suspended;
            if(busy||pending&&!independentStop)throw failure('operation_pending');
            if(!control.validControllerDocument(body,body.schema))throw failure('candidate_invalid');
            // Capture before I/O. Every uncertain result keeps this exact intent;
            // neither reconnect nor receipt lookup resends the mutation.
            const operation={resource,method,body:clone(body),edit,receipt:null,state:'pending',id:body.operation_id};
            if(independentStop)suspended=pending;
            pending=operation;busy=true;stopRead=null;
            try {
                const result=await api.targetRequest(device,resource,{method,body});checkReceipt(result.data,operation);
                operation.receipt=clone(result.data);operation.state=result.data.state;
                if(['committed','rejected'].includes(result.data.state)){confirmed=result.data.state==='committed'?clone(operation):null;releasePending();paused=true;}
                return clone(result.data);
            } catch(error) {
                // A closed explicit client-side/pre-dispatch refusal can release
                // this new attempt. Transport/server/shape failures cannot.
                // fetch may hide a PUT retry after an accepted reply was
                // lost. A later auth/context/busy refusal cannot prove that
                // the earlier copy did not commit. Only immutable input
                // refusal can release this exact unresolved operation.
                if(error.status>=400&&error.status<500&&['invalid_shape','invalid_json','candidate_invalid','payload_too_large'].includes(error.code))releasePending();
                else {operation.state='unknown';if(error.operation){operation.id=error.operation;operation.recovery=error.resource;operation.foreign=error.operation!==body.operation_id;}}
                paused=true;throw error;
            } finally {busy=false;}
        }
        return Object.freeze({
            get device(){return device;},get context(){return clone(context);},get current(){return clone(saved);},get draft(){return clone(draft);},
            get dirty(){return !!saved&&!equal(draft,saved.saved);},get paused(){return paused;},get busy(){return busy;},get operation(){return clone(pending);},get stopAvailable(){return !!stopRead&&Date.now()-stopRead.at<30000;},
            pause(){paused=true;stopRead=null;},
            async read() {
                if(busy)throw failure('management_busy');busy=true;paused=true;
                try {
                    let next,value;
                    for(let attempt=0;attempt<3;++attempt){
                        next=(await api.targetRequest(device,'context')).data;checkContext(next);
                        if(!next.capabilities.some(item=>item.resource==='pi-config'&&item.read))throw failure('unsupported_resource');
                        value=(await api.targetRequest(device,'settings')).data;
                        try{checkSettings(value,next);break;}
                        catch(error){if(error.code!=='connection_changed'||attempt===2)throw error;}
                    }
                    const sameIdentity=context&&equal(context.binding,next.binding)&&equal(authorization(context.authorization),authorization(next.authorization));
                    // An explicit enable/disable can legitimately change the
                    // capability digest. Its exact receipt and current binding
                    // prove readback; the new context grants future writes.
                    const accepted=confirmed&&confirmed.receipt?.revision===value.revision&&sameIdentity&&equal(confirmed.body.binding,next.binding) ||
                        reviewed&&equal(reviewed.binding,next.binding)&&reviewed.revision===value.revision&&equal(authorization(reviewed.authorization),authorization(next.authorization));
                    const metadataRefresh=sameIdentity&&saved&&(equal(value.saved,saved.saved)||equal(value.saved,draft));
                    if(context&&(!equal(scope(context),scope(next))||saved&&saved.revision!==value.revision)&&draft&&!equal(draft,saved.saved)&&!accepted&&!metadataRefresh)throw failure('revision_conflict');
                    if(!draft||!saved||equal(draft,saved.saved))draft=clone(value.saved);
                    if(accepted&&['enable','disable'].includes(confirmed?.resource)&&draft)draft.scheduling.enabled=value.saved.scheduling.enabled;
                    context=clone(next);saved=clone(value);confirmed=null;reviewed=null;paused=!!pending;return clone(saved);
                } finally {busy=false;}
            },
            set(section,key,value) {
                if(!draft||!Object.hasOwn(draft.configuration,section)||!Object.hasOwn(draft.configuration[section],key)||section==='WTP'&&privateKeys.has(key))throw failure('read_only_field');
                const next=clone(draft);next.configuration[section][key]=clone(value);
                const check={...intent('target-save'),candidate:next};
                if(!control.validControllerDocument(check,'target-save'))throw failure('candidate_invalid');
                draft=next;++edit;
            },
            setSolar(value) {
                const next=clone(draft);if(!next)throw failure('connection_changed');
                next.scheduling.solar=clone(value);
                if(!control.validControllerDocument({...intent('target-save'),candidate:next},'target-save'))throw failure('candidate_invalid');
                draft=next;++edit;
            },
            enable() {
                if(!capability('pi-scheduling')?.write||!saved||!equal(draft,saved.saved)||saved.saved.scheduling.enabled)throw failure('unsaved_schedule');
                return dispatch('enable','POST',{...intent('target-enable'),candidate_digest:saved.candidate_digest});
            },
            disable() {
                if(!capability('pi-scheduling')?.write)throw failure('unsupported_resource');
                return dispatch('disable','POST',intent('target-action'));
            },
            save() {
                if(!capability('pi-config')?.write)throw failure('unsupported_resource');
                return dispatch('settings','PUT',{...intent('target-save'),candidate:clone(draft)});
            },
            async status() {
                const value=(await api.targetRequest(device,'status')).data;
                if(!context||!equal(value.binding,context.binding)) {paused=true;throw failure('connection_changed');}
                if(value.revision===saved?.revision&&capability('stop')?.write)stopRead={context:clone(context),status:clone(value),at:Date.now()};
                return value;
            },
            async readStop() {
                if(busy)throw failure('management_busy');busy=true;stopRead=null;
                try{
                    const next=(await api.targetRequest(device,'context')).data;checkContext(next);
                    const value=(await api.targetRequest(device,'status')).data;
                    if(!control.validControllerDocument(value,'target-status')||!equal(value.binding,next.binding)||value.revision!==next.configuration_revision)throw failure('output_changed');
                    if(!next.capabilities.some(item=>item.resource==='stop'&&item.write))throw failure('unsupported_resource');
                    stopRead={context:clone(next),status:clone(value),at:Date.now()};return clone(value);
                }finally{busy=false;}
            },
            stop(observed) {
                if(!stopRead||Date.now()-stopRead.at>=30000||!equal(observed,stopRead.status))throw failure('output_changed');
                const current=stopRead.context;
                return dispatch('stop','POST',{contract:control.contract,schema:'target-stop',binding:clone(current.binding),
                    capability_revision:current.capability_revision,expected_revision:observed.revision,operation_id:id(),observed_output:clone(observed.observed_output)});
            },
            async readRecovery() {
                if(!pending)throw failure('operation_unknown');if(busy)throw failure('management_busy');busy=true;evidence=null;
                try {
                    const next=(await api.targetRequest(device,'context')).data;checkContext(next);
                    if(context&&!equal(authorization(context.authorization),authorization(next.authorization)))throw failure('connection_changed');
                    const resource=pending.recovery||(pending.resource==='stop'?'status':'settings');
                    if(!['settings','status'].includes(resource))throw failure('unsupported_resource');
                    const result=await api.targetRequest(device,resource);
                    if(!equal(result.data.binding,next.binding)||!(/^[0-9a-f]{64}$/.test(result.readbackRevision||'')))throw failure('invalid_response');
                    if(resource==='settings')checkSettings(result.data,next);
                    evidence={context:next,resource,token:result.readbackRevision,data:clone(result.data),at:Date.now(),operation:pending.id};
                    return clone(result.data);
                } finally {busy=false;}
            },
            async review() {
                if(!pending||!evidence||evidence.operation!==pending.id||Date.now()-evidence.at>=30000)throw failure('recovery_required');
                if(busy)throw failure('management_busy');busy=true;
                const captured=evidence;evidence=null;
                try {
                    if(pending.foreign&&pending.state==='prior_terminal'){
                        // The earlier receipt is already terminal, so no unknown
                        // write barrier remains to retire. This explicit review
                        // only adopts a read baseline for the unsent draft; its
                        // next Save still requires the exact current revision.
                        reviewed={binding:clone(captured.context.binding),revision:captured.data.revision,authorization:clone(captured.context.authorization)};
                        const receipt=clone(pending.receipt);releasePending();paused=true;
                        return {state:'earlier_terminal',receipt};
                    }
                    const body={contract:control.contract,schema:'target-operation-review',binding:clone(captured.context.binding),
                        operation_id:captured.operation,readback_revision:captured.token};
                    const value=(await api.targetRequest(device,'operations/'+captured.operation+'/review',{method:'POST',body})).data;
                    if(value.operation_id!==captured.operation||value.readback_revision!==captured.token||
                        ['device_id','boot_id','profile_generation'].some(key=>value.binding[key]!==body.binding[key])||
                        value.binding.connection_generation===body.binding.connection_generation)throw failure('invalid_response');
                    reviewed={binding:clone(value.binding),revision:captured.data.revision,authorization:clone(captured.context.authorization)};
                    releasePending();paused=true;return clone(value);
                } finally {busy=false;}
            },
            async check() {
                if(!pending)throw failure('operation_unknown');if(busy)throw failure('management_busy');
                const captured=pending;busy=true;
                try {
                    const value=(await api.targetRequest(device,'operations/'+captured.id)).data;
                    if(captured.foreign){if(!receiptShape(value)||value.operation_id!==captured.id||value.binding.device_id!==device)throw failure('invalid_response');captured.receipt=clone(value);captured.state=['committed','rejected'].includes(value.state)?'prior_terminal':value.state;return clone(value);}
                    checkReceipt(value,captured);
                    captured.receipt=clone(value);captured.state=value.state;
                    if(['committed','rejected'].includes(value.state)){confirmed=value.state==='committed'?clone(captured):null;releasePending();paused=true;}
                    return clone(value);
                } finally {busy=false;}
            }
        });
    }
    const api=Object.freeze({create});root.PiTargetClient=api;if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof window==='undefined'?globalThis:window);

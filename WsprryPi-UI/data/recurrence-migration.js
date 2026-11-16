// SPDX-License-Identifier: MIT
(function(root) {
    'use strict';
    const node=id=>root.document.getElementById('recurrenceMigration'+id);
    if(!node('')) return;
    const control=root.WsprryControl,client=root.ConfigurationRecoveryClient.create({base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'});
    const clone=value=>JSON.parse(JSON.stringify(value));
    const bound=(left,right)=>['device_id','boot_id','profile_generation','connection_generation'].every(key=>left[key]===right[key]);
    let current=null,review=null,operation=null,busy=false,timer=null;
    const feedback=text=>{node('Feedback').textContent=text;};
    const identity=()=>Array.from(root.crypto.getRandomValues(new Uint8Array(16)),byte=>byte.toString(16).padStart(2,'0')).join('');
    const canReconcile=()=>!!operation?.confirmed_pending&&current?.migration.state==='retiring'&&
        current.migration.jobs.some(job=>job.boot_id!==current.context.binding.boot_id);
    function controls() {
        const available=!!current&&!busy&&(!operation||canReconcile())&&!root.ConfigurationRecovery?.active;
        const state=current?.migration.state,cap=current?.context.capabilities.find(item=>item.resource==='migration');
        node('Review').disabled=!available||!cap?.write||!['legacy','rollback','retiring'].includes(state);
        node('Review').textContent=state==='retiring'?'Review reconciliation':'Review migration';
        node('Rollback').hidden=state!=='staged'||current?.migration.controllers.length!==1;
        node('Rollback').disabled=!available||!cap?.write;
        node('Refresh').disabled=busy;node('Retry').hidden=!operation;node('Retry').disabled=busy;
        node('Confirm').disabled=!available||!review||!node('Consent').checked;
        node('Cancel').disabled=busy;node('').setAttribute('aria-busy',String(busy));
    }
    function render() {
        if(!current) return;
        node('').hidden=false;
        node('Device').textContent=current.context.target.name+' · '+current.context.binding.device_id;
        const messages={legacy:'This device runs its existing schedule. Migration retires that schedule and saves the device-owned schedule disabled.',
            rollback:'The previous schedule is permitted to run under its saved transmission settings.',
            retiring:'Current accepted work is being reconciled. New recurring work is blocked.',
            staged:'The device-owned schedule is saved and disabled. Schedule enablement is unavailable in this version.',
            unclassified:'Existing scheduling history cannot be verified. Migration is unavailable until prior authority and outstanding work are resolved.',
            held:'Scheduling history or outstanding work requires recovery. Migration remains blocked.'};
        node('State').textContent=messages[current.migration.state]||'Scheduling authority requires review before any transition.';
        if(current.migration.state==='retiring'&&current.migration.jobs.some(job=>job.boot_id!==current.context.binding.boot_id))
            node('State').textContent='The device restarted during handover. Review current output and confirm reconciliation before completing migration.';
        controls();
    }
    async function load(attempt=0) {
        try{return await loadOnce(attempt);}
        catch(error){
            // Startup and another read can briefly own the local observation
            // lock. Retry bounded read-only snapshots, never a mutation.
            if(attempt<3&&['capacity','rate_limited'].includes(error.code)){
                await new Promise(resolve=>root.setTimeout(resolve,400*(attempt+1)));
                return load(attempt+1);
            }
            throw error;
        }
    }
    async function loadOnce(attempt) {
        const readiness=await client.readiness();
        if(readiness.role!=='transmitter'||readiness.state!=='ready') {
            node('').hidden=true;current=null;return;
        }
        const device=readiness.device_id;
        const context=(await client.targetRequest(device,'context')).data;
        if(context.binding.device_id!==device||context.target.product!=='WsprryPi'||!context.capabilities.every(control.supportedPiResource)) throw new Error('invalid_context');
        const settings=await client.targetRequest(device,'settings'),migration=await client.targetRequest(device,'migration');
        if(!bound(context.binding,settings.data.binding)||!bound(context.binding,migration.data.binding)||
            context.configuration_revision!==settings.data.revision||settings.etag!=='"'+settings.data.revision+'"'||migration.etag!==settings.etag||
            settings.data.capability_revision!==context.capability_revision) {
            if(attempt<2)return load(attempt+1);
            throw new Error('context_changed');
        }
        if(migration.data.state==='staged'&&(settings.data.saved.scheduling.enabled!==false||migration.data.staged_revision!==settings.data.revision)) throw new Error('invalid_readback');
        current={context,settings:settings.data,migration:migration.data};render();
        if(operation) return;
        // A fresh browser discovers the original accepted intent from target
        // history. It may look up that receipt; it cannot fabricate a retry body.
        if(current.migration.state==='retiring'&&current.migration.transition_id)
            operation={device,id:current.migration.transition_id,resource:'migration',body:null,confirmed_pending:true};
        controls();
    }
    function checkReceipt(value,captured) {
        if(value.operation_id!==captured.id||value.binding.device_id!==captured.device||
            captured.body&&!bound(value.binding,captured.body.binding)) throw new Error('receipt_mismatch');
        if(value.state==='committed') {
            operation=null;review=null;node('Confirmation').hidden=true;
            feedback('The saved result is confirmed. Reviewing the current device state.');
        }else {captured.confirmed_pending=value.state==='pending';feedback('The transition is recorded and is waiting for verified reconciliation. No new recurring work will start.');}
    }
    function poll() {
        root.clearTimeout(timer);
        if(operation) timer=root.setTimeout(()=>{void check(false);},2000);
    }
    async function check(retry) {
        if(busy||!operation) return;
        busy=true;controls();const captured=operation;
        try {
            let result;
            try {result=await client.targetRequest(captured.device,'operations/'+captured.id);}
            catch(error) {
                if(!retry||!captured.body||error.code!=='operation_unknown') throw error;
                result=await client.targetRequest(captured.device,captured.resource,{method:'POST',body:captured.body});
            }
            checkReceipt(result.data,captured);await load();
        }catch(_) {current=null;feedback(operation?'The result is unconfirmed. Use Check saved result; no new action has been submitted.':'The saved result is confirmed, but current state could not be verified. Refresh status.');}
        finally {busy=false;controls();poll();}
    }
    async function refresh() {
        if(busy) return;busy=true;controls();
        try {await load();}
        catch(error) {root.console.error('Migration readback:',error.code||error.message);current=null;node('').hidden=false;feedback(error.message==='invalid_context'?'The device’s scheduling interface could not be verified. Refresh before reviewing a transition.':error.message==='context_changed'?'Saved state changed while reading. Refresh before reviewing a transition.':'Saved scheduling history is unavailable. Refresh before reviewing a transition.');}
        finally {busy=false;controls();poll();}
    }
    function open(rollback) {
        if(busy||(operation&&!canReconcile())||!current) return;
        review={...clone(current),rollback};node('Consent').checked=false;
        node('Summary').textContent=current.context.target.name+' · '+current.context.binding.device_id+' · '+current.migration.controllers.length+' recorded prior scheduling authority.';
        node('ConsentLabel').textContent=rollback?'Restore the selected previous schedule. It may transmit under its saved settings.':
            'Retire this existing schedule, reconcile accepted work, and save the device-owned schedule disabled.';
        node('Confirm').textContent=rollback?'Restore previous schedule':current.migration.state==='retiring'?'Confirm reconciliation':'Confirm migration';
        node('Confirmation').hidden=false;controls();node('Consent').focus();
    }
    async function submit() {
        if(!review||!node('Consent').checked||busy||(operation&&!canReconcile())) return;
        const captured=review,device=captured.context.binding.device_id,id=identity();
        const body={contract:control.contract,schema:'target-migration',binding:clone(captured.context.binding),
            capability_revision:captured.context.capability_revision,operation_id:id,expected_revision:captured.settings.revision,
            expected_authority_generation:captured.migration.authority_generation};
        if(captured.rollback) {
            const row=captured.migration.controllers[0];body.resume_controller_id=row.controller_id;body.resume_assignment_id=row.assignment_id;
        }else body.controllers=clone(captured.migration.controllers);
        const resource=captured.rollback?'migration/rollback':'migration';
        operation={device,id,resource,body,prior:operation};busy=true;controls();feedback('Submitting the reviewed transition.');
        try {
            const result=await client.targetRequest(device,resource,{method:'POST',body});checkReceipt(result.data,operation);await load();
        }catch(_) {current=null;feedback(operation?'The result is unconfirmed. Your reviewed request is retained; use Check saved result before another action.':'The saved result is confirmed, but current state could not be verified. Refresh status.');}
        finally {busy=false;controls();poll();}
    }
    node('Review').addEventListener('click',()=>open(false));node('Rollback').addEventListener('click',()=>open(true));
    node('Consent').addEventListener('change',controls);node('Confirm').addEventListener('click',()=>{void submit();});
    node('Cancel').addEventListener('click',()=>{review=null;node('Confirmation').hidden=true;controls();node('Review').focus();});
    node('Refresh').addEventListener('click',()=>{void refresh();});node('Retry').addEventListener('click',()=>{void check(true);});
    root.addEventListener('pagehide',()=>root.clearTimeout(timer));void refresh();
})(window);

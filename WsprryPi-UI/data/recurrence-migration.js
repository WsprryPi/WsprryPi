// SPDX-License-Identifier: MIT
(function(root) {
    'use strict';
    const node=id=>root.document.getElementById('recurrenceMigration'+id);
    if(!node(''))return;
    const control=root.WsprryControl,base=root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1';
    const client=root.ConfigurationRecoveryClient.create({base});
    const clone=value=>structuredClone(value);
    const bound=(a,b)=>['device_id','boot_id','profile_generation','connection_generation'].every(key=>a[key]===b[key]);
    const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
    const states=new Map();let selected='',current=null,review=null,operation=null,busy=false,timer=null,evidence=null,available=new Set();
    const feedback=text=>{node('Feedback').textContent=text;};
    const identity=()=>Array.from(root.crypto.getRandomValues(new Uint8Array(16)),byte=>byte.toString(16).padStart(2,'0')).join('');
    const canReconcile=()=>!!operation?.confirmed_pending&&current?.migration.state==='retiring'&&current.migration.jobs.some(job=>job.boot_id!==current.context.binding.boot_id);
    const remoteOutput=()=>current?.settings.saved.configuration.Operation['Transmit Backend']==='wtp';
    const selectedAssignment=()=>current?.migration.controllers.find(row=>row.controller_id+'/'+row.assignment_id===node('Assignment').value);
    const allowedAssignment=row=>!!row&&row.controller_id===selected;
    function controls(){
        const ready=!!current&&available.has(selected)&&!busy&&(!operation||canReconcile())&&!root.ConfigurationRecovery?.active;
        const state=current?.migration.state,cap=current?.context.capabilities.find(item=>item.resource==='migration');
        node('Target').disabled=busy;node('Assignment').disabled=!ready;
        const canMigrate=['legacy','rollback','retiring'].includes(state)||state==='staged'&&current.migration.controllers.some(row=>row.controller_id!==selected);
        node('Review').disabled=!ready||!cap?.write||!canMigrate||remoteOutput();
        node('Review').textContent=state==='retiring'?'Review reconciliation':'Review migration';
        const restorable=['staged','native'].includes(state)&&!!current?.migration.controllers.length;
        node('Previous').hidden=!restorable;node('Rollback').hidden=!restorable;
        node('Rollback').disabled=!ready||!cap?.write||!allowedAssignment(selectedAssignment());
        node('Refresh').disabled=busy;node('Retry').hidden=!operation;node('Retry').disabled=busy;
        const canReview=!!operation&&!!current&&!!current.context.authorization&&!['retiring','reconciling'].includes(state);
        node('OutcomeReview').hidden=!canReview;node('ReadOutcome').disabled=!canReview||busy;
        node('ConfirmOutcome').disabled=!canReview||busy||!evidence;
        node('Confirm').disabled=!ready||!review||!node('Consent').checked;
        node('Cancel').disabled=busy;node('').setAttribute('aria-busy',String(busy));
    }
    function render(){
        if(!current){node('Device').textContent=selected?selected+' · status unavailable':'Choose a device';node('State').textContent='Current scheduling authority is unknown. Refresh status before a transition.';controls();return;}
        node('').hidden=false;
        node('Device').textContent=current.context.target.name+' · '+selected;
        const messages={legacy:'This device retains its existing schedule. Migration retires recorded prior schedules and saves the device-owned schedule disabled.',
            rollback:'The selected previous controller may run its saved schedule. Local device-owned recurrence remains disabled.',
            retiring:'Prior controllers and accepted work are being reconciled. New recurring work is blocked until verified retirement.',
            reconciling:'Restoration is unconfirmed. This device’s recurring schedules remain blocked while the previous controller confirms its saved result.',
            staged:'The device-owned schedule is saved and disabled.',native:'This device owns its recurring schedule.',
            unclassified:'Previous scheduling history cannot be verified. Migration remains unavailable.',held:'Scheduling history or outstanding work requires recovery.'};
        node('State').textContent=messages[current.migration.state]||'Scheduling authority requires review before a transition.';
        if(remoteOutput())node('State').textContent='This Pi sends transmissions to another device. Retire its remote schedule and reconcile accepted work with WTP-capable software. Then configure the transmitting device’s own schedule; migration cannot turn this remote output into a local schedule.';
        if(canReconcile()&&!remoteOutput())node('State').textContent='This device restarted during handover. Review current output and confirm reconciliation before completing migration.';
        const prior=states.get(selected)?.assignment||node('Assignment').value;node('Assignment').replaceChildren();
        const empty=root.document.createElement('option');empty.value='';empty.textContent='Choose a previous schedule';node('Assignment').append(empty);
        for(const row of current.migration.controllers){
            const option=root.document.createElement('option');option.value=row.controller_id+'/'+row.assignment_id;
            option.textContent=(row.controller_id===selected?'Selected Pi':('Controller '+row.controller_id.slice(0,8)))+' · schedule '+row.assignment_id.slice(0,8);
            option.disabled=!allowedAssignment(row);node('Assignment').append(option);
        }
        if(prior&&[...node('Assignment').options].some(option=>option.value===prior&&!option.disabled))node('Assignment').value=prior;
        else if(current.migration.controllers.length===1&&allowedAssignment(current.migration.controllers[0]))node('Assignment').value=current.migration.controllers[0].controller_id+'/'+current.migration.controllers[0].assignment_id;
        controls();
    }
    function remember(){if(selected)states.set(selected,{current,review,operation,assignment:node('Assignment').value});}
    async function load(attempt=0){
        const device=selected;
        try{
            const readiness=await client.readiness(),targets=await client.targets();
            const pis=targets.targets.filter(target=>target.product==='WsprryPi');available=new Set(pis.map(target=>target.device_id));
            if(!selected){selected=available.has(readiness.device_id)?readiness.device_id:pis[0]?.device_id||'';}
            node('Target').replaceChildren();
            for(const target of pis){const option=root.document.createElement('option');option.value=target.device_id;option.textContent=target.name+' · '+target.device_id;option.disabled=states.size>=64&&!states.has(target.device_id);node('Target').append(option);}
            if(selected&&!available.has(selected)){const option=root.document.createElement('option');option.value=selected;option.textContent=(states.get(selected)?.current?.context.target.name||selected)+' · unavailable';node('Target').append(option);}
            node('Target').value=selected;
            if(!selected){current=null;node('').hidden=true;return;}
            if(device&&selected!==device)throw Error('context_changed');
            if(!available.has(selected))throw Error('target_unavailable');
            const context=(await client.targetRequest(selected,'context')).data;
            if(context.binding.device_id!==selected||context.target.product!=='WsprryPi'||!context.capabilities.every(control.supportedPiResource))throw Error('invalid_context');
            const settings=await client.targetRequest(selected,'settings'),migration=await client.targetRequest(selected,'migration');
            if(!bound(context.binding,settings.data.binding)||!bound(context.binding,migration.data.binding)||context.configuration_revision!==settings.data.revision||
                settings.etag!=='"'+settings.data.revision+'"'||migration.etag!==settings.etag||settings.data.capability_revision!==context.capability_revision)throw Error('context_changed');
            if(migration.data.state==='staged'&&(settings.data.saved.scheduling.enabled!==false||migration.data.staged_revision!==settings.data.revision))throw Error('invalid_readback');
            if(evidence&&(!bound(evidence.context.binding,context.binding)||evidence.context.configuration_revision!==context.configuration_revision)){
                evidence=null;node('OutcomeSummary').textContent='Current state changed. Read it again before confirming.';
            }
            current={context,settings:settings.data,migration:migration.data};
            if(review&&(!bound(review.context.binding,context.binding)||review.settings.revision!==settings.data.revision)){review=null;node('Confirmation').hidden=true;}
            if(!operation&&['retiring','reconciling'].includes(migration.data.state)&&migration.data.transition_id)
                operation={device:selected,id:migration.data.transition_id,resource:migration.data.state==='retiring'?'migration':'migration/rollback',body:null,confirmed_pending:true};
            render();remember();
            if(node('Feedback').textContent==='Reading the selected device. Selection does not change output.')feedback('Current status loaded.');
        }catch(error){
            if(attempt<3&&['capacity','rate_limited','context_changed'].includes(error.code||error.message)){
                await new Promise(resolve=>root.setTimeout(resolve,400*(attempt+1)));return load(attempt+1);
            }
            throw error;
        }
    }
    function checkReceipt(value,captured){
        if(value.operation_id!==captured.id||value.binding.device_id!==captured.device||captured.body&&!bound(value.binding,captured.body.binding)||
            captured.receipt&&value.request_digest!==captured.receipt.request_digest)throw Error('receipt_mismatch');
        captured.receipt=clone(value);
        if(['committed','rejected'].includes(value.state)){
            operation=null;review=null;node('Confirmation').hidden=true;
            feedback(captured.foreign?'The earlier request is resolved. Review this transition again; it has not been submitted.':value.state==='rejected'?'The transition was refused. Review the current transmitter state.':
                captured.resource==='migration/rollback'?'Restoration is confirmed. The previous schedule may run under its saved settings.':
                captured.resource==='stop'||captured.resource==='disable'?'Preparation is confirmed. Review the current state before confirming restoration again.':'Migration is confirmed. Reviewing current state.');
        }else{captured.confirmed_pending=value.state==='pending';feedback('The transition is recorded and waiting for verified reconciliation. Its result remains unconfirmed.');}
        remember();
    }
    function poll(){root.clearTimeout(timer);if(operation)timer=root.setTimeout(()=>{void check();},2000);}
    async function check(){
        if(busy||!operation)return;busy=true;controls();const captured=operation;
        try{const result=await client.targetRequest(captured.device,'operations/'+captured.id);checkReceipt(result.data,captured);await load();}
        catch(_){current=null;feedback('The result is unconfirmed. Check saved result or refresh status; no request has been repeated.');}
        finally{busy=false;controls();poll();}
    }
    async function refresh(){
        if(busy)return;busy=true;controls();
        try{await load();}
        catch(_){current=null;node('').hidden=false;render();feedback('Saved scheduling history or this management connection is unavailable. Refresh status before a transition.');}
        finally{busy=false;controls();poll();}
    }
    async function readOutcome(){
        if(busy||!operation||!current||['retiring','reconciling'].includes(current.migration.state))return;
        busy=true;evidence=null;controls();
        try{
            const resource=operation.recovery||(operation.resource==='stop'?'status':['migration','migration/rollback'].includes(operation.resource)?'migration':'settings');
            if(!['status','migration','settings'].includes(resource))throw Error('unsupported_resource');
            const context=(await client.targetRequest(selected,'context')).data,result=await client.targetRequest(selected,resource);
            if(!bound(context.binding,result.data.binding)||!/^[0-9a-f]{64}$/.test(result.readbackRevision||''))throw Error('invalid_readback');
            evidence={context,resource,data:result.data,token:result.readbackRevision,operation:operation.id,at:Date.now()};
            const text=resource==='migration'?'Scheduling ownership: '+result.data.state+'. Recorded prior schedules: '+result.data.controllers.length+'.':
                resource==='status'?'Observed output: '+result.data.phase+'. Effective Transmit: '+(result.data.transmit_effective?'On':'Off')+'.':
                'Saved Transmit: '+(result.data.saved.configuration.Operation.Transmit?'On':'Off')+'. Device-owned recurrence: '+(result.data.saved.scheduling.enabled?'Enabled':'Disabled')+'.';
            node('OutcomeSummary').textContent=text+' The earlier result remains unknown.';
        }catch(_){feedback('Current state could not be verified for review. The earlier request remains retained.');}
        finally{busy=false;controls();}
    }
    async function confirmOutcome(){
        if(busy||!evidence||!operation)return;
        if(evidence.operation!==operation.id||Date.now()-evidence.at>=30000){evidence=null;feedback('Read current state again before confirming.');controls();return;}
        busy=true;controls();const captured=evidence;evidence=null;
        try{
            const body={contract:control.contract,schema:'target-operation-review',binding:clone(captured.context.binding),operation_id:captured.operation,readback_revision:captured.token};
            const result=await client.targetRequest(selected,'operations/'+captured.operation+'/review',{method:'POST',body});
            if(result.data.operation_id!==captured.operation||result.data.readback_revision!==captured.token||
                ['device_id','boot_id','profile_generation'].some(key=>result.data.binding[key]!==body.binding[key])||
                result.data.binding.connection_generation===body.binding.connection_generation)throw Error('invalid_readback');
            operation=null;review=null;node('OutcomeSummary').textContent='';await load();
            feedback('Current state was reviewed. The earlier result remains unknown; no request was repeated. Review a new transition separately.');
        }catch(_){feedback('Review is not confirmed. The earlier request remains retained. Refresh before trying another review.');}
        finally{busy=false;remember();controls();poll();}
    }
    function open(rollback){
        if(busy||(operation&&!canReconcile())||!current)return;
        const assignment=rollback?selectedAssignment():null;if(rollback&&!allowedAssignment(assignment))return;
        review={...clone(current),rollback,assignment:clone(assignment)};node('Consent').checked=false;
        node('Summary').textContent=current.context.target.name+' · '+selected+' · '+current.migration.controllers.length+' recorded prior scheduling authorities.';
        node('ConsentLabel').textContent=rollback?'Turn off output, disable device-owned recurrence, and restore the selected previous schedule. That schedule may transmit under its saved settings.':
            'Retire recorded prior schedules, reconcile accepted work, and save the device-owned schedule disabled.';
        node('Confirm').textContent=rollback?'Restore previous schedule':current.migration.state==='retiring'?'Confirm reconciliation':'Confirm migration';
        node('Confirmation').hidden=false;controls();node('Consent').focus();remember();
    }
    function body(schema,snapshot){return {contract:control.contract,schema,binding:clone(snapshot.context.binding),capability_revision:snapshot.context.capability_revision,
        operation_id:identity(),expected_revision:snapshot.settings.revision};}
    async function send(resource,intent){
        operation={device:selected,id:intent.operation_id,resource,body:clone(intent)};remember();
        let result;
        try{result=await client.targetRequest(selected,resource,{method:'POST',body:intent});}
        catch(error){
            if(error.operation&&error.operation!==operation.id)operation={device:selected,id:error.operation,resource,body:null,foreign:true,recovery:error.resource};
            else if(error.status>=400&&error.status<500&&['invalid_shape','invalid_json','candidate_invalid','payload_too_large'].includes(error.code))operation=null;
            remember();throw error;
        }
        checkReceipt(result.data,operation);
        if(result.data.state!=='committed')throw Error('transition_pending');
        await load();return result.data;
    }
    async function submit(){
        if(!review||!node('Consent').checked||busy||(operation&&!canReconcile()))return;
        const captured=clone(review);busy=true;controls();feedback('Submitting the reviewed transition.');
        try{
            if(captured.rollback){
                // Every step has an independent immutable operation/receipt.
                // An unknown step stops the workflow; receipt checks never
                // resend it or silently create its next mutation.
                const status=(await client.targetRequest(selected,'status')).data;
                if(!bound(status.binding,captured.context.binding)||status.revision!==captured.settings.revision)throw Error('context_changed');
                await send('stop',{...body('target-stop',captured),observed_output:clone(status.observed_output)});
                const afterStop=clone(captured.settings.saved);afterStop.configuration.Operation.Transmit=false;
                if(!current||!same(current.settings.saved,afterStop))throw Error('context_changed');
                const disabled=body('target-action',current);await send('disable',disabled);
                const afterDisable=clone(afterStop);afterDisable.scheduling.enabled=false;
                if(!current||!same(current.settings.saved,afterDisable)||!bound(current.context.binding,captured.context.binding))throw Error('context_changed');
                const intent={...body('target-migration',current),expected_authority_generation:current.migration.authority_generation,
                    resume_controller_id:captured.assignment.controller_id,resume_assignment_id:captured.assignment.assignment_id};
                await send('migration/rollback',intent);
            }else{
                const intent={...body('target-migration',captured),expected_authority_generation:captured.migration.authority_generation,controllers:clone(captured.migration.controllers)};
                await send('migration',intent);
            }
        }catch(_){feedback(operation?'The result is unconfirmed. The reviewed request is retained; check saved result before another action.':'Current state changed or preparation was refused. Refresh and review before another transition.');}
        finally{busy=false;review=null;node('Confirmation').hidden=true;remember();controls();poll();}
    }
    node('Target').addEventListener('change',()=>{
        if(busy){node('Target').value=selected;return;}
        const next=node('Target').value;if(states.size>=64&&!states.has(next)){node('Target').value=selected;feedback('This page retains the maximum number of devices. Resolve its pending results before opening another device.');return;}
        remember();root.clearTimeout(timer);evidence=null;node('OutcomeSummary').textContent='';selected=next;const saved=states.get(selected);
        current=null;review=null;operation=saved?.operation||null;node('Assignment').value=saved?.assignment||'';node('Confirmation').hidden=true;node('Consent').checked=false;
        render();feedback('Reading the selected device. Selection does not change output.');void refresh();
    });
    node('Assignment').addEventListener('change',()=>{review=null;node('Confirmation').hidden=true;remember();controls();});
    node('ReadOutcome').addEventListener('click',()=>{void readOutcome();});node('ConfirmOutcome').addEventListener('click',()=>{void confirmOutcome();});
    node('Review').addEventListener('click',()=>open(false));node('Rollback').addEventListener('click',()=>open(true));
    node('Consent').addEventListener('change',controls);node('Confirm').addEventListener('click',()=>{void submit();});
    node('Cancel').addEventListener('click',()=>{review=null;node('Confirmation').hidden=true;controls();node('Review').focus();});
    node('Refresh').addEventListener('click',()=>{void refresh();});node('Retry').addEventListener('click',()=>{void check();});
    root.addEventListener('pagehide',()=>root.clearTimeout(timer));void refresh();
})(window);

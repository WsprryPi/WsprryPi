// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict');
const crypto=require('node:crypto').webcrypto;
const control=require('../data/control-contract.js');
const {create}=require('../data/pi-target-client.js');
const vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=name=>structuredClone(vectors.find(item=>item.name===name).value);
const response=(value,status=200,headers={})=>new Response(JSON.stringify(value),{status,headers});
async function main(){
    let context=sample('paired Pi context'),settings=sample('Pi closed target-settings');
    settings.binding=structuredClone(context.binding);settings.capability_revision=context.capability_revision;settings.revision=context.configuration_revision;
    context.binding.connection_generation='1';context.target.connection_generation='1';context.capabilities[0].write=true;context.capabilities[0].activation='next_occurrence';Object.assign(context.capabilities[0],control.controllerPiDescriptors['pi-config']);
    settings.binding=structuredClone(context.binding);
    const device=context.binding.device_id,calls=[];let mode='normal',last=null,receipt=null,refusal='unauthorized';
    const fetch=async(url,options)=>{
        calls.push({url,options});
        assert.equal(options.redirect,'error');assert.equal(options.credentials,'same-origin');
        if(url.endsWith('/management-session'))return response({contract:control.contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'e'.repeat(64)});
        if(url.endsWith('/context'))return response(context);
        if(url.endsWith('/review')&&options.method==='POST'){const body=JSON.parse(options.body);context.binding={...context.binding,connection_generation:String(BigInt(context.binding.connection_generation)+1n)};context.target.connection_generation=context.binding.connection_generation;settings.binding=structuredClone(context.binding);return response({...body,schema:'target-operation-reviewed',binding:context.binding});}
        if(url.includes('/operations/'))return response(mode==='wrong-receipt'?{...receipt,operation_id:'f'.repeat(32)}:receipt);
        if(url.endsWith('/status')){const status=sample('Pi closed conservative status');status.binding=structuredClone(context.binding);status.revision=settings.revision;return response(status);}
        if(options.method==='GET')return response(settings,200,{'X-WsprryPi-Readback-Revision':'9'.repeat(64)});
        last=JSON.parse(options.body);assert.equal(last.binding.device_id,device);
        if(mode==='earlier')return response({contract:control.contract,schema:'target-error',error:{code:'recovery_required',retryable:false}},503,{Location:'/api/control/v1/targets/'+device+'/operations/'+receipt.operation_id,'X-WsprryPi-Recovery-Resource':'settings'});
        assert.equal(options.headers['If-Match'],'"'+last.expected_revision+'"');
        receipt={contract:control.contract,schema:'target-operation',binding:last.binding,operation_id:last.operation_id,
            request_digest:'1'.repeat(64),state:'committed',revision:'2'.repeat(64),error:null};
        if(mode==='lost')throw Error('lost reply');
        if(mode==='retried-refusal')return response({contract:control.contract,schema:'target-error',error:{code:refusal,retryable:false}},409);
        return response(receipt);
    };
    const client=create({device,fetch,crypto});await client.read();assert(!client.paused);
    const originalSchedule=client.draft.scheduling;
    client.set('CW','Message','REMOTE PI DRAFT');assert(client.dirty);
    assert.throws(()=>client.set('WTP','TLS Client Key','manual.key'),{code:'read_only_field'});
    assert.throws(()=>client.set('__proto__','Message','bad'),{code:'read_only_field'});
    assert.throws(()=>client.set('Operation','Mode','UNKNOWN'),{code:'candidate_invalid'});
    context.authorization.renewal_required=true;await client.read();assert.equal(client.draft.configuration.CW.Message,'REMOTE PI DRAFT');
    context.authorization.renewal_required=false;await client.read();assert(!client.paused,'Read-only renewal metadata cannot transfer or invalidate the same paired draft scope');
    settings.revision='6'.repeat(64);context.configuration_revision=settings.revision;
    settings.capability_revision='6'.repeat(64);context.capability_revision=settings.capability_revision;
    await client.read();assert.equal(client.draft.configuration.CW.Message,'REMOTE PI DRAFT');assert(!client.paused,'A verified read with unchanged saved values may refresh private-history metadata without discarding a draft');
    const savedMessage=settings.saved.configuration.CW.Message;settings.saved.configuration.CW.Message='ANOTHER REAL EDIT';settings.revision='7'.repeat(64);context.configuration_revision=settings.revision;
    await assert.rejects(client.read(),{code:'revision_conflict'});assert.equal(client.draft.configuration.CW.Message,'REMOTE PI DRAFT');
    settings.saved.configuration.CW.Message=savedMessage;settings.revision='6'.repeat(64);context.configuration_revision=settings.revision;await client.read();
    const copy=client.draft;copy.configuration.CW.Message='DETACHED';assert.equal(client.draft.configuration.CW.Message,'REMOTE PI DRAFT');
    context={...context,binding:{...context.binding,boot_id:'f'.repeat(32)}};settings.binding=context.binding;
    await assert.rejects(client.read(),{code:'revision_conflict'});assert(client.paused);assert.equal(client.draft.configuration.CW.Message,'REMOTE PI DRAFT');
    await assert.rejects(async()=>client.save(),{code:'connection_changed'});
    context=sample('paired Pi context');context.binding.connection_generation='1';context.target.connection_generation='1';context.capabilities[0].write=true;context.capabilities[0].activation='next_occurrence';Object.assign(context.capabilities[0],control.controllerPiDescriptors['pi-config']);settings.binding=context.binding;settings.capability_revision=context.capability_revision;settings.revision=context.configuration_revision;await client.read();
    mode='lost';await assert.rejects(client.save(),/lost reply/);
    assert.equal(client.operation.state,'unknown');assert.deepEqual(last.candidate.scheduling,originalSchedule);
    const writes=()=>calls.filter(item=>item.options.method==='PUT').length;
    assert.equal(writes(),1);await assert.rejects(async()=>client.save(),{code:'connection_changed'});
    mode='wrong-receipt';await assert.rejects(client.check(),{code:'invalid_response'});assert.equal(client.operation.state,'unknown');assert.equal(writes(),1);
    mode='normal';assert.equal((await client.check()).state,'committed');assert.equal(writes(),1);assert(client.paused);
    settings={...settings,revision:receipt.revision,saved:structuredClone(last.candidate)};context.configuration_revision=settings.revision;
    await client.read();assert(!client.paused);assert(!client.dirty);
    // Confirmed writes can refresh their own revision, but never erase edits
    // made after the captured request or adopt another authorization scope.
    client.set('CW','Message','SECOND SAVE');mode='normal';await client.save();
    settings.saved=structuredClone(last.candidate);await client.read();assert.equal(client.draft.configuration.CW.Message,'SECOND SAVE');
    assert.equal(writes(),2);
    client.set('CW','Message','UNKNOWN REVIEW DRAFT');mode='lost';await assert.rejects(client.save());
    const historical=structuredClone(receipt);historical.state='unknown';historical.revision=null;receipt=historical;
    settings.revision='7'.repeat(64);context.configuration_revision=settings.revision;
    const beforeReviewWrites=writes();const readback=await client.readRecovery();assert.equal(readback.revision,settings.revision);
    const acknowledgement=await client.review();assert.equal(acknowledgement.schema,'target-operation-reviewed');
    assert.equal(client.operation,null);assert(client.paused);assert.equal(writes(),beforeReviewWrites);
    assert.equal(receipt.state,'unknown','Review never converts an earlier unknown result into success');
    await client.read();assert(!client.paused);assert.equal(client.draft.configuration.CW.Message,'UNKNOWN REVIEW DRAFT');
    assert(client.dirty,'Explicit fenced review preserves the unsaved draft for a new explicit save');
    context.capabilities.push({resource:'stop',schema_id:'urn:wsprry:control:1:stop',schema_digest:control.controllerPiDescriptors.stop.schema_digest,read:false,write:true,activation:'immediate',maximum_bytes:32768});
    const stopping=create({device,fetch,crypto});await stopping.read();mode='normal';const observed=await stopping.status();
    const stale={...observed,revision:'f'.repeat(64)};assert.throws(()=>stopping.stop(stale),{code:'output_changed'});
    const stopReceipt=await stopping.stop(observed);assert.equal(stopReceipt.state,'committed');assert.equal(last.schema,'target-stop');
    assert.deepEqual(last.observed_output,observed.observed_output);assert(!Object.hasOwn(last,'candidate'));
    assert(calls.at(-1).url.endsWith('/stop'));assert.equal(calls.at(-1).options.method,'POST');
    const prior=create({device,fetch,crypto});await prior.read();prior.set('CW','Message','UNSENT NEW DRAFT');mode='earlier';
    await assert.rejects(prior.save(),{code:'recovery_required'});assert(prior.operation.foreign);
    const priorWrites=writes(),reviews=calls.filter(item=>item.url.endsWith('/review')).length;
    assert.equal((await prior.check()).state,'committed');assert.equal(prior.operation.state,'prior_terminal');
    await prior.readRecovery();assert.equal((await prior.review()).state,'earlier_terminal');await prior.read();
    assert.equal(prior.draft.configuration.CW.Message,'UNSENT NEW DRAFT');assert(!prior.paused);assert(prior.dirty);
    assert.equal(writes(),priorWrites);assert.equal(calls.filter(item=>item.url.endsWith('/review')).length,reviews,'A terminal earlier receipt needs no fictitious server unknown-operation retirement');
    for(const lostStop of [false,true]){
        const independent=create({device,fetch,crypto});mode='normal';await independent.read();independent.set('CW','Message','RETAIN ME THROUGH STOP');
        mode='lost';await assert.rejects(independent.save());const old=independent.operation,retained=independent.draft;
        const current=await independent.readStop();assert(independent.stopAvailable);assert.deepEqual(independent.draft,retained);
        mode=lostStop?'lost':'normal';
        if(lostStop){
            await assert.rejects(independent.stop(current));assert.equal(independent.operation.resource,'stop');assert.equal(independent.operation.state,'unknown');
            mode='normal';assert.equal((await independent.check()).state,'committed');
        }else assert.equal((await independent.stop(current)).state,'committed');
        assert.deepEqual(independent.operation,old,'Independent Stop preserves the exact earlier unknown operation');
        assert.deepEqual(independent.draft,retained,'Independent Stop and recovery preserve the draft');
        assert.throws(()=>independent.save(),error=>['connection_changed','operation_pending'].includes(error.code));
    }
    for(refusal of ['unauthorized','csrf_failed','connection_changed','revision_conflict','authority_pending']){
        const retried=create({device,fetch,crypto});mode='normal';await retried.read();retried.set('CW','Message','RETAIN ACCEPTED PUT '+refusal);
        mode='retried-refusal';const before=writes();await assert.rejects(retried.save(),{code:refusal});
        assert.equal(retried.operation.state,'unknown');assert.equal(retried.operation.id,receipt.operation_id);
        assert.equal(retried.draft.configuration.CW.Message,'RETAIN ACCEPTED PUT '+refusal);
        mode='normal';assert.equal((await retried.check()).state,'committed');assert.equal(writes(),before+1,'Receipt recovery never retries the PUT');
    }
    const wrong=create({device:'f'.repeat(32),fetch,crypto});await assert.rejects(wrong.read(),{code:'target_context_unavailable'});
    const descriptor=structuredClone(context.capabilities[0]);context.capabilities[0].schema_digest='f'.repeat(64);
    const unsupported=create({device,fetch,crypto});await assert.rejects(unsupported.read(),{code:'target_context_unavailable'});context.capabilities[0]=descriptor;
    assert(!calls.some(item=>item.url.includes('/enable')||item.url.includes('/migration')));
    console.log('PASS native Pi target client: bound drafts, current authorization, immutable scheduling/private references, lost-save receipt-only recovery, and no hidden mutation retry');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

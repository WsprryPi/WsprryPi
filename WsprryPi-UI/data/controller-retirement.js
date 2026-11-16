// SPDX-License-Identifier: MIT
(function(root){
    'use strict';
    const node=id=>root.document.getElementById('controllerRetirement'+id);
    if(!node(''))return;
    const client=root.ConfigurationRecoveryClient.create({base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'});
    const key='wsprrypi.controller.retirement.intent.1',fences=new Set();
    const forget=()=>{try{root.sessionStorage.removeItem(key);}catch(_){}};
    let target=null,name='',current=null,review=null,pending=null,busy=false,serial=0,timer=null,closed=false;
    try{pending=JSON.parse(root.sessionStorage.getItem(key)||'null');
        if(pending&&(!root.WsprryControl.validControllerDocument(pending.intent,'controller-retirement')||!/^"[0-9a-f]{64}"$/.test(pending.etag||'')))pending=null;
    }catch(_){pending=null;}
    const feedback=text=>{node('Feedback').textContent=text;};
    const identity=()=>Array.from(root.crypto.getRandomValues(new Uint8Array(16)),byte=>byte.toString(16).padStart(2,'0')).join('');
    function controls(){
        node('Review').disabled=closed||busy||!!pending||!current||current.row.state!=='legacy';
        node('Refresh').disabled=closed||busy;node('Confirm').disabled=closed||busy||!review||!node('Consent').checked;
        node('Cancel').disabled=busy;node('').setAttribute('aria-busy',String(busy));
        if(fences.has(target)||pending?.intent.target_id===target||current&&current.row.state!=='legacy'){const save=root.document.getElementById('fleet-schedule-save');if(save)save.disabled=true;}
    }
    function receipt(value,intent){
        if(!value||['controller_id','target_id','assignment_id','operation_id','transition_id'].some(k=>value[k]!==intent[k]))throw new Error('receipt_mismatch');
        fences.add(intent.target_id);const visible=intent.target_id===target;
        if(value.state==='retired'){pending=null;forget();if(visible)feedback('Retirement is confirmed. This controller cannot resume this saved schedule.');}
        else {if(pending)pending.confirmed=true;if(visible)feedback('Retirement is recorded. Accepted or unknown work remains pending; no new occurrences can start from this assignment.');}
    }
    function follow(){
        root.clearTimeout(timer);
        if(!closed&&pending?.confirmed&&pending.intent.target_id===target)timer=root.setTimeout(()=>{void load(false);},2000);
    }
    function refused(error){
        if(error.status!==409||error.code!=='revision_conflict')return false;
        pending=null;current=null;review=null;forget();node('Confirmation').hidden=true;
        feedback('This request was refused. Refresh and review the saved retirement state before another action.');return true;
    }
    async function load(retry=false){
        if(closed||busy||!target)return;const captured=target,epoch=serial;busy=true;current=null;controls();
        try{
            const result=await client.retirements();if(epoch!==serial)return;
            if(result.etag!=='"'+result.data.revision+'"')throw new Error('revision_mismatch');
            const rows=result.data.assignments.filter(row=>row.target_id===captured);if(rows.length!==1)throw new Error('assignment_unknown');
            current={...result.data,row:rows[0],etag:result.etag};
            if(!['legacy','retiring','retired'].includes(current.row.state)){
                feedback('Saved scheduling authority requires recovery. This controller cannot change or resume that assignment.');return;
            }
            if(pending){
                if(pending.intent.target_id!==captured){feedback('An earlier retirement result is unconfirmed for '+(pending.label||pending.intent.target_id)+'. Open that output before another retirement.');return;}
                const saved=current.row.receipt;
                if(saved){
                    try{receipt(saved,pending.intent);}
                    catch(error){
                        // A concurrent operator may have retired this same
                        // assignment. Only explicit Check may resubmit the
                        // immutable earlier intent for a definitive refusal.
                        if(!retry)throw error;
                        const response=await client.retirements(pending.intent,pending.etag);if(epoch!==serial)return;receipt(response.data,pending.intent);
                    }
                }
                else if(retry){const response=await client.retirements(pending.intent,pending.etag);if(epoch!==serial)return;receipt(response.data,pending.intent);}
                else feedback('The earlier result is unconfirmed. Check saved result before submitting another action.');
            }else if(current.row.receipt)receipt(current.row.receipt,current.row.receipt);
            else feedback('Review this existing assignment before retiring its schedule.');
        }catch(error){current=null;if(!refused(error))feedback(pending?'The saved result could not be verified. Your request is retained; check again before another action.':'Saved retirement state is unavailable. Refresh before reviewing an action.');}
        finally{busy=false;controls();follow();if(epoch!==serial&&target)void load();}
    }
    async function submit(){
        if(closed||busy||!review||pending||!node('Consent').checked)return;
        const captured=review,epoch=serial;pending={label:captured.label,intent:{contract:'Wsprry-Control/1',schema:'controller-retirement',controller_id:captured.controller_id,
            controller_incarnation:captured.controller_incarnation,operation_id:identity(),transition_id:identity(),target_id:captured.row.target_id,
            assignment_id:captured.row.assignment_id,expected_assignment_revision:captured.row.assignment_revision,expected_retirement_generation:captured.generation},etag:captured.etag};
        // Persist the immutable request before submission; a lost response is
        // resolved by reading the same assignment, never a second operation.
        try{root.sessionStorage.setItem(key,JSON.stringify(pending));}catch(_){pending=null;feedback('This browser cannot retain the request. Enable session storage before retiring a schedule.');controls();return;}
        busy=true;controls();node('Confirmation').hidden=true;
        try{const result=await client.retirements(pending.intent,pending.etag);receipt(result.data,pending.intent);current=null;}
        catch(error){if(!refused(error))feedback('The result is unconfirmed. Check saved result; the reviewed request is retained.');}
        finally{busy=false;review=null;controls();follow();if(epoch!==serial&&target)void load();}
    }
    node('Review').addEventListener('click',()=>{
        if(busy||pending||!current||current.row.state!=='legacy')return;
        review={...JSON.parse(JSON.stringify(current)),label:name};node('Consent').checked=false;
        node('Summary').textContent=(name||'Selected output')+' · '+target;
        node('Confirmation').hidden=false;controls();node('Consent').focus();
    });
    node('Consent').addEventListener('change',controls);node('Confirm').addEventListener('click',()=>{void submit();});
    node('Cancel').addEventListener('click',()=>{review=null;node('Confirmation').hidden=true;controls();node('Review').focus();});
    node('Refresh').addEventListener('click',()=>{void load(true);});
    root.addEventListener('pagehide',()=>{closed=true;root.clearTimeout(timer);});
    root.addEventListener('pageshow',event=>{closed=false;if(event.persisted&&target)void load();});
    root.ControllerRetirement=Object.freeze({get fenced(){return fences.has(target)||pending?.intent.target_id===target||!!current&&current.row.state!=='legacy';},select(id,label=''){name=String(label);target=/^[0-9a-f]{32}$/.test(id)?id:null;++serial;current=null;review=null;
        node('').hidden=!target;node('Confirmation').hidden=true;if(target)feedback('Reading saved retirement state…');controls();follow();if(target)void load();}});
})(window);

// SPDX-License-Identifier: MIT
(function(root){
    'use strict';
    document.addEventListener('DOMContentLoaded',()=>{
        const pane=document.getElementById('controllerAccess');if(!pane)return;
        const element=id=>document.getElementById(id);
        const local=element('pairingLocalFeedback'),feedback=element('pairingConnectFeedback');
        let client,state=null,devices=[],busy=false,expires=0,windowId='',timer=null,checking=false,viewGeneration=0,discoveryGeneration=0,disposed=false;
        try{client=root.PairingClient.create({base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'});}
        catch(_){element('pairingState').textContent='Controller access is unavailable. Refresh this page to check again.';return;}
        const message=error=>({
            invalid_code:'Enter the eight-character code shown on the other Pi.',
            csrf_failed:'This page’s access session expired. Refresh status, then try again.',
            revision_conflict:'Controller access changed. Refresh status before choosing another action.',
            unauthorized:'Controller access requires Enforced local-network safety.',
            authority_pending:'Controller access is not ready. Refresh status to check again.',
            operation_unknown:'No confirmed result is available. Do not assume the change failed.',
            capacity:'Controller access is busy or its recovery history is full. Check saved results before trying again.'
        }[error?.code||error?.message]||'The result could not be confirmed. Check the saved result or refresh status.');
        function pending(){element('pairingPending').hidden=!client.pending();}
        function controls(){
            element('pairingWindow').disabled=busy||!state||state.state==='unavailable';
            element('pairingRemoveReview').hidden=state?.state!=='paired';
            for(const id of ['pairingRemoveReview','pairingRemove','pairingRemoveCancel','pairingRefresh','pairingDiscoveryRefresh','pairingCheck'])element(id).disabled=busy;
            element('pairingDevice').disabled=busy||!state||state.state==='unavailable'||devices.length===0;
            element('pairingInput').disabled=busy||!state||state.state==='unavailable';
            element('pairingConnect').disabled=busy||!state||state.state==='unavailable'||!element('pairingDevice').value;
            pane.setAttribute('aria-busy',busy?'true':'false');pending();
        }
        function clearCode(){expires=0;windowId='';element('pairingCode').textContent='';element('pairingExpiry').textContent='';element('pairingCodePanel').hidden=true;}
        async function refresh(){
            const generation=viewGeneration;let value;
            try{value=await client.state();}catch(error){
                if(!disposed&&generation===viewGeneration){state=null;clearCode();element('pairingState').textContent='Controller access could not be confirmed. Refresh status to check again.';controls();}
                throw error;
            }
            if(disposed||generation!==viewGeneration)return;
            state=value;
            const host=root.ControllerMode?.active?'this host':'this Pi';
            const text=state.state==='paired'?'Managed by '+state.manager.name+'.':state.state==='unpaired'?'No controller is paired with '+host+'.':
                state.state==='access_recovery'?'Pairing access needs recovery. Pair again to establish fresh controller access.':'Controller access is unavailable. Check network safety and refresh status.';
            element('pairingState').textContent=text;
            element('pairingWindow').textContent=state.state==='paired'?'Replace controller':state.state==='access_recovery'?'Pair again':'Pair '+host;
            if(!state.window||state.window.window_id!==windowId)clearCode();
            controls();return state;
        }
        async function refreshAfterResult(target,text){
            target.textContent=text;
            try{await refresh();}catch(_){target.textContent=text+' Refresh status before another action.';}
        }
        async function discovery(){
            const generation=++discoveryGeneration,previous=element('pairingDevice').value,value=await client.discovery();
            if(disposed||generation!==discoveryGeneration)return;devices=value.discoveries;
            const select=element('pairingDevice');select.replaceChildren();
            const empty=document.createElement('option');empty.value='';empty.textContent=devices.length?'Select a Pi':value.available?'No nearby Pis found':'Discovery unavailable';select.append(empty);
            for(const device of devices){const option=document.createElement('option');option.value=device.discovery_id;option.textContent=device.name+' · '+device.device_id.slice(0,8);select.append(option);}
            if(devices.some(device=>device.discovery_id===previous))select.value=previous;
            controls();
        }
        async function action(target,run){
            if(busy||disposed)return;++viewGeneration;busy=true;controls();target.textContent='Working…';
            try{await run();}catch(error){target.textContent=message(error);}finally{busy=false;controls();}
        }
        element('pairingRefresh').addEventListener('click',()=>action(local,async()=>{await refresh();local.textContent='Controller access refreshed.';}));
        element('pairingDiscoveryRefresh').addEventListener('click',()=>action(feedback,async()=>{await discovery();feedback.textContent=devices.length?'Select the Pi showing your pairing code.':'No Pi is available yet. Open its pairing window and refresh devices.';}));
        element('pairingDevice').addEventListener('change',controls);
        element('pairingWindow').addEventListener('click',()=>action(local,async()=>{
            clearCode();const window=await client.window(state);if(disposed)return;
            if(window.device_id!==state.device_id||window.boot_id!==state.boot_id)throw Error('invalid_response');
            element('pairingCode').textContent=window.code;element('pairingCodePanel').hidden=false;
            windowId=window.window_id;expires=performance.now()+window.expires_in_ms;element('pairingExpiry').textContent='Expires in '+Math.ceil(window.expires_in_ms/1000)+' seconds.';local.textContent='Pairing window opened. This code is shown only on this page.';
            element('pairingCode').focus();
        }));
        element('pairingRemoveReview').addEventListener('click',()=>{element('pairingRemoveConfirmation').hidden=false;element('pairingRemove').focus();});
        element('pairingRemoveCancel').addEventListener('click',()=>{element('pairingRemoveConfirmation').hidden=true;element('pairingRemoveReview').focus();});
        element('pairingRemove').addEventListener('click',()=>action(local,async()=>{
            clearCode();const result=await client.remove(state);element('pairingRemoveConfirmation').hidden=true;
            await refreshAfterResult(local,result.state==='committed'?'Controller access removed.':'The removal is unconfirmed. Check its saved result.');
            (element('pairingWindow').disabled?element('pairingRefresh'):element('pairingWindow')).focus();
        }));
        element('pairingConnectForm').addEventListener('submit',event=>{
            event.preventDefault();const input=element('pairingInput'),code=input.value;
            const device=devices.find(device=>device.discovery_id===element('pairingDevice').value);
            if(!device)return;
            action(feedback,async()=>{
                input.value='';const result=await client.connect(device,code);
                await refreshAfterResult(feedback,result.state==='committed'?'Paired with '+device.name+'. Open Setup → Device to manage it and its attached Picos.':
                    'Pairing is unconfirmed. Check the saved result; do not enter the old code again.');
            });
        });
        element('pairingCheck').addEventListener('click',()=>action(element('pairingPendingFeedback'),async()=>{
            const prior=client.pending(),result=await client.check();
            const target=['committed','rejected'].includes(result.state)?(prior?.kind==='connect'?feedback:local):element('pairingPendingFeedback');
            await refreshAfterResult(target,result.state==='committed'?'The access change is confirmed.':
                result.state==='rejected'?'The access change was rejected. Refresh status before trying again.':'The access change is still unconfirmed. Recovery continues automatically.');
        }));
        async function tick(){
            if(expires){
                const seconds=Math.ceil((expires-performance.now())/1000);
                if(seconds<=0){clearCode();local.textContent='The pairing code expired. Open a new window for a new code.';}
                else element('pairingExpiry').textContent='Expires in '+seconds+' seconds.';
            }
            if(!busy&&!checking&&(expires||client.pending())&&!document.hidden){
                checking=true;try{
                    const prior=client.pending();
                    if(prior){const result=await client.check();if(result.state==='committed'){
                        (prior.kind==='connect'?feedback:local).textContent=prior.kind==='connect'?'Pairing is confirmed. Open Setup → Device to manage the Pi and its attached Picos.':'Controller access removal is confirmed.';
                    }}
                    await refresh();
                }catch(_){}finally{checking=false;controls();}
            }
        }
        timer=setInterval(tick,3000);
        root.addEventListener('pagehide',()=>{disposed=true;++viewGeneration;clearCode();clearInterval(timer);element('pairingInput').value='';},{once:true});
        root.addEventListener('pageshow',event=>{if(event.persisted)root.location.reload();});
        Promise.allSettled([refresh(),discovery()]).then(results=>{
            if(results[0].status==='rejected'){state=null;element('pairingState').textContent=message(results[0].reason);}
            if(results[1].status==='rejected'){devices=[];element('pairingDevice').replaceChildren(new Option('Discovery unavailable',''));}
            controls();
        });
    });
})(window);

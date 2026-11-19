// SPDX-License-Identifier: MIT
(function(root) {
    'use strict';
    const node=id=>root.document.getElementById('operationTarget'+id);
    if(!node('View'))return;
    const base=root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1';
    const api=root.ConfigurationRecoveryClient.create({base}),picos=root.PicoTargetClient.create({base});
    const entries=new Map();let targets=[],selected=null,inventoryBusy=false,closed=false,timer;
    const identity=value=>typeof value==='string'&&/^[0-9a-f]{32}$/.test(value);
    const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
    const failure=()=>{throw Error('Device context or status could not be verified.');};
    const available=item=>!closed&&targets.some(target=>target.device_id===item.target.device_id&&target.product===item.target.product&&target.profile_generation===item.target.profile_generation);
    const locked=()=>root.PicoFirmwareSafety?.locked||root.ConfigurationRecovery?.active;
    const pending=item=>!!item.client?.operation||item.unknown;
    function render() {
        const item=selected;node('View').hidden=!item;
        if(item){
            const pi=item.target.product==='WsprryPi',ready=available(item)&&!item.busy&&!locked();
            node('View').setAttribute('aria-busy',String(item.busy));
            node('Name').textContent=item.target.name;
            node('Identity').textContent=item.target.product+' · '+item.target.device_id+(pi?' · paired network':item.handle?.managed?' · USB through paired Pi':'');
            const phase=pi?item.status?.phase:item.status?.job.state;
            node('State').textContent=item.unknown?'Outcome unconfirmed':!available(item)?'Device unavailable':!item.status?'Device state unknown':phase==='empty'?'Idle':phase.charAt(0).toUpperCase()+phase.slice(1).replaceAll('_',' ');
            node('Detail').textContent=item.readOnly ? 'This Pi has no supported transmitter settings through this connection. Its supported attached Picos can be selected separately.' : item.status ? pi
                ? `Transmit permission ${item.status.transmit_effective?'on':'off'}. Scheduling ${item.status.schedule_enabled?'enabled':'disabled'}. Waiting reason: ${item.status.waiting_reason}.`
                : `Output ${item.status.job.output_active?'active':'inactive'}; ${item.status.job.owner_id?'owned':'unowned'}.`
                : 'Read this device’s status before operating. Output state is unknown.';
            node('Age').textContent=item.at?'Last read '+new Date(item.at).toLocaleTimeString()+'. Software observation; measured RF is not established.':'';
            node('Feedback').textContent=item.feedback||'';
            node('Read').disabled=item.busy||!!locked();
            node('Setup').disabled=item.busy||!available(item);
            node('Stop').disabled=!ready||item.readOnly||!item.status||pending(item)||pi&&!item.client?.stopAvailable||!pi&&!item.stop;
            node('Stop').textContent=pi?'Stop transmission':'Stop native schedules';
            for(const name of ['Enable','Disable','Transmit','Check','ReadReview','Review'])node(name).hidden=!pi;
            const capable=resource=>!item.readOnly&&item.client?.context?.capabilities.some(value=>value.resource===resource&&value.write);
            node('Enable').disabled=!ready||pending(item)||!item.status||item.client?.paused||!capable('pi-scheduling')||item.client?.current?.saved.scheduling.enabled!==false;
            node('Disable').disabled=!ready||pending(item)||!item.status||item.client?.paused||!capable('pi-scheduling')||item.client?.current?.saved.scheduling.enabled!==true;
            node('Transmit').disabled=!ready||pending(item)||!item.status||item.client?.paused||!capable('pi-config')||item.client?.current?.saved.configuration.Operation.Transmit!==false;
            node('Check').hidden=!pi||!item.client?.operation;
            node('Check').disabled=!ready;
            node('ReadReview').hidden=!pi||!item.client?.operation;
            node('ReadReview').disabled=!ready;
            node('Review').hidden=!pi||!item.review;
            node('Review').disabled=!ready;
            node('StopRead').hidden=pi||!item.unknown;
            node('StopRead').disabled=!ready;
            node('ReviewValues').textContent=item.review?JSON.stringify(item.review,null,2):'';
            node('Limit').textContent=pi?'Schedule enablement and Transmit permission are independent. Pausing scheduling allows accepted work to finish; Stop disables Transmit permission.':
                'Stop disables both native schedules. Enable or edit WSPR scheduling in Setup; keyed-message enablement uses the Pico’s own controls.';
            node('Summary').replaceChildren();
            const rows=!item.status?[]:pi?[
                ['Clock',item.status.clock.state],['Execution owner',item.status.execution_owner],['Output destination',item.status.destination_id||'Unknown'],
                ['Selected frequencies (Hz)',item.status.selected_frequencies_hz.join(', ')||'Unknown'],['Next start (UTC nanoseconds)',item.status.forecast==='known'?item.status.next_start_utc_ns:'Unknown']
            ]:[['WSPR scheduling',item.stop?(item.stop.wspr_enabled?'Enabled':'Disabled'):'Unknown'],['Message scheduling',item.stop?(item.stop.message_enabled?'Enabled':'Disabled'):'Unknown'],['Boot',item.status.job.boot_id]];
            for(const [label,value] of rows){const wrap=root.document.createElement('div'),term=root.document.createElement('dt'),detail=root.document.createElement('dd');term.textContent=label;detail.textContent=value;detail.className='text-break';wrap.append(term,detail);node('Summary').append(wrap);}
        }
        root.FleetOperation?.refresh();
    }
    async function run(item,action) {
        if(item.busy||locked())return;item.busy=true;render();
        try{await action(item);}catch(error){if(error.operation)item.operation=error.operation;item.status=null;item.client?.pause();item.feedback=pending(item)?'The earlier request is unconfirmed. Check its result or read current values for explicit review. No request was retried.':'Device status is unavailable. Read status to reconnect and verify this device before operating. For Wi-Fi authentication, open Setup → Device I/O.';}
        finally{item.busy=false;if(selected===item)render();}
    }
    function checkedStop(item,result,boot=null){
        const value=result.data;
        if(!result.response.ok||!value||Object.keys(value).sort().join(',')!=='contract,message_enabled,output_active,owned,schema,stopped,target,wspr_enabled'||
            value.contract!==root.WsprryControl.contract||value.schema!=='schedule-stop'||!value.target||Object.keys(value.target).sort().join(',')!=='boot_id,device_id,scope'||
            value.target.device_id!==item.target.device_id||value.target.scope!=='member'||!identity(value.target.boot_id)||boot&&value.target.boot_id!==boot||
            !['message_enabled','output_active','owned','stopped','wspr_enabled'].every(key=>typeof value[key]==='boolean')||
            value.stopped&&(value.message_enabled||value.wspr_enabled||value.output_active||value.owned)||!/^"[^"\x00-\x20\x7f]{1,126}"$/.test(result.response.headers.get('ETag')||''))failure();
        return value;
    }
    async function read(item){
        item.status=null;item.stop=null;
        if(!available(item))failure();
        if(item.target.product==='WsprryPi'){
            item.readOnly=false;
            try{
                await item.client.read();const status=await item.client.status();
                if(!available(item)||item.client.context.target.profile_generation!==item.target.profile_generation)failure();
                item.status=status;
            }catch(error){
                if(error.code!=='unsupported_resource')throw error;
                const context=(await api.targetRequest(item.target.device_id,'context')).data;
                const status=(await api.targetRequest(item.target.device_id,'status')).data;
                if(context.target.product!=='WsprryPi'||context.target.device_id!==item.target.device_id||context.target.profile_generation!==item.target.profile_generation||
                    !context.authorization||context.gateway!==null||!equal(status.binding,context.binding)||!available(item))failure();
                item.readOnly=true;item.status=status;
            }
        }else{
            item.handle?.retire();
            // The generic context identifies a paired-Pi gateway; saved local
            // Pico profiles remain explicitly bound to their catalog profile.
            const response=await root.fetch(base+'/host/devices',{credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000)});
            const catalog=await response.json();
            const profile=response.ok&&Array.isArray(catalog.profiles)?catalog.profiles.find(profile=>identity(profile.id)&&profile.settings?.['Device ID']===item.target.device_id&&!profile.legacy_unverified):null;
            item.handle=await picos.open(profile||{id:item.target.device_id,name:item.target.name,managed:true,settings:{'Device ID':item.target.device_id,Transport:'usb'}});
            if(!available(item)){item.handle.retire();failure();}
            if(!item.handle.state.connected){
                if(!profile||!['usb','network'].includes(profile.settings.Transport)){item.feedback='Authenticate this Pico in Setup → Device I/O, then read status here.';failure();}
                const opened=await item.handle.request('management',{method:'POST',body:{action:profile.settings.Transport==='usb'?'usb_physical':'engineering_mtls'}});
                if(!opened.response.ok)failure();
                const checked=await item.handle.request('management');if(!checked.response.ok||!checked.data.connected)failure();
            }
            const reply=await item.handle.request('status'),value=reply.data;
            if(!reply.response.ok||!value||Object.keys(value).sort().join(',')!=='contract,job,network,standalone,transport'||value.contract!==root.WsprryControl.contract||
                !value.job||!value.standalone||!identity(value.job.boot_id)||value.standalone.boot_id!==value.job.boot_id||typeof value.job.state!=='string'||typeof value.job.output_active!=='boolean')failure();
            const stop=await item.handle.request('schedules/stop');
            if(stop.response.ok)item.stop=checkedStop(item,stop,value.job.boot_id);
            else if(stop.data?.error?.code!=='unsupported_resource')failure();
            if(!available(item))failure();item.status=value;
        }
        item.at=Date.now();item.feedback=pending(item)?'An earlier request remains unconfirmed. An ordinary status read does not clear it.':'Current device status read. Selection did not change settings or output.';
    }
    async function inventory(){
        if(inventoryBusy||closed)return;inventoryBusy=true;
        try{
            const [value,host]=await Promise.all([api.targets(),api.readiness()]);
            const next=value.targets.filter(target=>target.device_id!==host.device_id);
            targets=next;
            if(selected&&!available(selected)){selected.status=null;selected.stop=null;selected.client?.pause();selected.handle?.retire();selected.feedback='This device or its profile changed. Return to This Pi and choose a current device; earlier requests remain attached to their original device.';}
        }catch(_){targets=[];if(selected){selected.status=null;selected.stop=null;selected.client?.pause();selected.handle?.retire();selected.feedback='Device inventory is unavailable. Output state is unknown; refresh before operating.';}}
        finally{inventoryBusy=false;render();}
    }
    async function command(action){
        const item=selected;if(!item||item.busy||locked()||action!=='Read'&&!available(item))return;
        const labels={Stop:'Stop output on',Enable:'Enable saved scheduling on',Disable:'Pause scheduling on',Transmit:'Enable transmission on'};
        if(node(action)?.disabled)return;
        if(labels[action]&&!root.confirm(labels[action]+' '+item.target.name+' ('+item.target.device_id+')?'))return;
        await run(item,async()=>{
            if(action==='Read'){await inventory();await read(item);return;}
            if(action==='StopRead'){
                const reply=await item.handle.request('schedules/stop'),value=checkedStop(item,reply,item.unknownBoot);
                if(value.stopped){item.unknown=false;if(item.operation)picos.reviewed(item.operation,item.target.device_id);}
                item.stop=value;item.feedback=value.stopped?'Both native schedules are disabled; output is confirmed inactive and unowned.':'Stop is not confirmed. Keep the hold and use this Pico’s local controls if needed.';return;
            }
            if(item.target.product==='WsprryPi'){
                if(action==='Check'){const receipt=await item.client.check();item.feedback='Earlier request: '+receipt.state+'. No write was repeated.';if(!item.client.operation)await read(item);return;}
                if(action==='ReadReview'){item.review=await item.client.readRecovery();return;}
                if(action==='Review'){await item.client.review();item.review=null;await read(item);return;}
                if(pending(item)||!item.status)failure();
                let result;
                if(action==='Stop')result=await item.client.stop(item.status);
                if(action==='Enable')result=await item.client.enable();
                if(action==='Disable')result=await item.client.disable();
                if(action==='Transmit'){item.client.set('Operation','Transmit',true);result=await item.client.save();}
                item.status=null;
                if(result?.state==='committed'){await read(item);item.feedback='Change confirmed and current status read. Software status does not establish measured RF output.';}
                else item.feedback='Change '+(result?.state||'unconfirmed')+'. Read and review the result before another action.';
            }else if(action==='Stop'){
                if(pending(item)||!item.status||!item.stop)failure();
                const before=await item.handle.request('schedules/stop'),value=checkedStop(item,before,item.status.job.boot_id);
                if(!available(item))failure();
                item.unknown=true;item.unknownBoot=value.target.boot_id;item.status=null;
                const result=await item.handle.request('schedules/stop',{method:'POST',revision:before.response.headers.get('ETag'),body:{schema:'schedule-stop',target:value.target,stop:true}});
                item.operation=result.operation;
                checkedStop(item,result,item.unknownBoot);
                // Explicit target/boot-bound readback, never an ordinary poll,
                // resolves a native stop hold after its mutation.
                const after=await item.handle.request('schedules/stop'),confirmed=checkedStop(item,after,item.unknownBoot);
                if(!confirmed.stopped)failure();item.unknown=false;picos.reviewed(item.operation,item.target.device_id);
                await read(item);item.feedback='Both native schedules are disabled; output is confirmed inactive and unowned.';
            }
        });
    }
    root.TargetOperation=Object.freeze({
        get selected(){return selected;},get busy(){return !!selected?.busy;},
        options(){return targets.map(target=>({id:'target:'+target.device_id,label:target.name+' · '+target.product+' · '+target.device_id.slice(0,8)}));},
        async select(id){
            if(selected?.busy)return;
            let target=targets.find(target=>target.device_id===id);if(!target)return;
            let item=entries.get(id);
            if(item&&!pending(item)&&!equal(item.target,target)){item.client?.pause();item.handle?.retire();entries.delete(id);item=null;}
            if(!item){if(entries.size>=64)return;item={target:structuredClone(target),status:null,feedback:'Reading device status…'};if(target.product==='WsprryPi')item.client=root.PiTargetClient.create({device:id,base});entries.set(id,item);}
            selected=item;render();await run(item,read);
        },
        leave(){if(!selected?.busy){selected=null;render();}},refresh:inventory
    });
    for(const name of ['Read','Stop','StopRead','Enable','Disable','Transmit','Check','ReadReview','Review'])node(name).addEventListener('click',()=>void command(name));
    node('Setup').addEventListener('click',()=>{
        if(!selected||selected.busy)return;
        root.sessionStorage.setItem('wsprrypi.setup.target',JSON.stringify({device_id:selected.target.device_id}));
        root.location.href='index.php?page=config';
    });
    root.addEventListener('pagehide',()=>{closed=true;root.clearTimeout(timer);for(const item of entries.values()){item.status=null;item.stop=null;item.client?.pause();item.handle?.retire();}});
    root.addEventListener('pageshow',()=>{if(closed){closed=false;void tick();}});
    async function tick(){
        await inventory();
        const item=selected;
        if(item&&!item.busy&&!pending(item)&&available(item)&&item.status&&!closed&&!root.document.hidden)await run(item,async()=>{
            if(item.client){
                if(item.readOnly){const status=(await api.targetRequest(item.target.device_id,'status')).data;if(!equal(status.binding,item.status.binding))failure();item.status=status;}
                else item.status=await item.client.status();
            }
            else{
                const reply=await item.handle.request('status'),value=reply.data;
                if(!reply.response.ok||!value||value.contract!==root.WsprryControl.contract||!value.job||value.job.boot_id!==item.status.job.boot_id||value.standalone?.boot_id!==value.job.boot_id||typeof value.job.state!=='string'||typeof value.job.output_active!=='boolean')failure();
                item.status=value;
            }
            if(!available(item))failure();item.at=Date.now();
        });
        if(!closed)timer=root.setTimeout(tick,10000);
    }
    void tick();
})(window);

// SPDX-License-Identifier: MIT
(function(root){
    'use strict';if(!root.document)return;
    const node=id=>root.document.getElementById('schedule-'+id),copy=value=>structuredClone(value),entries=new Map();
    let selected=null,available=new Set(),serial=0,epoch=0;
    const el=(tag,classes,text)=>{const n=root.document.createElement(tag);if(classes)n.className=classes;if(text!==undefined)n.textContent=text;return n;};
    function tell(item,text){if(item.requestEpoch!==epoch)return;item.feedback=text;if(selected===item){node('feedback').textContent=text;node('action-feedback').textContent=text;}}
    function summary(solar){return (solar.enabled?'Solar selection on':'Ordinary schedule; solar selection off')+'; target locator '+(solar.location.locator||'not set')+'; day '+(solar.day.allow?'allowed':'skipped')+'; night '+(solar.night.allow?'allowed':'skipped')+'.';}
    function row(parent,label,type,value,hint=''){
        const wrap=el('div','mb-3'),l=el('label','form-label',label),input=el('input',type==='checkbox'?'form-check-input ms-2':'form-control'),help=el('div','form-text',hint),id='schedule-field-'+(++serial);
        input.id=id;l.htmlFor=id;input.type=type;if(type==='checkbox')input.checked=value;else input.value=value??'';
        help.id=id+'-hint';input.setAttribute('aria-describedby',help.id);input.autocomplete='off';wrap.append(l,input,help);parent.append(wrap);return input;
    }
    function validSolar(item,solar){
        const context=item.client.context,candidate=item.client.draft;candidate.scheduling.solar=solar;
        return root.WsprryControl.validControllerDocument({contract:root.WsprryControl.contract,schema:'target-save',binding:context.binding,capability_revision:context.capability_revision,expected_revision:item.client.current.revision,operation_id:'0'.repeat(32),candidate},'target-save');
    }
    function policyText(candidate){
        const schedule=candidate.scheduling,solar=schedule.solar,lines=['Scheduling: '+(schedule.enabled?'enabled':'disabled'),'Solar selection: '+(solar.enabled?'on':'off'),'Target locator: '+(solar.location.locator||'not set')];
        for(const name of ['day','night','morning','evening']){
            const period=solar[name],list=period.wspr_override||candidate.configuration.WSPR;
            lines.push(name+': '+(Object.hasOwn(period,'enabled')?(period.enabled?'on':'off')+'; elevation ['+period.lower_mdeg/1000+', '+period.upper_mdeg/1000+') degrees':period.allow?'allow':'skip'));
            lines.push('  WSPR '+(period.wspr_override?'override':'inherits')+': '+list.Frequency+'; profile '+list['Frequency Profile']+'; band preferences '+JSON.stringify(list['Band Preferences']));
            lines.push('  CW base '+(period.cw_base_hz?'override: '+period.cw_base_hz:'inherits: '+candidate.configuration.CW['Base Frequency'])+' Hz; saved shift '+candidate.configuration.CW['Shift Hz']+' Hz');
        }
        return lines.join('\n');
    }
    function render(){
        const item=selected,c=item?.client,exists=!!c,locked=root.ConfigurationRecovery?.active||root.PicoFirmwareSafety?.locked;
        const connected=exists&&!c.paused&&!c.busy&&!item.busy&&available.has(c.device)&&!locked;
        const capable=connected&&c.context?.capabilities.some(v=>v.resource==='pi-scheduling'&&v.write);
        node('enabled').disabled=!capable;node('fields').inert=!connected;
        if(item?.fields){item.fields.disabled=!capable||!item.desired;item.solarFields.disabled=!item.inputs.solar.checked;for(const period of item.periods){period.fields.disabled=!period.toggle.checked;period.heading.textContent=period.name[0].toUpperCase()+period.name.slice(1)+' · '+(Object.hasOwn(period.original,'enabled')?(period.toggle.checked?'gray-line on':'gray-line off'):(period.toggle.checked?'allow':'skip'));}}
        node('read').disabled=!exists||c.busy||item?.busy||locked;
        node('save').disabled=!capable||!item.valid||!c.dirty||c.current?.saved.scheduling.enabled||!c.context.capabilities.some(v=>v.resource==='pi-config'&&v.write);
        node('enable').disabled=!capable||!item.desired||!item.valid||item.changed||c.dirty||c.current?.saved.scheduling.enabled!==false;
        node('disable').disabled=!capable||c.current?.saved.scheduling.enabled!==true;
        node('preview').disabled=!exists||!item.valid;node('discard').disabled=!exists||c.busy||item?.busy||!!c.operation||locked;
        node('check').hidden=!c?.operation;node('check').disabled=!exists||c.busy||item?.busy||locked;
        node('recovery').hidden=!c?.operation;node('read-review').disabled=!exists||c.busy||item?.busy||locked;
        node('confirm-review').disabled=!item?.review||!exists||c.busy||item?.busy||locked;
        root.SetupTargets?.editorChanged?.();
    }
    function observe(item,status){
        if(selected!==item||item.requestEpoch!==epoch)return;
        const saved=item.client.current,rows=[['Saved scheduling',saved.saved.scheduling.enabled?'Enabled':'Disabled'],['Saved solar selection',saved.saved.scheduling.solar.enabled?'Enabled':'Disabled'],['Saved revision',saved.revision],['Active revision',status?.active_revision||'Unknown'],['Application',saved.pending_application],['Configured output',item.client.draft.configuration.Operation['Transmit Backend']],['WTP destination',item.client.draft.configuration.Operation['Transmit Backend']==='wtp'?item.client.draft.configuration.WTP['Device ID']+' · '+item.client.draft.configuration.WTP.Hostname:'Not selected'],['Execution owner',status?.execution_owner||'Unknown'],['Output destination',status?.destination_id||(item.client.draft.configuration.Operation['Transmit Backend']==='wtp'?'WTP output; identity unknown':'Unknown')],['Transmit permission',status?(status.transmit_effective?'On':'Off'):'Unknown'],['Authority',status?.authority_state||'Unknown'],['Waiting reason',status?.waiting_reason||'Unknown'],['Target clock',status?.clock.state||'Unknown'],['Selected period',status?.selection||'Unknown'],['Selected RF frequencies (Hz)',status?.selected_frequencies_hz.join(', ')||'Unknown'],['Next start forecast (UTC nanoseconds)',status?.forecast==='known'?status.next_start_utc_ns:'Unknown'],['Prolonged solar window',status?.prolonged_window===null||status?.prolonged_window===undefined?'Unknown':status.prolonged_window?'Yes':'No'],['Observation age at read (ms)',status?.observation_age_ms??'Unknown']];
        const compact=[['Saved scheduling',saved.saved.scheduling.enabled?'Enabled':'Disabled'],['Solar selection',saved.saved.scheduling.solar.enabled?'On':'Off'],['Execution owner',status?.execution_owner===item.client.device?'This target · '+item.client.device.slice(0,8):status?.execution_owner||'Unknown'],['Output destination',item.client.draft.configuration.Operation['Transmit Backend']==='wtp'?'WTP · '+(status?.destination_id||item.client.draft.configuration.WTP['Device ID']||'Unknown'):status?.destination_id===item.client.device?'This target':status?.destination_id||'Unknown'],['Application',saved.pending_application],['Transmit',status?(status.transmit_effective?'On':'Off'):'Unknown']];
        node('summary').replaceChildren();for(const [label,value]of compact){const group=el('div');group.append(el('dt','',label),el('dd','',String(value)));node('summary').append(group);}
        node('hold').textContent=({clock:'The target is waiting for healthy UTC. Browser time does not supply execution time.',location:'The target is waiting for its own valid site location. No browser or controller location is used.',authority:'Review scheduling ownership for this target under Maintenance before enabling.',storage:'The target is holding work for durable storage recovery.',recovery:'The target requires execution reconciliation before another occurrence.',solar:'The target is waiting for an allowed solar window.',transmit_off:'Transmit permission is off. Enabling scheduling does not change it.',disabled:'Scheduling is saved disabled. No new occurrences are admitted.'})[status?.waiting_reason]||'Forecasts are target observations and may be unknown. They do not prove execution.';
        node('observation').replaceChildren();for(const [label,value]of rows)node('observation').append(el('dt','',label),el('dd','',String(value)));
    }
    function build(item){
        const draft=item.client.draft;if(!draft)return;item.panel.replaceChildren();item.inputs={};item.periods=[];
        const fields=el('fieldset','mt-3');item.fields=fields;item.panel.append(fields);
        item.inputs.solar=row(fields,'Use solar day/night selection','checkbox',draft.scheduling.solar.enabled,'Off runs ordinary recurrence without solar location or solar clock prerequisites. It does not turn scheduling on.');
        const solarFields=el('fieldset','mt-3');item.solarFields=solarFields;fields.append(solarFields);
        item.inputs.locator=row(solarFields,'Target-site locator','text',draft.scheduling.solar.location.locator,'4, 6 or 8 characters. Uses this target’s locator center. Blank holds solar occurrences; it does not use the browser or controller location. Native Pi GPS is unavailable.');
        item.inputs.locator.maxLength=8;
        const note=el('p','form-text','Blank frequency overrides inherit this target’s saved Signal Setup values. WSPR keeps full lists, skips and saved band preferences; CW uses base Hz and the saved shift. Elevations use degrees, up to three decimals, −90 through +90; lower inclusive and upper exclusive.');solarFields.append(note);
        for(const name of ['day','night','morning','evening']){
            const original=copy(draft.scheduling.solar[name]),group=el('details','py-3 border-top'),heading=el('summary','h5',name[0].toUpperCase()+name.slice(1));group.append(heading);
            const gray=Object.hasOwn(original,'enabled'),toggle=row(group,gray?'Use this gray-line period':'Allow occurrences','checkbox',gray?original.enabled:original.allow);
            const inside=el('fieldset');group.append(inside);const values={name,original,toggle,fields:inside,heading};
            if(gray){values.lower=row(inside,'Lower elevation (degrees)','text',String(original.lower_mdeg/1000));values.upper=row(inside,'Upper elevation (degrees)','text',String(original.upper_mdeg/1000));}
            values.wspr=row(inside,'WSPR frequency list override','text',original.wspr_override?.Frequency||'','Blank inherits. Use the existing Pi frequency-list syntax, including skip entries and selectors.');
            const profileWrap=el('div','mb-3'),label=el('label','form-label','WSPR list profile'),select=el('select','form-select');select.id='schedule-profile-'+(++serial);label.htmlFor=select.id;
            for(const v of ['existing_common','wrc15']){const option=el('option','',v==='wrc15'?'WRC-15':'Existing common');option.value=v;select.append(option);}select.value=original.wspr_override?.['Frequency Profile']||draft.configuration.WSPR['Frequency Profile'];values.profile=select;profileWrap.append(label,select);inside.append(profileWrap);
            values.cw=row(inside,'CW base override (Hz)','text',original.cw_base_hz||'','Blank inherits. Positive decimal Hz, up to eight places; the saved CW shift is preserved.');
            item.periods.push(values);solarFields.append(group);
        }
        item.changed=false;item.valid=true;item.panel.oninput=()=>update(item);item.panel.onchange=()=>update(item);
    }
    function parse(item){
        const baseline=item.client.draft.scheduling.solar,solar=copy(baseline),errors=[];
        for(const input of item.panel.querySelectorAll('input,select')){input.removeAttribute('aria-invalid');const old=item.panel.querySelector('#'+input.id+'-error');old?.remove();input.setAttribute('aria-describedby',input.getAttribute('aria-describedby')?.split(' ').filter(id=>!id.endsWith('-error')).join(' ')||'');}
        const invalid=(input,message)=>{
            input.setAttribute('aria-invalid','true');const error=el('div','form-text scheduling-field-error',message);error.id=input.id+'-error';input.after(error);input.setAttribute('aria-describedby',(input.getAttribute('aria-describedby')+' '+error.id).trim());
            const group=input.closest('details');if(group)group.open=true;errors.push(message);
        };
        const check=(input,message,apply)=>{const probe=copy(baseline);apply(probe);if(!validSolar(item,probe))invalid(input,message);};
        solar.enabled=item.inputs.solar.checked;solar.location.locator=item.inputs.locator.value||null;
        check(item.inputs.locator,'Target-site locator: use 4, 6 or 8 valid Maidenhead characters, or leave blank.',probe=>probe.location.locator=solar.location.locator);
        for(const p of item.periods){const value=solar[p.name],label=p.name[0].toUpperCase()+p.name.slice(1);if(Object.hasOwn(value,'enabled')){
            value.enabled=p.toggle.checked;
            const number=input=>{
                if(!/^-?(?:0|[1-9][0-9]*)(?:\.[0-9]{1,3})?$/.test(input.value)||Math.abs(Number(input.value))>90){invalid(input,label+': use elevation degrees from −90 through +90, with up to three decimals.');return null;}
                return Math.round(Number(input.value)*1000);
            };
            value.lower_mdeg=number(p.lower);value.upper_mdeg=number(p.upper);
            if(value.lower_mdeg!==null&&value.upper_mdeg!==null&&value.lower_mdeg>=value.upper_mdeg){invalid(p.lower,label+': lower elevation must be below upper elevation.');invalid(p.upper,label+': upper elevation must be above lower elevation.');}
        }else value.allow=p.toggle.checked;
            value.wspr_override=p.wspr.value.trim()?{...(copy(p.original.wspr_override)||{'Frequency Profile':p.profile.value,'Band Preferences':copy(item.client.draft.configuration.WSPR['Band Preferences'])}),Frequency:p.wspr.value,'Frequency Profile':p.profile.value}:null;
            check(p.wspr,label+': enter a supported WSPR list override, or leave blank to inherit.',probe=>probe[p.name].wspr_override=value.wspr_override);
            value.cw_base_hz=p.cw.value||null;
            check(p.cw,label+': use positive CW base Hz with up to eight decimal places and no trailing fractional zeros, or leave blank.',probe=>probe[p.name].cw_base_hz=value.cw_base_hz);
        }
        if(errors.length)throw Error(errors[0]);return solar;
    }
    function update(item){
        if(selected!==item||item.busy||item.client.paused)return;item.changed=true;item.review=false;
        try{item.client.setSolar(parse(item));item.valid=true;item.changed=item.client.dirty;tell(item,'Unsaved scheduling draft. Scheduling permission has not changed.');}
        catch(error){item.valid=false;tell(item,error.message&&error.code===undefined?error.message:'Correct the scheduling values before saving. Disabled periods are validated too.');}
        item.panel.setAttribute('aria-invalid',String(!item.valid));render();
    }
    async function run(action){const item=selected;if(!item||item.busy)return;const requestEpoch=epoch;item.busy=true;item.requestEpoch=requestEpoch;render();try{await action(item);}catch(error){tell(item,({capacity:'The device is busy. Your draft and earlier request are retained. Read or check again.',management_busy:'A request is still finishing. Keep this draft and check again.',candidate_invalid:'The target refused this policy. Correct the retained draft; disabled periods are validated too.',unsupported_resource:'This target cannot currently write the selected scheduling resource. Its draft is retained.',target_context_unavailable:'This target has no verified current scheduling context. Read or reconnect before editing.',unsaved_schedule:'Save and read back this draft before enabling. No enable was sent.',revision_conflict:'Saved values changed. Your draft is retained. Discard only after reviewing it.',authority_pending:'Scheduling ownership is not ready. Complete the Maintenance ownership workflow.',connection_changed:'Connection changed. Your draft is retained. Reconnect the selected device.'})[error.code]||'This request is refused or unconfirmed. Keep the draft and check the earlier scheduling request.');}finally{item.busy=false;if(requestEpoch!==epoch||!available.has(item.client.device))item.client.pause();if(selected===item)render();}}
    async function read(item){
        await item.client.read();let status=null;try{status=await item.client.status();}catch(error){if(error.code==='connection_changed'){item.client.pause();throw error;}}
        if(selected!==item||item.requestEpoch!==epoch)return;
        if(item.valid&&!item.client.operation&&!item.client.dirty)item.changed=false;
        if(!item.changed)build(item);
        if(item.desired===undefined)item.desired=item.client.current.saved.scheduling.enabled;
        node('enabled').checked=item.desired;observe(item,status);tell(item,item.client.dirty?'Saved values read. Your unsaved scheduling draft is retained.':'Saved scheduling read. Active application and forecasts are separate observations.');
    }
    root.SchedulingEditor=Object.freeze({
        get state(){return selected?{device:selected.client.device,pending:!!selected.client.operation,paused:selected.client.paused,changed:selected.changed}:null;},
        retainedTargets({remoteOnly=false}={}){return [...entries.values()].filter(i=>(!remoteOnly||!i.local)&&(i.changed||i.client.dirty||i.client.operation)).map(i=>copy(i.target));},
        targetsChanged(targets){available=new Set(targets.map(t=>t.device_id));if(selected&&!available.has(selected.client.device)){selected.client.pause();tell(selected,'This target is unavailable. Its confirmed settings and draft are retained; read or reconnect before editing.');}render();},
        leave(){++epoch;if(selected)selected.client.pause();selected=null;},
        async select(target,local=false){
            const generation=++epoch;if(selected)selected.client.pause();let item=entries.get(target.device_id);
            if(!item){if(entries.size>=64){
                selected=null;node('fields').replaceChildren();node('identity').textContent=target.name+' · '+target.device_id+' · unavailable';node('summary').replaceChildren(el('dt','','Selected target status'),el('dd','','Unknown; this target has no editor context.'));
                for(const key of ['hold','action-feedback','review-values','preview-result'])node(key).textContent='';node('observation').replaceChildren();node('enabled').checked=false;
                node('feedback').textContent='Scheduling draft limit reached for this page. All 64 target entries are retained. Return to a retained target to resolve or copy needed drafts before reloading. No settings were sent.';render();return;
            }item={target:copy(target),client:root.PiTargetClient.create({device:target.device_id,local,base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'}),panel:el('div'),local,busy:false,desired:undefined,changed:false,valid:false,periods:[]};entries.set(target.device_id,item);}
            selected=item;node('fields').replaceChildren(item.panel);node('identity').textContent=target.name+' · '+target.device_id+' · '+(local?'local Pi':'paired TLS');node('feedback').textContent=item.feedback||'Reading target scheduling…';node('action-feedback').textContent='';node('preview-result').textContent='';node('observation').replaceChildren();node('summary').replaceChildren(el('dt','','Selected target status'),el('dd','','Unknown until a verified read completes.'));node('hold').textContent='';node('enabled').checked=item.desired??false;node('review-values').textContent='';item.review=false;render();
            if(!available.has(target.device_id)){item.requestEpoch=generation;item.client.pause();tell(item,'This Pi is unavailable. Its scheduling draft and earlier request are retained read-only. Selection does not reconnect or send settings.');render();return;}
            await run(read);if(generation!==epoch)item.client.pause();
        }
    });
    function init(){if(!node('enabled'))return;
        node('enabled').addEventListener('change',()=>{if(selected){selected.desired=node('enabled').checked;render();}});
        node('read').addEventListener('click',()=>void run(read));
        node('save').addEventListener('click',()=>void run(async item=>{if(!item.valid||item.client.current.saved.scheduling.enabled)throw Error('Disable scheduling before saving policy.');const result=await item.client.save();if(result.state==='committed'){item.changed=false;await read(item);tell(item,'Disabled policy saved and read back. Enable saved schedule is a separate action.');}else tell(item,'Policy save is unconfirmed or refused. Draft retained; check the earlier request.');}));
        node('enable').addEventListener('click',()=>void run(async item=>{if(item.changed||!item.valid||!item.desired)throw Error('Review saved scheduling first.');const result=await item.client.enable();if(result.state==='committed'){await read(item);tell(item,'Saved scheduling enabled. This does not turn on Transmit or change boot behavior.');}else tell(item,'Enable is unconfirmed or refused. Check the earlier request.');}));
        node('disable').addEventListener('click',()=>void run(async item=>{const result=await item.client.disable();if(result.state==='committed'){await read(item);tell(item,'Scheduling disabled. Accepted complete work may finish; Stop is separate.');}else tell(item,'Disable is unconfirmed or refused. Check the earlier request.');}));
        node('check').addEventListener('click',()=>void run(async item=>{const result=await item.client.check();if(['committed','rejected'].includes(result.state)){await read(item);tell(item,'Earlier result '+result.state+'. No next action was submitted. Review the retained draft.');}else tell(item,'The earlier request remains unknown. Read current scheduling for review.');}));
        node('preview').addEventListener('click',()=>{if(selected&&selected.valid)node('preview-result').textContent='Draft only: '+summary(parse(selected))+' Target-reported forecasts above describe saved execution, not this draft. No settings were sent.';});
        node('discard').addEventListener('click',()=>void run(async item=>{if((item.changed||item.client.dirty)&&!root.confirm('Discard this target’s scheduling draft and read saved settings?'))return;item.client=root.PiTargetClient.create({device:item.target.device_id,local:item.local,base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'});item.changed=false;item.desired=undefined;await read(item);}));
        node('read-review').addEventListener('click',()=>void run(async item=>{
            const requestEpoch=epoch,value=await item.client.readRecovery();
            if(selected!==item||requestEpoch!==epoch)return;
            const raw=[...item.panel.querySelectorAll('input,select')].map(input=>{
                const period=input.closest('details')?.querySelector('summary')?.textContent||'Solar policy',label=item.panel.querySelector('label[for="'+input.id+'"]')?.textContent||input.id;
                return period+' / '+label+': '+(input.type==='checkbox'?(input.checked?'on':'off'):input.value||'(blank)');
            }).join('\n');
            node('review-values').textContent=value.saved?('Current saved revision: '+value.revision+'\n'+policyText(value.saved)+'\n\nLast valid parsed draft:\n'+policyText(item.client.draft)+'\n\nRetained raw entries (including invalid values; editor above):\n'+raw):'Current output '+value.phase+'.';item.review=true;
        }));
        node('confirm-review').addEventListener('click',()=>void run(async item=>{await item.client.review();item.review=false;await read(item);tell(item,'Current scheduling reviewed. Historical outcome may remain unknown; your draft was not sent.');}));
    }
    if(root.document.readyState==='loading')root.document.addEventListener('DOMContentLoaded',init);else init();
})(typeof window==='undefined'?globalThis:window);

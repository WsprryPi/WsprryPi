// SPDX-License-Identifier: MIT
(function(root){
    'use strict';if(!root.document)return;
    const node=id=>root.document.getElementById('pi-target-'+id),control=root.WsprryControl;
    const entries=new Map(),privateKeys=new Set(['TLS CA File','TLS Client Certificate','TLS Client Key','TLS Server Identity']);
    let selected=null,available=new Set(),sequence=0,serial=0;
    const element=(tag,classes,text)=>{const item=root.document.createElement(tag);if(classes)item.className=classes;if(text!==undefined)item.textContent=text;return item;};
    function tell(text){if(node('feedback'))node('feedback').textContent=text;}
    function render(){
        if(!node('save'))return;
        const item=selected,client=item?.client,locked=root.ConfigurationRecovery?.active||root.PicoFirmwareSafety?.locked;
        const writable=!!client&&!client.paused&&!client.busy&&!item.busy&&!locked&&available.has(item.target.device_id);
        const invalid=!!item?.panel.querySelector('[aria-invalid="true"]');
        node('save').disabled=!writable||!client.dirty||invalid||!client.context?.capabilities.some(cap=>cap.resource==='pi-config'&&cap.write);
        node('refresh').disabled=!client||client.busy||item.busy||locked;
        node('check').hidden=!client?.operation;node('check').disabled=!client||client.busy||item.busy||locked;
        node('discard').hidden=!client?.dirty&&!invalid;node('discard').disabled=!client||client.busy||item.busy||!!client.operation||locked;
        node('review').hidden=!client?.operation;
        node('read-review').disabled=!client||client.busy||item.busy||locked;
        node('confirm-review').disabled=!item?.review||!client||client.busy||item.busy||locked;
        const observable=!!client&&!client.busy&&!item.busy&&!locked&&available.has(item.target.device_id);
        node('read-output').disabled=!observable;
        node('stop').disabled=!observable||!item.output||Date.now()-item.outputAt>=30000||!client.stopAvailable||client.operation?.resource==='stop';
        for(const input of item?.panel.querySelectorAll('input,select,textarea')||[])input.disabled=!writable||input.dataset.piOptionalDisabled==='1';
        node('fields').setAttribute('aria-busy',String(!!item?.busy||!!client?.busy));root.SetupTargets?.editorChanged?.();
    }
    function field(item,key,rule,value,parent,onChange){
        if(value&&typeof value==='object'&&!Array.isArray(value)){
            if(!(rule.required||[]).length&&Object.keys(rule.properties||{}).length){
                const details=element('details','mt-3'),summary=element('summary','h6',key),values=structuredClone(value);
                details.append(summary,element('p','form-text','Optional band overrides. Use a qualified preset or an integral frequency in Hz. Unchecked bands keep their normal defaults.'));parent.append(details);
                for(const child of Object.keys(rule.properties)){
                    const row=element('div','mb-3'),check=element('input','form-check-input me-2'),label=element('label','form-label'),input=element('input','form-control'),message=element('div','form-text');
                    const id='pi-remote-optional-'+(++serial);check.type='checkbox';check.id=id;check.checked=Object.hasOwn(values,child);label.htmlFor=id;label.append(check,root.document.createTextNode('Override '+child));
                    input.id=id+'-value';input.type='text';input.autocomplete='off';input.value=check.checked?String(values[child]):'';input.dataset.piOptionalDisabled=check.checked?'0':'1';input.setAttribute('aria-label',child+' preset or frequency in Hz');message.id=id+'-feedback';input.setAttribute('aria-describedby',message.id);row.append(label,input,message);details.append(row);
                    function update(){
                        if(selected!==item||item.client.paused||item.busy)return;
                        const next=structuredClone(values);
                        try{
                            if(check.checked){
                                const text=input.value.trim();if(!text)throw Error('value_required');
                                const parsed=/^[1-9][0-9]*$/.test(text)?Number(text):text;
                                if(typeof parsed==='number'&&!Number.isSafeInteger(parsed))throw Error('invalid_frequency');next[child]=parsed;
                            }else delete next[child];
                            onChange(next);for(const name of Object.keys(values))delete values[name];Object.assign(values,next);
                            input.setAttribute('aria-invalid','false');message.textContent='Unsaved edit.';
                        }catch(_){input.setAttribute('aria-invalid','true');message.textContent='Enter a preset or an integral frequency in Hz before saving.';}
                        input.dataset.piOptionalDisabled=check.checked?'0':'1';item.output=null;render();
                    }
                    check.addEventListener('change',update);input.addEventListener('input',update);
                }return;
            }
            const group=element('fieldset','mt-3'),legend=element('legend','h6',key);group.append(legend);parent.append(group);
            const values=structuredClone(value);
            for(const [child,current]of Object.entries(value)){
                const childRule=rule.properties?.[child];if(!childRule)continue;
                field(item,child,childRule,current,group,next=>{values[child]=next;onChange(structuredClone(values));});
            }return;
        }
        const variants=rule.anyOf||[rule],kind=variants.find(v=>v.type===typeof value)||variants.find(v=>v.type===(Array.isArray(value)?'array':'integer'))||rule;
        const row=element('div','mb-3'),label=element('label','form-label',key),input=element(kind.enum||typeof value==='boolean'?'select':'input',kind.enum||typeof value==='boolean'?'form-select':'form-control');
        const id='pi-remote-field-'+(++serial),message=element('div','form-text','Saved value loaded.');label.htmlFor=id;input.id=id;message.id=id+'-feedback';input.setAttribute('aria-describedby',message.id);input.autocomplete='off';
        let values=null;
        if(input.tagName==='SELECT'){
            values=kind.enum||[true,false];for(const [index,option]of values.entries()){const choice=element('option','',typeof option==='boolean'?option?'Yes':'No':String(option));choice.value=String(index);input.append(choice);}input.value=String(values.findIndex(v=>v===value));
        }else{
            const numeric=typeof value==='number';input.type=numeric?'number':'text';input.value=Array.isArray(value)?value.join(' '):String(value??'');
            if(numeric){input.step=kind.type==='integer'?'1':'any';if(kind.minimum!==undefined)input.min=kind.minimum;if(kind.maximum!==undefined)input.max=kind.maximum;}
            if(kind.maxLength)input.maxLength=kind.maxLength;
        }
        input.addEventListener(input.tagName==='SELECT'?'change':'input',()=>{
            if(selected!==item||item.client.paused||item.busy)return;
            try{
                if(input.type==='number'&&(!input.value||!input.checkValidity()))throw Error('invalid');
                const next=values?values[Number(input.value)]:input.type==='number'?Number(input.value):Array.isArray(value)?input.value.trim().split(/\s+/).filter(Boolean):input.value;
                onChange(next);message.textContent='Unsaved edit.';input.setAttribute('aria-invalid','false');
            }catch(_){message.textContent='This value is not supported. Correct it before saving.';input.setAttribute('aria-invalid','true');}
            item.output=null;render();
        });
        row.append(label,input,message);parent.append(row);
    }
    function build(item){
        item.panel.replaceChildren();const draft=item.client.draft;if(!draft)return;
        for(const [section,values]of Object.entries(draft.configuration)){
            const group=element('details','py-3 border-bottom'),title=element('summary','h5 mb-0',section),fields=element('div','mt-3');group.dataset.piSection=section;group.open=['Operation','WSPR'].includes(section);group.append(title,fields);item.panel.append(group);
            for(const [key,value]of Object.entries(values)){
                if(section==='WTP'&&privateKeys.has(key))continue;
                const rule=control.controllerPiConfigurationField(section,key);if(!rule)continue;
                field(item,key,rule,value,fields,next=>item.client.set(section,key,next));
            }
        }
    }
    async function run(action){
        const item=selected;if(!item||item.busy)return;item.busy=true;render();
        try{await action(item);}
        catch(error){if(selected===item)tell(({revision_conflict:'Saved settings or the connection changed. Your draft is retained. Review it before discarding or reconnecting.',connection_changed:'The connection changed. Your draft is retained; read saved settings before another change.',candidate_invalid:'Settings were refused. Your draft is retained.',operation_unknown:'No matching receipt is available. Review current settings before another change.'})[error.code]||'The request is not confirmed. Your draft is retained. Check the earlier request or read saved settings.');}
        finally{item.busy=false;if(selected===item)render();}
    }
    async function refresh(item){
        const invalid=!!item.panel.querySelector('[aria-invalid="true"]'),previous=item.client.context,revision=item.client.current?.revision;
        const scope=value=>value?JSON.stringify({binding:value.binding,capability_revision:value.capability_revision,pair:value.authorization?.pair_id,generation:value.authorization?.authorization_generation}):null;
        await item.client.read();item.output=null;item.review=null;
        if(invalid&&previous&&(scope(previous)!==scope(item.client.context)||revision!==item.client.current?.revision)){
            item.client.pause();throw Object.assign(Error('revision_conflict'),{code:'revision_conflict'});
        }
        if(selected===item){if(!invalid)build(item);tell(invalid?'Your invalid entries are retained. Correct them before saving.':item.client.dirty?'Saved settings read. Your unsaved draft is retained.':'Saved settings read. Changes require Save.');}
    }
    root.PiTargetEditors=Object.freeze({
        get state(){return selected?{device:selected.target.device_id,paused:selected.client.paused,pending:!!selected.client.operation}:null;},
        showTab(index){if(!selected)return;const sections=[['WSPR','CW'],['GPIO','Calibration','Si5351','Band GPIO'],['Operation','WTP','WTP Server']][index]||[];for(const group of selected.panel.children)group.hidden=!sections.includes(group.dataset.piSection);},
        retainedTargets(){return [...entries.values()].filter(item=>item.client.dirty||item.client.operation||item.panel.querySelector('[aria-invalid="true"]')).map(item=>structuredClone(item.target));},
        leave(){++sequence;if(selected)selected.client.pause();selected=null;render();},
        targetsChanged(targets){available=new Set(targets.map(target=>target.device_id));if(selected&&!available.has(selected.target.device_id)){selected.client.pause();tell('This paired Pi is unavailable. Its draft is retained.');}render();},
        async select(target){
            const captured=++sequence;if(selected)selected.client.pause();
            let item=entries.get(target.device_id);
            if(!item){if(entries.size>=64){
                selected=null;node('fields').replaceChildren();node('identity').textContent=target.name+' · '+target.device_id+' · unavailable';node('output').textContent='';node('review-values').replaceChildren();node('discard-confirmation').hidden=true;
                tell('Settings draft limit reached for this page. All 64 target entries are retained. Return to a retained target to resolve or copy needed drafts before reloading. No settings were sent.');render();return;
            }item={target:structuredClone(target),client:root.PiTargetClient.create({device:target.device_id,base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'}),panel:element('div'),busy:false,output:null,review:null};entries.set(target.device_id,item);}
            selected=item;node('fields').replaceChildren(item.panel);node('identity').textContent=target.name+' · '+target.device_id;node('review-values').replaceChildren();node('output').textContent='';node('discard-confirmation').hidden=true;
            if(!available.has(target.device_id)){item.client.pause();tell('This paired Pi is unavailable. Its draft is retained.');render();return;}
            await run(async current=>{await refresh(current);if(captured!==sequence)current.client.pause();});
        }
    });
    function init(){
        if(!node('save'))return;
        node('save').addEventListener('click',()=>void run(async item=>{const result=await item.client.save();if(selected!==item)return;tell(result.state==='committed'?'Settings save confirmed. Read saved settings to continue.':result.state==='rejected'?'Settings were refused. Your draft is retained.':'Settings save is unconfirmed. Check the earlier request.');item.output=null;}));
        node('refresh').addEventListener('click',()=>void run(refresh));
        node('check').addEventListener('click',()=>void run(async item=>{const result=await item.client.check();if(selected===item)tell(result.state==='committed'?'Earlier request confirmed. Read saved settings to continue.':result.state==='rejected'?'Earlier request was refused. Your draft is retained.':'Earlier request remains unconfirmed. Read current settings for review.');}));
        node('discard').addEventListener('click',()=>{node('discard-confirmation').hidden=false;node('discard-no').focus();});
        node('discard-no').addEventListener('click',()=>{node('discard-confirmation').hidden=true;node('discard').focus();});
        node('discard-yes').addEventListener('click',()=>void run(async item=>{if(item.client.operation)throw Error('operation_pending');const replacement=root.PiTargetClient.create({device:item.target.device_id,base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'});await replacement.read();item.client=replacement;item.panel.replaceChildren();node('discard-confirmation').hidden=true;await refresh(item);}));
        node('read-review').addEventListener('click',()=>void run(async item=>{const value=await item.client.readRecovery();if(selected!==item)return;item.review=true;const container=node('review-values');container.replaceChildren();
            const rows=value.schema==='target-status'?[['Output phase',value.phase],['Output owner',value.observed_output.owner_id||'None'],['Active job',value.observed_output.job_id||'None'],['Effective Transmit',value.transmit_effective?'On':'Off']]:Object.entries(value.saved.configuration).flatMap(([section,values])=>Object.entries(values).filter(([key])=>section!=='WTP'||!privateKeys.has(key)).map(([key,v])=>[section+' · '+key,typeof v==='object'?JSON.stringify(v):String(v)]));
            for(const [key,value]of rows)container.append(element('dt','',key),element('dd','',String(value)));tell('Current saved values are shown for review. The earlier result remains unknown.');}));
        node('confirm-review').addEventListener('click',()=>void run(async item=>{const result=await item.client.review();item.review=null;if(selected===item){node('review-values').replaceChildren();tell(result.state==='earlier_terminal'?'Earlier request reviewed. This draft has not been sent. Read saved settings before continuing.':'Review confirmed. The earlier result remains unknown. Read saved settings before continuing.');}}));
        node('read-output').addEventListener('click',()=>void run(async item=>{const value=await item.client.readStop();if(selected!==item)return;item.output=value;item.outputAt=Date.now();node('output').textContent='Observed output: '+value.phase+'. Effective Transmit: '+(value.transmit_effective?'On':'Off')+'.';}));
        node('stop').addEventListener('click',()=>void run(async item=>{if(!item.output||Date.now()-item.outputAt>30000)throw Error('output_changed');const value=await item.client.stop(item.output);item.output=null;if(selected===item){const message=value.state==='committed'?(item.client.operation?'Stop was accepted. The earlier request is still unconfirmed. Read current output to verify inactivity.':'Stop was accepted. Read current output to verify inactivity.'):'Stop is unconfirmed. Check the earlier request.';node('output').textContent=message;tell(message);}}));
        render();
    }
    if(root.document.readyState==='loading')root.document.addEventListener('DOMContentLoaded',init);else init();
})(typeof window==='undefined'?globalThis:window);

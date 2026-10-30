// SPDX-License-Identifier: MIT
// Shared member-local editor. A save never tests GPIOs or starts a job.
(function(root) {
  'use strict';
  const control = root.WsprryControl || (typeof module !== 'undefined' && module.exports ? require('./control-contract.js') : null);
  if (!control) throw new Error('The control definitions could not be loaded. Reload the Pico page.');
  const descriptor = control.descriptors.filter_selector;
  const keys=(v,names)=>v && typeof v==='object' && !Array.isArray(v) && Object.keys(v).sort().join(',')===names.split(',').sort().join(',');
  const gp=n=>Number.isInteger(n) && (n>=0 && n<=22 || n>=26 && n<=28);
  const integer=(n,low,high)=>Number.isInteger(n) && n>=low && n<=high;
  const named=s=>typeof s==='string' && /^[A-Za-z0-9_/-](?:[A-Za-z0-9 _/-]{0,14}[A-Za-z0-9_/-])?$/.test(s);
  const supported=value=>control.supported('filter_selector',value);
  function validation(s,reserved=[]) {
    const bad=(message,fields=[])=>({message,fields});
    if(!keys(s,'policy,release_ms,settle_ms,selectors,ranges') || !Array.isArray(s.selectors) || s.selectors.length>5 || !Array.isArray(s.ranges) || s.ranges.length>10) return bad('Use at most five selectors and ten frequency ranges.');
    if(!['fixed_manual','required'].includes(s.policy))return bad('Choose fixed / manual filtering or required automatic selection.',['policy']);
    for(const key of ['release_ms','settle_ms'])if(!integer(s[key],0,250))return bad('Use whole release and settling milliseconds from 0 to 250.',[key]);
    for(let i=0;i<s.selectors.length;i++) {
      const o=s.selectors[i],path='selectors.'+i+'.';
      if(!keys(o,'id,name,gp,active_high') || !integer(o.id,1,5) || typeof o.active_high!=='boolean')return bad('A selector entry has an unsupported shape.');
      if(!named(o.name))return bad('Use a selector name of 1–16 ASCII letters, numbers, spaces, /, _ or -, without leading or trailing spaces.',[path+'name']);
      if(!gp(o.gp))return bad('Choose an exposed Pico GPIO: GP0–GP22 or GP26–GP28.',[path+'gp']);
      for(let j=0;j<i;j++) {
        const other=s.selectors[j],before='selectors.'+j+'.';
        if(other.id===o.id)return bad('Selector IDs must be unique.',[before+'name',path+'name']);
        if(other.name===o.name)return bad('Selector names must be unique.',[before+'name',path+'name']);
        if(other.gp===o.gp)return bad('Each GPIO can belong to only one selector.',[before+'gp',path+'gp']);
      }
      const owner=reserved.find(p=>p.gp===o.gp);
      if(owner)return bad(`GP${o.gp} is assigned to ${owner.role}. Read and change Radio pins before using it here.`,[path+'gp']);
    }
    for(let i=0;i<s.ranges.length;i++) {
      const r=s.ranges[i],path='ranges.'+i+'.';
      if(!keys(r,'name,selector,low_hz,high_hz'))return bad('A frequency range has an unsupported shape.');
      if(!named(r.name))return bad('Use a range name of 1–16 ASCII letters, numbers, spaces, /, _ or -, without leading or trailing spaces.',[path+'name']);
      if(!s.selectors.some(o=>o.id===r.selector))return bad('Choose an existing selector for this frequency range.',[path+'selector']);
      if(!integer(r.low_hz,1,1000000000))return bad('Enter a whole-Hz lower limit from 1 to 1,000,000,000.',[path+'low_hz']);
      if(!integer(r.high_hz,1,1000000000))return bad('Enter a whole-Hz upper limit from 1 to 1,000,000,000.',[path+'high_hz']);
      if(r.high_hz<r.low_hz)return bad('The upper frequency must be at least the lower frequency.',[path+'low_hz',path+'high_hz']);
      for(let j=0;j<i;j++) {
        const other=s.ranges[j],before='ranges.'+j+'.';
        if(other.name===r.name)return bad('Range names must be unique.',[before+'name',path+'name']);
        if(other.selector!==r.selector && r.low_hz<=other.high_hz && other.low_hz<=r.high_hz)return bad('Ranges for different selectors must not overlap, including their endpoints.',[before+'low_hz',before+'high_hz',path+'low_hz',path+'high_hz']);
      }
    }
    if(new TextEncoder().encode(JSON.stringify(s)).length>1536)return bad('Selector settings exceed 1,536 bytes. Shorten names or remove unused ranges.');
    return bad('');
  }
  function problem(s,reserved=[]) {return validation(s,reserved).message;}
  const identity=t=>keys(t,'scope,device_id,boot_id') && t.scope==='member' && ['device_id','boot_id'].every(k=>typeof t[k]==='string' && /^[0-9a-f]{32}$/.test(t[k]));
  const targetKey=t=>identity(t)?JSON.stringify([t.scope,t.device_id,t.boot_id]):'';
  const ordered=v=>Array.isArray(v)?v.map(ordered):v && typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,ordered(v[k])])):v;
  const same=(a,b)=>JSON.stringify(ordered(a))===JSON.stringify(ordered(b));
  function valid(v) {
    if(!keys(v,'schema,target,saved,boot_active,pending_restart,selected_id,selection_known,admission_error,output_active,rf_active,storage_healthy,reserved') ||
       v.schema!==descriptor.schema || !identity(v.target) || !Array.isArray(v.reserved) || v.reserved.length>26 ||
       v.reserved.some(o=>!keys(o,'gp,role') || !gp(o.gp) || !['rf','button','amplifier','indicator','i2c_sda','i2c_scl'].includes(o.role)) ||
       new Set(v.reserved.map(o=>o.gp)).size!==v.reserved.length ||
       ['pending_restart','selection_known','output_active','rf_active','storage_healthy'].some(k=>typeof v[k]!=='boolean') ||
       !(v.selected_id===null || integer(v.selected_id,1,5)) || v.rf_active && !v.output_active ||
       !v.selection_known && v.selected_id!==null || ![null,'filter_coverage_required'].includes(v.admission_error)) return false;
    if(v.saved!==null && problem(v.saved,v.reserved) || v.boot_active!==null && problem(v.boot_active)) return false;
    if(v.storage_healthy !== (v.saved!==null && v.boot_active!==null)) return false;
    if(v.saved && v.boot_active && !v.pending_restart && !same(v.saved,v.boot_active)) return false;
    if(v.selected_id!==null && (!v.selection_known || !v.output_active || !v.boot_active || v.boot_active.policy!=='required' || !v.boot_active.selectors.some(o=>o.id===v.selected_id))) return false;
    return true;
  }
  function mount(host,{exchange,run=fn=>fn(),expectedTarget=()=>null,afterSave=()=>{},onRead=()=>{},pi=false}={}) {
    if(!host) return null;
    const doc=host.ownerDocument;
    const node=(tag,text)=>{const n=doc.createElement(tag);if(text!==undefined)n.textContent=text;return n;};
    const field=(label,input)=>{const wrap=node('label',label);if(pi)wrap.className='form-label col-12 col-lg-4';wrap.append(input);return wrap;};
    const control=(tag,type)=>{const n=node(tag);if(type)n.type=type;if(pi)n.className=tag==='select'?'form-select':'form-control';return n;};
    const button=(text,primary=false)=>{const n=node('button',text);n.type='button';n.className=pi?'btn '+(primary?'btn-primary':'btn-outline-primary'):primary?'primary':'';return n;};
    const option=(text,value)=>{const n=node('option',text);n.value=String(value);return n;};
    const status=node('p','Automatic filter selection is unavailable until supported settings have been read.');status.setAttribute('role','status');status.setAttribute('aria-live','polite');
    const settings=node('fieldset'),legend=node('legend','LPF / band selector');settings.append(legend);
    const help=node('p','Declare the usable frequency range of each physical filter path. Declared coverage and GPIO readback do not measure filtering or relay position. Save, then restart to activate. Saving never switches a relay.');
    help.id=host.id+'-help';status.id=host.id+'-status';settings.setAttribute('aria-describedby',help.id+' '+status.id);
    const policy=control('select');policy.append(option('Fixed / manual filtering','fixed_manual'),option('Require an automatic selection','required'));
    const release=control('input','number'),settle=control('input','number');
    for(const input of [release,settle]) {input.min='0';input.max='250';input.step='1';}
    const fields=node('div');fields.className=pi?'row g-3':'fields';
    fields.append(field('Selection policy',policy),field('Release old selection (ms)',release),field('Settle new selection (ms)',settle));
    const timingHelp=node('p','Release and settling each accept 0–250 whole milliseconds (20 ms defaults). Both finish before the independent amplifier lead. Required selection refuses a complete job without one suitable path. Fixed / manual filtering leaves all configured selectors inactive and provides no automatic coverage check.');
    const outputList=node('div'),rangeList=node('div'),addOutput=button('Add selector'),addRange=button('Add frequency range');
    const outputTitle=node('h4','Selector outputs'),rangeTitle=node('h4','Declared frequency ranges');
    const read=button('Read saved selectors'),save=button('Save selectors',true),discard=button('Discard selector entries'),adopt=button('Use entries for this Pico');adopt.hidden=true;
    const summary=node('p');
    summary.id=host.id+'-validation';summary.setAttribute('role','status');summary.setAttribute('aria-live','polite');summary.setAttribute('aria-atomic','true');
    const actions=node('div');if(pi){actions.className='d-flex flex-wrap gap-2 mt-3';addOutput.className+=' mb-3';addRange.className+=' mb-3';rangeTitle.className='mt-4';}actions.append(read,save,discard,adopt);
    settings.append(help,fields,timingHelp,outputTitle,outputList,addOutput,rangeTitle,rangeList,addRange,summary,actions);
    host.replaceChildren(status,settings);
    let outputs=[],ranges=[],readable=false,writable=false,busy=false,edited=false,review=false,epoch=0,observed=null,revision='',draftTarget='',unavailable=false;
    const number=input=>/^(0|[1-9][0-9]{0,9})$/.test(input.value)?Number(input.value):NaN;
    const draft=()=>({policy:policy.value,release_ms:number(release),settle_ms:number(settle),selectors:outputs.map(o=>({id:o.id,name:o.name.value,gp:number(o.gp),active_high:o.polarity.value==='true'})),ranges:ranges.map(r=>({name:r.name.value,selector:number(r.selector),low_hz:number(r.low),high_hz:number(r.high)}))});
    function render() {
      settings.hidden=!readable;
      settings.disabled=busy || !readable;
      for(const input of [policy,release,settle,...outputs.flatMap(o=>[o.name,o.gp,o.polarity,o.remove]),...ranges.flatMap(r=>[r.name,r.selector,r.low,r.high,r.remove])]) input.disabled=busy || !writable || !observed || !revision;
      read.disabled=busy || !readable;
      const check=validation(draft(),observed?.reserved||[]),error=check.message;
      save.disabled=busy || !writable || !edited || review || !revision || !observed?.storage_healthy || !!error;
      discard.disabled=busy || !observed?.saved || !revision;
      adopt.hidden=!review;adopt.disabled=discard.disabled || !writable;
      addOutput.disabled=busy || !writable || !observed || !revision || outputs.length>=5;
      addRange.disabled=busy || !writable || !observed || !revision || ranges.length>=10;
      const explanation=edited && error?error:observed?.reserved?.length?'Other saved pin owners: '+observed.reserved.map(o=>`GP${o.gp} (${o.role})`).join(', '):'';
      if(summary.textContent!==explanation)summary.textContent=explanation;
      summary.className=pi?'form-text mt-2 mb-3'+(edited && error?' text-danger':''):edited && error?'error':'';
      const invalid=new Set(edited?check.fields:[]),fields=new Map([['policy',policy],['release_ms',release],['settle_ms',settle]]);
      outputs.forEach((o,i)=>{fields.set('selectors.'+i+'.name',o.name);fields.set('selectors.'+i+'.gp',o.gp);fields.set('selectors.'+i+'.active_high',o.polarity);});
      ranges.forEach((r,i)=>{fields.set('ranges.'+i+'.name',r.name);fields.set('ranges.'+i+'.selector',r.selector);fields.set('ranges.'+i+'.low_hz',r.low);fields.set('ranges.'+i+'.high_hz',r.high);});
      for(const [path,input] of fields){input.setAttribute('aria-invalid',String(invalid.has(path)));input.setAttribute('aria-describedby',invalid.has(path)?summary.id:help.id);}
    }
    function changed() {edited=true;if(observed && !review)draftTarget=targetKey(observed.target);render();}
    function mappingChoices() {
      for(const r of ranges) {
        const selected=r.selector.value;
        const choices=outputs.map(o=>option(o.name.value||`Selector ${o.id}`,o.id));
        if(selected && !outputs.some(o=>String(o.id)===selected)) choices.push(option(`Missing selector ${selected}`,selected));
        r.selector.replaceChildren(...choices);r.selector.value=selected;
      }
    }
    function addSelector(value) {
      const id=value?.id ?? [1,2,3,4,5].find(id=>!outputs.some(o=>o.id===id));
      if(!id || outputs.length>=5)return;
      const row=node('fieldset'),title=node('legend',`Selector ${id}`),name=control('input','text'),pin=control('select'),polarity=control('select'),remove=button(`Remove selector ${id}`);
      name.value=value?.name||'';
      pin.append(option('Choose a Pico GPIO',''));
      for(let gp=0;gp<=28;gp++)if(gp<=22 || gp>=26){const owner=observed?.reserved?.find(o=>o.gp===gp),choice=option(`GP${gp}${owner?' — '+owner.role:''}`,gp);choice.disabled=!!owner;pin.append(choice);}
      pin.value=value?String(value.gp):'';polarity.append(option('Active-high','true'),option('Active-low','false'));polarity.value=String(value?.active_high??true);
      const group=node('div');group.className=pi?'row g-3':'fields';group.append(field('Selector name',name),field('Pico GPIO',pin),field('Polarity',polarity));if(pi)row.className='mt-3 mb-3';row.append(title,group,remove);outputList.append(row);
      const entry={id,name,gp:pin,polarity,remove};outputs.push(entry);
      remove.onclick=()=>{outputs=outputs.filter(o=>o!==entry);row.remove();mappingChoices();changed();addOutput.focus();};
      for(const input of [name,pin,polarity])input.oninput=()=>{mappingChoices();changed();};mappingChoices();
      return name;
    }
    function addMapping(value) {
      if(ranges.length>=10)return;
      const row=node('fieldset'),title=node('legend','Frequency range'),name=control('input','text'),selector=control('select'),low=control('input','number'),high=control('input','number'),remove=button('Remove range');
      name.value=value?.name||'';for(const input of [low,high]){input.min='1';input.max='1000000000';input.step='1';}
      low.value=value?String(value.low_hz):'';high.value=value?String(value.high_hz):'';
      selector.append(option('Choose a selector',value?.selector??''));selector.value=String(value?.selector??'');
      const group=node('div');group.className=pi?'row g-3':'fields';group.append(field('Range name',name),field('Selector',selector),field('From (Hz)',low),field('Through (Hz)',high));if(pi)row.className='mt-3 mb-3';row.append(title,group,remove);rangeList.append(row);
      const entry={name,selector,low,high,remove};ranges.push(entry);mappingChoices();
      remove.onclick=()=>{ranges=ranges.filter(r=>r!==entry);row.remove();changed();addRange.focus();};for(const input of [name,selector,low,high])input.oninput=changed;
      return name;
    }
    function fill(s) {policy.value=s.policy;release.value=String(s.release_ms);settle.value=String(s.settle_ms);outputs=[];ranges=[];outputList.replaceChildren();rangeList.replaceChildren();for(const o of s.selectors)addSelector(o);for(const r of s.ranges)addMapping(r);}
    function checked(r) {
      const expected=expectedTarget();
      if(!valid(r?.data) || typeof r.revision!=='string' || !/^"[0-9a-f]{64}"$/.test(r.revision) || expected && targetKey(expected)!==targetKey(r.data.target))throw Error('Selector readback is malformed or belongs to a different Pico or boot.');
      return r.data;
    }
    function observedMessage(v) {
      if(!v.storage_healthy)return 'Saved selector settings and current selection are unavailable. Recover configuration storage before transmitting.';
      const saved=v.saved?.policy==='required'?'automatic selection required':'fixed / manual filtering';
      const unmapped=v.saved.policy==='required' && (!v.saved.selectors.length || !v.saved.ranges.length)?'No selectable mapping is declared; RF jobs will be refused. ':'';
      const refusal=v.admission_error?' Last preparation refused: no declared selector covers every nominal and programmed RF frequency in that complete job.':'';
      return `Saved: ${saved}. ${unmapped}${v.pending_restart?'Restart required. ':'Active settings match this boot. '}${v.selection_known?(v.selected_id===null?'No selector currently active.':`Selector ${v.selected_id} active at readback.`):'Current selection is unknown.'}${refusal} GPIO observation is not a relay-position or RF measurement.`;
    }
    async function load() {
      if(read.disabled)return;busy=true;revision='';const ticket=++epoch;render();
      try {const r=await exchange('GET'),v=checked(r);if(ticket!==epoch)return;observed=v;revision=r.revision;
        if(!edited && v.saved){fill(v.saved);draftTarget=targetKey(v.target);}review=edited && draftTarget!==targetKey(v.target);
        status.textContent=observedMessage(v)+(edited?' Entries preserved.':'')+(review?' Review this Pico, then use the entries explicitly.':'');onRead(v);
      }catch(e){if(ticket===epoch){if(e.code==='filter_selector_unavailable'){unavailable=true;readable=false;}status.textContent=e.message;}}finally{if(ticket===epoch){busy=false;render();}}
    }
    async function commit() {
      if(save.disabled)return;const value=draft(),target=observed.target,match=revision,ticket=++epoch;busy=true;revision='';render();
      try {const result=await exchange('PUT',{schema:descriptor.schema,target,settings:value},match),v=checked(result),reply=await exchange('GET'),after=checked(reply);if(ticket!==epoch)return;
        if(targetKey(v.target)!==targetKey(target) || targetKey(after.target)!==targetKey(target) || result.revision!==reply.revision || [v.saved,after.saved].some(s=>!same(s,value)))throw Error('Saved selectors did not confirm the entries.');
        observed=after;revision=reply.revision;edited=review=false;draftTarget=targetKey(after.target);status.textContent='Selectors saved. '+observedMessage(after);onRead(after);await afterSave(reply);
      }catch(e){if(ticket===epoch)status.textContent=e.message+' Read saved selectors before another save; entries are preserved.';}finally{if(ticket===epoch){busy=false;render();}}
    }
    addOutput.onclick=()=>{const focus=addSelector();changed();focus?.focus();};addRange.onclick=()=>{const focus=addMapping();changed();focus?.focus();};
    for(const input of [policy,release,settle])input.oninput=changed;
    read.onclick=()=>run(load);save.onclick=()=>run(commit);discard.onclick=()=>{if(!observed?.saved)return;fill(observed.saved);edited=review=false;draftTarget=targetKey(observed.target);status.textContent=observedMessage(observed);render();};adopt.onclick=()=>{if(!observed || !revision || !writable)return;draftTarget=targetKey(observed.target);review=false;status.textContent='Entries bound to this Pico. Review them, then save explicitly.';render();};
    render();return {load,root:host,setAvailability(r,w=r){readable=!!r && !unavailable;writable=!!w;if(!readable)status.textContent='Automatic selector controls are unavailable on this firmware or connection.';render();},invalidate(){++epoch;busy=false;revision='';review=edited;unavailable=false;status.textContent='Selector observation unavailable. Entries preserved; read saved selectors before saving.';render();}};
  }
  root.WsprryFilters=Object.freeze({descriptor,supported,problem,valid,mount});
  if(typeof module==='object' && module.exports)module.exports=root.WsprryFilters;
})(globalThis);

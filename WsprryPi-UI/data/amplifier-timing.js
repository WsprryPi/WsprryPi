// MIT. Shared Pico/member editor; saving never operates an output.
(function(root) {
  'use strict';
  const descriptor={schema:'transmitter-amplifier-timing/1',path:'/api/v1/amplifier-timing',method:'PUT',units:'ms',minimum:0,maximum:1000,default_lead_ms:200,default_tail_ms:200,activation:'restart',scope:'complete_job'};
  const keys=(v,names)=>v && typeof v==='object' && !Array.isArray(v) && Object.keys(v).sort().join(',')===names.split(',').sort().join(',');
  const supported=v=>keys(v,Object.keys(descriptor).join(',')) && Object.entries(descriptor).every(([k,n])=>v[k]===n);
  const number=text=>/^(0|[1-9][0-9]{0,3})$/.test(text) && Number(text)<=1000 ? Number(text) : null;
  const timing=v=>keys(v,'lead_ms,tail_ms') && ['lead_ms','tail_ms'].every(k=>Number.isInteger(v[k]) && v[k]>=0 && v[k]<=1000);
  const valid=v=>keys(v,'schema,target,saved,boot_active,pending_restart,phase,output_active,rf_active,storage_healthy') &&
    v.schema===descriptor.schema && keys(v.target,'scope,device_id,boot_id') && v.target.scope==='member' &&
    ['device_id','boot_id'].every(k=>typeof v.target[k]==='string' && /^[0-9a-f]{32}$/.test(v.target[k])) &&
    (v.saved===null || timing(v.saved)) && (v.boot_active===null || timing(v.boot_active)) &&
    ['pending_restart','output_active','rf_active','storage_healthy'].every(k=>typeof v[k]==='boolean') &&
    ['inactive','waiting','lead','rf','tail','fault'].includes(v.phase) &&
    v.rf_active===(v.phase==='rf') && (!['lead','rf','tail'].includes(v.phase) || v.output_active) &&
    (!v.saved || !v.boot_active || v.pending_restart || v.saved.lead_ms===v.boot_active.lead_ms && v.saved.tail_ms===v.boot_active.tail_ms);
  function create({lead,tail,read,save,discard,adopt,status,exchange,afterSave=()=>{},run=fn=>fn(),expectedTarget=()=>null}) {
    let readable=false,writable=false,busy=false,observed=null,revision='',epoch=0,edited=false,draftTarget='',review=false;
    const identity=v=>JSON.stringify([v.target.scope,v.target.device_id,v.target.boot_id]);
    function render() {
      read.disabled=busy || !readable;
      lead.disabled=tail.disabled=busy || !readable || !observed || !revision;
      save.disabled=lead.disabled || !writable || !edited || review || !observed?.saved || !observed.storage_healthy || number(lead.value)===null || number(tail.value)===null;
      discard.disabled=busy || !observed?.saved || !revision;
      adopt.hidden=!review; adopt.disabled=discard.disabled;
      for(const input of [lead,tail]) input.setAttribute('aria-invalid',String(edited && number(input.value)===null));
    }
    function checked(r) {
      const expected=expectedTarget();
      if(!valid(r.data) || !r.revision || expected && identity(r.data)!==identity({target:expected}))
        throw Error('Amplifier timing readback is unsupported or belongs to a different Pico or boot.');
      return r.data;
    }
    function message(v) {
      const show=t=>t ? `${t.lead_ms} ms lead / ${t.tail_ms} ms tail` : 'unavailable';
      return `Saved: ${show(v.saved)}. Active for this boot: ${show(v.boot_active)}. Phase at readback: ${v.phase}.${v.pending_restart ? ' Restart the Pico to activate saved settings.' : ''}`;
    }
    async function load() {
      if(read.disabled) return;
      busy=true;revision='';const ticket=++epoch;render();
      try {
        const r=await exchange('GET'),v=checked(r);
        if(ticket!==epoch) return;
        observed=v;revision=r.revision;
        if(!edited && v.saved) {lead.value=String(v.saved.lead_ms);tail.value=String(v.saved.tail_ms);draftTarget=identity(v);}
        review=edited && draftTarget!==identity(v);
        status.textContent=message(v)+(edited?' Your entries are preserved.':'')+(review?' Review this Pico before using the entries.':'');
      } catch(e) {if(ticket===epoch)status.textContent=e.message;}
      finally {if(ticket===epoch){busy=false;render();}}
    }
    async function commit() {
      if(save.disabled) return;
      const target=observed.target,value={lead_ms:number(lead.value),tail_ms:number(tail.value)},match=revision,ticket=++epoch;
      busy=true;revision='';render();
      try {
        const result=await exchange('PUT',{schema:descriptor.schema,target,timing:value},match),v=checked(result);
        const confirmed=await exchange('GET'),after=checked(confirmed);
        if(ticket!==epoch) return;
        if(identity(v)!==identity({target}) || identity(after)!==identity(v) || result.revision!==confirmed.revision ||
           !timing(v.saved) || !timing(after.saved) || [v.saved,after.saved].some(t=>t.lead_ms!==value.lead_ms || t.tail_ms!==value.tail_ms))
          throw Error('Saved timing did not confirm the entries.');
        observed=after;revision=confirmed.revision;edited=review=false;draftTarget=identity(after);
        status.textContent='Amplifier timing saved. '+message(after);
        await afterSave(confirmed);
      } catch(e) {if(ticket===epoch)status.textContent=e.message+' Read saved timing before another save; your entries are preserved.';}
      finally {if(ticket===epoch){busy=false;render();}}
    }
    for(const input of [lead,tail]) input.oninput=()=>{edited=true;if(observed && !review)draftTarget=identity(observed);render();};
    discard.onclick=()=>{if(discard.disabled)return;lead.value=String(observed.saved.lead_ms);tail.value=String(observed.saved.tail_ms);edited=review=false;draftTarget=identity(observed);render();};
    adopt.onclick=()=>{if(adopt.disabled)return;draftTarget=identity(observed);review=false;render();};
    read.onclick=()=>run(load);save.onclick=()=>run(commit);render();
    return {load,setAvailability(canRead,canWrite=canRead){readable=canRead;writable=canWrite;render();},invalidate(){++epoch;revision='';observed=null;busy=false;render();}};
  }
  root.WsprryAmplifierTiming={supported,valid,number,create};
  if(typeof module!=='undefined')module.exports=root.WsprryAmplifierTiming;
})(globalThis);

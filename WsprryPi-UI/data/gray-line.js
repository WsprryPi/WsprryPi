// SPDX-License-Identifier: MIT
(function(root) {
  'use strict';
  const object = v => v !== null && typeof v === 'object' && !Array.isArray(v);
  const keys = (v,names) => object(v) && Object.keys(v).sort().join(',') === names.split(',').sort().join(',');
  const integer = (v,lo,hi) => Number.isInteger(v) && v >= lo && v <= hi;
  const decimal = v => typeof v === 'string' && /^[1-9][0-9]{0,19}$/.test(v) && BigInt(v) <= 18446744073709551615n;
  const defaults = () => ({location:'manual',morning:{enabled:false,lower_mdeg:-6000,upper_mdeg:6000,wspr_dial_frequency_hz:null,message_frequency_nhz:null},evening:{enabled:false,lower_mdeg:-6000,upper_mdeg:6000,wspr_dial_frequency_hz:null,message_frequency_nhz:null}});
  function valid(v) {
    return keys(v,'location,morning,evening') && ['manual','gps'].includes(v.location) && ['morning','evening'].every(name => {
      const p=v[name];
      return keys(p,'enabled,lower_mdeg,upper_mdeg,wspr_dial_frequency_hz,message_frequency_nhz') && typeof p.enabled==='boolean' &&
        integer(p.lower_mdeg,-90000,89999) && integer(p.upper_mdeg,-89999,90000) && p.lower_mdeg<p.upper_mdeg &&
        (p.wspr_dial_frequency_hz===null || integer(p.wspr_dial_frequency_hz,6503,159998496)) &&
        (p.message_frequency_nhz===null || decimal(p.message_frequency_nhz));
    });
  }
  function readback(v,target) {
    if(object(v) && v.contract==='Wsprry-Control/1') {v={...v};delete v.contract;}
    return keys(v,'schema,target,saved,storage_healthy,runtime') && v.schema==='gray-line' &&
      keys(v.target,'scope,device_id,boot_id') && v.target.scope==='member' &&
      /^[0-9a-f]{32}$/.test(v.target.device_id) && /^[0-9a-f]{32}$/.test(v.target.boot_id) &&
      (!target || v.target.device_id===target.device_id && v.target.boot_id===target.boot_id) &&
      typeof v.storage_healthy==='boolean' && (v.saved===null || valid(v.saved)) &&
      (v.storage_healthy || v.saved===null) && keys(v.runtime,'selection,prolonged_window') && typeof v.runtime.prolonged_window==='boolean' &&
      ['disabled','ordinary','morning','evening','waiting_for_location','waiting_for_time'].includes(v.runtime.selection);
  }
  function sameSettings(a,b) {
    return valid(a) && valid(b) && a.location===b.location &&
      ['morning','evening'].every(name=>Object.keys(a[name]).every(key=>a[name][key]===b[name][key]));
  }
  let sequence=0;
  function mount(container,{exchange,expectedTarget=()=>null,bootstrap=false,run=fn=>fn(),afterSave=()=>{},onError=()=>{}}) {
    if(!container)return null;
    const id=`gray-line-${++sequence}`;
    container.innerHTML=`<fieldset><legend>Gray-line scheduling</legend><p id="${id}-help">Pico standalone scheduling only. Morning and evening override day/night selection within their solar windows. Missing required time or location holds new occurrences. Blank frequencies use the existing saved values; saving does not enable the main schedule.</p><div class="fields"><label>Schedule location<select data-gray="location" aria-describedby="${id}-help"><option value="manual">Saved locator center</option><option value="gps">Fresh GPS position</option></select></label></div>${['morning','evening'].map(name=>`<fieldset><legend>${name==='morning'?'Morning (rising Sun)':'Evening (descending Sun)'}</legend><label class="${bootstrap?'form-check d-flex align-items-center gap-2':'check'}"><input data-gray="${name}.enabled" type="checkbox"> Enable ${name} gray-line period</label><div class="fields">${[['lower_mdeg','Lower elevation (degrees)'],['upper_mdeg','Upper elevation (degrees)'],['wspr_dial_frequency_hz','WSPR dial (whole Hz, optional)'],['message_frequency_nhz','Keyed-message Mark (Hz, optional)']].map(([key,label])=>`<label>${label}<input data-gray="${name}.${key}" type="text" inputmode="${key==='wspr_dial_frequency_hz'?'numeric':'decimal'}" aria-describedby="${id}-help ${id}-status"></label>`).join('')}</div></fieldset>`).join('')}</fieldset><p id="${id}-status" role="status" aria-live="polite">Read saved gray-line settings to begin.</p><div class="buttons"><button type="button" data-action="read">Read saved gray-line settings</button><button type="button" data-action="save">Save gray-line settings</button><button type="button" data-action="discard">Discard gray-line entries</button><button type="button" data-action="adopt" hidden>Use entries for this Pico</button></div>`;
    const fieldset=container.querySelector('fieldset'),status=container.querySelector('[role="status"]');
    const inputs=[...container.querySelectorAll('[data-gray]')];
    const button=name=>container.querySelector(`[data-action="${name}"]`);
    for(const b of container.querySelectorAll('[data-action]'))b.id=`${id}-${b.dataset.action}`;
    let readable=false,writable=false,busy=false,observed=null,revision='',dirty=false,review=false,draftTarget='',epoch=0;
    const identity=v=>v.target.device_id+':'+v.target.boot_id;
    function fill(settings) {
      for(const input of inputs) {
        const parts=input.dataset.gray.split('.'), value=parts.length===1?settings.location:settings[parts[0]][parts[1]];
        if(input.type==='checkbox')input.checked=value;
        else if(parts[1]==='lower_mdeg'||parts[1]==='upper_mdeg')input.value=String(value/1000);
        else if(parts[1]==='message_frequency_nhz') input.value=value===null?'':`${BigInt(value)/1000000000n}.${String(BigInt(value)%1000000000n).padStart(9,'0')}`.replace(/\.?0+$/,'');
        else input.value=value===null?'':String(value);
      }
    }
    function draft() {
      const value=defaults();
      const signed=/^-?(0|[1-9][0-9]*)(\.[0-9]{1,3})?$/;
      for(const input of inputs) {
        const [name,key]=input.dataset.gray.split('.');
        if(!key){value.location=input.value;continue;}
        let parsed;
        if(key==='enabled')parsed=input.checked;
        else if(key==='lower_mdeg'||key==='upper_mdeg') {
          if(!signed.test(input.value))throw Error('Enter elevation between −90 and +90 degrees, with up to three decimal places.');
          parsed=Math.round(Number(input.value)*1000);
        } else if(key==='wspr_dial_frequency_hz') {
          if(input.value!=='' && !/^[1-9][0-9]*$/.test(input.value))throw Error('Enter a whole-Hz WSPR dial, or leave it blank.');
          parsed=input.value===''?null:Number(input.value);
        } else {
          if(input.value==='')parsed=null;
          else {
            if(!/^(0|[1-9][0-9]*)(\.[0-9]{1,9})?$/.test(input.value))throw Error('Enter a positive Mark frequency in Hz with up to nine decimal places, or leave it blank.');
            const [whole,fraction='']=input.value.split('.');parsed=String(BigInt(whole)*1000000000n+BigInt(fraction.padEnd(9,'0')));
          }
        }
        value[name][key]=parsed;
      }
      if(!valid(value))throw Error('Each lower elevation must be less than its upper elevation. Check the frequency limits and positive Mark frequency.');
      return value;
    }
    function render() {
      fieldset.disabled=!readable||busy||!observed||!revision;
      button('read').disabled=!readable||busy;
      let validation='';try{draft();}catch(e){validation=e.message;}
      button('save').disabled=fieldset.disabled||!writable||!dirty||review||Boolean(validation);
      button('discard').disabled=busy||!observed;
      button('adopt').hidden=!review;button('adopt').disabled=busy||!observed||!revision;
      for(const input of inputs)input.setAttribute('aria-invalid',String(dirty&&Boolean(validation)));
      if(dirty&&validation)status.textContent=validation;
    }
    async function read() {
      if(!readable||busy)return;
      const ticket=++epoch;busy=true;revision='';render();
      try {
        const r=await exchange('GET');
        if(ticket!==epoch)return;
        if(!r.revision||!readback(r.data,expectedTarget())||r.data.storage_healthy!==true||r.data.saved===null)throw Error('Gray-line settings are unavailable or belong to another Pico. Check station setup and read again.');
        observed=r.data;revision=r.revision;
        if(!dirty){fill(observed.saved);draftTarget=identity(observed);}
        review=dirty && draftTarget!==identity(observed);
        status.textContent=`Current solar selection: ${observed.runtime.selection.replaceAll('_',' ')}.${observed.runtime.prolonged_window?' The daily solar range stays inside this window.':''} ${dirty?'Your entries are preserved. ':''}${review?'Review this Pico, then choose Use entries for this Pico.':''}`;
      }catch(e){if(ticket===epoch){status.textContent=e.message;onError(e);}}
      finally{busy=false;render();}
    }
    async function save() {
      if(button('save').disabled)return;
      const expected=draft(),target=observed.target,match=revision,ticket=++epoch;
      busy=true;revision='';render();
      try {
        const r=await exchange('PUT',{schema:'gray-line',target,settings:expected},match);
        if(ticket!==epoch)return;
        if(!r.revision||r.revision===match||!readback(r.data,target)||!sameSettings(r.data.saved,expected))throw Error('Saved gray-line settings were not confirmed.');
        observed=r.data;revision=r.revision;dirty=review=false;draftTarget=identity(observed);fill(observed.saved);
        status.textContent='Gray-line settings saved for future standalone occurrences. Schedule enablement is unchanged.';
        await afterSave();
      }catch(e){if(ticket===epoch){status.textContent=e.message+' Read saved settings before another save; your entries are preserved.';onError(e);}}
      finally{busy=false;render();}
    }
    container.addEventListener('input',e=>{if(e.target.matches('[data-gray]')){dirty=true;if(observed&&!review)draftTarget=identity(observed);status.textContent=review?'Review this Pico before saving your entries.':'Your gray-line entries are not saved.';render();}});
    button('read').onclick=()=>run(read);button('save').onclick=()=>run(save);
    button('discard').onclick=()=>{if(observed){fill(observed.saved);dirty=review=false;draftTarget=identity(observed);status.textContent='Entries restored from saved settings.';render();}};
    button('adopt').onclick=()=>{if(observed){review=false;draftTarget=identity(observed);render();}};
    if(bootstrap) {
      for(const field of inputs)field.classList.add(field.type==='checkbox'?'form-check-input':field.tagName==='SELECT'?'form-select':'form-control');
      for(const check of container.querySelectorAll('input[type=checkbox]'))check.classList.add('m-0','flex-shrink-0');
      for(const grid of container.querySelectorAll('.fields')){grid.classList.add('row','g-3');for(const label of grid.children)label.classList.add('col-md-6');}
      container.querySelector('.buttons').classList.add('d-flex','flex-wrap','gap-2');
      for(const b of container.querySelectorAll('button'))b.classList.add('btn',b.dataset.action==='save'?'btn-primary':'btn-outline-secondary');
    }
    fill(defaults());render();
    return {read,load:read,invalidate(){++epoch;revision='';readable=writable=false;status.textContent='Read gray-line settings again after reconnecting. Your entries are preserved.';render();},setAvailability(canRead,canWrite){if(readable&&!canRead){++epoch;revision='';}readable=canRead;writable=canWrite;render();}};
  }
  const api={defaults,valid,readback,mount};root.WsprryGrayLine=api;
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof globalThis!=='undefined'?globalThis:this);

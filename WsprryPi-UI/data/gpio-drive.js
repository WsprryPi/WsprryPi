// MIT. Source: WsprryPico P15.15; the companion Pi uses the same bounded editor.
(function (root) {
  'use strict';
  const control = root.WsprryControl || (typeof module !== 'undefined' && module.exports ? require('./control-contract.js') : null);
  if (!control) throw new Error('The control definitions could not be loaded. Reload the Pico page.');
  const values = [2,4,8,12];
  const keys = (v,names) => v && typeof v === 'object' && !Array.isArray(v) && Object.keys(v).sort().join(',') === names.split(',').sort().join(',');
  const descriptor = control.descriptors.gpio_drive;
  function supported(value) { return control.supported('gpio_drive',value); }
  function valid(v) {
    return keys(v,'schema,target,saved,boot_active,pad_readback,pending_restart,application_error,rf_gp,execution_engine,storage_healthy') &&
      v.schema === 'gpio-drive' && keys(v.target,'scope,device_id,boot_id') && v.target.scope === 'member' &&
      /^[0-9a-f]{32}$/.test(v.target.device_id) && /^[0-9a-f]{32}$/.test(v.target.boot_id) &&
      ['saved','boot_active','pad_readback'].every(k=>v[k] === null || values.includes(v[k])) &&
      typeof v.pending_restart === 'boolean' && typeof v.storage_healthy === 'boolean' && typeof v.execution_engine === 'string' &&
      [null,'pad_application_failed','output_disable_failed'].includes(v.application_error) &&
      (v.rf_gp === null || Number.isInteger(v.rf_gp) && (v.rf_gp <= 22 || v.rf_gp >= 26) && v.rf_gp >= 0 && v.rf_gp <= 28);
  }
  function create({select,read,save,discard,status,saveDraft,applyDraft,draftStatus,exchange,afterSave = ()=>{},onError = ()=>{},run = fn=>fn()}) {
    let progress = null, allowedValues=[...values];
    let available = false, busy = false, observed = null, revision = '', draftTarget = '', edited = false, review = false, serial = 0, epoch = 0;
    select.value = '';
    const identity = v => JSON.stringify(v.target);
    function render() {
      read.disabled = busy || !available;
      select.disabled = busy || !available || !observed || !revision || !observed.storage_healthy || !!observed.application_error;
      save.disabled = select.disabled || observed.saved === null || !edited || !allowedValues.includes(Number(select.value)) || draftTarget !== identity(observed);
      discard.disabled = busy || !observed || !revision;
      if (saveDraft) saveDraft.disabled = select.disabled || !edited || !allowedValues.includes(Number(select.value)) || draftTarget !== identity(observed) || !progress;
      if (applyDraft) applyDraft.disabled = select.disabled || !progress || !allowedValues.includes(progress.fields.gpio_drive_ma) || !progress.apply_domains.drive || draftTarget !== identity(observed);
      select.setAttribute('aria-invalid',String(!!select.value && !allowedValues.includes(Number(select.value))));
    }
    const summary = v => `Pico ${v.target.device_id} · RF GP${v.rf_gp ?? 'unavailable'}. Saved: ${v.saved === null ? 'not configured' : v.saved + ' mA'}. Boot selection: ${v.boot_active === null ? 'unknown' : v.boot_active + ' mA'}. Verified pad: ${v.pad_readback === null ? 'unavailable' : v.pad_readback + ' mA'}.${v.pending_restart ? ' Pending explicit restart; output remains inhibited.' : ''}${v.application_error ? ' ' + v.application_error + '.' : ''}`;
    function validProgress(v,target) {
      return v?.schema === 'progress' && JSON.stringify(v.target) === JSON.stringify(target) &&
        typeof v.draft_revision === 'string' && /^(0|[1-9][0-9]*)$/.test(v.draft_revision) &&
        v.fields && typeof v.fields === 'object' && (!Object.hasOwn(v.fields,'gpio_drive_ma') || values.includes(v.fields.gpio_drive_ma)) &&
        typeof v.apply_domains?.drive === 'boolean';
    }
    async function load() {
      if (busy || !available) return;
      busy = true; revision = ''; const ticket = ++epoch; render(); status.textContent = 'Reading GPIO drive strength…';
      try {
        const r = await exchange('GET');
        if (ticket !== epoch) return;
        if (!r.revision || !valid(r.data)) throw new Error('GPIO drive readback could not be verified.');
        const changed = draftTarget && draftTarget !== identity(r.data);
        let nextProgress = null;
        if (saveDraft) {
          const draft = await exchange('GET',undefined,undefined,'progress');
          if (ticket !== epoch) return;
          if (!validProgress(draft.data,r.data.target) || draft.revision !== r.revision) throw new Error('Saved drive progress could not be verified.');
          nextProgress = draft.data;
        }
        observed = r.data; revision = r.revision; progress = nextProgress;
        if (draftStatus) draftStatus.textContent = progress && Object.hasOwn(progress.fields,'gpio_drive_ma') ? `Saved drive progress: ${progress.fields.gpio_drive_ma} mA. Apply saved drive separately; saving progress never programs the pad.` : 'No drive progress saved.';
        if (!edited) { select.value = observed.saved === null ? (progress?.fields.gpio_drive_ma === undefined ? '' : String(progress.fields.gpio_drive_ma)) : String(observed.saved); draftTarget = identity(observed); }
        status.textContent = summary(observed) + (edited ? changed ? ' Your draft belongs to the previous Pico or boot. Discard it before editing this member.' : ' Your draft is preserved; compare it with saved readback before saving.' : '');
      } catch (e) { if (ticket === epoch) { onError(e); observed = null; revision = ''; status.textContent = 'GPIO drive unavailable or unconfirmed. ' + e.message + ' Your draft is preserved; read again before saving.'; } }
      finally { busy = false; render(); }
    }
    read.addEventListener('click',()=>run(load));
    select.addEventListener('change',()=>{ ++serial; edited = true; review = false; status.textContent = (observed ? summary(observed) : 'Readback unavailable.') + ' Unsaved edit. Save requires an explicit restart to activate.'; render(); });
    discard.addEventListener('click',()=>{ if (!observed || !revision) return; select.value = observed.saved === null ? (progress?.fields.gpio_drive_ma === undefined ? '' : String(progress.fields.gpio_drive_ma)) : String(observed.saved); draftTarget = identity(observed); edited = false; review = false; ++serial; status.textContent = summary(observed); render(); });
    save.addEventListener('click',()=>run(async()=>{
      if (save.disabled || busy) return;
      if (review && !root.confirm('Review saved drive beside your preserved draft. Send one new explicit save using this revision?')) return;
      const value = Number(select.value), target = observed.target, match = revision, sentSerial = serial, ticket = ++epoch;
      busy = true; revision = ''; review = true; render(); status.textContent = 'Saving GPIO drive strength…';
      try {
        const r = await exchange('PUT',{schema:'gpio-drive',target,gpio_drive_ma:value},match);
        if (ticket !== epoch) return;
        if (!r.revision || r.revision === match || !valid(r.data) || identity(r.data) !== JSON.stringify(target) || r.data.saved !== value) throw new Error('Save acknowledgement could not be verified.');
        observed = r.data; revision = r.revision; review = false;
        if (serial === sentSerial) edited = false;
        status.textContent = 'GPIO drive confirmed saved. ' + summary(observed) + (edited ? ' Newer draft preserved.' : ' Use the existing Restart device action to activate.');
        afterSave();
      } catch (e) { if (ticket === epoch) { onError(e); revision = ''; status.textContent = 'Save result unconfirmed or rejected. ' + e.message + ' Your draft is preserved. Read and review saved drive before another explicit save.'; } }
      finally { busy = false; render(); }
    }));
    async function progressAction(apply) {
      const button = apply ? applyDraft : saveDraft;
      if (!button || button.disabled || busy || !progress) return;
      if (review && !root.confirm('Review saved drive and progress beside your draft. Send one new explicit action using this revision?')) return;
      const target = observed.target, match = revision, value = Number(select.value), sentSerial = serial, ticket = ++epoch;
      const body = apply ? {schema:'progress',target,application:{draft_revision:progress.draft_revision,domain:'drive'}} :
        {schema:'progress',target,request_id:Array.from(root.crypto.getRandomValues(new Uint8Array(16)),b=>b.toString(16).padStart(2,'0')).join(''),fields:{gpio_drive_ma:value}};
      busy = true; revision = ''; review = true; render(); draftStatus.textContent = apply ? 'Applying saved drive progress…' : 'Saving drive progress…';
      let refresh = false;
      try {
        const result = await exchange('POST',body,match,(apply ? 'progress/apply' : 'progress/update'));
        if (ticket !== epoch) return;
        if (!result.revision || result.revision === match) throw new Error('Progress acknowledgement could not be verified.');
        if (!apply && (!validProgress(result.data,target) || result.data.request_id !== body.request_id || result.data.outcomes?.gpio_drive_ma !== 'saved' || result.data.fields.gpio_drive_ma !== value)) throw new Error('Drive progress save could not be verified.');
        if (apply && !(validProgress(result.data,target) || result.data.config?.gpio_drive_ma === progress.fields.gpio_drive_ma)) throw new Error('Drive application acknowledgement could not be verified.');
        review = false; if (serial === sentSerial) edited = !apply;
        draftStatus.textContent = apply ? 'Saved drive progress applied. Read GPIO drive and explicitly restart to activate changed hardware.' : `Drive progress confirmed saved: ${value} mA. It has not been applied.`;
        afterSave(); refresh = true;
      } catch(e) { onError(e); draftStatus.textContent = 'Progress result unconfirmed or rejected. '+e.message+' Your draft is preserved; read and review before another action.'; }
      finally {busy = false; render();}
      if (refresh) await load();
    }
    saveDraft?.addEventListener('click',()=>run(()=>progressAction(false)));
    applyDraft?.addEventListener('click',()=>run(()=>progressAction(true)));
    render();
    return {load,setPolicy(value) {
      allowedValues=supported(value)?[...value.values]:[];
      for(const option of select.options || []) if(option.value!=='')option.disabled=!allowedValues.includes(Number(option.value));
      render();
    },setAvailability(value) { if (!value && available) { ++epoch; revision = ''; review = true; status.textContent = 'Disconnected or target changed. Drive readback unknown; your draft is preserved.'; } available = value; render(); }, invalidate() { ++epoch; revision = ''; review = true; status.textContent = 'Target or saved revision changed. Read GPIO drive again; your original member-bound draft is preserved.'; render(); }};
  }
  root.WsprryGpioDrive = {create,valid,supported};
  if (typeof module !== 'undefined') module.exports = {create,valid,supported};
})(typeof window === 'undefined' ? globalThis : window);

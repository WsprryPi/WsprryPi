// SPDX-License-Identifier: MIT
(function(root) {
  'use strict';
  const fields = 'reference_hz,reference_source,crystal_load_pf,address,drive_ma,transmit_output,loopback_output,loopback_input_gp,rf_gate_gp,rf_gate_active_high,reference_use,trusted_uncertainty_ppb,manual_correction_ppb,automatic_calibration';
  const object = v => v !== null && typeof v === 'object' && !Array.isArray(v);
  const keys = (v,names) => object(v) && Object.keys(v).sort().join(',') === names.split(',').sort().join(',');
  const integer = (v,low,high) => Number.isInteger(v) && v >= low && v <= high;
  const gp = v => integer(v,0,22) || integer(v,26,28);
  const decimal = v => typeof v === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(v) && BigInt(v) <= 18446744073709551615n;
  const pairs = [[0,1],[4,5],[8,9],[12,13],[16,17],[20,21],[2,3],[6,7],[10,11],[14,15],[18,19],[26,27]];
  function valid(v) {
    if (!keys(v,fields) || ![25000000,27000000].includes(v.reference_hz) || v.address !== 96 ||
        ![2,4,6,8].includes(v.drive_ma) || ![0,1,2].includes(v.transmit_output) ||
        ![null,0,1,2].includes(v.loopback_output) || v.transmit_output === v.loopback_output ||
        typeof v.rf_gate_active_high !== 'boolean' || typeof v.automatic_calibration !== 'boolean' || v.automatic_calibration && v.loopback_output===null ||
        !integer(v.manual_correction_ppb,-100000,100000)) return false;
    if (v.reference_source === 'crystal' ? ![6,8,10].includes(v.crystal_load_pf)
        : v.reference_source !== 'external_xa' || v.crystal_load_pf !== null) return false;
    if (v.loopback_output === null ? v.loopback_input_gp !== null || v.reference_use !== 'measurement_only'
        : !gp(v.loopback_input_gp) || v.loopback_input_gp % 2 !== 1) return false;
    if (v.rf_gate_gp !== null && (!gp(v.rf_gate_gp) || v.rf_gate_gp === v.loopback_input_gp)) return false;
    return v.reference_use === 'trusted_local' ? integer(v.trusted_uncertainty_ppb,1,100000)
        : v.reference_use === 'measurement_only' && v.trusted_uncertainty_ppb === null;
  }
  function allocation(v,p,selectors=[]) {
    if (!valid(v) || !object(p) || p.engine !== 'i2c' || p.rf_gp !== null ||
        !integer(p.i2c_pair,0,11) || !Array.isArray(p.lpf_gps) || p.lpf_gps.length || !Array.isArray(selectors)) return false;
    const used = new Set();
    for (const pin of [...pairs[p.i2c_pair],p.button_gp,p.amplifier_gp,
        p.indicator === 'external' ? p.indicator_gp : null,v.loopback_input_gp,v.rf_gate_gp,...selectors.map(s=>s.gp)]) {
      if (pin === null) continue;
      if (!gp(pin) || used.has(pin)) return false;
      used.add(pin);
    }
    return true;
  }
  function validMeasurement(m) {
    if (!keys(m,'start_raw_ns,observed_raw_ns,duration_ns,edges,endpoint_uncertainty_ns,plan_generation,observed_millihz,ratio_error_ppb,uncertainty_ppb,local_rate_ppb,reference_use,age_ns')) return false;
    for (const k of ['start_raw_ns','observed_raw_ns','duration_ns','edges','endpoint_uncertainty_ns','plan_generation','observed_millihz','age_ns']) if (!decimal(m[k])) return false;
    const duration = BigInt(m.duration_ns), end = BigInt(m.observed_raw_ns), start = BigInt(m.start_raw_ns);
    if (duration < 10000000000n || duration > 60000000000n || end <= start || end-start !== duration ||
        BigInt(m.edges) === 0n || BigInt(m.plan_generation) === 0n || BigInt(m.endpoint_uncertainty_ns) >= duration ||
        BigInt(m.age_ns) > 30000000000n || BigInt(m.observed_millihz)<990000n || BigInt(m.observed_millihz)>10100000000n || !integer(m.uncertainty_ppb,1,200100) ||
        typeof m.ratio_error_ppb !== 'string' || !/^(0|-?[1-9][0-9]{0,7})$/.test(m.ratio_error_ppb) || Math.abs(Number(m.ratio_error_ppb))>10000000) return false;
    return m.reference_use === 'measurement_only' ? m.local_rate_ppb === null
        : m.reference_use === 'trusted_local' && integer(m.local_rate_ppb,-100000,100000);
  }
  function validRuntime(v) {
    if (!keys(v,'transmit,reference_output,communication,engineering_enabled,inhibited,epoch,enable_command_completed_ns,disable_command_completed_ns,reference_measurement,measurement_error,clock_reference_state,clock_reference_observed_raw_ns,clock_reference_age_ns')) return false;
    for (const k of ['transmit','reference_output']) if (!['unknown','register_disabled','register_enabled'].includes(v[k])) return false;
    if(!['none','selected','quality','disagreement','expired','awaiting_utc'].includes(v.clock_reference_state) || !decimal(v.clock_reference_observed_raw_ns) || !decimal(v.clock_reference_age_ns) ||
       BigInt(v.clock_reference_observed_raw_ns)+BigInt(v.clock_reference_age_ns)>18446744073709551615n ||
       (v.clock_reference_state==='none' && v.clock_reference_age_ns!=='0') || (v.clock_reference_state==='none') !== (v.clock_reference_observed_raw_ns==='0')) return false;
    if(v.reference_measurement && decimal(v.reference_measurement.observed_raw_ns) && decimal(v.reference_measurement.age_ns) && (v.clock_reference_state!=='none' && BigInt(v.clock_reference_observed_raw_ns)+BigInt(v.clock_reference_age_ns)!==BigInt(v.reference_measurement.observed_raw_ns)+BigInt(v.reference_measurement.age_ns))) return false;
    return v.communication === (v.transmit === 'unknown' || v.reference_output === 'unknown' ? 'unknown' : 'register_control_verified') &&
        typeof v.engineering_enabled === 'boolean' && typeof v.inhibited === 'boolean' &&
        ['epoch','enable_command_completed_ns','disable_command_completed_ns'].every(k=>decimal(v[k])) &&
        (v.measurement_error === null || typeof v.measurement_error === 'string' && /^[a-z][a-z0-9_]{0,79}$/.test(v.measurement_error)) &&
        (v.reference_measurement === null || validMeasurement(v.reference_measurement));
  }
  function qualified(value) {
    const q = value.qualification;
    if (value.physically_qualified === false) return q === null && value.execution_engine !== 'si5351';
    const p = root.WsprryControl?.descriptors?.si5351?.ordinary_profile;
    const b = value.boot_active;
    return value.physically_qualified === true && p?.qualified === true &&
        keys(q,'profile_id,system_clock_hz,i2c_pair') && q.profile_id === p.id &&
        q.system_clock_hz === p.system_clock_hz && q.i2c_pair === p.i2c_pair && valid(b) &&
        b.reference_hz === p.reference_hz && b.reference_source === p.reference_source &&
        b.crystal_load_pf === p.crystal_load_pf && b.address === p.address &&
        b.transmit_output === p.transmit_output && b.drive_ma === p.drive_ma &&
        b.rf_gate_gp === p.rf_gate_gp && (b.loopback_output === null ? p.allow_none :
          b.loopback_output === p.loopback_output && b.loopback_input_gp === p.loopback_input_gp) &&
        ['si5351','si5351-engineering'].includes(value.execution_engine) &&
        (value.runtime === null || value.runtime.engineering_enabled === (value.execution_engine === 'si5351-engineering'));
  }
  function readback(v,target) {
    if (!object(v) || !keys(target,'scope,device_id,boot_id')) return false;
    const value = {...v};
    if (Object.hasOwn(value,'contract')) {
      if (value.contract !== 'Wsprry-Control/1') return false;
      delete value.contract;
    }
    return keys(value,'schema,target,saved,boot_active,pending_restart,physically_qualified,qualification,execution_engine,runtime,storage_healthy') &&
        value.schema === 'si5351' && keys(value.target,'scope,device_id,boot_id') && value.target.scope === 'member' &&
        value.target.device_id === target.device_id && value.target.boot_id === target.boot_id &&
        (value.saved === null || valid(value.saved)) && (value.boot_active === null || valid(value.boot_active)) &&
        typeof value.pending_restart === 'boolean' && qualified(value) &&
        typeof value.execution_engine === 'string' && value.execution_engine.length > 0 && value.execution_engine.length <= 64 &&
        (value.runtime === null || value.boot_active !== null && validRuntime(value.runtime) &&
          (value.runtime.reference_measurement === null || value.boot_active.loopback_output !== null &&
            value.runtime.reference_measurement.reference_use === value.boot_active.reference_use)) &&
        typeof value.storage_healthy === 'boolean' && (value.storage_healthy || value.saved === null);
  }
  function executionDescription(value) {
    if (value?.physically_qualified === true)
      return value.runtime?.engineering_enabled ? 'Qualified hardware profile; engineering firmware.' : 'Qualified hardware profile; ordinary firmware.';
    return value?.runtime?.engineering_enabled ? 'Engineering execution enabled for this unqualified hardware profile.' :
      'This configuration requires an engineering build. Match the qualified setup or use engineering firmware.';
  }
  function profileDescription(profile) {
    if (profile?.qualified !== true) return '';
    const pair = pairs[profile.i2c_pair];
    return `Qualified setup: QRP Labs Rev. 6 with a ${profile.reference_hz/1e6} MHz TCXO into XA; SDA GP${pair[0]} / SCL GP${pair[1]}; CLK${profile.transmit_output} at ${profile.drive_ma} mA. Loopback: None or CLK${profile.loopback_output} to GP${profile.loopback_input_gp}. Independent RF gate: None wired. Pico clock: ${profile.system_clock_hz/1e6} MHz.`;
  }
  function create(container, {changed = ()=>{}, bootstrap = false} = {}) {
    const profile = root.WsprryControl?.descriptors?.si5351?.ordinary_profile;
    const ordinary = profile?.qualified === true;
    const introduction = ordinary ? 'Ordinary firmware supports the qualified setup below. Other configurations require an engineering build.' :
      'Si5351 transmission requires an engineering build while physical qualification is incomplete.';
    container.innerHTML = `<p>${introduction} Saving settings does not enable an output.</p>
      ${ordinary ? `<p data-si-profile>${profileDescription(profile)}</p>` : ''}
      <div class="fields">
      <label>I²C SDA/SCL pair<select data-si="pair" required><option value="">Choose a pair</option>${pairs.map((p,i)=>`<option value="${i}">I²C${i<6?0:1}: GP${p[0]} / GP${p[1]}</option>`).join('')}</select></label>
      <label>Device address<select data-si="address"><option value="96">0x60 (supported address)</option></select></label>
      <label>Reference frequency (Hz)<select data-si="reference_hz" required><option value="">Choose the fitted reference</option><option value="25000000">25,000,000 Hz</option><option value="27000000">27,000,000 Hz</option></select></label>
      <label>Reference source<select data-si="reference_source" required><option value="">Choose reference wiring</option><option value="crystal">Passive crystal</option><option value="external_xa">External clock / TCXO into XA</option></select></label>
      <label data-si-group="crystal">Crystal load<select data-si="crystal_load_pf" required><option value="">Choose the crystal load</option><option value="6">6 pF</option><option value="8">8 pF</option><option value="10">10 pF</option></select></label>
      <label>Si5351 output drive<select data-si="drive_ma"><option value="2">2 mA</option><option value="4">4 mA</option><option value="6">6 mA</option><option value="8">8 mA</option></select></label>
      <label>Transmit clock<select data-si="transmit_output" required><option value="">Choose a transmit output</option><option value="0">CLK0</option><option value="1">CLK1</option><option value="2">CLK2</option></select></label>
      <label>Loopback clock<select data-si="loopback_output"><option value="">None</option><option value="0">CLK0</option><option value="1">CLK1</option><option value="2">CLK2</option></select></label>
      <label data-si-group="loopback">Loopback input GPIO<select data-si="loopback_input_gp" required><option value="">Choose a counter input</option>${[1,3,5,7,9,11,13,15,17,19,21,27].map(p=>`<option value="${p}">GP${p}</option>`).join('')}</select></label>
      <label>Reference use<select data-si="reference_use"><option value="measurement_only">Measure only</option><option value="trusted_local">Trust local reference for Pico rate</option></select></label>
      <label data-si-group="trusted">Declared reference uncertainty (ppb)<input data-si="trusted_uncertainty_ppb" type="number" min="1" max="100000" step="1" required></label>
      <label>Manual Si5351 correction (ppb)<input data-si="manual_correction_ppb" type="number" min="-100000" max="100000" step="1" value="0" required></label>
      <label>Automatic Si5351 correction<select data-si="automatic_calibration"><option value="false">Disabled</option><option value="true">Use GPS/PPS and loopback</option></select></label>
      <label>Independent RF gate GPIO<select data-si="rf_gate_gp"><option value="">None wired</option>${[...Array(23).keys(),26,27,28].map(p=>`<option value="${p}">GP${p}</option>`).join('')}</select></label>
      <label data-si-group="gate">RF gate polarity<select data-si="rf_gate_active_high"><option value="true">Active-high</option><option value="false">Active-low</option></select></label>
      </div>
      <p>External XA requires compatible module wiring, levels and grounding. It does not select CLKIN. A gate requires an inactive hardware bias. Output drive is electrical current, not RF power or reported WSPR dBm.</p>
      <p>Positive correction lowers programmed RF for a fast reference. Loopback measures a clock ratio; it does not establish UTC or absolute RF accuracy. Trusted use can reduce timer drift after NTP or phone time is accepted. It requires a known reference uncertainty and does not extend time validity. Automatic correction requires enabled GPS with a validated UTC/PPS association and a separate loopback measurement.</p>
      <p data-si-status role="status" aria-live="polite">Reference measurements have not been read.</p>`;
    if(bootstrap) {
      container.querySelector('.fields').className='row g-3';
      for(const label of container.querySelectorAll('label')) label.className='form-label col-12 col-lg-6';
      for(const field of container.querySelectorAll('input,select')) field.className=field.tagName==='SELECT'?'form-select':'form-control';
      for(const p of container.querySelectorAll('p')) p.className='form-text text-break';
    }
    const input = name => container.querySelector(`[data-si="${name}"]`);
    for (const name of ['pair',...fields.split(',')]) {
      const field = input(name);
      if (field) {
        field.id = `${container.id}-${name}`;
        field.closest('label')?.setAttribute('for',field.id);
        field.addEventListener('input',()=>{ field.setCustomValidity('');field.setAttribute('aria-invalid','false'); sync(); changed(); });
        field.addEventListener('change',()=>{ field.setCustomValidity('');field.setAttribute('aria-invalid','false'); sync(); changed(); });
      }
    }
    let active = false, enabled = true;
    function sync() {
      if(input('loopback_output').value==='' && input('reference_use').value==='trusted_local') input('reference_use').value='measurement_only';
      const groups = {crystal:input('reference_source').value==='crystal',loopback:input('loopback_output').value!=='',trusted:input('reference_use').value==='trusted_local',gate:input('rf_gate_gp').value!==''};
      for (const [name,shown] of Object.entries(groups)) {
        const group = container.querySelector(`[data-si-group="${name}"]`);
        group.hidden = !shown;
        for (const field of group.querySelectorAll('input,select')) field.disabled = !active || !enabled || !shown;
      }
      for (const field of container.querySelectorAll('[data-si]')) {
        if (!field.closest('[data-si-group]')) field.disabled = !active || !enabled;
      }
      input('reference_use').querySelector('[value="trusted_local"]').disabled = input('loopback_output').value==='';
    }
    function show(enabled) { active = Boolean(enabled); container.hidden = !active; sync(); }
    function load(value,pair) {
      if (value !== null && value !== undefined && !valid(value)) throw Error('Saved Si5351 settings are malformed. Read settings again.');
      input('pair').value = pair === null || pair === undefined ? '' : String(pair);
      for (const key of fields.split(',')) {
        const field = input(key);
        if (!field) continue;
        const fallback = {address:96,drive_ma:2,reference_use:'measurement_only',manual_correction_ppb:0,rf_gate_active_high:true,automatic_calibration:false};
        field.value = value?.[key] === null || value?.[key] === undefined ? String(fallback[key] ?? '') : String(value[key]);
        field.setCustomValidity('');
      }
      sync();
    }
    function invalid(field,message) {
      field.setCustomValidity(message);field.setAttribute('aria-invalid','true');
      if(!field.disabled) field.reportValidity();
      const error=Error(message);error.control=field;throw error;
    }
    function read() {
      if (!active || input('pair').value === '') invalid(input('pair'),'Choose an I²C pair before saving Si5351 settings.');
      for (const field of container.querySelectorAll('input,select')) {
        const number=Number(field.value),badNumber=field.type==='number' && field.value!=='' && (!Number.isInteger(number) || field.min!=='' && number<Number(field.min) || field.max!=='' && number>Number(field.max));
        if (!field.closest('[data-si-group]')?.hidden && (field.required && field.value==='' || badNumber || !field.disabled && !field.checkValidity())) invalid(field,'Complete this Si5351 setting with a supported whole value before saving.');
      }
      const value = {};
      const nullable = new Set(['crystal_load_pf','loopback_output','loopback_input_gp','rf_gate_gp','trusted_uncertainty_ppb']);
      for (const key of fields.split(',')) {
        const field = input(key), text = field.value;
        value[key] = ['rf_gate_active_high','automatic_calibration'].includes(key) ? text==='true' : ['reference_source','reference_use'].includes(key) ? text :
            nullable.has(key) && (field.closest('[data-si-group]')?.hidden || text==='') ? null : Number(text);
      }
      if (!valid(value)) {
        const field = input(value.transmit_output===value.loopback_output ? 'loopback_output' : 'reference_use');
        invalid(field,'Choose distinct transmit and loopback outputs and a valid reference policy.');
      }
      return {settings:value,pair:Number(input('pair').value)};
    }
    function observation(v) {
      const status = container.querySelector('[data-si-status]');
      const r = v?.runtime, m = r?.reference_measurement;
      status.textContent = !r ? 'Runtime observation unavailable. Saving settings does not prove hardware is connected or RF is inactive.' :
          `Transmit ${r.transmit.replaceAll('_',' ')}; reference clock ${r.reference_output.replaceAll('_',' ')}. ${r.inhibited ? 'Output inhibited for this boot. ' : ''}Clock reference: ${{none:'no trusted rate input',selected:'trusted local rate selected',quality:'existing clock-rate estimate has lower uncertainty',disagreement:'frequency disagreement; UTC admission blocked',expired:'capture lost or stale; learned timer rate retained with growing uncertainty',awaiting_utc:'rate input ready; an accepted UTC source is still required'}[r.clock_reference_state]}. ${r.clock_reference_state!=='none' ? 'Last reference rate sample '+(Number(r.clock_reference_age_ns)/1e9).toFixed(1)+' s ago. ' : ''}${executionDescription(v)} `+
          (m ? `Loopback ratio error ${m.ratio_error_ppb} ppb, uncertainty ≤${m.uncertainty_ppb} ppb; ${(Number(m.duration_ns)/1e9).toFixed(1)} s window, ${(Number(m.age_ns)/1e9).toFixed(1)} s old. ${m.reference_use==='measurement_only' ? 'Measurement only.' : 'Local rate estimate; no UTC epoch or absolute RF calibration.'}` : `No current loopback measurement${r.measurement_error ? ': '+r.measurement_error.replaceAll('_',' ') : ''}.`);
    }
    show(false);
    return {show,load,read,observation,
      validate:(pins,selectors=[])=>{const value=read();if(!allocation(value.settings,pins,selectors)) invalid(input('pair'),'The Si5351 pair, loopback, gate and other GPIO roles conflict. Choose separate pins.');return value;},
      enable:value=>{enabled=Boolean(value);sync();},element:container,
      pair:()=>input('pair').value===''?null:Number(input('pair').value),
      pinAssignments:()=>({loopback_input_gp:input('loopback_output').value==='' || input('loopback_input_gp').value===''?null:Number(input('loopback_input_gp').value),rf_gate_gp:input('rf_gate_gp').value===''?null:Number(input('rf_gate_gp').value)})};
  }
  const api = {valid,allocation,readback,validRuntime,validMeasurement,executionDescription,profileDescription,create};
  root.WsprrySi5351=api;
  if (typeof module !== 'undefined' && module.exports) module.exports=api;
})(globalThis);

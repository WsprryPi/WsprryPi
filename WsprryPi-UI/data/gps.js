// SPDX-License-Identifier: MIT
(function(root) {
  'use strict';
  const defaults = Object.freeze({enabled:false,receiver:'qlg3',rx_gp:1,pps_gp:3,tx_gp:null,baud:9600,
    pps_active_high:true,pairing_qualified:false,utc_second_offset:0,pps_offset_ns:0,
    minimum_message_delay_ms:0,maximum_message_delay_ms:800,receiver_uncertainty_us:100,
    capture_uncertainty_us:100,location:'manual',position_maximum_age_s:30,
    automatic_rf_calibration:false,calibration_maximum_age_s:3600,daylight:'always'});
  const object = v => v !== null && typeof v === 'object' && !Array.isArray(v);
  const keys = (v,names) => object(v) && Object.keys(v).sort().join(',') === names.split(',').sort().join(',');
  const integer = (v,min,max) => Number.isInteger(v) && v >= min && v <= max;
  const gp = v => integer(v,0,22) || integer(v,26,28);
  const decimal = v => typeof v === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(v) && BigInt(v)<=18446744073709551615n;
  const uart = v => [1,13,17].includes(v) ? 0 : [5,9,21].includes(v) ? 1 : null;
  const txUart = v => [0,12,16,28].includes(v) ? 0 : [4,8,20].includes(v) ? 1 : null;
  function locator(p) {
    const lon=Math.min(p.longitude_e7+1800000000,3599999999),lat=Math.min(p.latitude_e7+900000000,1799999999);
    return String.fromCharCode(65+Math.floor(lon/200000000),65+Math.floor(lat/100000000),
      48+Math.floor(lon/20000000)%10,48+Math.floor(lat/10000000)%10,
      65+Math.floor(lon%20000000*24/20000000),65+Math.floor(lat%10000000*24/10000000));
  }
  function valid(v) {
    if (!keys(v,Object.keys(defaults).join(',')) || !['qlg3','e108'].includes(v.receiver) ||
        uart(v.rx_gp)===null || !gp(v.pps_gp) || v.pps_gp%2!==1 || v.rx_gp===v.pps_gp ||
        ![9600,19200,38400,115200].includes(v.baud)) return false;
    for (const key of ['enabled','pps_active_high','pairing_qualified','automatic_rf_calibration'])
      if (typeof v[key] !== 'boolean') return false;
    if (v.tx_gp!==null && (v.receiver==='qlg3' || txUart(v.tx_gp)===null ||
        txUart(v.tx_gp)!==uart(v.rx_gp) || v.tx_gp===v.pps_gp)) return false;
    return integer(v.utc_second_offset,-1,1) && integer(v.pps_offset_ns,-500000000,500000000) &&
      integer(v.minimum_message_delay_ms,0,1999) && integer(v.maximum_message_delay_ms,0,1999) &&
      v.minimum_message_delay_ms<=v.maximum_message_delay_ms &&
      v.maximum_message_delay_ms-v.minimum_message_delay_ms<1000 &&
      integer(v.receiver_uncertainty_us,1,100000) && integer(v.capture_uncertainty_us,1,100000) &&
      ['manual','gps'].includes(v.location) && integer(v.position_maximum_age_s,1,3600) &&
      integer(v.calibration_maximum_age_s,10,86400) && ['always','day','night'].includes(v.daylight) &&
      (v.enabled || v.location==='manual' && !v.automatic_rf_calibration && v.daylight==='always') &&
      (!v.automatic_rf_calibration || v.pairing_qualified);
  }
  function allocation(v,p,si,selectors=[]) {
    if (v===null) return true;
    if (!valid(v)) return false;
    if (!v.enabled) return true;
    if (!object(p) || !Array.isArray(selectors)) return false;
    const pins=[p.rf_gp,p.button_gp,p.amplifier_gp,p.indicator==='external'?p.indicator_gp:null,
      ...(p.lpf_gps||[]),...selectors.map(s=>s.gp)];
    if (p.engine==='i2c') {
      if (!root.WsprrySi5351?.allocation(si,p,selectors)) return false;
      const pair=[[0,1],[4,5],[8,9],[12,13],[16,17],[20,21],[2,3],[6,7],[10,11],[14,15],[18,19],[26,27]][p.i2c_pair];
      if (!pair) return false;
      pins.push(...pair,si.loopback_input_gp,si.rf_gate_gp);
      if(si.loopback_input_gp!==null && ((si.loopback_input_gp>>1)&7)===((v.pps_gp>>1)&7))return false;
    } else if (p.engine!=='direct') return false;
    const used=new Set(pins.filter(pin=>pin!==null));
    for (const pin of [v.rx_gp,v.pps_gp,v.tx_gp]) {
      if (pin===null) continue;
      if (used.has(pin)) return false;
      used.add(pin);
    }
    return true;
  }
  function validCalibration(v) {
    if (v===null) return true;
    if (!keys(v,'oscillator_error_ppb,uncertainty_ppb,observed_raw_ns,age_ns,span_ns,generation,fresh') ||
        !integer(v.oscillator_error_ppb,-100000,100000) || !integer(v.uncertainty_ppb,1,4294967295) ||
        typeof v.fresh!=='boolean') return false;
    return ['observed_raw_ns','age_ns','span_ns','generation'].every(k=>decimal(v[k])) &&
      BigInt(v.observed_raw_ns)>0n && BigInt(v.generation)>0n && (!v.fresh || v.uncertainty_ppb<=100001) &&
      BigInt(v.span_ns)>=10000000000n && BigInt(v.span_ns)<=60000000000n &&
      BigInt(v.observed_raw_ns)+BigInt(v.age_ns)<=18446744073709551615n;
  }
  function validRuntime(v) {
    if (v===null) return true;
    if (!keys(v,'serial_present,pps_present,time_valid,position_valid,satellites,observation_raw_ns,age_ns,position,candidate_utc_ns,candidate_raw_ns,candidate_pps_raw_ns,observations,rejected_sentences,dropped,uart_errors,irq_latency_ns,maximum_irq_latency_ns,capture_error_bound_ns,capture_rejections,error,timer_calibration,synthesizer_calibration')) return false;
    for (const key of ['serial_present','pps_present','time_valid','position_valid']) if(typeof v[key]!=='boolean')return false;
    for (const key of ['observation_raw_ns','age_ns','candidate_utc_ns','candidate_raw_ns','candidate_pps_raw_ns']) if(!decimal(v[key]))return false;
    for (const key of ['irq_latency_ns','maximum_irq_latency_ns','capture_error_bound_ns'])if(!decimal(v[key]) || BigInt(v[key])>4294967295n)return false;
    if(BigInt(v.maximum_irq_latency_ns)<BigInt(v.irq_latency_ns) || v.time_valid && v.capture_error_bound_ns==='0')return false;
    for (const key of ['observations','rejected_sentences','dropped','uart_errors','capture_rejections']) if(!integer(v[key],0,4294967295))return false;
    if(BigInt(v.observation_raw_ns)+BigInt(v.age_ns)>18446744073709551615n ||
       v.time_valid && (BigInt(v.age_ns)>3000000000n || v.observations===0))return false;
    if(!integer(v.satellites,0,99) || typeof v.error!=='string' || !/^([a-z][a-z0-9_]{0,79})?$/.test(v.error) ||
       v.time_valid && v.observation_raw_ns==='0' || v.position_valid!==(v.position!==null))return false;
    if(v.position!==null && (!keys(v.position,'latitude_e7,longitude_e7,locator,observed_raw_ns,age_ns') ||
       !integer(v.position.latitude_e7,-900000000,900000000) || !integer(v.position.longitude_e7,-1800000000,1800000000) ||
       typeof v.position.locator!=='string' || v.position.locator!==locator(v.position) ||
       !decimal(v.position.observed_raw_ns) || v.position.observed_raw_ns==='0' || !decimal(v.position.age_ns) ||
       BigInt(v.position.observed_raw_ns)+BigInt(v.position.age_ns)>18446744073709551615n))return false;
    return validCalibration(v.timer_calibration) && validCalibration(v.synthesizer_calibration);
  }
  function readback(input,target) {
    if(!object(input) || !keys(target,'scope,device_id,boot_id'))return false;
    const v={...input};
    if(Object.hasOwn(v,'contract')) {if(v.contract!=='Wsprry-Control/1')return false;delete v.contract;}
    return keys(v,'schema,target,saved,boot_active,pending_restart,storage_healthy,runtime') &&
      v.schema==='gps' && keys(v.target,'scope,device_id,boot_id') && v.target.scope==='member' &&
      /^[0-9a-f]{32}$/.test(v.target.device_id) && /^[0-9a-f]{32}$/.test(v.target.boot_id) &&
      Object.keys(target).every(k=>target[k]===v.target[k]) && typeof v.pending_restart==='boolean' &&
      typeof v.storage_healthy==='boolean' && (v.saved===null || valid(v.saved)) &&
      (v.boot_active===null || valid(v.boot_active)) && validRuntime(v.runtime) &&
      (v.storage_healthy || v.saved===null) &&
      (v.runtime===null || v.boot_active?.enabled===true &&
        (!v.runtime.time_valid || v.boot_active.pairing_qualified) &&
        (!v.runtime.time_valid || BigInt(v.runtime.capture_error_bound_ns)<=BigInt(v.boot_active.capture_uncertainty_us)*1000n) &&
        (!v.runtime.position_valid || BigInt(v.runtime.position.age_ns)<=BigInt(v.boot_active.position_maximum_age_s)*1000000000n));
  }
  function create(container,{changed=()=>{},bootstrap=false,compact=false,scheduleContainer=null}={}) {
    const options = values => values.map(([value,label])=>`<option value="${value}">${label}</option>`).join('');
    const select=(name,label,values)=>`<label>${label}<select data-gps="${name}">${options(values)}</select></label>`;
    const number=(name,label,min,max)=>`<label>${label}<input data-gps="${name}" type="number" step="1" min="${min}" max="${max}" required></label>`;
    const bool=(name,label)=>select(name,label,[['false','Disabled'],['true','Enabled']]);
    container.innerHTML=`<h3>GPS and PPS</h3><p>Use an optional GPS receiver for offline time and location. GPS serial and PPS use separate inputs. Save RF output settings, then restart to activate changes.</p>
      <div class="fields">${bool('enabled','GPS input')}${select('receiver','Receiver',[['qlg3','QRP Labs QLG3 (receive only)'],['e108','E108 / GK9501 with optional configuration output']])}
      ${select('location','Station locator',[['manual','Use saved manual locator'],['gps','Use current GPS location']])}
      ${select('daylight','Automatic beacon schedule',[['always','All day'],['day','Daylight only'],['night','Night only']])}</div>
      <details><summary>GPS wiring and timing</summary>${compact ? '' : '<p>GP numbers identify GPIOs, not physical header pins. QLG3 uses receiver TX to Pico RX; it has no receiver RX header. The Pico keeps its PPS pin as an input.</p>'}
      <div class="fields">${select('rx_gp','GPS serial input GPIO',[1,5,9,13,17,21].map(v=>[v,`GP${v}`]))}
      ${select('pps_gp','PPS input GPIO',[1,3,5,7,9,11,13,15,17,19,21,27].map(v=>[v,`GP${v}`]))}
      ${select('baud','Serial baud rate',[9600,19200,38400,115200].map(v=>[v,String(v)]))}
      ${select('tx_gp','Optional receiver configuration output',[['','None'],...[0,4,8,12,16,20,28].map(v=>[v,`GP${v}`])])}
      ${select('pps_active_high','PPS capture edge',[['true','Rising'],['false','Falling']])}
      ${bool('pairing_qualified','Validated UTC/PPS association')}
      ${number('utc_second_offset','RMC second offset (-1, 0, or 1)',-1,1)}
      ${number('pps_offset_ns','PPS edge delay from UTC boundary (ns)',-500000000,500000000)}
      ${number('minimum_message_delay_ms','Minimum RMC delay after PPS (ms)',0,1999)}
      ${number('maximum_message_delay_ms','Maximum RMC delay after PPS (ms)',0,1999)}
      ${number('receiver_uncertainty_us','Receiver timing error allowance (µs)',1,100000)}
      ${number('capture_uncertainty_us','Capture timestamp error allowance (µs)',1,100000)}
      ${number('position_maximum_age_s','Maximum location age (s)',1,3600)}
      ${bool('automatic_rf_calibration','Automatic direct GPIO RF correction')}
      ${number('calibration_maximum_age_s','Maximum RF calibration age (s)',10,86400)}</div>
      ${compact ? '' : `
      <p>Enable validated association only after checking the receiver's UTC second and capture path. A positive edge delay means the edge arrives after the UTC boundary. PPS alone supplies no date. Timing allowances must include the receiver, cable, and capture path; typical specifications are not guaranteed bounds.</p>
      <p>The RMC second offset assigns UTC to the uniquely selected captured PPS edge: 0 uses the sentence timestamp, −1 assigns one second earlier, and +1 assigns one second later. Validate this relationship for the receiver before enabling it.</p>
      <p>PPS marks the second boundary; serial UTC labels a captured pulse and can arrive after another pulse. Set a measured delay range narrower than one second, with delays up to 1,999 ms.</p>
      <p>PPS uses an input-only, gated PWM counter to timestamp the electrical edge and measure interrupt latency. Choose an odd GPIO and keep its PWM slice separate from Si5351 loopback. A counter wrap, ambiguous edge, clock change or flash capture gap requires fresh pairs.</p>
      <p>This receiver profile has no advance leap announcement. GPS time pauses for five seconds on either side of a UTC month boundary, then reacquires; jobs crossing that boundary are refused. GPS holdover lasts at most one hour, with increasing uncertainty.</p>
      <p>Direct GPIO correction requires a verified shared oscillator. Si5351 correction uses its separate control and PPS-qualified CLK2 loopback. Manual correction becomes a residual trim when automatic correction is enabled. A job keeps its accepted correction and locator until completion.</p>`}</details>
      <p data-gps-status role="status" aria-live="polite">GPS observations have not been read.</p>`;
    if(bootstrap) {
      for(const row of container.querySelectorAll('.fields'))row.className='row g-3';
      for(const label of container.querySelectorAll('label'))label.className='form-label col-12 col-lg-6';
      for(const field of container.querySelectorAll('input,select'))field.className=field.tagName==='SELECT'?'form-select':'form-control';
      for(const p of container.querySelectorAll('p'))p.className='form-text text-break';
    }
    const fields=new Map([...container.querySelectorAll('[data-gps]')].map(field=>[field.dataset.gps,field]));
    const input=key=>fields.get(key);
    if(scheduleContainer) {
      scheduleContainer.append(input('daylight').closest('label'));
    }
    const clearValidity=()=>{for(const key of Object.keys(defaults))input(key).setCustomValidity('');};
    let enabled=false, configured=false, edited=false;
    for(const key of Object.keys(defaults)) {
      const field=input(key);field.id=`${container.id}-${key}`;
      field.closest('label').setAttribute('for',field.id);
      field.addEventListener('change',()=>{edited=true;clearValidity();sync();changed();});
      field.addEventListener('input',()=>{edited=true;clearValidity();changed();});
    }
    function sync() {
      for(const field of fields.values())field.disabled=!enabled;
      for(const option of input('tx_gp').options)option.disabled=input('receiver').value==='qlg3' && option.value!=='';
    }
    function load(value) {
      if(value!==null && !valid(value))throw Error('Saved GPS settings are malformed. Read settings again.');
      configured=value!==null;edited=false;
      for(const key of Object.keys(defaults)) {
        const v=(value||defaults)[key];input(key).value=v===null?'':String(v);input(key).setCustomValidity('');
      }
      sync();
    }
    function read(pins,si,selectors=[]) {
      if(!configured&&!edited)return null;
      clearValidity();
      const fail=(message,key)=>{
        const field=input(key),details=field.closest('details');
        if(details)details.open=true;
        field.setCustomValidity(message);
        const error=Error(message);error.control=field;throw error;
      };
      const value={};
      for(const [key,fallback] of Object.entries(defaults)) {
        const field=input(key),text=field.value;
        if(!field.checkValidity())fail(`Review ${field.closest('label').firstChild.textContent.trim()}.`,key);
        if(typeof fallback==='boolean' && !['true','false'].includes(text))
          fail('Choose Enabled or Disabled.',key);
        value[key]=key==='tx_gp'&&text===''?null:typeof fallback==='boolean'?text==='true':
          typeof fallback==='number'||key==='tx_gp'?Number(text):text;
        if(typeof value[key]==='number' && (text==='' || !/^-?(0|[1-9][0-9]*)$/.test(text)))
          fail('Use a whole value within the displayed limits.',key);
      }
      if(value.receiver==='qlg3' && value.tx_gp!==null)fail('QLG3 has no receiver RX connection. Choose None for the Pico serial output.', 'tx_gp');
      if(uart(value.rx_gp)===null)fail('Choose a supported UART input GPIO.','rx_gp');
      if(!gp(value.pps_gp) || value.pps_gp%2!==1 || value.pps_gp===value.rx_gp)fail('Choose a separate odd GPIO for the input-only PPS counter.','pps_gp');
      if(value.tx_gp!==null && (txUart(value.tx_gp)===null || txUart(value.tx_gp)!==uart(value.rx_gp) || value.tx_gp===value.pps_gp))
        fail('Choose an output on the same UART as the serial input, separate from PPS, or choose None.','tx_gp');
      if(!value.enabled && (value.location==='gps' || value.daylight!=='always' || value.automatic_rf_calibration))
        fail('Enable GPS input, or use the manual locator, All day schedule and disabled automatic correction.','enabled');
      if(value.minimum_message_delay_ms>value.maximum_message_delay_ms)fail('Maximum serial delay must be at least the minimum delay.','maximum_message_delay_ms');
      if(value.maximum_message_delay_ms-value.minimum_message_delay_ms>=1000)fail('Keep the serial-delay window narrower than one second so its PPS association is unique.','maximum_message_delay_ms');
      if(value.automatic_rf_calibration && !value.pairing_qualified)fail('Validate the receiver UTC/PPS association before enabling automatic correction.','pairing_qualified');
      if(!valid(value))fail('GPS settings are incomplete or unsupported. Read the saved settings and review the draft.','enabled');
      if(!allocation(value,pins,si,selectors)) {
        if(pins.engine==='i2c' && Number.isInteger(si?.loopback_input_gp) &&
           ((si.loopback_input_gp>>1)&7)===((value.pps_gp>>1)&7))
          fail('GPS PPS and Si5351 loopback must use different PWM slices.','pps_gp');
        const used=new Set([pins.rf_gp,pins.button_gp,pins.amplifier_gp,pins.indicator==='external'?pins.indicator_gp:null,
          ...(pins.lpf_gps||[]),...selectors.map(s=>s.gp)]);
        if(pins.engine==='i2c') {
          const pair=[[0,1],[4,5],[8,9],[12,13],[16,17],[20,21],[2,3],[6,7],[10,11],[14,15],[18,19],[26,27]][pins.i2c_pair];
          if(pair)pair.forEach(gp=>used.add(gp));
          used.add(si?.loopback_input_gp);used.add(si?.rf_gate_gp);
        }
        const key=['rx_gp','pps_gp','tx_gp'].find(k=>value[k]!==null&&used.has(value[k]))||'rx_gp';
        fail('GPS serial and PPS pins conflict with another saved hardware role. Choose a separate GPIO.',key);
      }
      return value;
    }
    function observation(value) {
      const r=value?.runtime, text=container.querySelector('[data-gps-status]');
      if(!r) {text.textContent=compact ? 'GPS runtime is unavailable.' : 'GPS runtime is unavailable. Saving settings does not verify wiring or timing.';return;}
      const describe=(name,c)=>c?`${name}: ${c.oscillator_error_ppb} ± ${c.uncertainty_ppb} ppb, ${(Number(c.age_ns)/1e9).toFixed(1)} s old${c.fresh?'':' (expired)'}. `:'';
      text.textContent=`Serial ${r.serial_present?'present':'unavailable'}; PPS ${r.pps_present?'present':'unavailable'}; UTC pairing ${r.time_valid?'valid':'not available'}; ${r.satellites} satellites. ${r.position_valid?'GPS locator '+r.position.locator+'. ':'No current position fix. '}Capture IRQ latency ${(Number(r.irq_latency_ns)/1000).toFixed(1)} µs (maximum ${(Number(r.maximum_irq_latency_ns)/1000).toFixed(1)} µs); timestamp bound ${(Number(r.capture_error_bound_ns)/1000).toFixed(1)} µs; ${r.capture_rejections} rejected captures. ${describe('Pico clock',r.timer_calibration)}${describe('Si5351 reference',r.synthesizer_calibration)}${r.error?'Receiver state: '+r.error.replaceAll('_',' ')+'. ':''}${value.pending_restart?'Saved changes need an explicit Pico restart.':''}`;
    }
    load(null);
    return {load,read,observation,enable(value){enabled=Boolean(value);sync();},element:container};
  }
  const api={defaults,valid,allocation,validCalibration,validRuntime,readback,create};
  root.WsprryGps=api;if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(globalThis);

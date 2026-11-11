// SPDX-License-Identifier: MIT
// Protected Pi coordinator -> actual network crypto or USB framing/ACK -> Pico journals.
'use strict';
const assert=require('node:assert/strict');
require('../data/control-contract.js');
require('../data/si5351.js');
const gps=require('../data/gps.js');
module.exports=async request=>{
 const path='/api/control/v1/member/configuration';
 const read=()=>request({method:'GET',path,body:''});
 const get=()=>request({method:'GET',path:'/api/control/v1/member/gps',body:''});
 const initial=await read();assert.equal(initial.status,200,JSON.stringify(initial));
 const target=initial.body.target,config=initial.body.config;
 const view=await get();assert.equal(view.status,200,JSON.stringify(view));assert(gps.readback(view.body,target));
 const filters=await request({method:'GET',path:'/api/control/v1/member/filter-selector',body:''});
 assert.equal(filters.status,200,JSON.stringify(filters));
 const p=config.pins;
 const used=new Set([p.rf_gp,p.button_gp,p.amplifier_gp,p.indicator==='external'?p.indicator_gp:null,...p.lpf_gps,
   ...(filters.body.saved?.selectors||[]).map(v=>v.gp)].filter(v=>v!==null));
 if(p.engine==='i2c') {
  const pair=[[0,1],[4,5],[8,9],[12,13],[16,17],[20,21],[2,3],[6,7],[10,11],[14,15],[18,19],[26,27]][p.i2c_pair];
  pair.forEach(v=>used.add(v));
  for(const v of [config.si5351.loopback_input_gp,config.si5351.rf_gate_gp])if(v!==null)used.add(v);
 }
 const rx=[1,5,9,13,17,21].find(v=>!used.has(v));assert.notEqual(rx,undefined);
 const pps=[3,5,7,9,11,13,15,17,19,21,27].find(v=>v!==rx&&!used.has(v));assert.notEqual(pps,undefined);
 const settings={...gps.defaults,enabled:true,rx_gp:rx,pps_gp:pps};
 const save=async(config,revision,destination=target,contract='Wsprry-Control/1')=>request({method:'POST',path,revision,
  body:JSON.stringify({contract,schema:'pico-settings',target:destination,operation:'save',config})});
 let before=await read();
 for(const bad of [{...settings,tx_gp:0},{...settings,baud:4800},{...settings,automatic_rf_calibration:true},
     {...settings,pps_gp:rx},{...settings,pps_gp:[...used][0]},{...settings,pps_offset_ns:500000001},
     {...settings,receiver_uncertainty_us:'100'},{...settings,extra:true},
     {...gps.defaults,daylight:'night'},{...gps.defaults,daylight:'twilight'}]) {
  const refused=await save({gps:bad},before.etag);assert.equal(refused.status,400,JSON.stringify(refused));
  const after=await read();assert.equal(after.etag,before.etag);assert.deepEqual(after.body.config,before.body.config);
 }
 assert.equal((await save({gps:settings},'"stale"')).status,412);
 assert.equal((await save({gps:settings},before.etag,{...target,boot_id:'f'.repeat(32)})).status,409);
 assert.equal((await save({gps:settings},before.etag,target,'Wsprry-Control/2')).status,409);
 assert.equal((await read()).etag,before.etag);
 for(const daylight of ['day_manual','night_manual']) {
  const manual={...gps.defaults,daylight};
  const selected=await save({gps:manual},before.etag);assert.equal(selected.status,200,JSON.stringify(selected));
  const readback=await get();assert.equal(readback.status,200,JSON.stringify(readback));
  assert(gps.readback(readback.body,target));assert.deepEqual(readback.body.saved,manual);
  before=await read();
 }
 const saved=await save({gps:settings},before.etag);assert.equal(saved.status,200,JSON.stringify(saved));
 assert.deepEqual(saved.body.config.gps,settings);
 let active=await get();assert.equal(active.status,200,JSON.stringify(active));
 assert(gps.readback(active.body,target));assert.deepEqual(active.body.saved,settings);assert(active.body.pending_restart);
 before=await read();
 const automatic={...settings,pairing_qualified:true,automatic_rf_calibration:true};
 const autoSaved=await save({gps:automatic},before.etag);assert.equal(autoSaved.status,200,JSON.stringify(autoSaved));
 before=await read();
 const stationOnly=await save({station:{power_dbm:13}},before.etag);assert.equal(stationOnly.status,200,JSON.stringify(stationOnly));
 assert.deepEqual(stationOnly.body.config.gps,automatic,'Omitted GPS remains selected across an unrelated merge');
 before=await read();
 const restore=await save({gps:config.gps??null,station:config.station},before.etag);
 assert.equal(restore.status,200,JSON.stringify(restore));
 assert.deepEqual(restore.body.config,config);
 active=await get();assert.equal(active.status,200);assert(active.body.pending_restart,'A/B/A preserves restart inhibition');
 console.log('GPS: actual closed settings, UTC/PPS qualification, allocation, identity/revision refusal before writes, omission preservation and A/B/A through production pair');
};

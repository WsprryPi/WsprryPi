// Production coordinator, carrier crypto/framing, Pico handlers and journals.
'use strict';
const assert=require('node:assert/strict'),si=require('../data/si5351.js');
module.exports=async (request,{qualifiedReadback=false}={})=>{
 const path='/api/control/v1/member/configuration',read=()=>request({method:'GET',path,body:''});
 const initial=await read();assert.equal(initial.status,200,JSON.stringify(initial));
 const target=initial.body.target,pins=initial.body.config.pins;
 const settings={reference_hz:25000000,reference_source:'external_xa',crystal_load_pf:null,address:96,drive_ma:2,transmit_output:0,loopback_output:2,loopback_input_gp:7,rf_gate_gp:null,rf_gate_active_high:true,reference_use:'measurement_only',trusted_uncertainty_ppb:null,manual_correction_ppb:-1234,automatic_calibration:false};
 // Choose a complete nonconflicting allocation; other tests may own a selector.
 const active={...pins,engine:'i2c',rf_gp:null,i2c_pair:1,button_gp:14,amplifier_gp:null,indicator:'onboard_led',indicator_gp:null,lpf_gps:[]};
 const save=async(config,revision)=>request({method:'POST',path,revision,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'pico-settings',target,operation:'save',config})});
 const get=()=>request({method:'GET',path:'/api/control/v1/member/si5351',body:''});
 let before=await read();
 for(const patch of [{...settings,address:97},{...settings,transmit_output:2},{...settings,loopback_output:null},{...settings,automatic_calibration:true},{...settings,extra:0}]){
   const refused=await save({pins:active,si5351:patch},before.etag);assert.equal(refused.status,400,JSON.stringify(refused));
   const after=await read();assert.equal(after.etag,before.etag);assert.deepEqual(after.body.config,before.body.config);
 }
 const conflict=await save({pins:{...active,i2c_pair:7},si5351:settings},before.etag);assert.equal(conflict.status,400,JSON.stringify(conflict));
 assert.equal((await read()).etag,before.etag);
 const stale=await save({pins:active,si5351:settings},'"stale"');assert.equal(stale.status,412,JSON.stringify(stale));
 before=await read();assert.equal(before.status,200,JSON.stringify(before));
 const saved=await save({pins:active,si5351:settings},before.etag);assert.equal(saved.status,200,JSON.stringify(saved));assert.deepEqual(saved.body.config.si5351,settings);assert.equal(saved.body.config.pins.engine,'i2c');
 let view=await get();assert.equal(view.status,200,JSON.stringify(view));assert(si.readback(view.body,target));assert.deepEqual(view.body.saved,settings);assert(view.body.pending_restart);assert.equal(view.body.physically_qualified,false);
 const zero={...settings,transmit_output:1,loopback_output:0};before=await read();const savedZero=await save({si5351:zero},before.etag);assert.equal(savedZero.status,200,JSON.stringify(savedZero));view=await get();assert.equal(view.body.saved.loopback_output,0);
 const none={...zero,loopback_output:null,loopback_input_gp:null};before=await read();assert.equal((await save({si5351:none},before.etag)).status,200);view=await get();assert.equal(view.body.saved.loopback_output,null);assert.equal(view.body.saved.loopback_input_gp,null);
 before=await read();const restore=await save({pins},before.etag);assert.equal(restore.status,200,JSON.stringify(restore));assert.deepEqual(restore.body.config.si5351,none);view=await get();assert.equal(view.status,200,JSON.stringify(view));assert(view.body.pending_restart,'A/B/A cannot clear restart interlock');
 if (qualifiedReadback) {
  // Exercise qualified readback validation with test-only peer response edits.
  // The host fixture cannot boot a physical Si5351. Keep the real transport,
  // descriptor, target, revision, and response validation in this check.
  const profile=require('../data/control-contract.js').descriptors.si5351.ordinary_profile;
  const qualification={profile_id:profile.id,system_clock_hz:profile.system_clock_hz,i2c_pair:profile.i2c_pair};
  const qualified={...view.body,physically_qualified:true,qualification,execution_engine:'si5351',runtime:null};
  const check=async readback=>{
   await request({control:'corrupt',corruption:{resource:'si5351',pointer:'',value:readback}});
   return get();
  };
  try {
   for (const drive_ma of [2,4,6,8]) {
    const readback=await check({...qualified,boot_active:{...settings,drive_ma}});
    assert.equal(readback.status,200,JSON.stringify(readback));
    assert(si.readback(readback.body,target));
   }
   for (const readback of [
     {...qualified,boot_active:{...settings,drive_ma:12}},
     {...qualified,boot_active:{...settings,address:97}},
     {...qualified,boot_active:settings,qualification:{...qualification,profile_id:'qrp-labs-rev6-25mhz-clk0-8ma'}},
     {...qualified,boot_active:settings,target:{...target,boot_id:'f'.repeat(32)}}
   ]) {
    assert.equal((await check(readback)).status,502);
    assert(!si.readback(readback,target));
   }
  } finally {await request({control:'clear',corruption:null});}
 }
 console.log('Si5351: atomic engine/settings, strict refusal before persistence, stale revision, None/CLK0, inactive retention and A/B/A through production pair');
};

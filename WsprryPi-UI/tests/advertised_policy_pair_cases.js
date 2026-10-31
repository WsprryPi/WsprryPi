// Test-only carrier observation rewriting; all validation and refusal use the
// production Pi coordinator after a real Pico response, with no write dispatch.
'use strict';
const assert=require('node:assert/strict');
const {descriptors}=require('../data/control-contract.js');
module.exports=async call=>{
 const prefix='/api/control/v1/member/';
 const get=resource=>call({method:'GET',path:prefix+resource});
 const clear=()=>call({control:'clear'});
 const inject=(name,value)=>call({control:'corrupt',corruption:{resource:'capabilities',pointer:'/'+name,value}});
 const refuse=async command=>{
  const r=await call(command);assert.notEqual(r.status,200,JSON.stringify(r));
  assert(Array.isArray(r.exchanges),'Pair adapter must report actual dispatches');
  assert.equal(r.exchanges.filter(x=>x.method!=='GET').length,0,'Out-of-policy value must refuse before any Pico write: '+JSON.stringify(r));
 };
 const initial=await get('gpio-drive');assert.equal(initial.status,200,JSON.stringify(initial));
 const target=initial.body.target;
 const setDrive=async value=>{
  const r=await get('gpio-drive');assert.equal(r.status,200,JSON.stringify(r));
  const saved=await call({method:'PUT',path:prefix+'gpio-drive',revision:r.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'gpio-drive',target,gpio_drive_ma:value})});
  assert.equal(saved.status,200,JSON.stringify(saved));
 };
 // Select a policy-compatible saved value while the authenticated policy is broad.
 await setDrive(4);
 const previousDraft=await get('progress');assert.equal(previousDraft.status,200,JSON.stringify(previousDraft));
 const setDraft=async(value,id)=>{const r=await get('progress');const result=await call({method:'POST',path:prefix+'progress/update',revision:r.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'progress',target,request_id:id.repeat(32),fields:{gpio_drive_ma:value}})});assert.equal(result.status,200,JSON.stringify(result));};
 const hadDrive=Object.hasOwn(previousDraft.body.fields,'gpio_drive_ma');
 if(hadDrive && previousDraft.body.fields.gpio_drive_ma!==4)await setDraft(4,'f');
 const drive=await get('gpio-drive'),config=await get('configuration'),progress=await get('progress');
 for(const r of [drive,config,progress])assert.equal(r.status,200,JSON.stringify(r));
 try {
  // The narrowed policy must still admit the immutable fresh 12 mA boot.
  // Eight mA remains outside this policy and must dispatch no write.
  await inject('gpio_drive',{...descriptors.gpio_drive,values:[2,4,12],default:4});
  await refuse({method:'PUT',path:prefix+'gpio-drive',revision:drive.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'gpio-drive',target,gpio_drive_ma:8})});
  const admittedConfig=await get('configuration');assert.equal(admittedConfig.status,200,JSON.stringify(admittedConfig));
  await refuse({method:'POST',path:prefix+'configuration',revision:admittedConfig.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'pico-settings',target,operation:'save',config:{gpio_drive_ma:8}})});
  const admittedProgress=await get('progress');assert.equal(admittedProgress.status,200,JSON.stringify(admittedProgress));
  await refuse({method:'POST',path:prefix+'progress/update',revision:admittedProgress.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'progress',target,request_id:'e'.repeat(32),fields:{gpio_drive_ma:8}})});
  await clear();assert.equal((await get('gpio-drive')).etag,drive.etag);
  assert.equal((await get('configuration')).etag,config.etag);assert.equal((await get('progress')).etag,progress.etag);
  await inject('gpio_drive',{...descriptors.gpio_drive,values:[2],default:2});
  for(const resource of ['gpio-drive','configuration','application'])await refuse({method:'GET',path:prefix+resource});
  await clear();
  const timing=await get('amplifier-timing');assert.equal(timing.status,200,JSON.stringify(timing));
  assert.deepEqual(timing.body.saved,{lead_ms:200,tail_ms:200});
  await inject('amplifier_timing',{...descriptors.amplifier_timing,minimum:100,maximum:300,default_lead_ms:200,default_tail_ms:200});
  for(const timingValue of [{lead_ms:99,tail_ms:200},{lead_ms:200,tail_ms:301}])await refuse({method:'PUT',path:prefix+'amplifier-timing',revision:timing.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'amplifier-timing',target,timing:timingValue})});
  await clear();assert.equal((await get('amplifier-timing')).etag,timing.etag);
  await inject('amplifier_timing',{...descriptors.amplifier_timing,minimum:201,maximum:300,default_lead_ms:201,default_tail_ms:201});
  await refuse({method:'GET',path:prefix+'amplifier-timing'});
 } finally {await clear();if(hadDrive && previousDraft.body.fields.gpio_drive_ma!==4)await setDraft(previousDraft.body.fields.gpio_drive_ma,'a');await setDrive(initial.body.saved);}
 console.log('Advertised policy: actual paired GPIO/timing/config/progress refusals dispatch zero writes; out-of-policy readbacks fail closed');
};

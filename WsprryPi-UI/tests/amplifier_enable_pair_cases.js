// SPDX-License-Identifier: MIT
// Mandatory production coordinator -> selected carrier -> Pico handler/journal.
'use strict';
const assert=require('node:assert/strict');
const {browser,response}=require('./wtp_management_fixture.js');
module.exports=async request=>{
 const path='/api/control/v1/member/';
 const read=async()=>{
  const result=await request({method:'GET',path:path+'application',body:''});
  assert.equal(result.status,200,JSON.stringify(result));
  assert.equal(result.body.contract,'Wsprry-Control/1');
  assert.equal(result.body.application.schema,'application');
  assert.equal(result.body.application.hardware.schema,'hardware');
  return result;
 };
 let before=await read();const target=before.body.target;
 const original=before.body.application.hardware.saved;
 assert(original && Object.hasOwn(original,'amplifier_active_high'),'Amplifier settings are required, not an optional compatibility fixture');
 const reserved=[original.rf_gp,original.button_gp,original.indicator==='external'?original.indicator_gp:null,...original.lpf_gps].filter(v=>v!==null);
 const pin=Array.from({length:23},(_,i)=>i).find(v=>!reserved.includes(v));assert.notEqual(pin,undefined);
 const savedDrive=before.body.application.hardware.gpio_drive.saved;
 const write=async(pins,context=before,patch={})=>request({method:'PUT',path:path+'hardware',revision:context.etag,member_context:context.body.member_context,
  body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hardware',target,pins,...patch})});
 for(const pins of [{...original,amplifier_gp:pin,amplifier_active_high:true},{...original,amplifier_gp:pin,amplifier_active_high:false},{...original,amplifier_gp:null,amplifier_active_high:true},original]){
  before=await read();const changed=await write(pins);
  assert.equal(changed.status,200,JSON.stringify(changed));assert.deepEqual(changed.body.saved,pins);
  const after=await read();assert.deepEqual(after.body.application.hardware.saved,pins);
  assert.equal(after.body.application.hardware.gpio_drive.saved,savedDrive);
  assert.deepEqual(after.body.application.station.station,before.body.application.station.station);
  assert.equal(after.body.application.station.storage_healthy,before.body.application.station.storage_healthy);
  assert.equal(after.body.application.hardware.pending_restart,true,'A/B/A saves retain restart inhibition');
 }
 // Current sparse pin updates preserve the prior authenticated polarity.
 before=await read();assert.equal((await write({...original,amplifier_active_high:false})).status,200);
 before=await read();const sparse={...before.body.application.hardware.saved};delete sparse.amplifier_active_high;
 const preview=await request({method:'POST',path:path+'pins/validate',revision:before.etag,member_context:before.body.member_context,body:JSON.stringify({contract:'Wsprry-Control/1',...sparse})});
 assert.equal(preview.status,200,JSON.stringify(preview));assert.equal(preview.body.valid,true);
 const previewWire=preview.exchanges?.find(e=>e.method==='POST')?.body;if(typeof previewWire==='string')assert(!Object.hasOwn(JSON.parse(previewWire),'amplifier_active_high'));
 before=await read();const preserved=await write(sparse);
 assert.equal(preserved.status,200,JSON.stringify(preserved));assert.equal(preserved.body.saved.amplifier_active_high,false);
 const preservedWire=preserved.exchanges?.find(e=>e.method==='PUT')?.body;if(typeof preservedWire==='string')assert(!Object.hasOwn(JSON.parse(preservedWire).pins,'amplifier_active_high'));
 before=await read();assert.equal((await write(original)).status,200);
 const uiCalls=[];
 const ui=browser(async emitted=>{
  const command={method:emitted.method||'GET',path:emitted.url,body:emitted.body?JSON.stringify(emitted.body):'',headers:emitted.headers||{}};
  const result=await request(command);uiCalls.push({command,result});return response(result.body,result.status,result.etag);
 });
 const selection=await request({method:'GET',path:path+'management',body:''});assert.equal(selection.status,200);
 ui.root.managementState=selection.body;
 if(selection.body.carrier==='usb_physical')ui.root.WtpUi.read=()=>({Transport:'usb'});
 for(const [value,polarity] of [[String(pin),'false'],['','true']]){
  await ui.click('wtp-member-load');
  ui.element('wtp-member-amplifier').value=value;ui.element('wtp-member-amplifier-polarity').value=polarity;
  for(const name of ['wtp-member-amplifier','wtp-member-amplifier-polarity'])ui.element(name).listeners.input?.({target:ui.element(name)});
  await ui.click('wtp-member-validate');
  const count=uiCalls.length;await ui.click('wtp-member-hardware-save');
  const writes=uiCalls.slice(count).filter(v=>v.command.method==='PUT');
  assert.equal(writes.length,1,'Current Pi amplifier controls must emit exactly one hardware update');
  assert.equal(writes[0].result.status,200,JSON.stringify(writes[0]));
  const emitted=JSON.parse(writes[0].command.body);assert.equal(emitted.contract,'Wsprry-Control/1');
  assert.equal(emitted.pins.amplifier_gp,value===''?null:pin);assert.equal(emitted.pins.amplifier_active_high,polarity==='true');
 }
 const draftRead=()=>request({method:'GET',path:path+'progress',body:''});
 for(const [index,pins] of [{...original,amplifier_gp:pin,amplifier_active_high:false},original].entries()){
  const draft=await draftRead();assert.equal(draft.status,200,JSON.stringify(draft));
  const active=await read();
  const saved=await request({method:'POST',path:path+'progress/update',revision:draft.etag,
   body:JSON.stringify({contract:'Wsprry-Control/1',schema:'progress',target,request_id:(index?'d':'c').repeat(32),fields:{pins}})});
  assert.equal(saved.status,200,JSON.stringify(saved));assert.deepEqual(saved.body.fields.pins,pins);
  assert.deepEqual((await read()).body.application.hardware.saved,active.body.application.hardware.saved,'Saving amplifier draft must not apply hardware');
  assert.equal(saved.body.apply_domains.radio,true);
  const currentDraft=await draftRead();assert.equal(currentDraft.status,200,JSON.stringify(currentDraft));
  const applied=await request({method:'POST',path:path+'progress/apply',revision:currentDraft.etag,
   body:JSON.stringify({contract:'Wsprry-Control/1',schema:'progress',target,application:{draft_revision:currentDraft.body.draft_revision,domain:'radio'}})});
  assert.equal(applied.status,200,JSON.stringify(applied));
  assert.deepEqual((await read()).body.application.hardware.saved,pins);
 }
 for(const bad of [{...original,amplifier_gp:original.rf_gp},{...original,amplifier_gp:29},{...original,amplifier_active_high:'true'}]){
  before=await read();const denied=await write(bad);assert.equal(denied.status,400,JSON.stringify(denied));
  assert.equal((await read()).etag,before.etag,'Invalid amplifier plan must refuse before writes');
 }
 for(const patch of [{contract:'Wsprry-Control/2'},{target:{...target,boot_id:'f'.repeat(32)}},{unexpected:true}]){
  before=await read();const denied=await write(original,before,patch);assert.notEqual(denied.status,200,JSON.stringify(denied));assert.equal((await read()).etag,before.etag);
 }
 before=await read();const stale=await write(original,{...before,etag:'"'+ '0'.repeat(64)+'"'});assert([409,412].includes(stale.status),JSON.stringify(stale));assert.equal((await read()).etag,before.etag);
 for(const old of ['/api/v1/member/hardware','/api/control/v1/member/hardware/3']){
  const denied=await request({method:'PUT',path:old,revision:before.etag,body:JSON.stringify({schema:'transmitter-hardware/3',target,pins:original})});assert.notEqual(denied.status,200,JSON.stringify(denied));
 }
 console.log('Amplifier enable: mandatory production read/write, polarity, disable, A/B/A, pin conflicts, typed inputs, identity/revision and old-client refusal passed');
};

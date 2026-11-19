// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),crypto=require('node:crypto').webcrypto,control=require('../data/control-contract.js');
const {create}=require('../data/pi-target-client.js');
const cases=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=name=>structuredClone(cases.find(v=>v.name===name).value);
async function fixture(local=false){
 const context=sample('paired Pi context'),saved=sample('Pi closed target-settings'),calls=[],receipts=new Map();
 if(local)context.authorization=null;
 context.capabilities=['pi-config','pi-scheduling'].map(resource=>({resource,...control.controllerPiDescriptors[resource],read:true,write:true,activation:'next_occurrence',maximum_bytes:32768}));
 saved.binding=structuredClone(context.binding);saved.capability_revision=context.capability_revision;saved.revision=context.configuration_revision;saved.saved.scheduling.enabled=false;
 let lost=false,count=0;
 const reply=(value,status=200,headers={})=>new Response(JSON.stringify(value),{status,headers});
 const fetch=async(url,options)=>{
  if(url.endsWith('/management-session'))return reply({contract:control.contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'e'.repeat(64)});
  calls.push({url,method:options.method,body:options.body&&JSON.parse(options.body)});
  if(url.endsWith('/context'))return reply(context);
  if(options.method==='GET'){
   if(url.includes('/operations/'))return reply(receipts.get(url.split('/').at(-1)));
   return reply(saved);
  }
  const body=JSON.parse(options.body);assert.equal(body.binding.device_id,context.binding.device_id);assert.equal(options.headers['If-Match'],'"'+saved.revision+'"');
  if(body.candidate)saved.saved=structuredClone(body.candidate);
  if(url.endsWith('/enable')){assert.equal(body.candidate_digest,saved.candidate_digest);saved.saved.scheduling.enabled=true;}
  if(url.endsWith('/disable')){saved.saved.scheduling.enabled=false;context.capability_revision='8'.repeat(64);saved.capability_revision=context.capability_revision;}
  saved.revision=String(++count).padStart(64,'0');context.configuration_revision=saved.revision;
  saved.candidate_digest=String(count+10).padStart(64,'0');
  const receipt={contract:control.contract,schema:'target-operation',binding:body.binding,operation_id:body.operation_id,request_digest:'f'.repeat(64),state:'committed',revision:saved.revision,error:null};receipts.set(body.operation_id,receipt);
  if(lost)throw Error('accepted reply lost');return reply(receipt);
 };
 const client=create({device:context.binding.device_id,local,fetch,crypto});await client.read();
 return {client,context,saved,calls,setLost:v=>{lost=v;}};
}
(async()=>{
 for(const local of [false,true]){
  const {client,calls,saved,setLost}=await fixture(local);const writes=()=>calls.filter(v=>v.method!=='GET');
  const solar=client.draft.scheduling.solar;solar.location.locator='EM18IG';solar.enabled=true;solar.day.allow=false;
  solar.morning.wspr_override={'Frequency':'20m 0 30m','Frequency Profile':'existing_common','Band Preferences':structuredClone(client.draft.configuration.WSPR['Band Preferences'])};solar.night.cw_base_hz='14097000.12345678';
  client.setSolar(solar);assert.equal(writes().length,0);assert.equal(client.draft.scheduling.enabled,false);
  await assert.rejects(async()=>client.enable(),{code:'unsaved_schedule'});assert.equal(writes().length,0,'Unsaved enable must not contact the target');
  assert.throws(()=>client.setSolar({...solar,location:{source:'gps',locator:null}}),{code:'candidate_invalid'});
  assert.throws(()=>client.setSolar({...solar,morning:{...solar.morning,lower_mdeg:-90001}}),{code:'candidate_invalid'});
  const save=await client.save();assert.equal(save.state,'committed');await client.read();assert(!client.dirty);assert.deepEqual(client.current.saved.scheduling.solar,solar);
  await client.enable();await client.read();assert.equal(client.current.saved.scheduling.enabled,true);assert(!client.dirty);
  const changed=client.draft.scheduling.solar;changed.evening.enabled=false;changed.evening.cw_base_hz='7040000.12345678';client.setSolar(changed);
  await client.disable();await client.read();assert.equal(client.current.saved.scheduling.enabled,false);assert.equal(client.draft.scheduling.enabled,false);assert.deepEqual(client.draft.scheduling.solar,changed,'Disable preserves the separate unsaved policy');
  setLost(true);await assert.rejects(client.save());const frozen=client.operation;assert(frozen&&client.paused);const n=writes().length;
  await client.check();assert.equal(writes().length,n,'Receipt recovery never repeats Save');setLost(false);await client.read();assert(!client.dirty);assert.deepEqual(saved.saved.scheduling.solar,changed);
  assert.equal(writes().filter(v=>v.url.endsWith('/enable')).length,1,'Only one explicit confirmed enable');
 }
 const remote=await fixture();remote.context.authorization=null;await assert.rejects(remote.client.read(),{code:'target_context_unavailable'});
 const own=await fixture(true);own.context.authorization=sample('paired Pi context').authorization;await assert.rejects(own.client.read(),{code:'target_context_unavailable'});
 console.log('PASS local/paired scheduling: draft isolation, disabled save/readback/digest-bound enable, inactive values, exact precision, no unsaved enable, preserved drafts after disable, and lost-reply receipt-only recovery');
})().catch(error=>{console.error(error);process.exitCode=1;});

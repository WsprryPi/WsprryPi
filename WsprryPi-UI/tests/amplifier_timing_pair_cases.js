// Actual shared editor producer -> protected Pi coordinator -> Pico persistence.
'use strict';
const assert=require('node:assert/strict');
const editor=require('../data/amplifier-timing.js');
module.exports=async request=>{
  const get=()=>request({method:'GET',path:'/api/v1/member/amplifier-timing',body:''});
  let before=await get();assert.equal(before.status,200,JSON.stringify(before));
  const target=before.body.target;
  const node=()=>({disabled:false,hidden:false,value:'',textContent:'',setAttribute(){}});
  const fields=Object.fromEntries(['lead','tail','read','save','discard','adopt','status'].map(k=>[k,node()]));
  const view=editor.create({...fields,exchange:async(method,body,revision)=>{
    const r=await request({method,path:'/api/v1/member/amplifier-timing',body:body?JSON.stringify(body):'',revision});
    if(r.status!==200)throw Error(JSON.stringify(r));
    return {data:r.body,revision:r.etag};
  }});
  view.setAvailability(true);await view.load();
  assert.equal(fields.lead.value,'200');assert.equal(fields.tail.value,'200');
  for(const [lead,tail] of [[0,1000],[1000,0],[200,200]]){
    fields.lead.value=String(lead);fields.tail.value=String(tail);fields.lead.oninput();fields.tail.oninput();
    assert.equal(fields.save.disabled,false);await fields.save.onclick();
    assert.match(fields.status.textContent,/Amplifier timing saved/);
    const saved=await get();assert.deepEqual(saved.body.saved,{lead_ms:lead,tail_ms:tail});
    assert.equal(saved.body.rf_active,false);assert.equal(saved.body.phase,'inactive');
    if(lead!==200)assert.equal(saved.body.pending_restart,true);
  }
  for(const bad of [-1,0.1,1001,'200',null,true,{},[]]){
    before=await get();
    const invalid=await request({method:'PUT',path:'/api/v1/member/amplifier-timing',revision:before.etag,
      body:JSON.stringify({schema:'transmitter-amplifier-timing/1',target,timing:{lead_ms:bad,tail_ms:200}})});
    assert.equal(invalid.status,400,JSON.stringify(invalid));assert.equal((await get()).etag,before.etag);
  }
  for(const change of [{schema:'transmitter-amplifier-timing/2'},{target:{...target,boot_id:'f'.repeat(32)}},{extra:0},{timing:{lead_ms:0}}]){
    before=await get();
    const invalid=await request({method:'PUT',path:'/api/v1/member/amplifier-timing',revision:before.etag,
      body:JSON.stringify({schema:'transmitter-amplifier-timing/1',target,timing:{lead_ms:0,tail_ms:0},...change})});
    assert.equal(invalid.status,400,JSON.stringify(invalid));assert.equal((await get()).etag,before.etag);
  }
  const legacy=await request({method:'GET',path:'/api/v1/member/configuration',body:''});
  assert.equal(legacy.status,200,JSON.stringify(legacy));assert(!Object.hasOwn(legacy.body.config,'amplifier_timing'));
  const written=await request({method:'POST',path:'/api/v1/member/configuration',revision:legacy.etag,
    body:JSON.stringify({schema:'wsprrypi-pico-settings/1',target,operation:'save',config:{expires_utc_s:0}})});
  assert.equal(written.status,200,JSON.stringify(written));assert.deepEqual((await get()).body.saved,{lead_ms:200,tail_ms:200});
  console.log('Amplifier timing: shared UI producer, boundaries, strict negative contracts and legacy preservation passed through production pair');
};

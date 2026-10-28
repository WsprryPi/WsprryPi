// SPDX-License-Identifier: MIT
// Shared UI contract producer through real Pi negotiation/carriers/Pico journals.
const assert=require('node:assert/strict'),udp=require('../data/udp-logging.js');
module.exports=async function(request){
 const path='/api/v1/member/udp-logging';
 const caps=await request({method:'GET',path:path+'/capabilities',body:''});assert.equal(caps.status,200,JSON.stringify(caps));assert(udp.capabilities(caps.body));
 let read=await request({method:'GET',path,body:''});assert.equal(read.status,200,JSON.stringify(read));assert(udp.readback(read.body,caps.body));
 const original=JSON.parse(JSON.stringify(read.body.saved));
 const candidate={...original,enabled:true,destination_ipv4:'192.0.2.40',destination_port:5514,maximum_severity:5,rate:3,burst:6,event_codes:['BOOT','USB_STATE','UDP_TEST']};assert(udp.settings(candidate));
 const envelope=settings=>({schema:caps.body.settings_schema,target:read.body.target,settings});
 let saved=await request({method:'PUT',path,revision:read.etag,body:JSON.stringify(envelope(candidate))});assert.equal(saved.status,200,JSON.stringify(saved));assert(udp.readback(saved.body,caps.body));assert.deepEqual(saved.body.saved,candidate);
 const diagnostic=await request({method:'GET',path:path+'/test/capabilities',body:''});assert.equal(diagnostic.status,200,JSON.stringify(diagnostic));assert(udp.testCapabilities(diagnostic.body,caps.body));
 read=await request({method:'GET',path,body:''});
 const event=await request({method:'POST',path:path+'/test',revision:read.etag,body:JSON.stringify({schema:'udp-logging-test/1',target:read.body.target})});assert.equal(event.status,200,JSON.stringify(event));assert.equal(event.body.schema,'udp-logging-test/1');assert.equal(event.body.accepted,true);assert.equal(event.body.receipt,'unknown');
 const afterEvent=await request({method:'GET',path,body:''});assert.equal(afterEvent.etag,read.etag);assert.equal(BigInt(afterEvent.body.active.counters.generated),BigInt(read.body.active.counters.generated)+1n);
 read=await request({method:'GET',path,body:''});const staleEvent=await request({method:'POST',path:path+'/test',revision:read.etag,body:JSON.stringify({schema:'udp-logging-test/1',target:{...read.body.target,boot_id:'e'.repeat(32)}})});assert.equal(staleEvent.status,400,JSON.stringify(staleEvent));
 let nested={};for(let i=0;i<9;i++)nested={nested};
 const invalid=[{...candidate,rate:9},{...candidate,rate:true},{...candidate,destination_ipv4:'127.0.0.1'},{...candidate,destination_ipv4:'239.1.2.3'},{...candidate,burst:17},{...candidate,extra:false},{...candidate,event_codes:['BOOT','BOOT']},{...candidate,extra:nested}];
 for(const settings of invalid){
  read=await request({method:'GET',path,body:''});const bad=await request({method:'PUT',path,revision:read.etag,body:JSON.stringify(envelope(settings))});assert.equal(bad.status,400,JSON.stringify(bad));
  const after=await request({method:'GET',path,body:''});assert.equal(after.etag,read.etag);assert.deepEqual(after.body.saved,candidate);
 }
 for(const mutation of [body=>({...body,schema:'transmitter-udp-logging/2'}),body=>({...body,target:{...body.target,boot_id:'e'.repeat(32)}}),body=>({...body,extra:false})]){
  read=await request({method:'GET',path,body:''});const bad=await request({method:'PUT',path,revision:read.etag,body:JSON.stringify(mutation(envelope(candidate)))});assert([400,409].includes(bad.status),JSON.stringify(bad));
  const after=await request({method:'GET',path,body:''});assert.equal(after.etag,read.etag);
 }
 read=await request({method:'GET',path,body:''});saved=await request({method:'PUT',path,revision:read.etag,body:JSON.stringify(envelope(original))});assert.equal(saved.status,200,JSON.stringify(saved));assert.deepEqual(saved.body.saved,original);
 console.log('Actual UDP settings pair: negotiated schema, persistence, strict limits/types, boot binding and refusal-before-writes passed');
};

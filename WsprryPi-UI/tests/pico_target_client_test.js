// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),crypto=require('node:crypto').webcrypto;
const {create}=require('../data/pico-target-client.js');
const profiles=Object.fromEntries(['a','b'].map((id,index)=>[id,{id:String(index+1).repeat(32),settings:{'Device ID':id.repeat(32)}}]));
const reply=(data,status=200,headers={})=>new Response(JSON.stringify(data),{status,headers});
const error=code=>({contract:'Wsprry-Control/1',schema:'target-error',error:{code,retryable:false}});
(async()=>{
 const requests=[],pending=[];let bootstrap=0,hold=false,badHint=false,metadataFault=false,metadataSession=null;
 const client=create({crypto,base:'/radio/api/control/v1',fetch:async(url,options)=>{
  requests.push({url,options});
  if(url.endsWith('/host/management-session')){++bootstrap;return reply({contract:'Wsprry-Control/1',schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'c'.repeat(64)});}
  if(url.endsWith('/targets'))return reply({contract:'Wsprry-Control/1',schema:'targets',targets:['a','b'].map(id=>({device_id:id.repeat(32),product:'WsprryPico',name:'Pico '+id,profile_generation:'1',connection_generation:'1'}))});
  const device=url.includes('a'.repeat(32))?'a':'b';
  if(url.endsWith('/management'))return reply({contract:'Wsprry-Control/1',carrier:'usb_physical',host:'',session:metadataSession,connected:true,selection_id:device.repeat(32),...(metadataFault?{unexpected:true}:{})},200,{'X-WsprryPi-Target-Context':device.repeat(32)});
  if(options.method==='POST')return new Promise(resolve=>pending.push(()=>resolve(hold?reply(error('recovery_required'),503,{'Location':'/api/control/v1/targets/'+device.repeat(32)+'/operations/'+'9'.repeat(32),'X-WsprryPi-Recovery-Resource':badHint?'https://untrusted.invalid/':'restart'}):reply({contract:'Wsprry-Control/1',saved:true},200))));
  return reply({contract:'Wsprry-Control/1',schema:'pico-settings'},200);
 }});
 await assert.rejects(client.open({id:'bad'}),/saved_profile_required/);
 const a=await client.open(profiles.a),b=await client.open(profiles.b);
 assert.equal(await a.generation(),'1','Draft keys use the current server-issued profile generation');
 metadataFault=true;await assert.rejects(client.open(profiles.a),/target_context_unavailable/);
 await assert.rejects(a.generation(),error=>error.cause?.code==='invalid_management_metadata');metadataFault=false;
 for(const value of [{},{selector:'a'.repeat(32),device_id:'b'.repeat(32),boot_id:'1'.repeat(32),generation:'1'},
     {selector:'a'.repeat(32),device_id:'a'.repeat(32),boot_id:'1'.repeat(32),generation:'1'},
     {selector:'a'.repeat(32),device_id:'a'.repeat(32),boot_id:'1'.repeat(32),generation:'18446744073709551616'}]) {
  metadataSession=value;await assert.rejects(client.open(profiles.a),/target_context_unavailable/);
 }
 metadataSession=null;
 await assert.rejects(a.request('jobs',{method:'POST',revision:'"x"',body:{}}),/unsupported_resource/);
 await assert.rejects(a.request('configuration',{method:'POST',body:{}}),/revision_required/);
 const count=requests.length;
 const saveA=a.request('configuration',{method:'POST',revision:'"1"',body:{password:'never-record-this'}});
 const saveB=b.request('configuration',{method:'POST',revision:'"2"',body:{password:'nor-this'}});
 while(pending.length<2)await new Promise(resolve=>setImmediate(resolve));
 assert.equal(bootstrap,1,'Concurrent targets share one browser-session bootstrap');
 assert.equal(client.operations().length,2);
 assert(!JSON.stringify(client.operations()).includes('password'));assert(!JSON.stringify(client.operations()).includes('never-record'));
 a.retire();pending[1]();const resultB=await saveB;pending[0]();const resultA=await saveA;
 assert.equal(resultA.response.status,200);assert.equal(resultB.response.status,200);
 const writes=requests.slice(count).filter(item=>item.url.endsWith('/configuration'));
 assert.equal(writes.length,2);assert.notEqual(writes[0].options.headers['X-WsprryPi-Operation-ID'],writes[1].options.headers['X-WsprryPi-Operation-ID']);
 for(const item of writes){assert.equal(item.options.redirect,'error');assert.equal(item.options.credentials,'same-origin');assert.equal(item.options.headers['X-WsprryPi-CSRF'],'c'.repeat(64));assert(item.url.includes('/targets/'+item.options.headers['X-WsprryPi-Target-Context']+'/pico/'));}
 assert(client.operations().every(item=>item.state==='committed'),'A completed old-target write remains attached to A');
 await assert.rejects(a.request('configuration'),/connection_changed/);
 hold=true;const heldPromise=b.request('configuration',{method:'POST',revision:'"3"',body:{}});
 while(pending.length<3)await new Promise(resolve=>setImmediate(resolve));pending[2]();
 const held=await heldPromise;assert.equal(held.recovery.device,'b'.repeat(32));assert.equal(held.recovery.resource,'restart');assert.equal(held.recovery.read,'hostname');assert.equal(held.recovery.operation,'9'.repeat(32));
 const heldRecord=client.operations().find(item=>item.operation===held.operation);assert.equal(heldRecord.state,'refused');assert.deepEqual(heldRecord.recovery,held.recovery);
 badHint=true;const malformed=b.request('configuration',{method:'POST',revision:'"4"',body:{}});
 while(pending.length<4)await new Promise(resolve=>setImmediate(resolve));pending[3]();await assert.rejects(malformed,error=>error.code==='request_unconfirmed'&&error.cause.code==='invalid_recovery_metadata');
 assert.equal(requests.filter(item=>item.url.endsWith('/configuration')&&item.options.method==='POST').length,4,'No mutation retry or redirect');
 for(const base of ['https://untrusted.invalid/api/control/v1','//untrusted.invalid/api/control/v1','/../api/control/v1','/radio/./api/control/v1','/radio%2felse/api/control/v1','/api/control/v1?target=else'])
  assert.throws(()=>create({fetch:async()=>{},crypto,base}),/invalid_base/,base);
 assert.doesNotThrow(()=>create({fetch:async()=>{},crypto,base:'/radio.pi/api/control/v1'}));
 let drop=true,firstOperation;
 const bounded=create({crypto,fetch:async(url,options)=>{
  if(url.endsWith('/management'))return reply({contract:'Wsprry-Control/1',carrier:'usb_physical',host:'',session:null,connected:true,selection_id:'a'.repeat(32)},200,{'X-WsprryPi-Target-Context':'a'.repeat(32)});
  if(url.endsWith('/host/management-session'))return reply({contract:'Wsprry-Control/1',schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'c'.repeat(64)});
  if(drop){drop=false;firstOperation=options.headers['X-WsprryPi-Operation-ID'];throw Error('Lost reply');}
  return reply({contract:'Wsprry-Control/1',saved:true});
 }}),boundedTarget=await bounded.open(profiles.a);
 await assert.rejects(boundedTarget.request('configuration',{method:'POST',revision:'"1"',body:{}}),/request_unconfirmed/);
 for(let i=0;i<270;i++)await boundedTarget.request('configuration',{method:'POST',revision:'"1"',body:{}});
 assert.equal(bounded.operations().length,256);assert(bounded.operations().some(item=>item.operation===firstOperation&&item.state==='unconfirmed'),'Terminal eviction must retain unresolved requests');
 bounded.reviewed(firstOperation,'a'.repeat(32));assert(bounded.operations().some(item=>item.operation===firstOperation&&item.state==='unknown'&&item.reviewed));
 await boundedTarget.request('configuration',{method:'POST',revision:'"1"',body:{}});
 assert(!bounded.operations().some(item=>item.operation===firstOperation),'Explicitly reviewed history can leave bounded retention');
 let refusalCode='capacity',sessions=0;
 const ambiguous=create({crypto,fetch:async(url)=>{
  if(url.endsWith('/management'))return reply({contract:'Wsprry-Control/1',carrier:'usb_physical',host:'',session:null,connected:true,selection_id:'a'.repeat(32)},200,{'X-WsprryPi-Target-Context':'a'.repeat(32)});
  if(url.endsWith('/host/management-session')){++sessions;return reply({contract:'Wsprry-Control/1',schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'c'.repeat(64)});}
  return reply(error(refusalCode),refusalCode==='csrf_failed'?403:409);
 }}),ambiguousTarget=await ambiguous.open(profiles.a);
 for(const code of ['capacity','csrf_failed','connection_changed','profile_changed','revision_conflict']){
  refusalCode=code;const result=await ambiguousTarget.request('hostname',{method:'PUT',revision:'"1"',body:{hostname:'draft'}});
  assert.equal(ambiguous.operations().find(item=>item.operation===result.operation).state,'unconfirmed',code+' can follow an accepted browser-internal PUT retry');
 }
 assert.equal(sessions,2,'A CSRF refusal renews browser protection before another explicit operation');
 console.log('PASS: captured target contexts, concurrent accepted-write tracking, one CSRF bootstrap, no secret records, explicit recovery metadata, retired handles, and no mutation retries');
})().catch(error=>{console.error(error);process.exitCode=1;});

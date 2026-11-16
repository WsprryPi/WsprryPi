// SPDX-License-Identifier: MIT
// Independent production management context/journal and actual Pico USB APIs.
'use strict';
const assert=require('node:assert/strict'),crypto=require('node:crypto'),readline=require('node:readline');
const {spawnFixtureChild:spawn,finishChildren}=require('./browser_fixture_process.js');
const [binary,driver]=process.argv.slice(2);if(!binary||!driver)throw Error('Explicit management fixture and candidate Pico USB driver required');
(async()=>{
 let child,failure;
 try{
  child=spawn(binary,[driver],{stdio:['pipe','pipe','inherit']});const queue=[],pending=[];
  readline.createInterface({input:child.stdout}).on('line',line=>pending.length?pending.shift()(line):queue.push(line));
  const line=()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('USB management fixture deadline')),45000);pending.push(value=>{clearTimeout(timer);resolve(value);});});
  assert.equal(await line(),'READY');
  const call=async command=>{child.stdin.write(JSON.stringify(command)+'\n');const result=JSON.parse(await line());assert.equal(result.execution_ready,false,'Settings must not construct/select a WTP execution backend');return result;};
  const operation=()=>crypto.randomBytes(16).toString('hex');
  const before=await call({control:'status'});assert.equal(before.status,200);assert.equal(before.body.job.owner_id,null);assert.equal(before.body.job.output_active,false);
  let network=await call({resource:'network',method:'GET'});assert.equal(network.status,200,JSON.stringify(network));
  const enabled=await call({resource:'network',method:'PUT',revision:network.etag,context:network.context,operation_id:operation(),body:JSON.stringify({contract:'Wsprry-Control/1',enabled:true})});
  assert.equal(enabled.status,200,JSON.stringify(enabled));assert.equal(enabled.body.requested_enabled,true);
  network=await call({resource:'network',method:'GET'});assert.equal(network.status,200);assert.equal(network.body.enabled,true);
  const networkOperation=operation();
  const networkRequest={resource:'network',method:'PUT',revision:network.etag,context:network.context,operation_id:networkOperation,body:JSON.stringify({contract:'Wsprry-Control/1',enabled:false})};
  const disabled=await call(networkRequest);assert.equal(disabled.status,200,JSON.stringify(disabled));
  assert.equal(disabled.body.requested_enabled,false,'Acceptance retains the requested transition');
  const replayedNetwork=await call(networkRequest);assert.equal(replayedNetwork.status,200,JSON.stringify(replayedNetwork));
  assert.deepEqual(replayedNetwork.body,disabled.body,'Network replay returns the original observation');
  network=await call({resource:'network',method:'GET'});assert.equal(network.status,200);assert.equal(network.body.enabled,false);
  let name=await call({resource:'hostname',method:'GET'});assert.equal(name.status,200,JSON.stringify(name));
  const nameSave=await call({resource:'hostname',method:'PUT',revision:name.etag,context:name.context,operation_id:operation(),body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:name.body.target,hostname:'Independent.local'})});
  assert.equal(nameSave.status,200,JSON.stringify(nameSave));assert.equal(nameSave.body.saved,'independent.local');
  let wifi=await call({resource:'wifi',method:'GET'});assert.equal(wifi.status,200);assert.equal(wifi.body.wifi,null);
  const secret='synthetic-private-password';
  const wifiBody={contract:'Wsprry-Control/1',schema:'wifi',target:wifi.body.target,wifi:{ssid:'Independent radio',password:secret,time_server:'pool.ntp.org'}};
  const saveRequest={resource:'wifi',method:'PUT',revision:wifi.etag,context:wifi.context,operation_id:operation(),body:JSON.stringify(wifiBody)};
  const saved=await call(saveRequest);assert.equal(saved.status,200,JSON.stringify(saved));assert.equal(saved.body.wifi.password,null);assert(!JSON.stringify(saved).includes(secret));
  const repeated=await call(saveRequest);assert.equal(repeated.status,saved.context===wifi.context?200:409);
  wifi=await call({resource:'wifi',method:'GET'});assert.equal(wifi.body.generation,saved.body.generation,'Replay must not dispatch another native write');assert.equal(wifi.body.wifi.ssid,'Independent radio');
  let journal=await call({control:'journal'});assert(!JSON.stringify(journal.body).includes(secret));const count=journal.body.receipts.length;
  const invalid=await call({resource:'wifi',method:'PUT',revision:wifi.etag,operation_id:operation(),body:JSON.stringify({...wifiBody,wifi:{...wifiBody.wifi,password:'short'}})});assert.equal(invalid.status,400);
  const wrong=await call({resource:'wifi',method:'PUT',revision:wifi.etag,operation_id:operation(),body:JSON.stringify({...wifiBody,target:{...wifiBody.target,boot_id:'0'.repeat(32)}})});assert.equal(wrong.status,409);
  journal=await call({control:'journal'});assert.equal(journal.body.receipts.length,count,'Native validation refusals must not create operation records');
  let log=await call({resource:'logging',method:'GET'});assert.equal(log.status,200,JSON.stringify(log));
  const logSave=await call({resource:'logging',method:'PUT',revision:log.etag,operation_id:operation(),body:JSON.stringify({contract:'Wsprry-Control/1',schema:'logging',target:log.body.target,logging:{enabled:false,maximum_severity:3}})});assert.equal(logSave.status,200,JSON.stringify(logSave));assert.deepEqual(logSave.body.saved,{enabled:false,maximum_severity:3});
  name=await call({resource:'hostname',method:'GET'});
  const ap=await call({resource:'ap',method:'POST',revision:name.etag,operation_id:operation(),body:JSON.stringify({contract:'Wsprry-Control/1',schema:'ap-request',target:name.body.target})});assert.equal(ap.status,202,JSON.stringify(ap));assert.deepEqual(ap.body,{contract:'Wsprry-Control/1',requested:true});
  // This software producer has no restart callback. A 5xx reply is retained as
  // uncertain; only fresh boot-bound hostname readback plus explicit review
  // releases the settings hold. No automatic action retry is permitted.
  name=await call({resource:'hostname',method:'GET'});const restartId=operation();
  const restart=await call({resource:'restart',method:'POST',revision:name.etag,operation_id:restartId,body:JSON.stringify({contract:'Wsprry-Control/1'})});assert.equal(restart.status,503,JSON.stringify(restart));assert.equal(restart.body.state,'unknown');
  wifi=await call({resource:'wifi',method:'GET'});assert.equal(wifi.status,200);
  const blocked=await call({resource:'wifi',method:'DELETE',revision:wifi.etag,operation_id:operation(),body:JSON.stringify({...wifiBody,wifi:null})});assert.equal(blocked.status,503);
  assert.equal(restart.recovery_operation,restartId);assert.equal(restart.recovery_resource,'restart');
  assert.equal(blocked.body.error.code,'recovery_required');assert.equal(blocked.recovery_operation,restartId);assert.equal(blocked.recovery_resource,'restart');
  const freshName=await call({resource:'hostname',method:'GET'});assert.equal(freshName.status,200);
  const reviewed=await call({control:'reconcile',operation_id:restartId,readback_revision:freshName.readback_revision});assert.equal(reviewed.status,200,JSON.stringify(reviewed));assert.equal(reviewed.body.state,'unknown');
  wifi=await call({resource:'wifi',method:'GET'});
  const removed=await call({resource:'wifi',method:'DELETE',revision:wifi.etag,operation_id:operation(),body:JSON.stringify({...wifiBody,wifi:null})});assert.equal(removed.status,200,JSON.stringify(removed));assert.equal(removed.body.wifi,null);
  const after=await call({control:'status'});assert.equal(after.body.job.owner_id,null);assert.equal(after.body.job.output_active,false);assert.equal(after.body.job.state,before.body.job.state);
  console.log('PASS: independent USB context, actual framing/ACK/Pico aliases, durable redacted replay, refused inputs before journaling, explicit action recovery, no WTP backend/claim/output');
 }catch(error){failure=error;}
 await finishChildren({failure,children:[child]});
})().catch(error=>{console.error(error);process.exitCode=1;});

// SPDX-License-Identifier: MIT
// Independent production management context/journal and actual Pico USB APIs.
'use strict';
const assert=require('node:assert/strict'),crypto=require('node:crypto'),readline=require('node:readline');
const {spawnFixtureChild:spawn,finishChildren,waitForExit}=require('./browser_fixture_process.js');
const [binary,driver,mode]=process.argv.slice(2);if(!binary||!driver||mode&&mode!=='--paired-pi')throw Error('Explicit management fixture and candidate Pico USB driver required');
(async()=>{
 let child,failure;
 try{
  child=spawn(binary,[driver,...(mode?[mode]:[])],{stdio:['pipe','pipe','inherit']});const queue=[],pending=[];let closedError;
  child.once('close',(code,signal)=>{closedError=Error('USB management fixture closed: '+(signal||code));while(pending.length)pending.shift().reject(closedError);});
  readline.createInterface({input:child.stdout}).on('line',line=>pending.length?pending.shift()(line):queue.push(line));
  const line=()=>queue.length?Promise.resolve(queue.shift()):closedError?Promise.reject(closedError):new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('USB management fixture deadline')),45000);const receive=value=>{clearTimeout(timer);resolve(value);};receive.reject=error=>{clearTimeout(timer);reject(error);};pending.push(receive);});
  assert.equal(await line(),'READY');
  const call=async command=>{
   // The paired mode uses the actual listener's 10/s source quota. Pace the
   // fixture rather than weakening production admission or retrying a write.
   if(mode)await new Promise(resolve=>setTimeout(resolve,110));
   child.stdin.write(JSON.stringify(command)+'\n');const result=JSON.parse(await line());assert.equal(result.execution_selected,false,'Settings/time must not select a WTP execution backend');assert.equal(result.execution_ready,false,'Settings must not construct/select a WTP execution backend');return result;
  };
  const refreshOwner=async()=>{
   // Refresh only between operation chains: observation can retire a browser
   // context, and must never be inserted between a write and its exact replay.
   await new Promise(resolve=>setTimeout(resolve,1100));
   assert.equal((await call({control:'owner',resource:'inventory'})).status,200);
   assert.equal((await call({control:'owner',resource:'context'})).status,200);
  };
  const operation=()=>crypto.randomBytes(16).toString('hex');
  const before=await call({control:'status'});assert.equal(before.status,200);assert.equal(before.body.job.owner_id,null);assert.equal(before.body.job.output_active,false);
  if(!mode){
   const waitTime=async predicate=>{const deadline=Date.now()+8000;for(;;){const observed=await call({control:'time-status'});if(predicate(observed.body.time))return observed;assert(Date.now()<deadline,'Management time-owner deadline: '+JSON.stringify(observed.body));await new Promise(resolve=>setTimeout(resolve,50));}};
   await call({control:'time-owner',qualified:false});
   await waitTime(time=>time?.reason==='host_utc_unqualified');
   await call({control:'time-owner',qualified:true});
   const accepted=await waitTime(time=>time?.accepted===true&&time?.selected===true);
   assert.equal(accepted.body.time.selected_source,'controller');
   const native=await call({control:'operations'});assert.equal(native.status,200);
   assert(['synchronized','holdover'].includes(native.body.standalone.clock_state),JSON.stringify(native.body));
   assert.equal(native.body.job.owner_id,null);assert.equal(native.body.job.output_active,false);
   await call({control:'time-owner',qualified:false});
   await waitTime(time=>time?.reason==='host_utc_unqualified');
   console.log('PASS: independent management owner supplies actual Pico UTC without a browser/WTP backend; loss of host qualification refuses new samples without output');
  }
  if(mode){
   const gateway=await call({resource:'context',method:'GET'});assert.equal(gateway.status,200,JSON.stringify(gateway));
   assert.equal(gateway.body.schema,'target-context');assert.equal(gateway.body.target.product,'WsprryPico');
   assert.deepEqual(gateway.body.gateway.parent,{device_id:'d'.repeat(32),boot_id:'b'.repeat(32),profile_generation:'1',connection_generation:'1'});
   assert.deepEqual(gateway.body.gateway.child,gateway.body.binding);
   assert.deepEqual(gateway.body.authorization,{pair_id:'c'.repeat(32),authorization_generation:'1',renewal_required:false});
   assert.equal(gateway.body.gateway.pair_id,'c'.repeat(32));assert.equal(gateway.body.gateway.authorization_generation,'1');
   assert.match(gateway.body.capability_revision,/^[0-9a-f]{64}$/);assert.match(gateway.body.configuration_revision,/^[0-9a-f]{64}$/);
   for(const resource of ['configuration','hostname','wifi','network','status','schedules/stop'])assert(gateway.body.gateway.resources.includes(resource));
   for(const resource of ['jobs','firmware','resume','authorities/register'])assert(!gateway.body.gateway.resources.includes(resource));
   const oldName=await call({resource:'hostname',method:'GET'});assert.equal(oldName.status,200);
   assert.equal((await call({control:'reconnect-usb'})).status,200);
   const staleId=operation();const stale=await call({resource:'hostname',method:'PUT',revision:oldName.etag,context:oldName.context,operation_id:staleId,
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:oldName.body.target,hostname:'Must-not-reach-Pico.local'})});
   assert.equal(stale.status,409,JSON.stringify(stale));
   assert(!(await call({control:'journal'})).body.receipts.some(entry=>entry.receipt.operation_id===staleId),'Same-boot reconnect must refuse the old context before reservation');
   const withdrawn=await call({control:'inventory'});assert.equal(withdrawn.status,200);assert.equal(withdrawn.body.targets.length,0);
   const reenrolled=await call({control:'inventory'});assert.equal(reenrolled.status,200);assert.equal(reenrolled.body.targets.length,1);
   const freshGateway=await call({resource:'context',method:'GET'});assert.equal(freshGateway.status,200,JSON.stringify(freshGateway));
   assert.deepEqual(freshGateway.body.gateway.parent,gateway.body.gateway.parent);assert.equal(freshGateway.body.binding.boot_id,gateway.body.binding.boot_id);
   assert.notEqual(freshGateway.body.binding.connection_generation,gateway.body.binding.connection_generation);
   const freshName=await call({resource:'hostname',method:'GET'});assert.equal(freshName.status,200);assert.notEqual(freshName.body.saved,'must-not-reach-pico.local');
   const foreignId=operation();assert.equal((await call({control:'seed-foreign-receipt',operation_id:foreignId})).status,200);
   const foreignReceipt=await call({resource:'operations/'+foreignId,method:'GET'});assert.equal(foreignReceipt.status,404,JSON.stringify(foreignReceipt));
   assert((await call({control:'journal'})).body.receipts.some(entry=>entry.receipt.operation_id===foreignId),'Another scope remains retained, never adopted or erased');
   const refusedReview=await call({control:'reconcile',operation_id:foreignId,readback_revision:freshName.readback_revision});assert.equal(refusedReview.status,403,JSON.stringify(refusedReview));
   const unknownId=operation();assert.equal((await call({control:'seed-foreign-receipt',operation_id:unknownId,unknown:true})).status,200);
   let reviewName=await call({resource:'hostname',method:'GET'});assert.equal(reviewName.status,200);
   const reviewBody={contract:'Wsprry-Control/1',schema:'target-operation-review',binding:reviewName.binding,operation_id:unknownId,readback_revision:reviewName.readback_revision};
   // Actual steady-clock expiry, without weakening or replacing target time.
   await new Promise(resolve=>setTimeout(resolve,31000));
   const expiredReview=await call({resource:'operations/'+unknownId+'/review',method:'POST',context:reviewName.context,body:JSON.stringify(reviewBody)});
   assert.equal(expiredReview.status,409,JSON.stringify(expiredReview));
   reviewName=await call({resource:'hostname',method:'GET'});reviewBody.readback_revision=reviewName.readback_revision;
   const reviewedForeign=await call({resource:'operations/'+unknownId+'/review',method:'POST',context:reviewName.context,body:JSON.stringify(reviewBody)});
   assert.equal(reviewedForeign.status,200,JSON.stringify(reviewedForeign));assert.equal(reviewedForeign.body.schema,'target-operation-reviewed');
   assert.equal(reviewedForeign.body.operation_id,unknownId);assert.equal(reviewedForeign.body.readback_revision,reviewBody.readback_revision);
   assert.notEqual(reviewedForeign.body.binding.connection_generation,reviewBody.binding.connection_generation);
   const retainedForeign=(await call({control:'journal'})).body.receipts.find(entry=>entry.receipt.operation_id===unknownId);
   assert.equal(retainedForeign.receipt.state,'unknown');assert.equal(retainedForeign.reconciled_revision,reviewBody.readback_revision);assert.equal(retainedForeign.response,null);
   assert.equal((await call({resource:'operations/'+unknownId,method:'GET'})).status,404,'Review must not expose another pair receipt');
   const delayedId=operation();
   const delayed=await call({resource:'hostname',method:'PUT',revision:reviewName.etag,context:reviewName.context,operation_id:delayedId,
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:reviewName.body.target,hostname:'Delayed-after-review.local'})});
   assert.equal(delayed.status,409,JSON.stringify(delayed));
   assert(!(await call({control:'journal'})).body.receipts.some(entry=>entry.receipt.operation_id===delayedId));
   const afterReview=await call({resource:'hostname',method:'GET'});assert.equal(afterReview.status,200);assert.equal(afterReview.body.saved,reviewName.body.saved);
   assert.equal((await call({control:'owner',resource:'inventory'})).status,200);
   const ownerContext=await call({control:'owner',resource:'context'});assert.equal(ownerContext.status,200,JSON.stringify(ownerContext));
   const ownerName=await call({control:'owner',resource:'hostname'});assert.equal(ownerName.status,200,JSON.stringify(ownerName));
   assert.notEqual(ownerContext.context,freshGateway.context,'Controller context must not expose the parent wire context');
   const parentUnknown=operation();
   assert.equal((await call({control:'seed-foreign-receipt',operation_id:parentUnknown,unknown:true})).status,200);
   const refusedId=operation();
   const parentBarrier=await call({control:'owner',resource:'hostname',method:'PUT',context:ownerName.context,revision:ownerName.etag,operation_id:refusedId,
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:ownerName.body.target,hostname:'Must-not-dispatch.local'})});
   assert.equal(parentBarrier.status,409,JSON.stringify(parentBarrier));assert.equal(parentBarrier.recovery_operation,parentUnknown);
   const refusedRecord=(await call({control:'owner-journal'})).body.receipts.find(entry=>entry.receipt.operation_id===refusedId);
   assert.equal(refusedRecord.receipt.state,'rejected','Authenticated pre-dispatch refusal must not create a second uncertainty');
   assert(!(await call({control:'owner-journal'})).body.receipts.some(entry=>entry.receipt.operation_id===parentUnknown),'Controller must not fabricate the former manager receipt');
   assert.equal((await call({control:'owner-reopen'})).status,200,'Recovery does not depend on the controller RAM hint');
   assert.equal((await call({control:'owner',resource:'context'})).status,200);
   const parentRead=await call({control:'owner',resource:'hostname'});assert.equal(parentRead.status,200,JSON.stringify(parentRead));
   const parentReviewed=await call({control:'owner',resource:'operations/'+parentUnknown+'/review',method:'POST',
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'target-operation-review',binding:parentRead.binding,operation_id:parentUnknown,readback_revision:parentRead.readback_revision})});
   assert.equal(parentReviewed.status,200,JSON.stringify(parentReviewed));
   assert.equal((await call({resource:'context',method:'GET'})).status,200);
   assert.equal((await call({resource:'operations/'+parentUnknown,method:'GET'})).status,404,'Review does not disclose the former manager receipt');
   assert.equal((await call({control:'owner',resource:'context'})).status,200);
   await refreshOwner();
   const readyOwnerName=await call({control:'owner',resource:'hostname'});assert.equal(readyOwnerName.status,200);
   const ownerOperation=operation();
   const ownerSave={control:'owner',resource:'hostname',method:'PUT',context:readyOwnerName.context,revision:readyOwnerName.etag,operation_id:ownerOperation,
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:ownerName.body.target,hostname:'Managed-through-Pi.local'})};
   const badOwnerId=operation(),badOwner={...ownerSave,operation_id:badOwnerId,body:JSON.stringify({...JSON.parse(ownerSave.body),contract:'Wsprry-Control/2'})};
   assert.equal((await call(badOwner)).status,400);
   const wrongOwnerId=operation(),wrongOwner={...ownerSave,operation_id:wrongOwnerId,body:JSON.stringify({...JSON.parse(ownerSave.body),target:{...ownerName.body.target,boot_id:'0'.repeat(32)}})};
   assert.equal((await call(wrongOwner)).status,409);
   const beforeOwnerJournal=(await call({control:'owner-journal'})).body;
   assert(!beforeOwnerJournal.receipts.some(entry=>[badOwnerId,wrongOwnerId].includes(entry.receipt.operation_id)),'Wrong contract/boot must refuse before controller reservation');
   const ownerSaved=await call(ownerSave);assert.equal(ownerSaved.status,200,JSON.stringify(ownerSaved));
   const ownerReplay=await call(ownerSave);assert.equal(ownerReplay.status,200,JSON.stringify(ownerReplay));assert.deepEqual(ownerReplay.body,ownerSaved.body);
   const ownReceipt=await call({control:'owner',resource:'operations/'+ownerOperation});assert.equal(ownReceipt.status,200,JSON.stringify(ownReceipt));
   assert.equal(ownReceipt.body.operation_id,ownerOperation);assert.equal(ownReceipt.body.state,'committed');
   assert.equal((await call({control:'owner',resource:'operations/'+parentUnknown})).status,404,'Another manager receipt stays private through the controller');
   assert.equal((await call({control:'owner',resource:'context'})).status,200);
   // Validate the pure success-family mapping with real current native reads,
   // including unconfigured resources and nested/conditional schema families.
   // This is structural response checking only, never mutation authority.
   const invalidReplies=[];
   await refreshOwner();let inventoryObserved=performance.now();
   for(const resource of ['configuration','application','station','hardware','pins','gpio-drive','gpio-drive-progress','amplifier-timing','filter-selector','si5351','gps','gray-line','progress','udp-logging','udp-logging/capabilities','udp-logging/test/capabilities','wifi','hostname','logging','network']){
    if(performance.now()-inventoryObserved>=5000){await refreshOwner();inventoryObserved=performance.now();}
    const actual=await call({control:'owner',resource,validate_response:true});
    if(actual.status!==200||actual.response_valid!==true)invalidReplies.push({resource,status:actual.status,body:actual.body});
   }
   assert.equal(invalidReplies.length,0,JSON.stringify(invalidReplies));
   let ownerFresh;
   for(const fault of ['malformed-reply','malformed-error']){
    await refreshOwner();ownerFresh=await call({control:'owner',resource:'hostname'});assert.equal(ownerFresh.status,200,JSON.stringify(ownerFresh));
    const malformedId=operation(),hostname=fault==='malformed-reply'?'Malformed-authenticated-reply.local':'Malformed-authenticated-error.local';
    const malformedOwner={control:'owner',resource:'hostname',method:'PUT',context:ownerFresh.context,revision:ownerFresh.etag,operation_id:malformedId,fault,
     body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:ownerFresh.body.target,hostname})};
    const malformedReply=await call(malformedOwner);assert.equal(malformedReply.status,503,JSON.stringify(malformedReply));assert.equal(malformedReply.recovery_operation,malformedId);
    const malformedJournal=(await call({control:'owner-journal'})).body,unknownMalformed=malformedJournal.receipts.find(entry=>entry.receipt.operation_id===malformedId);
    assert.equal(unknownMalformed.receipt.state,'unknown');assert.equal(unknownMalformed.response,null);
    assert(!JSON.stringify(malformedJournal).includes('unexpected_reply_field'),'Malformed authenticated response never enters a terminal replay record');
    const malformedBarrier=await call({...malformedOwner,fault:undefined,operation_id:operation()});assert.equal(malformedBarrier.status,503);assert.equal(malformedBarrier.recovery_operation,malformedId);
    const malformedRead=await call({control:'owner',resource:'hostname'});assert.equal(malformedRead.status,200);assert.equal(malformedRead.body.saved,hostname.toLowerCase());
    const malformedReviewed=await call({control:'owner',resource:'operations/'+malformedId+'/review',method:'POST',
     body:JSON.stringify({contract:'Wsprry-Control/1',schema:'target-operation-review',binding:malformedRead.binding,operation_id:malformedId,readback_revision:malformedRead.readback_revision})});
    assert.equal(malformedReviewed.status,200,JSON.stringify(malformedReviewed));assert.equal((await call({control:'owner',resource:'context'})).status,200);
    ownerFresh=await call({control:'owner',resource:'hostname'});assert.equal(ownerFresh.status,200);
   }

   await refreshOwner();ownerFresh=await call({control:'owner',resource:'hostname'});assert.equal(ownerFresh.status,200);
   const lostOwnerId=operation();
   const lostOwner={...ownerSave,context:ownerFresh.context,revision:ownerFresh.etag,operation_id:lostOwnerId,fault:'lose-reply',
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'hostname',target:ownerFresh.body.target,hostname:'Unconfirmed-owner-save.local'})};
   const lostOwnerReply=await call(lostOwner);assert.equal(lostOwnerReply.status,503,JSON.stringify(lostOwnerReply));assert.equal(lostOwnerReply.recovery_operation,lostOwnerId);
   const ownerBarrier=await call({...lostOwner,fault:undefined,operation_id:operation()});assert.equal(ownerBarrier.status,503,JSON.stringify(ownerBarrier));assert.equal(ownerBarrier.recovery_operation,lostOwnerId);
   const ownerReadback=await call({control:'owner',resource:'hostname'});assert.equal(ownerReadback.status,200,JSON.stringify(ownerReadback));
   const ownerReviewed=await call({control:'owner',resource:'operations/'+lostOwnerId+'/review',method:'POST',
    body:JSON.stringify({contract:'Wsprry-Control/1',schema:'target-operation-review',binding:ownerReadback.binding,operation_id:lostOwnerId,readback_revision:ownerReadback.readback_revision})});
   assert.equal(ownerReviewed.status,200,JSON.stringify(ownerReviewed));assert.equal(ownerReviewed.body.schema,'target-operation-reviewed');
   const ownerStale=await call({...lostOwner,fault:undefined,operation_id:operation()});assert.equal(ownerStale.status,409,JSON.stringify(ownerStale));
   // Exercise the shipped managed-Pico client through the real remote owner,
   // TLS listener, independent parent journal and candidate USB handlers.
   await refreshOwner();
   let loseShippedReply=false;
   const shipped=require('../data/pico-target-client.js').create({crypto:crypto.webcrypto,fetch:async(url,options={})=>{
    const headers=new Headers(options.headers||{});
    if(url.endsWith('/host/management-session'))return new Response(JSON.stringify({contract:'Wsprry-Control/1',schema:'controller-browser-session'}),{headers:{'Content-Type':'application/json','X-WsprryPi-CSRF':'f'.repeat(64)}});
    assert.equal(headers.has('X-WsprryPi-Profile-ID'),false,'A paired child must not invent a local WTP profile');
    const resource=url.endsWith('/targets')?'targets':url.split('/targets/')[1]?.slice(33).replace(/^pico\//,'');
    assert(resource);const command={control:'owner',resource,method:options.method||'GET',...(options.body?{body:options.body}:{}),
     ...(headers.has('X-WsprryPi-Target-Context')?{context:headers.get('X-WsprryPi-Target-Context')}:{}),
     ...(headers.has('If-Match')?{revision:headers.get('If-Match')}:{}),
     ...(headers.has('X-WsprryPi-Operation-ID')?{operation_id:headers.get('X-WsprryPi-Operation-ID')}:{}),
     ...(loseShippedReply&&options.method==='PUT'?{fault:'lose-reply'}:{})};
    if(command.fault)loseShippedReply=false;
    const result=await call(command),metadata={'Content-Type':'application/json'};
    if(result.context)metadata['X-WsprryPi-Target-Context']=result.context;
    if(result.etag)metadata.ETag=result.etag;
    if(result.readback_revision)metadata['X-WsprryPi-Readback-Revision']=result.readback_revision;
    if(result.recovery_operation)metadata.Location='/api/control/v1/targets/'+gateway.body.binding.device_id+'/operations/'+result.recovery_operation;
    if(result.recovery_resource)metadata['X-WsprryPi-Recovery-Resource']=result.recovery_resource;
    if(result.operation)metadata['X-WsprryPi-Operation-ID']=result.operation;
    return new Response(JSON.stringify(result.body),{status:result.status,headers:metadata});
   }});
   const descriptor={id:gateway.body.binding.device_id,name:'Managed attached Pico',managed:true,settings:{'Device ID':gateway.body.binding.device_id,Transport:'usb'}};
   let attached=await shipped.open(descriptor);assert(attached.managed&&attached.state.connected);assert.equal(await attached.generation(),gateway.body.binding.profile_generation);
   let shippedName=await attached.request('hostname');assert.equal(shippedName.response.status,200);
   const shippedSave={method:'PUT',revision:shippedName.response.headers.get('ETag'),body:{schema:'hostname',target:shippedName.data.target,hostname:'Shipped-managed-client.local'}};
   const shippedSaved=await attached.request('hostname',shippedSave);assert.equal(shippedSaved.response.status,200,JSON.stringify(shippedSaved.data));
   await refreshOwner();attached=await shipped.open(descriptor);
   shippedName=await attached.request('hostname');loseShippedReply=true;
   const shippedLost=await attached.request('hostname',{...shippedSave,revision:shippedName.response.headers.get('ETag'),body:{...shippedSave.body,hostname:'Unconfirmed-shipped-client.local'}});
   assert.equal(shippedLost.response.status,503);assert.equal(shippedLost.recovery.operation,shippedLost.operation);
   const shippedReceipt=await attached.lookup(shippedLost.operation);assert.equal(shippedReceipt.data.state,'unknown');
   const shippedReadback=await attached.request('hostname');assert.equal(shippedReadback.response.status,200);
   const shippedReviewed=await attached.review(shippedLost.operation,shippedReadback.readback);assert.equal(shippedReviewed.response.status,200);
   await assert.rejects(attached.request('hostname'),/connection_changed/);
   attached=await shipped.open(descriptor);assert.equal((await attached.request('hostname')).data.saved,'unconfirmed-shipped-client.local');
   assert(!JSON.stringify(shipped.operations()).includes('hostname:'),'Tracked replies retain no command body');
   // Restore this browser's independently held context after the owner review.
   assert.equal((await call({resource:'context',method:'GET'})).status,200);
  }
  if(mode){const refused=await call({resource:'hostname',method:'GET',fault:'wrong-parent-context'});assert.equal(refused.status,403);assert.equal(refused.body.error.code,'unauthorized');}
  const nativeStatus=await call({resource:'status',method:'GET'});assert.equal(nativeStatus.status,200,JSON.stringify(nativeStatus));
  assert.equal(nativeStatus.body.job.boot_id,nativeStatus.body.standalone.boot_id);
  assert.equal(nativeStatus.body.job.output_active,false);
  let stopRead=await call({resource:'schedules/stop',method:'GET'});assert.equal(stopRead.status,200,JSON.stringify(stopRead));
  const stopBody={contract:'Wsprry-Control/1',schema:'schedule-stop',target:stopRead.body.target,stop:true};
  const wrongStop=await call({resource:'schedules/stop',method:'POST',revision:stopRead.etag,context:stopRead.context,operation_id:operation(),
   body:JSON.stringify({...stopBody,target:{...stopBody.target,boot_id:'9'.repeat(32)}})});
  assert.equal(wrongStop.status,409,JSON.stringify(wrongStop));
  stopRead=await call({resource:'schedules/stop',method:'GET'});assert.equal(stopRead.status,200);
  const stopRequest={resource:'schedules/stop',method:'POST',revision:stopRead.etag,context:stopRead.context,operation_id:operation(),body:JSON.stringify(stopBody)};
  const stopped=await call(stopRequest);assert.equal(stopped.status,200,JSON.stringify(stopped));assert.equal(stopped.body.stopped,true);
  assert.equal(stopped.body.wspr_enabled,false);assert.equal(stopped.body.message_enabled,false);assert.equal(stopped.body.owned,false);assert.equal(stopped.body.output_active,false);
  const stopReplay=await call(stopRequest);assert.equal(stopReplay.status,200,JSON.stringify(stopReplay));assert.deepEqual(stopReplay.body,stopped.body);
  const stopConfirmed=await call({resource:'schedules/stop',method:'GET'});assert.equal(stopConfirmed.etag,stopped.etag,'Retained stop reply must not dispatch another stop');
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
  if(mode){const own=await call({resource:'operations/'+saveRequest.operation_id,method:'GET'});assert.equal(own.status,200,JSON.stringify(own));assert.equal(own.body.operation_id,saveRequest.operation_id);assert.equal(own.body.state,'committed');}
  const repeated=await call(saveRequest);assert.equal(repeated.status,saved.context===wifi.context?200:409);
  wifi=await call({resource:'wifi',method:'GET'});assert.equal(wifi.body.generation,saved.body.generation,'Replay must not dispatch another native write');assert.equal(wifi.body.wifi.ssid,'Independent radio');
  let journal=await call({control:'journal'});assert(!JSON.stringify(journal.body).includes(secret));if(mode){const actual=journal.body.receipts.find(entry=>entry.receipt.operation_id===saveRequest.operation_id);assert.equal(actual.principal_id,'c'.repeat(32));assert.equal(actual.authorization_generation,'1');}const count=journal.body.receipts.length;
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
  if(mode){
    wifi=await call({resource:'wifi',method:'GET'});const cancelledId=operation();
    const cancelled=await call({resource:'wifi',method:'PUT',revision:wifi.etag,operation_id:cancelledId,
      fault:'cancel-before-handoff',body:JSON.stringify({...wifiBody,target:wifi.body.target,wifi:{...wifiBody.wifi,ssid:'Cancelled before Pico'}})});
    assert.equal(cancelled.status,409,JSON.stringify(cancelled));
    const cancelledActual=await call({control:'inspect-wifi'});assert.equal(cancelledActual.body.wifi,null);
    const cancelledJournal=await call({control:'journal'});assert.equal(cancelledJournal.body.receipts.find(entry=>entry.receipt.operation_id===cancelledId).receipt.state,'rejected');
    assert.equal((await call({resource:'wifi',method:'GET'})).status,200,'The cancelled operation must not leave a stale callback or require pairing repair');
    wifi=await call({resource:'wifi',method:'GET'});const refusedId=operation();
    const refused=await call({resource:'wifi',method:'PUT',revision:wifi.etag,operation_id:refusedId,
      fault:'revoke-before-handoff',body:JSON.stringify({...wifiBody,target:wifi.body.target,wifi:{...wifiBody.wifi,ssid:'Must not reach Pico'}})});
    assert.equal(refused.status,409,JSON.stringify(refused));
    const actual=await call({control:'inspect-wifi'});assert.equal(actual.status,200);assert.equal(actual.body.wifi,null,'Revocation during zero-progress wait must prevent the actual Pico mutation');
    const retained=await call({control:'journal'});const receipt=retained.body.receipts.find(entry=>entry.receipt.operation_id===refusedId);
    assert.equal(receipt.receipt.state,'rejected');assert(!JSON.stringify(retained).includes(secret));
    console.log('PASS: actual Pi TLS principal, automatic attached-Pico negotiation, typed USB settings and recovery, revoked-before-handoff refusal without a Pico mutation');
  }
  child.stdin.end();
  assert(await waitForExit(child,5000),'Management owners and time worker must retire without a forced process kill');
  assert.equal(child.exitCode,0,'Management owner teardown must complete successfully');
  console.log('PASS: independent USB context, actual framing/ACK/Pico aliases, durable redacted replay, refused inputs before journaling, explicit action recovery, joined time-owner teardown, no WTP backend/claim/output');
 }catch(error){failure=error;}
 await finishChildren({failure,children:[child]});
})().catch(error=>{console.error(error);process.exitCode=1;});

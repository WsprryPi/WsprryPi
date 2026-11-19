// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {freePort,spawnFixtureChild,waitForSpawn,waitForPhp,createBrowserProfile,launchBrowser,waitFor,finishFixture,closeServer}=require('./browser_harness.js');
const control=require('../data/control-contract.js');
const vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=name=>structuredClone(vectors.find(item=>item.name===name).value);
(async()=>{
 const ui=path.resolve(__dirname,'..'),port=await freePort(),a='a'.repeat(32),b='b'.repeat(32),c='c'.repeat(32),contract=control.contract;
 let php,proxy,browser,client,profile,failure,lostStop=true,withdraw=false,privateReceipt=false;
 const calls=[],records=new Map(),targets=new Map();
 for(const [device,name]of [[a,'Workshop Pi'],[c,'Field Pi']]){
  const context=sample('paired Pi context'),settings=sample('Pi closed target-settings'),migration=sample('P501 pending authoritative migration read');
  context.target.device_id=device;context.target.name=name;context.binding.device_id=device;
  context.capabilities=[...new Set(context.capabilities.map(cap=>cap.resource))].map(resource=>({...control.controllerPiDescriptors[resource],resource,read:resource!=='stop',write:true,activation:resource==='pi-config'?'next_occurrence':'immediate',maximum_bytes:32768}));
  for(const resource of ['migration','stop','pi-scheduling'])if(!context.capabilities.some(cap=>cap.resource===resource))context.capabilities.push({...control.controllerPiDescriptors[resource],resource,read:resource!=='stop',write:true,activation:resource==='pi-scheduling'?'next_occurrence':'immediate',maximum_bytes:32768});
  settings.binding=structuredClone(context.binding);settings.capability_revision=context.capability_revision;settings.revision=context.configuration_revision;
  settings.saved.configuration.Operation.Transmit=true;settings.saved.scheduling.enabled=true;
  Object.assign(migration,{binding:structuredClone(context.binding),state:'native',authority_generation:'4',controllers:[{controller_id:b,assignment_id:(device===a?'4':'5').repeat(32),assignment_revision:'6'.repeat(64)}],retirements:[],jobs:[],transition_id:'7'.repeat(32),staged_revision:null,wtp_binding:{revocation_generation:'8',wtp_port:31417,management_port:31415}});
  assert(control.validControllerDocument(context,'target-context'));assert(control.validControllerDocument(settings,'target-settings'));assert(control.validControllerDocument(migration,'target-migration-read'));
  targets.set(device,{context,settings,migration,generation:5,last:null});
 }
 try{
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${port}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);await waitForPhp(php,`http://127.0.0.1:${port}/index.php?page=maintenance`,'Maintenance templates');
  proxy=http.createServer(async(request,response)=>{
   const route=new URL(request.url,'http://fixture').pathname;
   const reply=(value,status=200,headers={})=>{response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store',...headers});response.end(JSON.stringify(value));};
   if(route==='/api/control/v1/targets'){reply({contract,schema:'targets',targets:[...targets].filter(([device])=>!withdraw||device!==a).map(([,item])=>item.context.target)});return;}
   if(route.endsWith('/host/readiness')){const value=sample('P209 native recovery readiness');Object.assign(value,{device_id:b,role:'controller-only',state:'ready',recovery_editable:false});reply(value);return;}
   if(route.endsWith('/host/management-session')){reply({contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'f'.repeat(64)});return;}
   if(route.startsWith('/api/control/v1/targets/')){
    const [device,...parts]=route.slice('/api/control/v1/targets/'.length).split('/'),resource=parts.join('/'),item=targets.get(device);assert(item);
    calls.push({device,resource,method:request.method});
    if(request.method==='GET'){
     if(resource==='context'){reply(item.context);return;}
     if(resource.startsWith('operations/')){const receipt=privateReceipt?null:records.get(parts[1]);reply(receipt||{contract,schema:'target-error',error:{code:'operation_unknown',retryable:false}},receipt?200:404);return;}
     if(resource==='status'){const value=sample('Pi closed conservative status');value.binding=structuredClone(item.context.binding);value.revision=item.settings.revision;reply(value);return;}
     reply(resource==='migration'?item.migration:item.settings,200,{ETag:'"'+item.settings.revision+'"','X-WsprryPi-Readback-Revision':'9'.repeat(64)});return;
    }
    let raw='';for await(const part of request)raw+=part;const body=JSON.parse(raw);
    assert.equal(body.binding.device_id,device);
    if(resource.endsWith('/review')){
     assert.equal(body.operation_id,parts[1]);assert.equal(body.readback_revision,'9'.repeat(64));
     const binding={...item.context.binding,connection_generation:String(Number(item.context.binding.connection_generation)+1)};
     item.context.binding=binding;item.context.target.connection_generation=binding.connection_generation;item.settings.binding=structuredClone(binding);item.migration.binding=structuredClone(binding);
     reply({contract,schema:'target-operation-reviewed',binding,operation_id:body.operation_id,readback_revision:body.readback_revision});return;
    }
    assert.equal(body.expected_revision,item.settings.revision);
    assert(['stop','disable','migration/rollback','migration'].includes(resource));
    item.last=body.operation_id;item.generation++;const revision=item.generation.toString(16).padStart(64,'0');
    const receipt={contract,schema:'target-operation',binding:body.binding,operation_id:body.operation_id,request_digest:'8'.repeat(64),state:'committed',revision,error:null};
    if(resource==='stop')item.settings.saved.configuration.Operation.Transmit=false;
    if(resource==='disable')item.settings.saved.scheduling.enabled=false;
    if(resource==='migration/rollback'){
     assert.equal(item.settings.saved.configuration.Operation.Transmit,false);assert.equal(item.settings.saved.scheduling.enabled,false);
     assert.equal(body.resume_controller_id,device);item.migration.state='reconciling';item.migration.transition_id=body.operation_id;
     receipt.state='pending';receipt.revision=null;
    }
    item.settings.revision=revision;item.context.configuration_revision=revision;if(item.migration.state==='staged')item.migration.staged_revision=revision;
    records.set(body.operation_id,receipt);
    if(resource==='stop'&&lostStop){lostStop=false;reply({contract,schema:'target-error',error:{code:'transport_unavailable',retryable:true}},503);return;}
    reply(receipt,receipt.state==='pending'?202:200);return;
   }
   if(route.startsWith('/api/control/v1/host/pairing')){reply({contract,schema:'target-error',error:{code:'storage_unavailable',retryable:false}},503);return;}
   const upstream=http.request({hostname:'127.0.0.1',port,path:request.url,method:request.method,headers:request.headers},other=>{response.writeHead(other.statusCode,other.headers);other.pipe(response);});upstream.on('error',()=>response.destroy());request.pipe(upstream);
  });await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  const run=expression=>client.evaluate(expression),click=id=>run(`document.getElementById('recurrenceMigration${id}').click()`);
  const output=process.env.WSPRRYPI_MIGRATION_SCREENSHOTS||path.resolve(ui,'../build/phase62-migration-ui');fs.mkdirSync(output,{recursive:true});
  async function capture(scene){for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');document.getElementById('recurrenceMigration').scrollIntoView({behavior:'instant',block:'start'});window.scrollBy({top:-120,behavior:'instant'});`);await client.settleRendering();
   assert.equal(await run("[...document.querySelectorAll('#recurrenceMigration .btn-outline-secondary:disabled')].every(b=>getComputedStyle(b).color===getComputedStyle(document.body).color)"),true,'Held migration action labels retain the theme foreground');
   assert.equal(await run('document.documentElement.scrollWidth<=innerWidth'),true,'Migration overflow '+width);
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`${scene}-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  }
  await client.send('Page.navigate',{url:`http://127.0.0.1:${proxy.address().port}/index.php?page=maintenance`});
  await waitFor(()=>run("document.getElementById('recurrenceMigrationRollback')?.disabled===true&&document.getElementById('recurrenceMigrationDevice')?.textContent.includes('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')&&[...document.getElementById('recurrenceMigrationAssignment').options].some(o=>o.disabled)"),'retired controller restore held');
  assert.equal(calls.filter(call=>call.method!=='GET').length,0);
  assert.match(await run("document.getElementById('recurrenceMigrationRestoreHelp').textContent"),/Controller-owned WTP schedules remain held/);
  await run("document.getElementById('recurrenceMigrationRollback').dispatchEvent(new Event('click',{bubbles:true}))");
  assert.equal(await run("document.getElementById('recurrenceMigrationConfirmation').hidden"),true);
  assert.equal(calls.filter(call=>call.method==='POST').length,0,'Retired restore cannot issue preparatory Stop, disable, or a browser session');
  await capture('controller-held');
  const nativePrevious={...targets.get(a).migration.controllers[0],controller_id:a,assignment_id:'1'.repeat(32)};
  targets.get(a).migration.controllers.unshift(nativePrevious);
  targets.get(a).migration.wtp_binding=null;
  await click('Refresh');await waitFor(()=>run("!document.getElementById('recurrenceMigrationAssignment').disabled"),'native previous schedule read');
  await run(`document.getElementById('recurrenceMigrationAssignment').value='${a}/${'1'.repeat(32)}';document.getElementById('recurrenceMigrationAssignment').dispatchEvent(new Event('change'))`);
  await waitFor(()=>run("!document.getElementById('recurrenceMigrationRollback').disabled"),'explicit previous local schedule');
  await click('Rollback');assert.equal(await run('document.activeElement.id'),'recurrenceMigrationConsent');
  const confirm=async()=>{await run("document.getElementById('recurrenceMigrationConsent').checked=true;document.getElementById('recurrenceMigrationConsent').dispatchEvent(new Event('change'))");await click('Confirm');};
  await confirm();await waitFor(()=>run("!document.getElementById('recurrenceMigrationRetry').hidden&&!document.getElementById('recurrenceMigrationRetry').disabled"),'lost preparation retained');
  assert.deepEqual(calls.filter(call=>call.method==='POST').map(call=>call.resource),['stop']);
  await click('Retry');await waitFor(()=>run("document.getElementById('recurrenceMigrationRetry').hidden&&!document.getElementById('recurrenceMigrationRollback').disabled"),'read-only preparation recovery');
  assert.equal(calls.filter(call=>call.method==='POST').length,1,'Receipt recovery never starts the next step');
  await click('Rollback');await confirm();
  await waitFor(()=>run("document.getElementById('recurrenceMigrationState').textContent.includes('Restoration is unconfirmed')&&!document.getElementById('recurrenceMigrationTarget').disabled"),'pending guarded handback');
  const grant=targets.get(a).last;
  assert.deepEqual(calls.filter(call=>call.method==='POST').map(call=>call.resource),['stop','stop','disable','migration/rollback']);
  const select=async device=>{await run(`document.getElementById('recurrenceMigrationTarget').value='${device}';document.getElementById('recurrenceMigrationTarget').dispatchEvent(new Event('change'))`);await waitFor(()=>run(`document.getElementById('recurrenceMigrationDevice').textContent.includes('${device}')&&!document.getElementById('recurrenceMigrationTarget').disabled`),'selected migration target '+device.slice(0,1));};
  await select(c);assert.equal(await run("document.getElementById('recurrenceMigrationRetry').hidden"),true);await select(a);
  assert.equal(await run("document.getElementById('recurrenceMigrationRetry').hidden"),false);assert.equal(targets.get(a).last,grant);
  await capture('pending');
  const rejected=records.get(grant);rejected.state='rejected';rejected.revision=null;rejected.error={code:'authority_pending',retryable:false};
  targets.get(a).migration.state='staged';targets.get(a).migration.staged_revision=targets.get(a).settings.revision;
  await click('Retry');await waitFor(()=>run("document.getElementById('recurrenceMigrationFeedback').textContent.includes('refused')&&document.getElementById('recurrenceMigrationRetry').hidden"),'definite restore refusal');
  assert.equal(calls.filter(call=>call.method==='POST'&&call.resource==='migration/rollback').length,1);
  await click('Rollback');await confirm();
  await waitFor(()=>run("document.getElementById('recurrenceMigrationState').textContent.includes('Restoration is unconfirmed')&&!document.getElementById('recurrenceMigrationTarget').disabled"),'second explicit restore');
  const secondGrant=targets.get(a).last;assert.notEqual(secondGrant,grant);
  const previous=targets.get(a);previous.migration.state='rollback';records.get(secondGrant).state='committed';records.get(secondGrant).revision=previous.settings.revision;
  privateReceipt=true;previous.context.authorization={pair_id:'e'.repeat(32),authorization_generation:'2',renewal_required:false};
  await click('Refresh');await waitFor(()=>run("!document.getElementById('recurrenceMigrationOutcomeReview').hidden&&!document.getElementById('recurrenceMigrationReadOutcome').disabled"),'review after re-pair with private old receipt');
  await click('Retry');await waitFor(()=>run("document.getElementById('recurrenceMigrationFeedback').textContent.includes('unconfirmed')&&!document.getElementById('recurrenceMigrationRefresh').disabled"),'old receipt remains private');
  await click('Refresh');await waitFor(()=>run("!document.getElementById('recurrenceMigrationReadOutcome').disabled"),'fresh outcome context');
  const beforeReview=JSON.stringify(previous.settings.saved),restoreWrites=calls.filter(call=>call.resource==='migration/rollback'&&call.method==='POST').length;
  await click('ReadOutcome');await waitFor(()=>run("!document.getElementById('recurrenceMigrationConfirmOutcome').disabled"),'fresh authenticated state review');
  const beforeExpiry=calls.filter(call=>call.method==='POST').length;
  await run("window.originalClock=Date.now;const later=Date.now()+31000;Date.now=()=>later");await click('ConfirmOutcome');
  assert.equal(await run("document.getElementById('recurrenceMigrationConfirmOutcome').disabled"),true);
  assert((await run("document.getElementById('recurrenceMigrationFeedback').textContent")).includes('Read current state again'));
  assert.equal(calls.filter(call=>call.method==='POST').length,beforeExpiry,'Expired review cannot dispatch');
  await run('Date.now=window.originalClock');await click('ReadOutcome');await waitFor(()=>run("!document.getElementById('recurrenceMigrationConfirmOutcome').disabled"),'renewed state review');
  await click('ConfirmOutcome');await waitFor(()=>run("document.getElementById('recurrenceMigrationFeedback').textContent.includes('earlier result remains unknown')&&document.getElementById('recurrenceMigrationRetry').hidden"),'explicit unknown outcome review');
  assert.equal(JSON.stringify(previous.settings.saved),beforeReview);assert.equal(calls.filter(call=>call.resource==='migration/rollback'&&call.method==='POST').length,restoreWrites);
  previous.migration.state='staged';previous.migration.staged_revision=previous.settings.revision;
  await click('Refresh');await waitFor(()=>run("!document.getElementById('recurrenceMigrationReview').disabled"),'newly recorded remote authority can migrate from disabled staging');
  withdraw=true;await click('Refresh');await waitFor(()=>run("document.getElementById('recurrenceMigrationFeedback').textContent.includes('unavailable')"),'withdrawn target');
  assert.equal(await run("document.getElementById('recurrenceMigrationReview').disabled&&document.getElementById('recurrenceMigrationRollback').disabled"),true);
  const remoteOutput=targets.get(c);remoteOutput.settings.saved.configuration.Operation['Transmit Backend']='wtp';
  remoteOutput.migration.state='legacy';remoteOutput.migration.staged_revision=null;
  assert.equal(control.validControllerDocument(remoteOutput.settings,'target-settings'),false,'Retired backend is not a historical current-control view');
  const beforeRemote=calls.filter(call=>call.method==='POST').length;
  await select(c);await waitFor(()=>run("document.getElementById('recurrenceMigrationFeedback').textContent.includes('unavailable')"),'unsupported legacy settings refused');
  assert.equal(await run("document.getElementById('recurrenceMigrationReview').disabled&&document.getElementById('recurrenceMigrationRollback').disabled"),true);
  assert((await run("document.getElementById('recurrenceMigrationState').textContent")).includes('unknown'));
  assert((await run("document.getElementById('recurrenceMigrationDevice').textContent")).includes(c),'Unavailable read must not display the previous target identity');
  await click('Review');assert.equal(calls.filter(call=>call.method==='POST').length,beforeRemote,'Unsupported old control never sends a transition');
  await capture('unsupported-legacy-control');
  remoteOutput.settings.saved.configuration.Operation['Transmit Backend']='gpio';
  remoteOutput.migration.state='retiring';remoteOutput.migration.jobs=[{controller_id:b,job_id:'6'.repeat(32),boot_id:remoteOutput.context.binding.boot_id,state:'unknown'}];
  assert(control.validControllerDocument(remoteOutput.migration,'target-migration-read'));
  await click('Refresh');await waitFor(()=>run("!document.getElementById('recurrenceMigrationRetry').hidden&&!document.getElementById('recurrenceMigrationRefresh').disabled"),'retained native handover hold');
  assert.equal(await run("document.getElementById('recurrenceMigrationReview').disabled"),true,'Same-boot unknown work cannot invent reboot reconciliation');
  assert.equal(calls.filter(call=>call.method==='POST').length,beforeRemote);
  console.log('PASS Maintenance migration: retired controller restore refuses before all writes; controller-only/two targets, explicit local Stop+disable+restore, lost-step receipt-only recovery, retained target operations, refused result, re-pair/private-receipt outcome review, withdrawal and controller-held/pending desktop/mobile theme renders');
 }catch(error){failure=error;console.error('Fixture state:',[...targets].map(([id,t])=>({device:id[0],context:control.validControllerDocument(t.context,'target-context'),settings:control.validControllerDocument(t.settings,'target-settings'),migration:control.validControllerDocument(t.migration,'target-migration-read'),state:t.migration.state,backend:t.settings.saved.configuration.Operation['Transmit Backend'],staged:t.migration.staged_revision,revision:t.settings.revision})));if(client)try{console.error(await client.evaluate("({feedback:document.getElementById('recurrenceMigrationFeedback')?.textContent,state:document.getElementById('recurrenceMigrationState')?.textContent,device:document.getElementById('recurrenceMigrationDevice')?.textContent,recovery:window.ConfigurationRecovery?.active,assignment:document.getElementById('recurrenceMigrationAssignment')?.value,choices:[...document.getElementById('recurrenceMigrationAssignment')?.options||[]].map(o=>({value:o.value,disabled:o.disabled})),busy:document.getElementById('recurrenceMigration')?.getAttribute('aria-busy')})"));}catch(_){}}
 await finishFixture({failure,client,browser,php,profile,cleanups:[async()=>{if(proxy)await closeServer(proxy);} ]});
})().catch(error=>{console.error(error);process.exitCode=1;});

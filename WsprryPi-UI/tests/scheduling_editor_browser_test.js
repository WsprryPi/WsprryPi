// SPDX-License-Identifier: MIT
// Synthetic HTTP presentation fixture, using shipped native-target client and
// editor. Actual paired TLS/storage owners are qualified separately.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {freePort,spawnFixtureChild,waitForSpawn,waitForPhp,createBrowserProfile,launchBrowser,waitFor,finishFixture,closeServer}=require('./browser_harness.js');
const control=require('../data/control-contract.js');
const vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=name=>structuredClone(vectors.find(item=>item.name===name).value);
(async()=>{
 const ui=path.resolve(__dirname,'..'),port=await freePort(),device='a'.repeat(32),host='b'.repeat(32),contract='Wsprry-Control/1';
 let php,proxy,browser,client,profile,failure,advertised=true,localMode=false,failSecond=false,holdReview=false,heldReview=false,releaseReview,lost=false,receipt=null;
 const calls=[],receipts=new Map(),context=sample('paired Pi context'),settings=sample('Pi closed target-settings');
 context.capabilities.push({...control.controllerPiDescriptors['pi-scheduling'],resource:'pi-scheduling',read:true,write:true,activation:'next_occurrence',maximum_bytes:32768});
 context.capabilities.push({...control.controllerPiDescriptors.stop,resource:'stop',read:false,write:true,activation:'immediate',maximum_bytes:32768});
 context.target.device_id=device;context.binding.device_id=device;context.target.name='Workshop Pi';context.capabilities[0].write=true;context.capabilities[0].activation='next_occurrence';Object.assign(context.capabilities[0],control.controllerPiDescriptors['pi-config']);
 assert(control.validControllerDocument({contract,schema:'targets',targets:[context.target]},'targets'));
 assert(control.validControllerDocument(context,'target-context'));
 settings.binding=structuredClone(context.binding);settings.capability_revision=context.capability_revision;settings.revision=context.configuration_revision;
 settings.saved.scheduling.enabled=false;
 const second=structuredClone(context);second.target.device_id=second.binding.device_id='c'.repeat(32);second.target.name='Second site Pi';const other=structuredClone(settings);other.binding=structuredClone(second.binding);
 let inventory=[context.target,second.target];const peers=new Map([[device,{context,settings}],['c'.repeat(32),{context:second,settings:other}]]);let revision=0;
 try{
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${port}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);await waitForPhp(php,`http://127.0.0.1:${port}/index.php?page=config`,'Pi editor templates');
  proxy=http.createServer(async(request,response)=>{
   const route=new URL(request.url,'http://fixture').pathname;
   const reply=(body,status=200,headers={})=>{response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store',...headers});response.end(JSON.stringify(body));};
   if(route==='/api/control/v1/targets'){reply({contract,schema:'targets',targets:advertised?inventory:[]});return;}
   if(route.endsWith('/host/readiness')){const value=sample('P209 native recovery readiness');value.device_id=localMode?device:host;value.role=localMode?'transmitter':'controller-only';value.state='ready';value.recovery_editable=false;reply(value);return;}
   if(route.endsWith('/host/management-session')){reply({contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'f'.repeat(64)});return;}
   const chosen=peers.get(route.split('/')[5]);
   if(chosen){
    const {context,settings}=chosen,device=context.binding.device_id;
    calls.push({method:request.method,route});
    if(route.endsWith('/context')){if(failSecond&&device==='c'.repeat(32)){reply({contract,schema:'target-error',error:{code:'transport_unavailable',retryable:true}},503);return;}reply(context);return;}
    if(request.method==='GET'){
     if(holdReview&&route.endsWith('/settings')){holdReview=false;heldReview=true;releaseReview=()=>reply(settings,200,{'X-WsprryPi-Readback-Revision':'9'.repeat(64)});return;}
     if(route.endsWith('/status')){const value=sample('Pi closed conservative status');value.binding=structuredClone(context.binding);value.revision=settings.revision;value.schedule_enabled=settings.saved.scheduling.enabled;value.solar_enabled=settings.saved.scheduling.solar.enabled;value.execution_owner=device;value.destination_id=device;reply(value);return;}
     reply(route.includes('/operations/')?receipts.get(route.split('/').at(-1)):settings,200,{'X-WsprryPi-Readback-Revision':'9'.repeat(64)});return;
    }
    let raw='';for await(const part of request)raw+=part;const body=JSON.parse(raw);assert.equal(body.binding.device_id,device);
    receipt={contract,schema:'target-operation',binding:body.binding,operation_id:body.operation_id,request_digest:'c'.repeat(64),revision:String(++revision).padStart(64,'0'),state:'committed',error:null};
    if(body.candidate)settings.saved=structuredClone(body.candidate);
    if(route.endsWith('/enable')){assert.equal(body.candidate_digest,settings.candidate_digest);settings.saved.scheduling.enabled=true;}
    if(route.endsWith('/disable'))settings.saved.scheduling.enabled=false;
    settings.revision=receipt.revision;context.configuration_revision=receipt.revision;settings.candidate_digest=String(revision+10).padStart(64,'0');
    if(route.endsWith('/stop')){receipt.revision='e'.repeat(64);settings.saved.configuration.Operation.Transmit=false;settings.revision=receipt.revision;context.configuration_revision=receipt.revision;}
    receipts.set(receipt.operation_id,structuredClone(receipt));
    if(lost){reply({contract,schema:'target-error',error:{code:'transport_unavailable',retryable:true}},503);return;}
    reply(receipt);return;
   }
   const upstream=http.request({hostname:'127.0.0.1',port,path:request.url,method:request.method,headers:request.headers},other=>{response.writeHead(other.statusCode,other.headers);other.pipe(response);});upstream.on('error',()=>response.destroy());request.pipe(upstream);
  });await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  const run=expression=>client.evaluate(expression);
  const captureState=async(scene,anchor,widths=[390])=>{const output=path.resolve(ui,'../build/phase7-scheduling-ui');fs.mkdirSync(output,{recursive:true});for(const theme of ['light','dark'])for(const width of widths){await client.send('Emulation.setDeviceMetricsOverride',{width,height:1100,deviceScaleFactor:1,mobile:width<768});await run(`document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});document.querySelector(${JSON.stringify(anchor)}).scrollIntoView({behavior:'instant'});window.scrollBy(0,-120)`);await client.settleRendering();assert(await run('document.documentElement.scrollWidth<=innerWidth'));const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`${scene}-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));}};

  await client.send('Page.navigate',{url:`http://127.0.0.1:${proxy.address().port}/index.php?page=config`});
  await waitFor(()=>run("document.readyState==='complete'&&!!window.PiTargetEditors&&!!document.querySelector('#setup-device option[value=\"pi:'+ 'a'.repeat(32)+'\"]')"),'paired Pi Device choice');
  await waitFor(()=>run("!document.getElementById('setup-device').disabled"),'unlocked Device');
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("!document.getElementById('setup-pi-workspace').hidden&&PiTargetEditors.state&&!PiTargetEditors.state.paused&&document.querySelector('#pi-target-fields label')"),'native Pi saved editor');
  assert.equal(calls.filter(call=>call.method!=='GET').length,0,'Selection and reads never save');
  await run("document.getElementById('scheduling-tab').click()");
  await waitFor(()=>run("window.SchedulingEditor?.state&&!SchedulingEditor.state.paused&&document.querySelector('#schedule-fields fieldset')&&!document.getElementById('setup-scheduling').hidden"),'Scheduling read');
  assert.deepEqual(await run("[...document.querySelectorAll('#configTabs [role=tab]')].map(n=>n.textContent.trim())"),['Signal Setup','Scheduling','Transmitter','Device I/O','Fleet']);
  assert.equal(await run("document.getElementById('setup-device').closest('form,[role=tablist]')"),null);
  const writeCount=()=>calls.filter(v=>v.method!=='GET').length;
  assert.equal(await run("document.querySelector('#schedule-fields fieldset').disabled"),true);
  await run("document.getElementById('schedule-enabled').click()");
  assert.equal(writeCount(),0,'Preparing enable never sends an action');
  const field=async(label,value)=>run(`(()=>{const l=[...document.querySelectorAll('#schedule-fields label')].find(n=>n.textContent===${JSON.stringify(label)});const n=document.getElementById(l.htmlFor);if(n.type==='checkbox')n.checked=${JSON.stringify(value)};else n.value=${JSON.stringify(value)};n.dispatchEvent(new Event(n.type==='checkbox'?'change':'input',{bubbles:true}));})()`);
  await field('Use solar day/night selection',true);await field('Target-site locator','EM18IG');
  await run("document.querySelector('#schedule-fields details').open=true");await field('Allow occurrences',true);
  await field('CW base override (Hz)','14097000.12345678');
  assert.equal(await run("document.getElementById('schedule-enable').disabled"),true,'Unsaved policy cannot enable old saved settings');
  await run("document.getElementById('schedule-preview').click()");assert.equal(writeCount(),0,'Review draft is read-only');
  assert.equal(await run("document.getElementById('schedule-save').disabled"),false);
  await run("document.getElementById('schedule-save').click()");
  await waitFor(()=>run("!document.getElementById('schedule-enable').disabled"),'saved disabled policy readback');
  assert.equal(writeCount(),1);assert.equal(settings.saved.scheduling.enabled,false);
  lost=true;await run("document.getElementById('schedule-enable').click()");
  await waitFor(()=>run("SchedulingEditor.state.pending&&!document.getElementById('schedule-check').disabled"),'lost enable response');
  assert.equal(writeCount(),2);assert.equal(settings.saved.scheduling.enabled,true);
  await run("document.getElementById('schedule-read-review').click()");
  await waitFor(()=>run("!document.getElementById('schedule-confirm-review').disabled"),'full current recovery policy');
  const review=await run("document.getElementById('schedule-review-values').textContent");
  for(const expected of [settings.revision,'14097000.12345678','elevation [-6, 6)','band preferences','profile existing_common','saved shift 500 Hz','inherits: 14096900','Retained raw entries'])assert(review.includes(expected),'Recovery includes '+expected);
  assert.equal(writeCount(),2,'Recovery read never sends the retained draft');
  await captureState('recovery','#schedule-review-values');
  holdReview=true;await run("document.getElementById('schedule-read-review').click()");await waitFor(()=>heldReview,'held recovery read');
  await run("document.getElementById('setup-device').value='pi:'+'c'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");await waitFor(()=>run("SchedulingEditor.state?.device==='"+'c'.repeat(32)+"'&&!SchedulingEditor.state.paused"),'away during recovery');
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");await waitFor(()=>run("SchedulingEditor.state?.device==='"+device+"'"),'returned during recovery');
  releaseReview();await waitFor(()=>run("!document.getElementById('schedule-read-review').disabled"),'retired read completion');
  assert.equal(await run("document.getElementById('schedule-confirm-review').disabled"),true,'Retired epoch cannot authorize review');assert.equal(await run("document.getElementById('schedule-review-values').textContent"),'');
  await run("document.getElementById('schedule-check').click()");
  await waitFor(()=>run("!SchedulingEditor.state.pending&&!SchedulingEditor.state.paused"),'exact enable receipt recovery');
  assert.equal(writeCount(),2,'Receipt checking never resends enable');lost=false;
  // A retired ordinary read must not enable an unseen new saved candidate.
  await waitFor(()=>run("!document.getElementById('schedule-read').disabled&&!document.getElementById('pi-target-refresh').disabled"),'both target editors settled before ordinary-read race');
  settings.saved.scheduling.enabled=false;settings.saved.scheduling.solar.location.locator='FN31PR';settings.revision=context.configuration_revision='f'.repeat(64);settings.candidate_digest='a'.repeat(64);
  heldReview=false;holdReview=true;await run("document.getElementById('schedule-read').click()");await waitFor(()=>heldReview,'held ordinary scheduling read');
  await run("document.getElementById('setup-device').value='pi:'+'c'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");await waitFor(()=>run("SchedulingEditor.state?.device==='"+'c'.repeat(32)+"'&&!SchedulingEditor.state.paused"),'away during ordinary read');
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");await waitFor(()=>run("SchedulingEditor.state?.device==='"+device+"'"),'return during ordinary read');releaseReview();
  await waitFor(()=>run("!document.getElementById('schedule-read').disabled"),'retired ordinary read completion');
  assert.equal(await run("SchedulingEditor.state.paused&&document.getElementById('schedule-fields').inert&&document.getElementById('schedule-enabled').disabled&&document.getElementById('schedule-save').disabled&&document.getElementById('schedule-enable').disabled"),true,'Unseen saved policy never grants editing or enable');
  await run("document.getElementById('schedule-read').click()");await waitFor(()=>run("!SchedulingEditor.state.paused&&!document.getElementById('schedule-enable').disabled"),'explicit fresh policy read');
  assert.equal(await run("(()=>{const l=[...document.querySelectorAll('#schedule-fields label')].find(n=>n.textContent==='Target-site locator');return document.getElementById(l.htmlFor).value})()"),'FN31PR','Fresh read presents the actual new policy');assert.equal(writeCount(),2);
  await field('CW base override (Hz)','NaN');await field('Allow occurrences',false);
  const select=async id=>{await run(`document.getElementById('setup-device').value='pi:'+${JSON.stringify(id)};document.getElementById('setup-device').dispatchEvent(new Event('change'))`);await waitFor(()=>run(`SchedulingEditor.state?.device===${JSON.stringify(id)}&&!SchedulingEditor.state.paused`),'target scheduling context');};
  assert.equal(await run("document.querySelector('#schedule-fields input[aria-invalid=true]')?.value"),'NaN');
  assert.equal(await run("document.querySelector('#schedule-fields input[aria-invalid=true]').closest('details').open"),true);
  assert.equal(await run("document.querySelector('#schedule-fields input[aria-invalid=true]').getAttribute('aria-describedby').includes('-error')"),true);
  await captureState('invalid','#schedule-fields input[aria-invalid=true]');
  failSecond=true;await run("document.getElementById('setup-device').value='pi:'+'c'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("SchedulingEditor.state?.device==='"+'c'.repeat(32)+"'&&SchedulingEditor.state.paused&&document.getElementById('schedule-feedback').textContent.includes('unconfirmed')"),'failed new target read');
  assert.equal(await run("document.getElementById('schedule-summary').textContent"),'Selected target statusUnknown until a verified read completes.');
  assert.equal(await run("document.getElementById('schedule-enabled').checked"),false,'No inherited switch choice');
  assert.equal(await run("document.getElementById('schedule-review-values').textContent"),'');
  await captureState('failed-target','#setup-scheduling');
  failSecond=false;await run("document.getElementById('schedule-read').click()");await waitFor(()=>run('!SchedulingEditor.state.paused'),'second target read recovery');
  await select('c'.repeat(32));assert.equal(other.saved.scheduling.enabled,false);assert.equal(writeCount(),2);
  await select(device);
  assert.equal(await run("(()=>{const l=[...document.querySelectorAll('#schedule-fields label')].find(n=>n.textContent==='CW base override (Hz)');return document.getElementById(l.htmlFor).value})()"),'NaN','Invalid typed draft survives target switches');
  assert.equal(await run("document.getElementById('schedule-save').disabled"),true);
  await field('CW base override (Hz)','14097000.12345678');
  const output=path.resolve(ui,'../build/phase7-scheduling-ui');fs.mkdirSync(output,{recursive:true});
  for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1100,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});document.getElementById('setup-scheduling').scrollIntoView({behavior:'instant'});window.scrollBy({top:-120,behavior:'instant'})`);await client.settleRendering();
   assert.equal(await run('document.documentElement.scrollWidth<=innerWidth'),true,'Scheduling page overflow');
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`scheduling-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  // Full document captures include the selector, tab order, fields and actions.
  for(const theme of ['light','dark'])for(const width of [1440,390]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1100,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});document.querySelectorAll('#schedule-fields details').forEach(n=>n.open=true);window.scrollTo(0,0)`);await client.settleRendering();
   const height=await run('document.documentElement.scrollHeight');
   const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height,scale:1}});fs.writeFileSync(path.join(output,`full-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await run("document.getElementById('schedule-enabled').focus()");
  await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  assert.equal(await run("document.activeElement.closest('#schedule-fields')!==null"),true,'Keyboard reaches enabled policy controls');
  lost=true;await run("document.getElementById('schedule-save').click()");await waitFor(()=>run("SchedulingEditor.state.pending&&!document.getElementById('schedule-check').disabled"),'retained unknown policy save');const earlierOperation=receipt.operation_id;assert.equal(writeCount(),3);lost=false;
  inventory=[second.target];await waitFor(()=>run(`SchedulingEditor.state.paused&&document.querySelector('#setup-device option[value="pi:${device}"]')?.textContent.includes('unavailable Pi')`),'source withdrawal reflected in Device inventory',10000);assert.equal(await run("document.getElementById('schedule-fields').inert&&document.getElementById('schedule-enabled').disabled&&document.getElementById('schedule-save').disabled"),true,'Withdrawal immediately disables semantic editing');
  await select('c'.repeat(32));assert.equal(await run(`!!document.querySelector('#setup-device option[value="pi:${device}"]')`),true,'Scheduling-only retained target stays selectable');
  const earlierTargetRequests=calls.filter(v=>v.route.includes('/targets/'+device+'/')).length;
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");await waitFor(()=>run("SchedulingEditor.state?.device==='"+device+"'&&SchedulingEditor.state.paused&&SchedulingEditor.state.pending"),'read-only retained scheduling target');
  assert.equal(calls.filter(v=>v.route.includes('/targets/'+device+'/')).length,earlierTargetRequests,'Selecting withdrawn retained target never negotiates');assert.equal(writeCount(),3);
  assert.equal(await run("document.getElementById('schedule-fields').inert&&document.getElementById('schedule-enable').disabled&&document.getElementById('schedule-save').disabled"),true);
  assert.equal(await run("(()=>{const l=[...document.querySelectorAll('#schedule-fields label')].find(n=>n.textContent==='Target-site locator');return document.getElementById(l.htmlFor).value})()"),'FN31PR','Unavailable target retains its own entries');
  await captureState('retained-unavailable','#setup-scheduling');
  const lookups=calls.filter(v=>v.route.endsWith('/operations/'+earlierOperation)).length;await run("document.getElementById('schedule-check').click()");await waitFor(()=>calls.filter(v=>v.route.endsWith('/operations/'+earlierOperation)).length===lookups+1,'same retained operation identity');assert.equal(writeCount(),3,'Explicit lookup never resends the retained save');
  await waitFor(()=>run("!document.getElementById('schedule-check').disabled"),'retained check settled');

  // Reload in a distinct local-host session, using the identical editor and
  // requiring the local null-authorization contract rather than paired access.
  localMode=true;advertised=true;inventory=[context.target,second.target];context.authorization=null;settings.saved.scheduling.enabled=false;
  await finishFixture({client,browser,profile});client=browser=profile=null;
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  await client.send('Page.navigate',{url:`http://127.0.0.1:${proxy.address().port}/index.php?page=config`});
  await waitFor(()=>run("document.readyState==='complete'&&window.ControllerMode&&!ControllerMode.active&&window.SetupTargets"),'local host mode');
  await run("document.getElementById('scheduling-tab').click()");
  await waitFor(()=>run("SchedulingEditor.state?.device==='"+device+"'&&!SchedulingEditor.state.paused&&!!document.querySelector('#schedule-fields fieldset')"),'local Scheduling same controls');
  assert.equal(await run("document.getElementById('schedule-identity').textContent.includes('local Pi')"),true);
  const beforeLocal=writeCount();await run("document.getElementById('schedule-enabled').click()");await field('Use solar day/night selection',false);await run("document.getElementById('schedule-preview').click()");assert.equal(writeCount(),beforeLocal,'Local drafts and review never save');
  assert.equal(await run(`document.querySelectorAll('#setup-device option[value="pi:${device}"]').length`),0,'This Pi never acquires a paired alias');
  // One snapshot remains <=64; lifetime churn must not evict retained drafts
  // or leave the old target editable when the bounded editor cache is full.
  const churn=[];
  for(let i=0;i<65;i++){const id=(i+1).toString(16).padStart(32,'0'),ctx=structuredClone(context),value=structuredClone(settings);ctx.target.device_id=ctx.binding.device_id=id;ctx.target.name='Churn Pi '+i;ctx.authorization=sample('paired Pi context').authorization;value.binding=structuredClone(ctx.binding);value.saved.scheduling.solar.location.locator='EM18IG';peers.set(id,{context:ctx,settings:value});churn.push(ctx.target);}
  const chooseChurn=async target=>{
   await run(`document.getElementById('setup-device').value='pi:'+${JSON.stringify(target.device_id)};document.getElementById('setup-device').dispatchEvent(new Event('change'))`);
  };
  inventory=[context.target,...churn.slice(0,63)];
  await waitFor(()=>run(`!!document.querySelector('#setup-device option[value="pi:${churn[62].device_id}"]')`),'snapshot-valid churn inventory');
  for(const target of churn.slice(0,63)){await chooseChurn(target);await waitFor(()=>run(`SchedulingEditor.state?.device===${JSON.stringify(target.device_id)}&&!SchedulingEditor.state.paused&&PiTargetEditors.state?.device===${JSON.stringify(target.device_id)}&&!PiTargetEditors.state.paused`),'shared Device churn target');await run("document.getElementById('schedule-enabled').checked=true;document.getElementById('schedule-enabled').dispatchEvent(new Event('change'))");await field('Target-site locator','EM18JJ');}
  const retained=await run('SchedulingEditor.retainedTargets().length');assert.equal(retained,64);
  inventory=[context.target,...churn.slice(1,64)];await waitFor(()=>run(`!!document.querySelector('#setup-device option[value="pi:${churn[63].device_id}"]')`),'next churn snapshot');await chooseChurn(churn[63]);
  await waitFor(()=>run("SchedulingEditor.state===null&&document.getElementById('schedule-feedback').textContent.includes('draft limit reached')"),'graceful Scheduling capacity');
  await waitFor(()=>run(`PiTargetEditors.state?.device===${JSON.stringify(churn[63].device_id)}&&!PiTargetEditors.state.paused`),'native cache reaches64');
  inventory=[context.target,...churn.slice(2)];await waitFor(()=>run(`!!document.querySelector('#setup-device option[value="pi:${churn[64].device_id}"]')`),'native capacity snapshot');await chooseChurn(churn[64]);
  await waitFor(()=>run("SchedulingEditor.state===null&&PiTargetEditors.state===null&&document.getElementById('pi-target-feedback').textContent.includes('draft limit reached')"),'graceful shared cache capacity');
  assert.equal(await run("document.getElementById('schedule-fields').children.length===0&&document.getElementById('schedule-enabled').disabled&&document.getElementById('schedule-read').disabled&&document.getElementById('schedule-save').disabled"),true);
  await captureState('capacity','#setup-scheduling');
  for(const tab of ['radio-tab','transmitter-hardware-tab','pi-hardware-tab']){await run(`document.getElementById('${tab}').click()`);assert.equal(await run("document.getElementById('pi-target-fields').children.length===0&&document.getElementById('pi-target-save').disabled&&document.getElementById('pi-target-stop').disabled"),true,'No old target fields/actions exposed across native tabs');}
  await run("document.getElementById('radio-tab').click()");await captureState('native-capacity','#setup-pi-workspace',[1440,390]);
  assert.equal(await run('SchedulingEditor.retainedTargets().length'),retained,'All retained work survives shared cache exhaustion');assert.equal(writeCount(),beforeLocal);
  await run("document.getElementById('scheduling-tab').click()");await chooseChurn(churn[2]);await waitFor(()=>run(`SchedulingEditor.state?.device===${JSON.stringify(churn[2].device_id)}&&!SchedulingEditor.state.paused`),'retained shared target recovery');
  assert.equal(await run("(()=>{const l=[...document.querySelectorAll('#schedule-fields label')].find(n=>n.textContent==='Target-site locator');return document.getElementById(l.htmlFor).value})()"),'EM18JJ','The original retained draft is recoverable');assert.equal(writeCount(),beforeLocal);
  console.log('PASS Scheduling: local/paired contract client, selected two-Pi drafts, explicit disabled save/readback/enable, unknown enable receipt-only recovery, invalid retained inputs, semantic withdrawal, 64-entry churn capacity with retained drafts, preview without writes and desktop/mobile light/dark renders');
 }catch(error){failure=error;if(client)try{console.error(await client.evaluate("({feedback:document.getElementById('schedule-feedback')?.textContent,state:window.SchedulingEditor?.state,fields:document.getElementById('schedule-fields')?.textContent})"));}catch(_){}}
 await finishFixture({failure,client,browser,php,profile,cleanups:[async()=>{if(proxy)await closeServer(proxy);} ]});
})().catch(error=>{console.error(error);process.exitCode=1;});

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
 let php,proxy,browser,client,profile,failure,advertised=true,lost=false,receipt=null;
 const calls=[],receipts=new Map(),context=sample('paired Pi context'),settings=sample('Pi closed target-settings');
 context.capabilities.push({...control.controllerPiDescriptors.stop,resource:'stop',read:false,write:true,activation:'immediate',maximum_bytes:32768});
 context.target.device_id=device;context.binding.device_id=device;context.target.name='Workshop Pi';context.capabilities[0].write=true;context.capabilities[0].activation='next_occurrence';Object.assign(context.capabilities[0],control.controllerPiDescriptors['pi-config']);
 assert(control.validControllerDocument({contract,schema:'targets',targets:[context.target]},'targets'));
 assert(control.validControllerDocument(context,'target-context'));
 settings.binding=structuredClone(context.binding);settings.capability_revision=context.capability_revision;settings.revision=context.configuration_revision;
 try{
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${port}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);await waitForPhp(php,`http://127.0.0.1:${port}/index.php?page=config`,'Pi editor templates');
  proxy=http.createServer(async(request,response)=>{
   const route=new URL(request.url,'http://fixture').pathname;
   const reply=(body,status=200,headers={})=>{response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store',...headers});response.end(JSON.stringify(body));};
   if(route==='/api/control/v1/targets'){reply({contract,schema:'targets',targets:advertised?[context.target]:[]});return;}
   if(route.endsWith('/host/readiness')){const value=sample('P209 native recovery readiness');value.device_id=host;value.role='controller-only';value.state='ready';value.recovery_editable=false;reply(value);return;}
   if(route.endsWith('/host/management-session')){reply({contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'f'.repeat(64)});return;}
   if(route.startsWith('/api/control/v1/targets/'+device+'/')){
    calls.push({method:request.method,route});
    if(route.endsWith('/context')){reply(context);return;}
    if(request.method==='GET'){
     if(route.endsWith('/status')){const value=sample('Pi closed conservative status');value.binding=structuredClone(context.binding);value.revision=settings.revision;reply(value);return;}
     reply(route.includes('/operations/')?receipts.get(route.split('/').at(-1)):settings);return;
    }
    let raw='';for await(const part of request)raw+=part;const body=JSON.parse(raw);assert.equal(body.binding.device_id,device);
    receipt={contract,schema:'target-operation',binding:body.binding,operation_id:body.operation_id,request_digest:'c'.repeat(64),revision:'d'.repeat(64),state:'committed',error:null};
    if(body.candidate){settings.saved=structuredClone(body.candidate);settings.revision=receipt.revision;context.configuration_revision=receipt.revision;}
    if(route.endsWith('/stop')){receipt.revision='e'.repeat(64);settings.saved.configuration.Operation.Transmit=false;settings.revision=receipt.revision;context.configuration_revision=receipt.revision;}
    receipts.set(receipt.operation_id,structuredClone(receipt));
    if(lost){reply({contract,schema:'target-error',error:{code:'transport_unavailable',retryable:true}},503);return;}
    reply(receipt);return;
   }
   const upstream=http.request({hostname:'127.0.0.1',port,path:request.url,method:request.method,headers:request.headers},other=>{response.writeHead(other.statusCode,other.headers);other.pipe(response);});upstream.on('error',()=>response.destroy());request.pipe(upstream);
  });await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  const run=expression=>client.evaluate(expression);
  await client.send('Page.navigate',{url:`http://127.0.0.1:${proxy.address().port}/index.php?page=config`});
  await waitFor(()=>run("document.readyState==='complete'&&!!window.PiTargetEditors&&!!document.querySelector('#setup-device option[value=\"pi:'+ 'a'.repeat(32)+'\"]')"),'paired Pi Device choice');
  await waitFor(()=>run("!document.getElementById('setup-device').disabled"),'unlocked Device');
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("!document.getElementById('setup-pi-workspace').hidden&&PiTargetEditors.state&&!PiTargetEditors.state.paused&&document.querySelector('#pi-target-fields label')"),'native Pi saved editor');
  assert.equal(calls.filter(call=>call.method!=='GET').length,0,'Selection and reads never save');
  const edit=async text=>run(`(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='Message');const input=document.getElementById(label.htmlFor);input.closest('details').open=true;input.value=${JSON.stringify(text)};input.dispatchEvent(new Event('input'));})()`);
  await edit('BROWSER REMOTE PI DRAFT');assert.equal(await run("document.getElementById('pi-target-save').disabled"),false);
  await run("(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='TX Power');const input=document.getElementById(label.htmlFor);input.value='';input.dispatchEvent(new Event('input'));})()");
  assert.equal(await run("document.getElementById('pi-target-save').disabled"),true,'An invalid DOM draft blocks Save');

  await run("document.getElementById('setup-device').value='local';document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("!PiTargetEditors.state.paused"),'retained Pi selection');
  assert.equal(await run("(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='Message');return document.getElementById(label.htmlFor).value;})()"),'BROWSER REMOTE PI DRAFT');
  assert.equal(await run("(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='TX Power');const input=document.getElementById(label.htmlFor);return input.value===''&&input.getAttribute('aria-invalid')==='true';})()"),true,'Invalid typed entries survive target switches and read-only refresh');
  await run("(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='TX Power');const input=document.getElementById(label.htmlFor);input.value='20';input.dispatchEvent(new Event('input'));})()");
  await run("document.getElementById('pi-target-discard').click()");assert.equal(await run("document.activeElement.id"),'pi-target-discard-no');
  await run("document.getElementById('pi-target-discard-no').click()");assert.equal(await run("document.activeElement.id"),'pi-target-discard');
  lost=true;await run("document.getElementById('pi-target-save').click()");await waitFor(()=>run("PiTargetEditors.state.pending&&!document.getElementById('pi-target-check').disabled"),'lost save retained');
  assert.equal(calls.filter(call=>call.method==='PUT').length,1);
  assert.equal(await run("document.getElementById('pi-target-read-output').disabled"),false,'Unknown save does not block fresh Stop readback');
  await run("document.getElementById('pi-target-stop').closest('details').open=true;document.getElementById('pi-target-read-output').click()");
  await waitFor(()=>run("!document.getElementById('pi-target-stop').disabled"),'independent Stop with unresolved save');
  const stopOutput=path.resolve(ui,'../build/phase62-native-pi-ui');fs.mkdirSync(stopOutput,{recursive:true});
  for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');document.getElementById('pi-target-stop').scrollIntoView({behavior:'instant',block:'center'});`);await client.settleRendering();
   assert.equal(await run('document.documentElement.scrollWidth<=innerWidth'),true,'Unknown-save Stop overflow '+width);
   const stopBounds=await run("(()=>{const n=document.getElementById('pi-target-stop'),r=n.getBoundingClientRect();return {height:r.height,top:r.top,bottom:r.bottom,viewport:innerHeight,open:n.closest('details').open,display:getComputedStyle(n).display,scroll:scrollY};})()");assert(stopBounds.height>0&&stopBounds.top>=80&&stopBounds.bottom<=stopBounds.viewport-40,'Stop is visibly rendered '+width+': '+JSON.stringify(stopBounds));
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(stopOutput,`unknown-save-stop-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  lost=false;await run("document.getElementById('pi-target-stop').click()");
  await waitFor(()=>run("document.getElementById('pi-target-feedback').textContent.includes('earlier request is still unconfirmed')&&!document.getElementById('pi-target-check').disabled"),'Stop retains earlier unknown save');
  assert.equal(await run('PiTargetEditors.state.pending'),true);
  assert.equal(calls.filter(call=>call.route.endsWith('/stop')&&call.method==='POST').length,1);
  await run("document.getElementById('pi-target-check').click()");
  await waitFor(()=>run("document.getElementById('pi-target-feedback').textContent.includes('confirmed')&&!PiTargetEditors.state.pending"),'receipt confirmed');
  assert.equal(calls.filter(call=>call.method==='PUT').length,1,'Receipt checks never resend save');
  await run("document.getElementById('pi-target-refresh').click()");await waitFor(()=>run("!PiTargetEditors.state.paused&&!document.getElementById('pi-target-refresh').disabled"),'confirmed readback');
  await edit('RETAIN ON WITHDRAWAL');
  const output=path.resolve(ui,'../build/phase62-native-pi-ui');fs.mkdirSync(output,{recursive:true});
  for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');document.getElementById('setup-pi-workspace').scrollIntoView();window.scrollBy(0,-100)`);await client.settleRendering();
   assert.equal(await run('document.documentElement.scrollWidth<=innerWidth'),true,'Native Pi editor overflow '+width);
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`native-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  advertised=false;await waitFor(()=>run('PiTargetEditors.state.paused'),'paired source withdrawal',10000);
  assert.equal(calls.filter(call=>call.method==='PUT').length,1);
  console.log('PASS native Pi rendered editor: paired Device selection, retained drafts and cancellation focus, explicit save, lost-reply receipt recovery without replay, independent Stop with retained unknown save, source withdrawal and twelve desktop/mobile theme renders');
 }catch(error){failure=error;if(client)try{console.error(await client.evaluate("({feedback:document.getElementById('pi-target-feedback')?.textContent,state:window.PiTargetEditors?.state,options:document.getElementById('setup-device')?.textContent})"));}catch(_){}}
 await finishFixture({failure,client,browser,php,profile,cleanups:[async()=>{if(proxy)await closeServer(proxy);} ]});
})().catch(error=>{console.error(error);process.exitCode=1;});

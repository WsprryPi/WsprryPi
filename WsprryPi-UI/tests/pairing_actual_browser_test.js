// SPDX-License-Identifier: MIT
// Production protected pairing owners, actual PAKE/TLS and private journals.
// Only unrelated RF/status presentation is synthetic; all connections loop back.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),readline=require('node:readline');
const {freePort,spawnFixtureChild,waitForSpawn,waitForPhp,createBrowserProfile,launchBrowser,waitFor,finishFixture,closeServer,stopChild}=require('./browser_harness.js');
const {waitForExit}=require('./browser_fixture_process.js');
(async()=>{
 const binary=process.argv[2];assert(binary&&path.isAbsolute(binary),'Explicit compiled private-state pairing fixture required');
 const ui=path.resolve(__dirname,'..'),phpPort=await freePort(),id=c=>c.repeat(32);
 let actor,php,proxy,browser,client,profile,failure,selected,drop=false,dropSave=false,dropMigration=false,dropEnable=false,withhold=false;
 const requests=[];
 try{
  actor=spawnFixtureChild(binary,[],{stdio:['pipe','pipe','inherit']});await waitForSpawn(actor);
  const queued=[],pending=[];readline.createInterface({input:actor.stdout}).on('line',line=>pending.length?pending.shift()(line):queued.push(line));
  const line=()=>queued.length?Promise.resolve(queued.shift()):new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Pairing fixture reply deadline')),15000);pending.push(value=>{clearTimeout(timer);resolve(value);});});
  const ports=JSON.parse(await line());selected=ports.target_port;
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);
  await waitForPhp(php,`http://127.0.0.1:${phpPort}/index.php?page=maintenance`,'actual pairing PHP templates');
  proxy=http.createServer((request,response)=>{
   const route=new URL(request.url,'http://fixture').pathname;
   if(route==='/api/control/v1/status'){
    response.writeHead(200,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:'Wsprry-Control/1',host:{selected:false,firmware_maintenance:false,ready:false,phase:'idle',session_phase:'disconnected'}}));return;
   }
   if(withhold&&route.includes('/host/pairing/operations/')){
    response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:'Wsprry-Control/1',schema:'target-error',error:{code:'transport_unavailable',retryable:false}}));return;
   }
   const pairing=route.startsWith('/api/control/v1/host/pairing')||route.endsWith('/host/management-session');
   const management=route.startsWith('/api/control/v1/targets')||route.endsWith('/host/readiness');
   if(pairing||management)requests.push({port:selected,path:route,method:request.method});
   const port=pairing||management?selected:phpPort;
   const outgoing=http.request({hostname:'127.0.0.1',port,path:request.url,method:request.method,headers:request.headers},incoming=>{
    if(drop&&route.endsWith('/pairing/connect')&&[200,202].includes(incoming.statusCode)){
     drop=false;incoming.resume();incoming.once('end',()=>response.destroy());return;
    }
    if(dropMigration&&request.method==='POST'&&route.endsWith('/migration')&&[200,202].includes(incoming.statusCode)){
     dropMigration=false;incoming.resume();incoming.once('end',()=>{response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:'Wsprry-Control/1',schema:'target-error',error:{code:'transport_unavailable',retryable:true}}));});return;
    }
    if(dropEnable&&request.method==='POST'&&route.endsWith('/enable')&&incoming.statusCode===200){dropEnable=false;incoming.resume();incoming.once('end',()=>{response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:'Wsprry-Control/1',schema:'target-error',error:{code:'transport_unavailable',retryable:true}}));});return;}
    if(dropSave&&request.method==='PUT'&&route.endsWith('/settings')&&incoming.statusCode===200){dropSave=false;incoming.resume();incoming.once('end',()=>{response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:'Wsprry-Control/1',schema:'target-error',error:{code:'transport_unavailable',retryable:true}}));});return;}
    response.writeHead(incoming.statusCode,incoming.headers);incoming.pipe(response);
   });outgoing.on('error',()=>{response.writeHead(503);response.end();});request.pipe(outgoing);
  });
  await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${proxy.address().port}`;
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  const run=code=>client.evaluate(code);
  const navigate=async(port,paired=false)=>{
   selected=port;await client.send('Page.navigate',{url:origin+'/index.php?page=maintenance'});
   await waitFor(()=>run(`document.getElementById('controllerAccess')?.getAttribute('aria-busy')==='false' && !document.getElementById('pairingWindow')?.disabled && document.getElementById('pairingState')?.textContent.includes(${JSON.stringify(paired?'Managed by':'No controller')})`),'actual pairing page ready',15000);
  };
  const open=async()=>{
   await run("document.getElementById('pairingWindow').click()");
   await waitFor(()=>run("!!document.getElementById('pairingCode')?.textContent && document.getElementById('controllerAccess').getAttribute('aria-busy')==='false'"),'actual protected window',15000);
   return run("document.getElementById('pairingCode').textContent");
  };
  const connect=async code=>{
   await waitFor(()=>run("document.getElementById('pairingDevice')?.options.length===2"),'actual discovered target');
   await run(`document.getElementById('pairingDevice').selectedIndex=1;document.getElementById('pairingDevice').dispatchEvent(new Event('change'));document.getElementById('pairingInput').value=${JSON.stringify(code)};document.getElementById('pairingConnect').click()`);
  };
  await navigate(ports.target_port);let code=await open();assert.match(code,/^[0-9A-Z]{4}-[0-9A-Z]{4}$/);
  await navigate(ports.controller_port);assert.equal(await run("document.getElementById('pairingCode').textContent"),'','Code is confined to the initiating target page');
  drop=true;withhold=true;await connect(code);code='';
  await waitFor(()=>run("!document.getElementById('pairingPending').hidden && document.getElementById('controllerAccess').getAttribute('aria-busy')==='false' && document.getElementById('pairingInput').value===''"),'accepted actual pairing with lost browser reply',40000);
  const posts=requests.filter(item=>item.path.endsWith('/pairing/connect')).length;
  await navigate(ports.controller_port);assert.equal(await run("document.getElementById('pairingPending').hidden"),false);
  withhold=false;await waitFor(()=>run("document.getElementById('pairingPending').hidden && document.getElementById('pairingConnectFeedback').textContent.includes('confirmed')"),'actual fresh-browser durable confirmation',15000);
  assert.equal(requests.filter(item=>item.path.endsWith('/pairing/connect')).length,posts,'Receipt recovery does not send another pairing command');
  // Shipped native Pi Device editor through the actual protected controller,
  // current paired TLS and target configuration/WAL owners. No output starts.
  selected=ports.controller_port;await client.send('Page.navigate',{url:origin+'/index.php?page=config'});
  await waitFor(()=>run("!!window.PiTargetEditors&&!!document.querySelector('#setup-device option[value=\"pi:'+ 'a'.repeat(32)+'\"]')&&!document.getElementById('setup-device').disabled"),'actual native Pi inventory',15000);
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("PiTargetEditors.state&&!PiTargetEditors.state.paused&&!document.getElementById('setup-pi-workspace').hidden"),'actual native Pi editor',15000);
  await run("(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='Message');const input=document.getElementById(label.htmlFor);input.closest('details').open=true;input.value='ACTUAL PAIRED PI BROWSER DRAFT';input.dispatchEvent(new Event('input'));})()");
  const beforeSave=requests.filter(item=>item.method==='PUT'&&item.path.endsWith('/settings')).length;
  dropSave=true;await run("document.getElementById('pi-target-save').click()");
  await waitFor(()=>run("PiTargetEditors.state.pending&&!document.getElementById('pi-target-check').disabled"),'actual native Pi lost accepted save',15000);
  assert.equal(dropSave,false,'The lost response must follow an accepted actual settings save');
  let lastRecovery=0;
  await waitFor(async()=>{if(await run("!PiTargetEditors.state.pending&&document.getElementById('pi-target-feedback').textContent.includes('confirmed')"))return true;
   if(Date.now()-lastRecovery>=500&&await run("!document.getElementById('pi-target-check').disabled")){lastRecovery=Date.now();await run("document.getElementById('pi-target-check').click()");}return false;
  },'actual native Pi receipt recovery',15000);
  assert.equal(requests.filter(item=>item.method==='PUT'&&item.path.endsWith('/settings')).length,beforeSave+1,'Actual receipt recovery must not resend settings');
  await run("document.getElementById('pi-target-refresh').click()");
  await waitFor(()=>run("!PiTargetEditors.state.paused&&!document.getElementById('pi-target-refresh').disabled"),'actual native Pi saved readback',15000);
  assert.equal(await run("(()=>{const label=[...document.querySelectorAll('#pi-target-fields label')].find(item=>item.textContent==='Message');return document.getElementById(label.htmlFor).value;})()"),'ACTUAL PAIRED PI BROWSER DRAFT');
  console.log('PASS actual native Pi Device workflow: protected browser session, paired TLS, native configuration/WAL, lost accepted response, exact receipt lookup without another save, and authenticated saved readback');
  await navigate(ports.controller_port);
  await waitFor(()=>run("!document.getElementById('recurrenceMigrationReview').disabled&&document.getElementById('recurrenceMigrationDevice').textContent.includes('a'.repeat(32))"),'actual paired Maintenance migration');
  const migrationPosts=requests.filter(item=>item.method==='POST'&&item.path.endsWith('/migration')).length;
  dropMigration=true;
  await run("document.getElementById('recurrenceMigrationReview').click();document.getElementById('recurrenceMigrationConsent').checked=true;document.getElementById('recurrenceMigrationConsent').dispatchEvent(new Event('change'));document.getElementById('recurrenceMigrationConfirm').click()");
  await waitFor(()=>run("!document.getElementById('recurrenceMigrationRetry').hidden&&!document.getElementById('recurrenceMigrationRetry').disabled"),'actual accepted migration with interrupted browser reply');
  actor.stdin.write('ADVANCE_NATIVE\n');assert.equal(await line(),'ACK');
  await run("document.getElementById('recurrenceMigrationRetry').click()");
  await waitFor(()=>run("document.getElementById('recurrenceMigrationRetry').hidden&&document.getElementById('recurrenceMigrationState').textContent.includes('schedule disabled pending review')"),'actual native atomic disabled staging and receipt recovery',15000);
  assert.equal(requests.filter(item=>item.method==='POST'&&item.path.endsWith('/migration')).length,migrationPosts+1,'Migration recovery never repeats the accepted command');
  console.log('PASS actual paired Maintenance migration: protected browser, current normal TLS, native configuration/authority/WAL, interrupted accepted reply, explicit progress reconciliation and receipt-only disabled staging');
  selected=ports.controller_port;await client.send('Page.navigate',{url:origin+'/index.php?page=config'});
  await waitFor(()=>run("!!document.querySelector('#setup-device option[value=\"pi:'+ 'a'.repeat(32)+'\"]')&&!document.getElementById('setup-device').disabled"),'actual Scheduling inventory');
  await run("document.getElementById('setup-device').value='pi:'+'a'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("PiTargetEditors.state&&!PiTargetEditors.state.paused"),'actual selected Pi');
  await run("document.getElementById('scheduling-tab').click()");
  await waitFor(()=>run("SchedulingEditor.state&&!SchedulingEditor.state.paused&&!!document.querySelector('#schedule-fields fieldset')"),'actual Scheduling read');
  const policyWrites=requests.filter(item=>item.method==='PUT'&&item.path.endsWith('/settings')).length;
  await run("document.getElementById('schedule-enabled').click();(()=>{const label=[...document.querySelectorAll('#schedule-fields label')].find(n=>n.textContent==='Target-site locator');const input=document.getElementById(label.htmlFor);input.value='EM18IG';input.dispatchEvent(new Event('input',{bubbles:true}));})()");
  assert.equal(await run("document.getElementById('schedule-enable').disabled"),true,'Actual unsaved policy cannot enable');
  dropSave=true;await run("document.getElementById('schedule-save').click()");
  await waitFor(()=>run("SchedulingEditor.state.pending&&!document.getElementById('schedule-check').disabled"),'actual lost policy save');
  await waitFor(async()=>{
   const state=await run("({pending:SchedulingEditor.state.pending,paused:SchedulingEditor.state.paused,check:!document.getElementById('schedule-check').disabled,read:!document.getElementById('schedule-read').disabled})");
   if(Date.now()-lastRecovery<500)return false;lastRecovery=Date.now();
   if(state.pending&&state.check)await run("document.getElementById('schedule-check').click()");
   else if(!state.pending&&state.paused&&state.read)await run("document.getElementById('schedule-read').click()");
   return !state.pending&&!state.paused;
  },'actual policy receipt-only recovery',15000);
  assert.equal(requests.filter(item=>item.method==='PUT'&&item.path.endsWith('/settings')).length,policyWrites+1);
  // Receipt recovery preserves an unconfirmed raw draft until explicit reread.
  await run("document.getElementById('schedule-read').click()");
  await waitFor(()=>run("!document.getElementById('schedule-read').disabled"),'actual saved policy read');
  await waitFor(()=>run("!document.getElementById('schedule-enable').disabled"),'actual disabled policy verified');
  const enables=requests.filter(item=>item.method==='POST'&&item.path.endsWith('/enable')).length;
  dropEnable=true;await run("document.getElementById('schedule-enable').click()");
  await waitFor(()=>run("SchedulingEditor.state.pending&&!document.getElementById('schedule-check').disabled"),'actual lost enable');
  await waitFor(async()=>{
   const state=await run("({pending:SchedulingEditor.state.pending,paused:SchedulingEditor.state.paused,check:!document.getElementById('schedule-check').disabled,read:!document.getElementById('schedule-read').disabled})");
   if(Date.now()-lastRecovery<500)return false;lastRecovery=Date.now();
   if(state.pending&&state.check)await run("document.getElementById('schedule-check').click()");
   else if(!state.pending&&state.paused&&state.read)await run("document.getElementById('schedule-read').click()");
   return !state.pending&&!state.paused;
  },'actual enable receipt recovery',15000);
  assert.equal(requests.filter(item=>item.method==='POST'&&item.path.endsWith('/enable')).length,enables+1);
  console.log('PASS actual Scheduling: protected browser, paired TLS, native policy/WAL, disabled save/readback and explicit enable; lost policy and enable replies recover through receipts without replay');
  await navigate(ports.target_port,true);assert((await run("document.getElementById('pairingState').textContent")).includes(id('b')));
  actor.stdin.write('STOP_OLD\n');assert.equal(await line(),'ACK');
  code=await open();await navigate(ports.replacement_port);await connect(code);code='';
  await waitFor(()=>run("(document.getElementById('pairingConnectFeedback').textContent.includes('Paired with') || document.getElementById('pairingConnectFeedback').textContent.includes('Pairing is confirmed')) && document.getElementById('controllerAccess').getAttribute('aria-busy')==='false'"),'actual replacement without old controller',40000);
  await navigate(ports.target_port,true);assert((await run("document.getElementById('pairingState').textContent")).includes(id('e')));
  await run("document.getElementById('pairingRemoveReview').click();document.getElementById('pairingRemove').click()");
  await waitFor(()=>run("document.getElementById('pairingState').textContent.includes('No controller') && document.getElementById('pairingLocalFeedback').textContent.includes('removed')"),'actual protected removal');
  assert.equal(await run("JSON.stringify(localStorage).includes('code')"),false,'No code field is persisted');
  console.log('PASS actual Maintenance workflow: protected window, PAKE/TLS, durable lost-reply readback after reload, replacement with old controller offline, removal and code confinement; no hardware or RF');
 }catch(error){
  if(client)try{console.error('Actual pairing UI state:',await client.evaluate("({state:document.getElementById('pairingState')?.textContent,local:document.getElementById('pairingLocalFeedback')?.textContent,connect:document.getElementById('pairingConnectFeedback')?.textContent,disabled:document.getElementById('pairingWindow')?.disabled,migration:document.getElementById('recurrenceMigrationState')?.textContent,migrationFeedback:document.getElementById('recurrenceMigrationFeedback')?.textContent,migrationHidden:document.getElementById('recurrenceMigration')?.hidden,device:document.getElementById('recurrenceMigrationDevice')?.textContent,recovery:window.ConfigurationRecovery?.active,piFeedback:document.getElementById('pi-target-feedback')?.textContent,piPaused:window.PiTargetEditors?.state?.paused,scheduling:window.SchedulingEditor?.state,scheduleFeedback:document.getElementById('schedule-feedback')?.textContent})"));}catch(_){}
  failure=error;
 }finally{await finishFixture({failure,client,browser,php,profile,cleanups:[async()=>{if(proxy)await closeServer(proxy);},async()=>{if(actor){if(actor.exitCode===null&&!actor.signalCode){actor.stdin.write('QUIT\n');if(!await waitForExit(actor,5000))await stopChild(actor);}assert.equal(actor.exitCode,0,'Pairing fixture must drain and remove its private histories cleanly');}}]});}
})().catch(error=>{console.error(error);process.exitCode=1;});

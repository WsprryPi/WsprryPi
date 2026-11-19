// SPDX-License-Identifier: MIT
// Shipped legacy Fleet presentation: retained history is never execution authority.
'use strict';
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {freePort,createBrowserProfile,spawnFixtureChild,waitForSpawn,waitForPhp,launchBrowser,waitFor,finishFixture}=require('./browser_harness.js');
(async()=>{
 let php,browser,client,profile,failure;
 try {
  const ui=path.resolve(__dirname,'..'),port=await freePort(),url=`http://127.0.0.1:${port}/index.php?page=config`;
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${port}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);await waitForPhp(php,url,'retired Fleet fixture');
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  const run=expression=>client.evaluate(expression);
  await client.send('Page.navigate',{url});await waitFor(()=>run("document.readyState==='complete' && !!window.FleetClient && !!window.FleetSchedules"),'Fleet scripts');
  const install=async()=>run(`(()=>{
   if(typeof clearPendingPopulateConfigRetry==='function'){configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();}
   if(typeof syncConnectionAlert==='function'){backendCurrentlyConnected=true;websocketCurrentlyConnected=true;syncConnectionAlert();clearBackendStatus('runtime');clearConfigLoadFailureState();}
   window.__originalFetch=window.fetch;window.__requests=[];window.__fail=false;window.__revision=0;
   const id='a'.repeat(32);
   window.__fleet={scope:'wsprrypi-wtp-fleet/1',version:1,maximum_outputs:32,controller_incarnation:'f'.repeat(32),publication_revision:'1',now_ms:'10000',removals:[],assignments:[{device_id:id,name:'Bench Pico with retained controller work',product:'WsprryPico',enabled:true,in_flight:true,schedule:{mode:'tone',frequency_hz:14097100,duration_ms:1000,period_seconds:60,phase_seconds:0}}],outputs:{[id]:{state:'migration_required',message:'Migration required. Output and ownership remain unconfirmed.',assignment_generation:id,control_revision:'0',observation_revision:'1',observation_age_ms:null,boot_id:null,can_resume:false,can_pause:false,can_reconcile:false,actions:[]}}};
   window.fetch=async(url,options={})=>{
    window.__requests.push({url:String(url),method:options.method||'GET'});
    if(String(url).includes('/host/fleet')){
     if(window.__fail)throw Error('Controller disconnected');
     window.__fleet.publication_revision=String(++window.__revision);window.__fleet.now_ms=String(10000+window.__revision);
     return new Response(JSON.stringify(window.__fleet),{headers:{ETag:'"fleet-1"'}});
    }
    if(String(url).endsWith('/host/readiness'))return new Response(JSON.stringify({contract:'Wsprry-Control/1',schema:'host-readiness',device_id:'f'.repeat(32),boot_id:'f'.repeat(32),role:'transmitter',state:'ready',recovery_editable:false}));
    if(String(url).endsWith('/targets'))return new Response(JSON.stringify({contract:'Wsprry-Control/1',schema:'targets',targets:[]}));
    if(String(url).endsWith('/host/devices'))return new Response(JSON.stringify({scope:'wsprrypi-wtp-catalog/1',version:1,profiles:[],active_id:'',active_unmatched:false}));
    if(String(url).endsWith('/host/discovery'))return new Response(JSON.stringify({available:false,candidates:[],reason:'Fixture'}));
    return window.__originalFetch(url,options);
   };
   if(document.getElementById('fleet-tab'))bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();
  })()`);
  await install();await run('FleetClient.refresh(true)');
  await waitFor(()=>run("document.getElementById('fleet-output-list').textContent.includes('accepted work recorded: yes')"),'retained legacy record');
  assert.equal(await run("document.getElementById('fleet-schedule-new')===null"),true,'No new legacy assignment control');
  assert.equal(await run("[...document.querySelectorAll('#fleet-output-list button')].map(n=>n.textContent).join(',')"),'View retirement');
  assert.match(await run("document.getElementById('fleet-schedules').textContent"),/cannot dispatch remote transmissions/);
  const output=path.resolve(process.env.WSPRRYPI_FLEET_RETIREMENT_SCREENSHOTS||path.join(ui,'../build/wtp-retirement/fleet-ui'));fs.mkdirSync(output,{recursive:true});
  for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
   await run(`applyThemePreference('${theme}');document.getElementById('fleet-schedules').scrollIntoView({block:'start',behavior:'instant'})`);await client.settleRendering();
   assert(await run('document.documentElement.scrollWidth<=innerWidth'),'Retained Fleet must fit '+width);
   await waitFor(()=>run("getComputedStyle(document.getElementById('fleet-pane')).opacity==='1'"),'Fleet transition finished');
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`retired-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await client.send('Page.navigate',{url:url.replace('page=config','page=operation')});
  await waitFor(()=>run("document.readyState==='complete' && !!window.FleetOperation"),'Operations scripts');
  await install();await run('FleetClient.refresh(true)');
  await run("document.getElementById('operationDevice').value='a'.repeat(32)+':'+'a'.repeat(32);document.getElementById('operationDevice').dispatchEvent(new Event('change'))");
  assert.equal(await run("document.getElementById('operationFleetState').textContent"),'Migration required');
  assert.match(await run("document.getElementById('operationFleetOutput').textContent"),/unknown/);
  assert.equal(await run("document.getElementById('operationFleetPause')===null && document.getElementById('operationFleetResume')===null"),true,'Retained Operations has no remote execution actions');
  await run("document.getElementById('operationDevice').value='';document.getElementById('operationDevice').dispatchEvent(new Event('change'))");
  assert.equal(await run("document.getElementById('operationLocalView').hidden"),false,'Native local Operations remains available');
  assert.equal(await run("window.__requests.filter(r=>r.method!=='GET').length"),0,'Operations selection cannot mutate or dispatch');
  // Return to the same read-only saved-state component for failed-read coverage.
  await client.send('Page.navigate',{url});await waitFor(()=>run("document.readyState==='complete' && !!window.FleetSchedules"),'Fleet return');await install();await run('FleetClient.refresh(true)');
  await run('window.__fail=true;FleetClient.refresh(true).catch(()=>{})');
  await waitFor(()=>run("document.getElementById('fleet-schedule-feedback').textContent.includes('unavailable or stale')"),'failed read remains unknown');
  assert.equal(await run("window.__requests.filter(r=>r.method!=='GET').length"),0,'Reads, navigation, and failure cannot dispatch or rewrite history');
  console.log('PASS read-only retained Fleet, saved enable/in-flight history, failed-read hold, zero mutations, desktop/mobile light/dark');
 }catch(error){failure=error;throw error;}finally{await finishFixture({failure,client,browser,php,profile});}
})().catch(error=>{console.error(error);process.exitCode=1;});

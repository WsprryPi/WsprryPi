// SPDX-License-Identifier: MIT
// Rendered editor lifecycle with an injected management transport. Crypto and
// network/USB producer paths have separate actual-pair integration coverage.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {freePort,createBrowserProfile,spawnFixtureChild,waitForSpawn,waitForPhp,launchBrowser,waitFor,finishFixture}=require('./browser_harness.js');
(async()=>{
 let php,browser,client,profile,failure;
 try{
  const ui=path.resolve(__dirname,'..'),port=await freePort(),url=`http://127.0.0.1:${port}/index.php?page=config`;
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${port}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);await waitForPhp(php,url,'consumer editor fixture');
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000,expectedDialogs:[
   'Review the current saved values beside your preserved draft. Matching values do not prove an earlier save committed. Send one new explicit save with this fresh revision?']}));
  const run=expression=>client.evaluate(expression);
  await client.send('Page.navigate',{url});
  await waitFor(()=>run("document.readyState==='complete' && !!window.WtpUsbSettingsEditor && !!window.PicoTargetEditors"),'editor factories');
  await client.send('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
  await run(`(()=>{
   configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();
   window.__generation='1';window.__editorCalls=[];window.__reviews=0;
   window.__profiles=Object.fromEntries(['a','b'].map(id=>[id,{id:id.repeat(32),name:'Consumer '+id,settings:{Transport:'network_plain',Endpoint:'',Hostname:'pico-'+id+'.local','Device ID':id.repeat(32),'TCP Port':31417}}]));
   window.__profiles.a.name='A'.repeat(80);
   // Inject at construction, rather than adding a production transport setter.
   window.PicoTargetClient={create:()=>({operations:()=>[],reviewed:()=>{++window.__reviews;},open:async profile=>{
    const device=profile.settings['Device ID'];let retired=false;
    const state={contract:'Wsprry-Control/1',carrier:profile.managed?'usb_physical':'consumer_station',connected:Boolean(profile.managed),host:profile.settings.Hostname||'',selection_id:device,session:null};
    const recovery={device,operation:'9'.repeat(32),resource:'configuration',read:'configuration'};
    const handle={device,profileId:profile.id,managed:Boolean(profile.managed),parentDevice:'d'.repeat(32),scope:profile.managed?(window.__pairedScope||'paired-controller-a'):profile.id,state,context:device,
     review:async()=>{++window.__fencedReviews;return {data:{},response:new Response('{}',{status:200})};},retire(){retired=true;},generation:async()=>window.__generation,
     lookup:async()=>{if(window.__receiptState==='missing')throw Object.assign(Error('Receipt expired'),{status:404});return {data:{state:window.__receiptState},recovery};},
     request:async(resource,options={})=>{
      if(retired)throw Error('Retired handle');window.__editorCalls.push({device,resource,method:options.method||'GET'});
      if(resource==='configuration'&&options.method==='POST'&&window.__refuseSave)return {data:{schema:'target-operation',state:'unknown'},response:new Response('{}',{status:503}),recovery,operation:recovery.operation};
      if(resource==='management'&&options.method==='POST')state.connected=options.body.action==='connect';
      const data=resource==='management'?{...state}:resource==='configuration'?{contract:'Wsprry-Control/1',schema:'pico-settings',target:{scope:'member',device_id:device,boot_id:'1'.repeat(32)},config:{enabled:false,station:{callsign:'W1AW',locator:'EM18',power_dbm:20},wifi:{ssid:'network',password:null,ntp_ipv4:'192.0.2.1'},schedules:[{period_s:120,phase_s:0}],expires_utc_s:0},reboot_required:false}:{enabled:true};
      return {data,response:new Response(JSON.stringify(data),{headers:{ETag:'"1"'}}),readback:'1'.repeat(64)};
     }};return handle;
   }})};
   bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();
  })()`);
  await run(fs.readFileSync(path.join(ui,'data/pico-target-editors.js'),'utf8'));
  // Re-evaluation initializes immediately on the already loaded document.
  const select=async id=>{await run(`PicoTargetEditors.select(window.__profiles.${id})`);await waitFor(()=>run("!!document.getElementById('wtp-management-connect') && !document.getElementById('wtp-management-connect').disabled"),'consumer selection');};
  const login=async()=>{
   await waitFor(()=>run("document.getElementById('wtp-management').getAttribute('aria-busy')==='false'"),'consumer inspection finished');
   await run("document.getElementById('wtp-management-carrier').value='consumer_station';document.getElementById('wtp-management-carrier').dispatchEvent(new Event('change'));document.getElementById('wtp-management-password').value='fixture password';document.getElementById('wtp-management-connect').click()");
   await waitFor(()=>run("document.getElementById('wtp-management').getAttribute('aria-busy')==='false' && !document.getElementById('wtp-remote-load').disabled"),'authenticated editor');
  };
  const load=async()=>{await run("document.getElementById('wtp-remote-load').click()");await waitFor(()=>run("!document.getElementById('wtp-remote-save').disabled"),'consumer settings read');};
  await select('a');await login();await load();
  await run("document.getElementById('wtp-remote-callsign').value='K1ABC';document.getElementById('wtp-remote-callsign').dispatchEvent(new Event('input'));window.__firstEditor=document.getElementById('wtp-management')");
  await select('b');await login();await load();
  await run("document.getElementById('pico-target-retained').open=true");
  assert(await run('document.documentElement.scrollWidth<=innerWidth'),'A maximum-length retained profile name must fit mobile width');
  await select('a');await login();
  await waitFor(()=>run("document.getElementById('wtp-management')===window.__firstEditor"),'same-generation authenticated draft restoration');
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC');
  assert.equal(await run("document.getElementById('wtp-management-password').value"),'');
  assert.equal(await run("document.getElementById('wtp-remote-save').disabled"),true,'Restored draft requires a fresh resource revision');
  await load();assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC');
  await select('b');await login();await run("window.__generation='2'");await select('a');await login();await load();
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'W1AW','A new profile generation cannot inherit an older draft');
  assert.equal(await run("document.getElementById('wtp-management')===window.__firstEditor"),false);
  assert.equal(await run("window.__editorCalls.filter(call=>call.method!=='GET'&&call.resource!=='management').length"),0,'Selection, login, read and profile removal cannot save settings');
  await run("document.getElementById('wtp-remote-callsign').value='K2ABC';document.getElementById('wtp-remote-callsign').dispatchEvent(new Event('input'))");
  for(const state of ['committed','missing']) {
   await run(`window.__receiptState='${state}';window.__refuseSave=true;document.getElementById('wtp-remote-save').click()`);
   await waitFor(()=>run("!document.getElementById('pico-target-recovery').hidden"),'uncertain editor hold');
   await run("document.getElementById('pico-target-read-recovery').click()");await waitFor(()=>run("!document.getElementById('pico-target-review-recovery').disabled"),'receipt and readback review');
   await run("document.getElementById('pico-target-review-recovery').click()");await waitFor(()=>run("document.getElementById('pico-target-recovery').hidden"),'explicit receipt review');
   await load();
  }
  assert.equal(await run('window.__reviews'),2,'Committed or expired receipts require explicit review without a native reconcile');
  await run("window.__receiptState='unknown';document.getElementById('wtp-remote-save').click()");await waitFor(()=>run("!document.getElementById('pico-target-recovery').hidden"),'held draft before revocation');
  await run("document.getElementById('pico-target-read-recovery').click()");await waitFor(()=>run("!document.getElementById('pico-target-review-recovery').disabled"),'readback before revocation');
  await run("PicoTargetEditors.catalogChanged([])");
  assert.equal(await run("document.getElementById('wtp-management').inert && document.getElementById('pico-target-review-recovery').disabled && document.getElementById('pico-target-read-recovery').disabled"),true);
  await run("document.getElementById('pico-target-review-recovery').dispatchEvent(new Event('click'))");
  assert.equal(await run('window.__reviews'),2,'Retired profile cannot release its previous recovery hold');
  // Automatic paired inventory is a transient editor source, not a saved
  // WTP profile or another login/delegation workflow.
  await run(`(()=>{
   window.__fencedReviews=0;window.__advertiseManaged=true;window.__refuseSave=false;
   const previous=window.fetch;window.fetch=async(url,options)=>{
    if(url.endsWith('/host/readiness'))return new Response(JSON.stringify({contract:'Wsprry-Control/1',schema:'host-readiness',device_id:'f'.repeat(32),boot_id:'f'.repeat(32),role:'transmitter',state:'ready',recovery_editable:false}));
    if(url.endsWith('/targets'))return new Response(JSON.stringify({contract:'Wsprry-Control/1',schema:'targets',targets:window.__advertiseManaged?[{device_id:'c'.repeat(32),product:'WsprryPico',name:'Attached Pico',profile_generation:'1',connection_generation:'1'}]:[]}));
    return previous(url,options);
   };
   window.WtpUi={...window.WtpUi,catalogReady:true};SetupTargets.catalogChanged([],[]);
  })()`);
  await waitFor(()=>run("!!document.querySelector('#setup-device option[value=\"managed:'+ 'c'.repeat(32)+'\"]') && !document.getElementById('setup-device').disabled"),'automatic paired child inventory',10000);
  await run("document.getElementById('setup-device').value='managed:'+'c'.repeat(32);document.getElementById('setup-device').dispatchEvent(new Event('change'))");
  await waitFor(()=>run("PicoTargetEditors.state?.device==='c'.repeat(32) && PicoTargetEditors.state.connected && !document.getElementById('setup-pico-workspace').hidden && !document.getElementById('wtp-remote-load').disabled"),'automatic paired child editor');
  assert.equal(await run("document.getElementById('wtp-management-carrier').closest('.row').hidden"),true,'Paired child needs no separate login');
  assert.equal(await run("window.__editorCalls.filter(call=>call.device==='c'.repeat(32)&&call.method!=='GET').length"),0,'Negotiation does not save or select an output');
  await load();await run("document.getElementById('wtp-remote-callsign').value='K3ABC';document.getElementById('wtp-remote-callsign').dispatchEvent(new Event('input'));window.__receiptState='missing';window.__refuseSave=true;document.getElementById('wtp-remote-save').click()");
  await waitFor(()=>run("!document.getElementById('pico-target-recovery').hidden"),'paired unknown operation');
  await run("document.getElementById('pico-target-read-recovery').click()");await waitFor(()=>run("!document.getElementById('pico-target-review-recovery').disabled"),'private older receipt fresh readback');
  await run("document.getElementById('pico-target-review-recovery').click()");await waitFor(()=>run("document.getElementById('pico-target-recovery').hidden"),'paired fenced review');
  assert.equal(await run('window.__fencedReviews'),1,'A private or missing receipt must not skip the paired server fence');
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K3ABC','Review preserves the draft');
  await run('PicoTargetEditors.catalogChanged([])');assert.equal(await run('PicoTargetEditors.state.paused'),false,'Unrelated local WTP catalog changes cannot revoke a paired editor');
  const evidence=process.env.WSPRRYPI_MANAGED_PICO_SCREENSHOTS||path.resolve(ui,'../build/phase62-managed-pico-ui');fs.mkdirSync(evidence,{recursive:true});
  for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');window.scrollTo(0,0)`);await client.settleRendering();
   assert.equal(await run('document.documentElement.scrollWidth<=innerWidth'),true,'Managed editor overflow '+width);
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(evidence,`managed-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await run("bootstrap.Tab.getOrCreateInstance(document.getElementById('pi-hardware-tab')).show()");
  await waitFor(()=>run("document.getElementById('setup-pico-io').classList.contains('active')"),'managed Device I/O tab');
  assert.equal(await run("document.getElementById('wtp-management-carrier').closest('.row').hidden"),true,'Device I/O cannot expose a second login or certificate workflow for a managed child');
  for(const theme of ['light','dark'])for(const width of [1440,390,320]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
   await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');window.scrollTo(0,0)`);await client.settleRendering();
   assert.equal(await run('document.documentElement.scrollWidth<=innerWidth'),true,'Managed Device I/O overflow '+width);
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(evidence,`managed-io-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await run('window.__advertiseManaged=false');await waitFor(()=>run('PicoTargetEditors.state.paused'),'withdrawn parent observation holds editing',10000);
  console.log('PASS rendered managed Pico: automatic Device choice, no extra login/profile, retained drafts, missing-receipt server fence, local-catalog isolation, withdrawal and six theme/viewport renders');
  console.log('PASS: rendered consumer re-login restores only the same-generation draft onto a fresh handle; passwords, revision readback, committed/expired receipt review, and profile revocation remain fenced');
 }catch(error){failure=error;if(client)try{console.error(await client.evaluate("({feedback:document.getElementById('wtp-management-feedback')?.textContent,recovery:document.getElementById('pico-target-recovery-state')?.textContent,calls:window.__editorCalls})"));}catch(_){} }
 await finishFixture({failure,client,browser,php,profile});
})().catch(error=>{console.error(error);process.exitCode=1;});

// SPDX-License-Identifier: MIT
// Actual Pi main, explicit simulated output, actual authenticated Pico software peers.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),http=require('node:http'),net=require('node:net');
const {waitForPhp,createBrowserProfile,freePort,waitFor,launchBrowser,finishFixture,spawnFixtureChild,waitForSpawn,closeServer,stopChild}=require('./browser_harness.js');
module.exports=async function exercise({credentials,dumpA,dumpB,activeA,jobA,endA,rebootA}) {
 const binary=process.env.WSPRRYPI_SHARED_SETUP_MAIN_FIXTURE;assert(binary&&path.isAbsolute(binary),'Explicit offline-output actual-main fixture required');
 const ui=path.resolve(__dirname,'..'),directory=fs.mkdtempSync(path.join(os.tmpdir(),'shared-setup-main-'));
 async function nativePort(start){for(let n=start;n<start+100;n++){const server=net.createServer();if(await new Promise(resolve=>{server.once('error',()=>resolve(false));server.listen(n,'127.0.0.1',()=>server.close(()=>resolve(true)));}))return n;}throw Error('No valid native listener port');}
 const port=await nativePort(39500),socketPort=await nativePort(port+1),phpPort=await freePort(),backend=new URL(`http://127.0.0.1:${port}`),sockets=new Set(),requests=[];
 const ini=path.join(directory,'native.ini'),state=path.join(directory,'state'),ledger=path.join(directory,'revocation-v1');
 let source=fs.readFileSync(path.join(ui,'../config/wsprrypi.ini'),'utf8');
 function set(section,key,value){const marker='['+section+']',begin=source.indexOf(marker),end=source.indexOf('\n[',begin+marker.length);assert(begin>=0);const stop=end<0?source.length:end;const part=source.slice(begin,stop);const escaped=key.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');const re=new RegExp('^'+escaped+' =.*$','m');assert(re.test(part),key);source=source.slice(0,begin)+part.replace(re,key+' = '+value)+source.slice(stop);}
 for(const [key,value]of Object.entries({'Transmit Backend':'gpio',Transmit:'false','Enable on Boot':'Never','Use LED':'false','Use Amp':'false','Use Shutdown':'false','Web Port':port,'Socket Port':socketPort}))set('Operation',key,value);
 for(const [key,value]of Object.entries({'Call Sign':'AA0NT','Grid Square':'EM18','TX Power':20}))set('WSPR',key,value);
 fs.writeFileSync(ini,source,{mode:0o600});
 let initialRead=false,releaseInitial,holdInitial=true,nativeLog='',lastReadiness='',application,web,php,proxy,chrome,client,profile,failure;
 const output=process.env.WSPRRYPI_SHARED_SETUP_SCREENSHOTS||path.join(os.tmpdir(),'issue473-phase3-shared-setup-captures');fs.mkdirSync(output,{recursive:true});
 try{
  const migration=spawnFixtureChild(binary,[ledger,credentials,'--migrate-state','--state-directory',state,'--ini-source',ini],{stdio:['ignore','pipe','pipe']});
  let migrationLog='';for(const stream of [migration.stdout,migration.stderr])stream.on('data',chunk=>migrationLog=(migrationLog+chunk).slice(-12000));
  await waitForSpawn(migration);await waitFor(()=>migration.exitCode!==null||migration.signalCode!==null,'one-time native migration',15000);assert.equal(migration.exitCode,0,migrationLog);assert.equal(fs.existsSync(ini),false);
  application=spawnFixtureChild(binary,[ledger,credentials,'--backend','simulated','--state-directory',state,'--socket-loopback-only','--socket-loopback-family','ipv4'],{stdio:['ignore','pipe','pipe']});application.stdout.on('data',chunk=>nativeLog=(nativeLog+chunk).slice(-12000));await waitForSpawn(application);
  web=spawnFixtureChild(binary,[ledger,credentials,'--web-service','--listener-cache',path.join(fs.realpathSync(directory),'listeners.json')],{stdio:['ignore','pipe','pipe']});for(const stream of [web.stdout,web.stderr])stream.on('data',chunk=>nativeLog=(nativeLog+chunk).slice(-12000));await waitForSpawn(web);
  await waitFor(async()=>{try{const r=await fetch(backend.origin+'/api/control/v1/host/readiness');const body=await r.text();lastReadiness=String(r.status)+body;return r.ok&&JSON.parse(body).state==='ready';}catch(_){return false;}},'owning Pi readiness',20000);
  let cookie='',csrf='';async function call(resource,method='GET',body,headers={}){await new Promise(r=>setTimeout(r,110));const response=await fetch(backend.origin+resource,{method,headers:{Origin:backend.origin,'Content-Type':'application/json','X-WsprryPico-Request':'1',...(cookie?{Cookie:cookie,'X-WsprryPi-CSRF':csrf}:{}),...headers},...(body?{body:JSON.stringify(body)}:{})});const value=await response.json();assert.equal(response.status,200,JSON.stringify(value));return {body:value,response};}
  async function localConfiguration(){return (await call('/config')).body;}
  const session=await call('/api/control/v1/host/management-session','POST',{contract:'Wsprry-Control/1',schema:'controller-browser-session'});csrf=session.response.headers.get('X-WsprryPi-CSRF');cookie=session.response.headers.get('Set-Cookie').split(';')[0];const profiles={};
  for(const [id,tcp]of [['a',18443],['b',18444]]){const catalog=await call('/api/control/v1/host/devices');const settings={'Endpoint':'','USB Serial':'','USB Vendor ID':0,'USB Product ID':0,'TLS Server Identity':'',Transport:'network',Hostname:'wsprrypico-'+id.repeat(32)+'.local','Device ID':id.repeat(32),'TCP Port':tcp,'TLS CA File':path.join(credentials,id,'client-ca.crt'),'TLS Client Certificate':path.join(credentials,id,'client.crt'),'TLS Client Key':path.join(credentials,id,'client.key')};const saved=await call('/api/control/v1/host/devices','POST',{operation:'add',name:id==='a'?'Pico A with a deliberately long station location and equipment name':'Pico B',method:'manual',discovery_id:'',settings,consent_plain:false},{'If-Match':catalog.response.headers.get('ETag')});profiles[id]=saved.body.profiles.at(-1).id;const route='/api/control/v1/targets/'+id.repeat(32)+'/pico/management';const opened=await call(route,'GET',undefined,{'X-WsprryPi-Profile-ID':profiles[id]});const connected=await call(route,'POST',{contract:'Wsprry-Control/1',action:'engineering_mtls'},{'X-WsprryPi-Profile-ID':profiles[id],'X-WsprryPi-Target-Context':opened.response.headers.get('X-WsprryPi-Target-Context')});}
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);
  await waitForPhp(php,`http://127.0.0.1:${phpPort}/index.php?page=config`,'target editor PHP');
  proxy=http.createServer((request,response)=>{
   const pathname=new URL(request.url,'http://fixture').pathname;
   const production=['/config','/version'].includes(pathname)||pathname.startsWith('/api/control/v1/');
   requests.push({path:pathname,method:request.method});
   const outgoing=http.request({hostname:'127.0.0.1',port:production?backend.port:phpPort,path:request.url,method:request.method,headers:request.headers},upstream=>{
    if(holdInitial&&request.method==='GET'&&['/config','/api/control/v1/host/config'].includes(pathname)) {
     holdInitial=false;const parts=[];upstream.on('data',part=>parts.push(part));upstream.on('end',()=>{initialRead=true;releaseInitial=()=>{if(!response.destroyed){response.writeHead(upstream.statusCode,upstream.headers);response.end(Buffer.concat(parts));}};});
    }else{response.writeHead(upstream.statusCode,upstream.headers);upstream.pipe(response);}
   });outgoing.on('error',()=>{response.writeHead(503);response.end();});request.pipe(outgoing);
  });
  proxy.on('upgrade',(request,downstream,head)=>{
   if(request.url!=='/socket'){downstream.destroy();return;}
   const upstream=net.connect(socketPort,'127.0.0.1');sockets.add(upstream);sockets.add(downstream);
   upstream.on('connect',()=>{upstream.write(`${request.method} / HTTP/${request.httpVersion}\r\n`+request.rawHeaders.reduce((text,value,index,all)=>text+(index%2===0?`${value}: ${all[index+1]}\r\n`:''),'')+'\r\n');if(head.length)upstream.write(head);downstream.pipe(upstream).pipe(downstream);});
   upstream.on('error',()=>downstream.destroy());downstream.on('error',()=>upstream.destroy());
   upstream.on('close',()=>{sockets.delete(upstream);downstream.destroy();});downstream.on('close',()=>{sockets.delete(downstream);upstream.destroy();});
  });
  await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${proxy.address().port}`;
  profile=createBrowserProfile();({browser:chrome,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000,expectedDialogs:['Review the current saved values beside your preserved draft. Matching values do not prove an earlier save committed. Send one new explicit save with this fresh revision?']}));
  const run=expression=>client.evaluate(expression);
  client.on('Runtime.exceptionThrown',event=>console.error('Browser exception:',JSON.stringify(event.exceptionDetails)));
  // A network-disabled Linux container has no default route, although these
  // owned loopback HTTP/WebSocket peers are reachable. Model that fixture
  // connectivity without changing production offline guards.
  if(process.platform==='linux')await client.send('Page.addScriptToEvaluateOnNewDocument',{source:"Object.defineProperty(navigator,'onLine',{configurable:true,get:()=>true});"});
  await client.send('Page.navigate',{url:origin+'/index.php?page=config'});
  await waitFor(()=>initialRead,'held initial host read');
  await waitFor(()=>run("!!window.SetupTargets && !!window.WtpUi?.catalogReady && !PicoFirmwareSafety.locked"),'selector available before native read');
  assert.equal(await run('SetupTargets.localWritable'),false,'Unloaded native fields cannot save');
  await run(`document.getElementById('setup-device').value='known:${profiles.a}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);
  assert.equal(await run('SetupTargets.retainingLocalDraft'),false,'Unloaded defaults cannot become a retained native draft');
  releaseInitial();await waitFor(()=>run("document.getElementById('callsign').value==='AA0NT' && PicoTargetEditors.state?.connected"),'independent initial host and Pico reads',30000);
  await waitFor(()=>run("PicoTargetEditors.state?.pending===0 && document.getElementById('wtp-management').getAttribute('aria-busy')==='false'"),'initial inspection finished');
  assert.match(await run("document.getElementById('wtp-management-connection-feedback').textContent"),/Engineering HTTPS authenticated/);
  await run("document.getElementById('setup-device').value='local';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));");
  await waitFor(()=>run("SetupTargets.localWritable && !SetupTargets.retainingLocalDraft"),'loaded native context');
  await waitFor(()=>run("!hasUnsavedLocalConfigChanges()"),'settled native baseline',15000);
  const original=JSON.stringify(await localConfiguration()),beforeA=await dumpA(),beforeB=await dumpB();
  const writes=()=>requests.filter(r=>!['GET','HEAD'].includes(r.method)&&['/config','/api/control/v1/host/config','/control/stop'].includes(r.path)).length;
  const baseline=writes();
  await run(`document.getElementById('callsign').value='K1XYZ';document.getElementById('callsign').dispatchEvent(new Event('input',{bubbles:true}));document.getElementById('setup-device').focus();document.getElementById('setup-device').value='known:${profiles.a}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);
  await waitFor(()=>run("PicoTargetEditors.state?.connected && !document.getElementById('wtp-remote-load').disabled"),'authenticated Pico A',30000);
  assert.equal(await run('document.activeElement.id'),'setup-device');
  assert.equal(await run('SetupTargets.retainingLocalDraft'),true);
  await run("populateConfig();scheduleAutosave();flushAutosave();patchTransmitControl(false);stopTransmission();");
  await new Promise(r=>setTimeout(r,1800));assert.equal(writes(),baseline,'No remote-context host autosave or stop');assert.equal(JSON.stringify(await localConfiguration()),original);assert.equal(await run("document.getElementById('callsign').value"),'K1XYZ','Delayed host read preserves native draft');
  await run("document.getElementById('wtp-combined-settings').open=true;document.getElementById('wtp-remote-load').click()");await waitFor(()=>run("!document.getElementById('wtp-remote-save').disabled"),'Pico snapshot');
  await run("document.getElementById('wtp-remote-callsign').value='K2XYZ';document.getElementById('wtp-remote-callsign').dispatchEvent(new Event('input',{bubbles:true}));document.getElementById('setup-device').value='local';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));");
  assert.equal(await run("document.getElementById('callsign').value"),'K1XYZ');assert.equal(await run("document.getElementById('setup-local-draft').hidden"),false);await new Promise(r=>setTimeout(r,1000));assert.equal(writes(),baseline,'Returning does not silently save');
  async function capture(scene){for(const theme of ['light','dark'])for(const width of [1440,390]){await client.send('Emulation.setDeviceMetricsOverride',{width,height:1050,deviceScaleFactor:1,mobile:width===390});await run(`applyThemePreference('${theme}');window.scrollTo(0,0)`);await client.settleRendering();assert(await run("['pico-native-status-read','pico-native-stop-read'].every(id=>{const b=document.getElementById(id);return b.textContent.trim()&&(!b.disabled||getComputedStyle(b).color===getComputedStyle(document.body).color)})"),'Disabled native status labels retain the theme foreground');assert(await run('document.documentElement.scrollWidth<=innerWidth'),'No horizontal page overflow');for(const tab of ['radio-tab','transmitter-hardware-tab','pi-hardware-tab','fleet-tab'])assert(await run(`(()=>{const r=document.getElementById('${tab}').getBoundingClientRect();return r.left>=0&&r.right<=innerWidth})()`),'Every tab visible');const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`${scene}-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));if(await run('SetupTargets.managingPico')){await run("document.querySelector('#wtp-management .tab-pane.active')?.scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-100)");await client.settleRendering();const controls=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`${scene}-controls-${theme}-${width}.png`),Buffer.from(controls.data,'base64'));}}}
  await capture('local-retained');
  await run("document.getElementById('setup-local-resume').click()");await waitFor(async()=>(await localConfiguration()).WSPR['Call Sign']==='K1XYZ','explicit local save',20000);await waitFor(()=>run('!hasUnsavedLocalConfigChanges()'),'saved local baseline',15000);assert(!await run("document.getElementById('configSaveStatusHint').textContent.includes('paused')"),'Resumed autosave must clear its paused hint');assert.equal(writes(),baseline+1);assert.deepEqual(await dumpA(),beforeA);assert.deepEqual(await dumpB(),beforeB);
  for(const [tab,scene]of [['radio-tab','local-signal'],['transmitter-hardware-tab','local-transmitter'],['pi-hardware-tab','local-device-io']]){await run(`bootstrap.Tab.getOrCreateInstance(document.getElementById('${tab}')).show()`);await capture(scene);}
  await run(`document.getElementById('setup-device').value='known:${profiles.a}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);await waitFor(()=>run("PicoTargetEditors.state?.connected && document.getElementById('wtp-remote-callsign').value==='K2XYZ'"),'retained Pico draft');
  const localSaved=JSON.stringify(await localConfiguration());await run("document.getElementById('wtp-remote-load').click()");await waitFor(()=>run("!document.getElementById('wtp-remote-save').disabled"),'fresh Pico revision');await run("document.getElementById('wtp-remote-save').click()");await waitFor(async()=>(await dumpA()).config.station.callsign==='K2XYZ','owning Pico save',30000);assert.equal(JSON.stringify(await localConfiguration()),localSaved);assert.deepEqual(await dumpB(),beforeB);assert.equal(writes(),baseline+1);
  for(const [tab,scene,pane]of [['radio-tab','signal','setup-pico-signal'],['transmitter-hardware-tab','transmitter','setup-pico-transmitter'],['pi-hardware-tab','device-io','setup-pico-io']]){await run(`bootstrap.Tab.getOrCreateInstance(document.getElementById('${tab}')).show()`);await capture(scene);assert.equal(await run(`Array.from(document.querySelectorAll('#wtp-management [role=tabpanel]')).filter(p=>getComputedStyle(p).display!=='none').map(p=>p.id).join(',')`),pane,'Only the selected Pico pane occupies layout space');assert.equal(await run(`document.getElementById('${tab}').getAttribute('aria-controls')`),pane);assert.equal(await run(`document.getElementById('${pane}').getAttribute('aria-labelledby')`),tab);}
  await activeA();const active=await jobA();assert.match(active,/^JOB 2 d{32} e{32} /,'Actual Pico service has an armed software job');
  for(const id of ['b','a']){await run(`document.getElementById('setup-device').value='known:${profiles[id]}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);await waitFor(()=>run(`PicoTargetEditors.state?.device==='${id.repeat(32)}' && PicoTargetEditors.state?.connected`),'active-work selection');await run("document.getElementById('wtp-remote-load').click()");await waitFor(()=>run("document.getElementById('wtp-management').getAttribute('aria-busy')==='false'"),'active-work settings read');}
  assert.equal(await jobA(),active,'Selection and settings reads preserve armed job, owner, and engine counters');await endA();
  // Restart only the volatile software peer with a new real boot identity.
  // This checks client fencing, not persistence across a physical reboot.
  await run("document.getElementById('wtp-remote-callsign').value='K8ABC';document.getElementById('wtp-remote-callsign').dispatchEvent(new Event('input',{bubbles:true}));");
  const beforeRebootWrites=requests.filter(item=>item.method!=='GET'&&item.path.endsWith('/pico/configuration')).length;await rebootA();await run("document.getElementById('wtp-remote-load').click()");
  await waitFor(()=>run("PicoTargetEditors.state?.paused && document.getElementById('wtp-management').inert"),'actual changed-boot editor hold',30000);
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K8ABC');await capture('reboot-readonly');
  await run("document.getElementById('setup-device-reconnect').click()");await waitFor(()=>run("PicoTargetEditors.state?.connected && !PicoTargetEditors.state?.paused"),'explicit changed-boot reconnect',30000);
  await run("document.getElementById('wtp-remote-load').click()");await waitFor(()=>run("!document.getElementById('wtp-remote-save').disabled"),'new-boot read');
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K8ABC','Same-profile drafts survive a verified reboot without silent loss');
  assert.equal(requests.filter(item=>item.method!=='GET'&&item.path.endsWith('/pico/configuration')).length,beforeRebootWrites,'Boot refusal, reconnect, and readback never resubmit settings');
  assert.notEqual((await dumpA()).config.station.callsign,'K8ABC');
  await run("document.getElementById('wtp-remote-save').click()");await waitFor(async()=>(await dumpA()).config.station.callsign==='K8ABC','explicit reviewed save with fresh boot binding',30000);
  assert.equal(JSON.stringify(await localConfiguration()),localSaved);
  await run("document.getElementById('setup-device').value='local';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));bootstrap.Tab.getOrCreateInstance(document.getElementById('radio-tab')).show();document.getElementById('qrss_mode').click();");
  await waitFor(async()=>(await localConfiguration()).Operation.Mode==='QRSS','explicit local disabled-mode save',20000);
  await capture('local-cw');assert.equal((await call('/config')).body.Operation.Transmit,false);
  await run("document.getElementById('setup-device').focus()");await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});assert(await run('document.activeElement!==document.body'),'Keyboard focus remains visible in document');
  await run(`document.getElementById('setup-device').value='known:${profiles.b}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);
  await waitFor(()=>run("PicoTargetEditors.state?.device==='"+'b'.repeat(32)+"' && PicoTargetEditors.state?.connected"),'other Pico before maintenance hold');
  await run("PicoFirmwareSafety.begin();bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show()");
  assert.equal(await run("document.getElementById('setup-device').disabled && document.getElementById('wsprform').inert"),true);
  assert.equal(await run("document.getElementById('wtp-firmware-read')"),null,'Retired backend firmware actions are absent');assert.equal(await run("document.getElementById('fleet-refresh').closest('[inert]')"),null,'Read-only Fleet refresh remains available during maintenance');
  console.log('PASS: actual native Pi plus two authenticated Pico services; retained native/Pico drafts, fenced delayed host read, zero cross-target writes, explicit native resume, own-target save, long names, all tabs, preserved armed software job/owner, real changed-boot refusal/reconnect, desktop/mobile and themes');
 }catch(error){failure=error;if(client)try{console.error(await client.evaluate("({local:SetupTargets?.localWritable,retained:SetupTargets?.retainingLocalDraft,feedback:document.getElementById('configSaveStatus')?.textContent,pico:document.getElementById('pico-target-feedback')?.textContent,callsign:document.getElementById('callsign')?.value,dirty:configAutosaveDirty,inflight:configAutosaveInFlight,pending:configAutosavePendingAfterFlight,baseline:lastSavedConfigPayload,current:currentConfigPayloadSnapshot()})"));}catch(_){}console.error(require('./browser_fixture_process.js').childDiagnostic(application),nativeLog,lastReadiness);}
 finally{releaseInitial?.();for(const socket of sockets)socket.destroy();const outcomes=await Promise.allSettled([finishFixture({failure,browser:chrome,client,php,profile}),proxy?closeServer(proxy):Promise.resolve(),application?(async()=>{if(web){await stopChild(web);assert.equal(web.exitCode,0,'Separate browser fixture must exit cleanly');}await stopChild(application,10000);assert.equal(application.exitCode,0,'Native software fixture must exit without any output transport write: '+nativeLog);})():Promise.resolve()]);fs.rmSync(directory,{recursive:true,force:true});const rejected=outcomes.find(r=>r.status==='rejected');if(rejected)throw rejected.reason;}
};

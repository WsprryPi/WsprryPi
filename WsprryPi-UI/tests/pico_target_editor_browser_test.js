// SPDX-License-Identifier: MIT
// Called by the actual two-Pico HTTP pair with --browser. No mocked API or RF.
'use strict';
const os=require('node:os'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),net=require('node:net');
const {waitForPhp,createBrowserProfile,freePort,waitFor,launchBrowser,finishFixture,spawnFixtureChild,waitForSpawn,closeServer}=require('./browser_harness.js');
module.exports=async function exercise({backendOrigin,socketPort,profiles,failA,repairA,dumpA}) {
 const ui=path.resolve(__dirname,'..'),backend=new URL(backendOrigin),phpPort=await freePort(),sockets=new Set(),requests=[];
 let php,proxy,chrome,client,profile,failure,delay=false,release,held,dropStop=false;
 const output=process.env.WSPRRYPI_EDITOR_SCREENSHOTS||path.join(os.tmpdir(),'issue473-target-editor-ui');fs.mkdirSync(output,{recursive:true});
 try{
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);
  await waitForPhp(php,`http://127.0.0.1:${phpPort}/index.php?page=config`,'target editor PHP');
  proxy=http.createServer((request,response)=>{
   const pathname=new URL(request.url,'http://fixture').pathname;
   const production=['/config','/version'].includes(pathname)||pathname.startsWith('/api/control/v1/');
   requests.push({path:pathname,method:request.method});
   const outgoing=http.request({hostname:'127.0.0.1',port:production?backend.port:phpPort,path:request.url,method:request.method,headers:request.headers},upstream=>{
    if(dropStop&&request.method==='POST'&&pathname.endsWith('/pico/schedules/stop')){
     // Commit through the actual producer, then withhold its receipt. An
     // explicit failed response keeps delivery uncertainty deterministic across
     // browser TCP retry policies without replaying the production mutation.
     assert.equal(upstream.statusCode,200,'The native stop must commit before its receipt is withheld');
     dropStop=false;upstream.resume();upstream.once('end',()=>{response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:'Wsprry-Control/1',schema:'target-error',error:{code:'storage_unavailable',retryable:false}}));});
    }else if(delay&&request.method==='POST'&&pathname===`/api/control/v1/targets/${'a'.repeat(32)}/pico/configuration`){
     const parts=[];upstream.on('data',part=>parts.push(part));upstream.on('end',()=>{held=true;release=()=>{if(response.headersSent||response.destroyed)return;response.writeHead(upstream.statusCode,upstream.headers);response.end(Buffer.concat(parts));};});
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
  profile=createBrowserProfile();
  ({browser:chrome,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000,expectedDialogs:[
   'Review the current saved values beside your preserved draft. Matching values do not prove an earlier save committed. Send one new explicit save with this fresh revision?',
   ...['A temporarily renamed','Pico a'].map(name=>`Save ${name} as a network manual device? Saving does not enable a schedule.`)]}));
  const run=expression=>client.evaluate(expression);
  const navigate=async()=>{await client.send('Page.navigate',{url:origin+'/index.php?page=config'});await waitFor(()=>run("!!window.ControllerMode?.ready && !!window.PicoTargetEditors && !!window.WtpManagementEditor && !document.getElementById('fleet-device').disabled"),'target editor ready',20000);};
  const manage=async id=>{
   await run(`document.getElementById('setup-device').focus();document.getElementById('setup-device').value='known:${profiles[id]}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);
   await waitFor(()=>run(`document.getElementById('pico-target-identity').textContent.includes('${id.repeat(32)}') && document.getElementById('wtp-remote-load') && !document.getElementById('wtp-remote-load').disabled && document.getElementById('wtp-management').getAttribute('aria-busy')==='false'`),'bound '+id+' editor',30000);
   assert.equal(await run("document.activeElement.id"),'setup-device','Device selection preserves keyboard focus');
  };
  const load=async()=>{await run("document.getElementById('scheduling-tab').click();document.getElementById('wtp-combined-settings').open=true;document.getElementById('wtp-remote-load').click()");await waitFor(()=>run("!document.getElementById('wtp-remote-save').disabled && document.getElementById('wtp-management').getAttribute('aria-busy')==='false'"),'saved target settings',30000);};
  const draft=async value=>run(`document.getElementById('wtp-remote-callsign').value='${value}';document.getElementById('wtp-remote-callsign').dispatchEvent(new Event('input',{bubbles:true}));`);
  await navigate();await manage('a');await load();await draft('K1ABC');
  assert.equal(await run("document.getElementById('setup-device').closest('form,[role=tablist]')"),null);
  assert.equal(await run("document.getElementById('wtp-remote-callsign').closest('form')"),null);
  assert.equal(await run("document.getElementById('wtp-remote-callsign').closest('[role=tabpanel]').id"),'setup-pico-scheduling');
  assert.equal(await run("document.getElementById('wtp-member-rf').closest('[role=tabpanel]').id"),'setup-pico-io');
  assert.equal(await run("document.getElementById('gpio-drive-select').closest('[role=tabpanel]').id"),'setup-pico-transmitter');
  assert.equal(await run("document.getElementById('pi-hardware-tab').textContent.trim()"),'Device I/O');
  assert.equal(await run("document.getElementById('fleet-pane').contains(document.getElementById('wtp-management'))"),false);
  assert.equal(await run("document.querySelectorAll('#configTabs [role=tab]').length"),5,'Scheduling joins the existing Setup tabs');

  assert.equal(await run("document.getElementById('setup-pico-scheduling').textContent.includes('Keyed-message')"),true);
  assert.equal(await run("document.getElementById('wtp-gray-line-controls').closest('[role=tabpanel]').id"),'setup-pico-scheduling');
  for(const theme of ['light','dark'])for(const width of [1440,390]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1100,deviceScaleFactor:1,mobile:width<768});
   await run(`applyThemePreference('${theme}');document.getElementById('setup-pico-scheduling').scrollIntoView({behavior:'instant'});window.scrollBy(0,-120)`);await client.settleRendering();
   assert(await run('document.documentElement.scrollWidth<=innerWidth'),'Pico scheduling fits viewport');const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`scheduling-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await run("document.getElementById('pico-native-status-read').click()");
  await waitFor(()=>run("document.getElementById('pico-native-status').textContent.includes('unowned')"),'authenticated native status',30000);
  const stopPosts=()=>requests.filter(item=>item.method==='POST'&&item.path.endsWith('/pico/schedules/stop')).length;
  const stopsBefore=stopPosts();
  await run("document.getElementById('pico-native-stop').focus();document.getElementById('pico-native-stop').click()");
  await waitFor(()=>run("document.getElementById('pico-native-stop-status').textContent.includes('confirmed inactive and unowned')"),'confirmed native stop',30000);
  assert.equal(stopPosts(),stopsBefore+1);assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC','Stop preserves the unsaved settings draft');
  dropStop=true;
  await run("document.getElementById('pico-native-stop').click()");
  await waitFor(()=>run("document.getElementById('pico-native-stop-status').textContent.includes('unconfirmed') && document.getElementById('pico-native-stop').disabled"),'lost stop response hold',30000);
  const unknownPosts=stopPosts();assert.equal(unknownPosts,stopsBefore+2);
  assert.match(await run("document.getElementById('pico-native-status').textContent"),/unknown/);
  await run("document.getElementById('pico-native-status-read').click()");
  await waitFor(()=>run("!document.getElementById('pico-native-status-read').disabled && document.getElementById('pico-native-status').textContent.includes('unknown')"),'ordinary status preserves stop hold',30000);
  for(const theme of ['light','dark'])for(const width of [1440,390]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width===390});
   await run(`applyThemePreference('${theme}');document.getElementById('pico-native-stop-heading').scrollIntoView({block:'center',behavior:'instant'})`);await client.settleRendering();
   assert(await run('document.documentElement.scrollWidth<=innerWidth'),'Native safety controls fit viewport');
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`native-stop-unknown-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
  }
  await run("document.getElementById('pico-native-stop-read').click()");
  await waitFor(()=>run("!document.getElementById('pico-native-stop').disabled && document.getElementById('pico-native-stop-status').textContent.includes('confirmed inactive and unowned')"),'explicit stop readback',30000);
  assert.equal(stopPosts(),unknownPosts,'Readback cannot replay stop');assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC');
  assert.equal(await run("WtpUi.selected()"),false);
  assert.equal(await run("document.getElementById('wtp_use')"),null);
  await manage('b');await load();assert.notEqual(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC');await draft('K2ABC');
  await manage('a');assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC');await load();
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K1ABC','Fresh read preserves A draft');
  delay=true;held=false;await run("document.getElementById('wtp-remote-save').click()");await waitFor(()=>held,'accepted A reply held',30000);
  await manage('b');await load();assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K2ABC');
  await run(`document.getElementById('setup-device').value='known:${profiles.a}';document.getElementById('setup-device').dispatchEvent(new Event('change',{bubbles:true}));`);
  assert.equal(await run("document.getElementById('wtp-management')"),null,'A pending target cannot display the previously selected B editor');
  assert.equal(await run("document.getElementById('pico-target-identity').textContent.includes('"+'a'.repeat(32)+"')"),true);
  await manage('b');await load();assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K2ABC');
  delay=false;release();await waitFor(()=>run("PicoTargetEditors.operations().some(item=>item.device==='"+'a'.repeat(32)+"'&&item.state==='committed')"),'old-target receipt tracked');
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K2ABC','Late A reply must not replace B draft');
  await manage('a');await load();await draft('K3ABC');await failA();await run("document.getElementById('wtp-remote-save').click()");
  await waitFor(()=>run("!document.getElementById('pico-target-recovery').hidden && document.getElementById('wtp-management').inert"),'uncertain A save',30000);await repairA();
  // A new browser context must discover the durable hold on a new explicit save.
  await navigate();await manage('a');await load();await draft('K4ABC');const before=await dumpA();
  await run("document.getElementById('wtp-remote-save').click()");await waitFor(()=>run("!document.getElementById('pico-target-recovery').hidden"),'fresh-browser earlier hold',30000);
  assert.deepEqual(await dumpA(),before,'A held new save must not change the Pico');
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K4ABC');
  await run("document.getElementById('pico-target-read-recovery').click()");await waitFor(()=>run("!document.getElementById('pico-target-review-recovery').disabled"),'authoritative recovery read',30000);
  assert(!await run("document.getElementById('pico-target-recovery-observation').textContent.includes('capabilities')"),'Recovery presents settings rather than protocol descriptors');
  for(const theme of ['light','dark'])for(const width of [1440,390]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width===390});await client.settleRendering();
   await run(`applyThemePreference('${theme}');document.getElementById('pico-target-heading').scrollIntoView({block:'center',behavior:'instant'});`);await client.settleRendering();
   assert(await run('document.documentElement.scrollWidth<=innerWidth'),'Recovery must fit its viewport');
   const image=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`recovery-${theme}-${width}.png`),Buffer.from(image.data,'base64'));
  }
  await run("document.getElementById('pico-target-review-recovery').click()");await waitFor(()=>run("document.getElementById('pico-target-recovery').hidden && !document.getElementById('wtp-management').inert"),'explicit review',30000);
  await load();assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K4ABC');
  await run("document.getElementById('wtp-remote-save').click()");await waitFor(async()=>(await dumpA()).config.station.callsign==='K4ABC','new reviewed save',30000);
  await waitFor(()=>run("document.getElementById('wtp-management').getAttribute('aria-busy')==='false'"),'reviewed save finished');
  await draft('K5ABC');
  await run("document.getElementById('wtp-remote-password').value='do-not-display-retained-password';document.getElementById('wtp-remote-password').dispatchEvent(new Event('input',{bubbles:true}))");
  for(const name of ['A temporarily renamed','Pico a']) {
   await run(`bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();document.getElementById('fleet-device').value='known:${profiles.a}';document.getElementById('fleet-device').dispatchEvent(new Event('change',{bubbles:true}));document.getElementById('fleet-edit').click();document.getElementById('fleet-name').value=${JSON.stringify(name)};document.getElementById('fleet-name').dispatchEvent(new Event('input',{bubbles:true}));document.getElementById('fleet-save').click();`);
   await waitFor(()=>run("document.getElementById('fleet-editor').hidden && !document.getElementById('fleet-manage').disabled && document.getElementById('wtp-management').inert"),'profile replacement retires editor',30000);
   assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K5ABC','Retired draft stays intact');
  }
  await manage('a');await load();
  assert.equal(await run("document.getElementById('wtp-remote-callsign').value"),'K4ABC','Profile ABA cannot restore an older generation draft');
  const mutationsBeforeReview=requests.filter(item=>item.method!=='GET').length;
  await run(`document.getElementById('pico-target-retained').open=true;document.querySelector('[data-retained-device="${'a'.repeat(32)}"]').open=true;`);
  await waitFor(()=>run("document.getElementById('pico-target-retained-list').textContent.includes('K5ABC')"),'inspect old-generation draft');
  assert.equal(await run("[...document.querySelectorAll('#pico-target-retained-list dt')].some(n=>n.textContent==='Schedule location')"),true,'Retained labels exclude selector option contents');
  assert(!await run("document.getElementById('pico-target-retained-list').textContent.includes('do-not-display-retained-password')"),'Read-only draft inspection must not reveal passwords');
  assert.equal(requests.filter(item=>item.method!=='GET').length,mutationsBeforeReview,'Viewing an old draft cannot connect or save');
  for(const theme of ['light','dark'])for(const width of [1440,390]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width===390});
   await run(`applyThemePreference('${theme}');document.querySelector('[data-retained-device="${'a'.repeat(32)}"] summary').scrollIntoView({block:'center',behavior:'instant'})`);await client.settleRendering();
   assert(await run('document.documentElement.scrollWidth<=innerWidth'),'Retained drafts fit their viewport');
   const image=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`retained-${theme}-${width}.png`),Buffer.from(image.data,'base64'));
  }
  assert(!requests.some(item=>item.path==='/api/control/v1/host/devices/use'||['/api/control/v1/host/config','/config','/control/stop'].includes(item.path)&&item.method!=='GET'),'Managing must not select/save a host backend');
  assert(!requests.some(item=>item.path.startsWith('/api/control/v1/member/')&&item.path!=='/api/control/v1/member/firmware'),'Production UI must not call retired selected-member settings');
  console.log('PASS: real browser and two actual Pico services, no output backend, per-target drafts, accepted late reply isolation, fresh-browser durable recovery, explicit review, profile ABA fencing, and responsive themes');
 }catch(error){failure=error;if(client)try{console.error(await client.evaluate("({message:document.getElementById('pico-target-feedback')?.textContent,recovery:document.getElementById('pico-target-recovery-state')?.textContent,connection:document.getElementById('wtp-management-connection-feedback')?.textContent,stop:document.getElementById('pico-native-stop-status')?.textContent,stopDisabled:document.getElementById('pico-native-stop')?.disabled})"));}catch(_){} }
 finally{release?.();for(const socket of sockets)socket.destroy();const outcomes=await Promise.allSettled([finishFixture({failure,browser:chrome,client,php,profile}),proxy?closeServer(proxy):Promise.resolve()]);const rejected=outcomes.find(item=>item.status==='rejected');if(rejected)throw rejected.reason;}
};

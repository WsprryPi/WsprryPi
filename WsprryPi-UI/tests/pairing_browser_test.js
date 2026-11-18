// SPDX-License-Identifier: MIT
// Synthetic presentation/interaction fixture only. PAKE/TLS, durable owners
// and actual carrier qualification use the independent production-pair suites.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {freePort,spawnFixtureChild,waitForSpawn,waitForPhp,createBrowserProfile,launchBrowser,waitFor,finishFixture,closeServer}=require('./browser_harness.js');
const control=require('../data/control-contract.js');
(async()=>{
 const ui=path.resolve(__dirname,'..'),phpPort=await freePort(),contract=control.contract,id=c=>c.repeat(32);
 const digest=fs.readFileSync(path.join(ui,'data/control-contract.js'),'utf8').match(/const controlSchemaDigest="([a-f0-9]{64})"/)[1];
 const output=path.resolve(process.env.WSPRRYPI_PAIRING_SCREENSHOTS||path.join(ui,'../build/phase62-pairing-ui'));fs.mkdirSync(output,{recursive:true});
 let php,proxy,browser,client,profile,failure,confirm=false,unavailable=false,receipt=null,windowSequence=0,accepted=0;
 const writes=[],state={contract,schema:'pairing-state',device_id:id('a'),boot_id:id('c'),authorization_generation:'0',state:'unpaired',manager:null,window:null};
 try{
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await waitForSpawn(php);
  await waitForPhp(php,`http://127.0.0.1:${phpPort}/index.php?page=maintenance`,'pairing PHP templates');
  proxy=http.createServer(async(request,response)=>{
   const url=new URL(request.url,'http://fixture'),route=url.pathname;
   const reply=(value,status=200)=>{response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store','X-WsprryPi-CSRF':'f'.repeat(64)});response.end(JSON.stringify(value));};
   if(route.startsWith('/api/control/v1/host/pairing')||route.endsWith('/host/management-session')){
    if(unavailable){reply({contract,schema:'target-error',error:{code:'unauthorized',retryable:false}},403);return;}
    if(request.method==='GET'){
     if(route.endsWith('/discovery'))reply({contract,schema:'pair-discovery',available:true,discoveries:[{discovery_id:id('d'),device_id:id('b'),boot_id:id('c'),name:'Bench Pi',host:'wsprry-management-'+id('b')+'.local.',bootstrap_port:8080,management_port:31418,suite:'SPAKE2+-P256-SHA256-HKDF-HMAC',control_schema_digest:digest}]});
     else if(route.includes('/operations/')){
      if(!receipt){reply({contract,schema:'target-error',error:{code:'operation_unknown',retryable:false}},404);return;}
      if(confirm){receipt.state='committed';receipt.revision='e'.repeat(64);}reply(receipt);
     }else reply(state);
     return;
    }
    let text='';for await(const chunk of request){text+=chunk;if(text.length>4096){response.destroy();return;}}
    const value=JSON.parse(text);writes.push({route,operation:value.operation_id});
    if(route.endsWith('/management-session'))reply({contract,schema:'controller-browser-session'});
    else if(route.endsWith('/window')){
     state.window={window_id:(++windowSequence).toString(16).padStart(32,'0'),expires_in_ms:120000};
     reply({contract,schema:'pair-window',...state.window,device_id:state.device_id,boot_id:state.boot_id,authorization_generation:state.authorization_generation,code:'0123-ABCD'});
    }else if(route.endsWith('/connect')){
     assert.equal(value.code,'0123ABCD');
     if(receipt?.operation_id===value.operation_id){reply({contract,schema:'target-error',error:{code:'request_id_reuse',retryable:false}},409);return;}
     ++accepted;receipt={contract,schema:'target-operation',binding:{device_id:id('b'),boot_id:id('c'),profile_generation:'1',connection_generation:'1'},operation_id:value.operation_id,request_digest:'e'.repeat(64),state:'pending',revision:null,error:null};
     response.destroy();
    }else if(route.endsWith('/remove')){
     state.state='unpaired';state.manager=null;state.window=null;state.authorization_generation='2';
     reply({contract,schema:'target-operation',binding:{device_id:id('a'),boot_id:id('c'),profile_generation:'1',connection_generation:'1'},operation_id:value.operation_id,request_digest:'e'.repeat(64),state:'committed',revision:'e'.repeat(64),error:null});
    }else reply({contract,schema:'target-error',error:{code:'unsupported_resource',retryable:false}},404);
    return;
   }
   const upstream=http.request({hostname:'127.0.0.1',port:phpPort,path:request.url,method:request.method,headers:request.headers},incoming=>{response.writeHead(incoming.statusCode,incoming.headers);incoming.pipe(response);});
   upstream.on('error',()=>{response.writeHead(503);response.end();});request.pipe(upstream);
  });
  await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${proxy.address().port}`;
  profile=createBrowserProfile();({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),args:['--headless','--no-sandbox','--disable-gpu','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
  const run=code=>client.evaluate(code),navigate=async()=>{await client.send('Page.navigate',{url:origin+'/index.php?page=maintenance'});await waitFor(()=>run("document.getElementById('pairingState')?.textContent.includes('No controller') && document.getElementById('pairingDevice')?.options.length===2 && !document.getElementById('pairingWindow').disabled"),'pairing controls ready',15000);};
  const capture=async scene=>{
   for(const theme of ['light','dark'])for(const width of [1440,390,320]){
    await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<768});
    await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');window.scrollTo(0,document.getElementById('controllerAccess').getBoundingClientRect().top+scrollY-(document.querySelector('.navbar')?.getBoundingClientRect().height||0)-16)`);await client.settleRendering();
    const metrics=await run("({page:document.documentElement.scrollWidth>innerWidth+1,pane:document.getElementById('controllerAccess').scrollWidth>document.getElementById('controllerAccess').clientWidth+1,fields:[...document.querySelectorAll('#controllerAccess input')].map(node=>node.type)})");
    assert.equal(metrics.page,false,scene+' page overflow '+width);assert.equal(metrics.pane,false,scene+' pane overflow '+width);assert(!metrics.fields.includes('file'));
    const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`${scene}-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));
   }
  };
  await navigate();await run("document.getElementById('pairingWindow').click()");await waitFor(()=>run("document.getElementById('pairingCode').textContent==='0123-ABCD'"),'originating window code');await capture('window');
  await run("document.getElementById('pairingDevice').value='"+id('d')+"';document.getElementById('pairingDevice').dispatchEvent(new Event('change'));document.getElementById('pairingInput').value='0123-ABCD';document.getElementById('pairingConnect').click()");
  await waitFor(()=>run("!document.getElementById('pairingPending').hidden && document.getElementById('pairingInput').value==='' && document.getElementById('controllerAccess').getAttribute('aria-busy')==='false'"),'lost pairing reply');
  assert.equal(await run("JSON.stringify(localStorage).includes('0123')"),false,'No code in browser storage');
  await navigate();assert.equal(await run("document.getElementById('pairingCode').textContent"),'','Reload cannot recover a code');await capture('pending');
  confirm=true;await waitFor(()=>run("document.getElementById('pairingConnectFeedback').textContent.includes('confirmed') && document.getElementById('pairingPending').hidden"),'fresh-browser saved result',10000);
  assert.equal(new Set(writes.filter(item=>item.route.endsWith('/connect')).map(item=>item.operation)).size,1,'Browser recovery cannot create a new pairing operation');assert.equal(accepted,1,'A transparent HTTP retry cannot repeat an accepted effect');
  state.state='paired';state.authorization_generation='1';state.manager={device_id:id('e'),name:'Controller '+id('e')};state.window=null;
  await run("document.getElementById('pairingRefresh').click()");await waitFor(()=>run("document.getElementById('pairingWindow').textContent==='Replace controller' && !document.getElementById('pairingWindow').disabled"),'paired owner state');
  await run("document.getElementById('pairingWindow').click()");await waitFor(()=>run("document.getElementById('pairingCode').textContent==='0123-ABCD'"),'replacement code');
  await new Promise(resolve=>setTimeout(resolve,3500));assert.equal(await run("document.getElementById('pairingCodePanel').hidden"),false,'Polling must retain an initiating replacement code');
  await run("document.getElementById('pairingRemoveReview').click()");assert.equal(await run("document.activeElement.id"),'pairingRemove');await capture('remove');
  await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  assert.equal(await run("document.activeElement.id"),'pairingRemoveCancel');
  await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:' ',code:'Space',windowsVirtualKeyCode:32});
  await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:' ',code:'Space',windowsVirtualKeyCode:32});
  assert.equal(await run("document.activeElement.id"),'pairingRemoveReview');
  await run("document.getElementById('pairingRemoveReview').click();document.getElementById('pairingRemove').click()");await waitFor(()=>run("document.getElementById('pairingLocalFeedback').textContent.includes('removed') && document.getElementById('pairingRemoveReview').hidden"),'removed access');
  unavailable=true;await run("document.getElementById('pairingRefresh').click()");await waitFor(()=>run("document.getElementById('pairingWindow').disabled && document.getElementById('pairingConnect').disabled"),'unavailable controls');await capture('unavailable');
  console.log('PASS synthetic pairing UI: originating code, replacement polling, lost-reply reload without replay, explicit removal, keyboard focus and 24 desktop/mobile/theme renders: '+output);
 }catch(error){
  if(client)try{console.error('Pairing UI diagnostic:',await client.evaluate("({state:document.getElementById('pairingState')?.textContent,feedback:document.getElementById('pairingLocalFeedback')?.textContent,disabled:document.getElementById('pairingWindow')?.disabled,hasCode:!!document.getElementById('pairingCode')?.textContent,firmwareLocked:window.PicoFirmwareSafety?.locked})"),writes);}catch(_){}
  failure=error;
 }finally{await finishFixture({failure,client,browser,php,profile,cleanups:[async()=>{if(proxy)await closeServer(proxy);} ]});}
})().catch(error=>{console.error(error);process.exitCode=1;});

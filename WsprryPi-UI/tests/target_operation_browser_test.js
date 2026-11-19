// SPDX-License-Identifier: MIT
// Shipped clients/templates over synthetic HTTP. No physical or paired-TLS evidence.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),os=require('node:os');
const h=require('./browser_harness.js'),control=require('../data/control-contract.js');
const vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=name=>structuredClone(vectors.find(item=>item.name===name).value);
(async()=>{
 const ui=path.resolve(__dirname,'..'),port=await h.freePort(),pi='a'.repeat(32),pico='c'.repeat(32),host='f'.repeat(32),boot='b'.repeat(32),contract=control.contract;
 const context=sample('paired Pi context'),settings=sample('Pi closed target-settings');
 context.target.name='Workshop Pi with a long descriptive name';context.capabilities=[['pi-config','next_occurrence'],['pi-scheduling','next_occurrence'],['stop','immediate']].map(([resource,activation])=>({...control.controllerPiDescriptors[resource],resource,activation,read:resource!=='stop',write:true,maximum_bytes:32768}));
 settings.binding=structuredClone(context.binding);settings.capability_revision=context.capability_revision;settings.revision=context.configuration_revision;settings.saved.configuration.Operation.Transmit=false;settings.saved.scheduling.enabled=false;
 assert(control.validControllerDocument(context,'target-context'));
 const picoTarget={device_id:pico,product:'WsprryPico',name:'Paired USB Pico',profile_generation:'1',connection_generation:'2'};
 let noSettings=false,advertised=true,lost=false,picoLost=false,stopped=false,picoBoot=boot,inventoryBad=false,delayed=false,releaseStatus;
 const writes=[],receipts=new Map();let php,proxy,browser,client,profile,failure;
 try{
  php=h.spawnFixtureChild('php',['-S',`127.0.0.1:${port}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});await h.waitForSpawn(php);await h.waitForPhp(php,`http://127.0.0.1:${port}/index.php?page=operation`);
  proxy=http.createServer(async(request,response)=>{
   try{
    const route=new URL(request.url,'http://fixture').pathname;
    const reply=(value,status=200,headers={})=>{response.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store',...headers});response.end(JSON.stringify(value));};
    if(route.endsWith('/targets')){reply(inventoryBad?{contract:'wrong',schema:'targets',targets:[]}:{contract,schema:'targets',targets:advertised?[context.target,picoTarget]:[picoTarget]});return;}
    if(route.endsWith('/host/readiness')){const value=sample('P209 native recovery readiness');Object.assign(value,{device_id:host,role:'controller-only',state:'ready',recovery_editable:false});reply(value);return;}
    if(route.endsWith('/host/devices')){reply({profiles:[]});return;}
    if(route.endsWith('/host/management-session')){reply({contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'f'.repeat(64)});return;}
    if(route.endsWith('/host/fleet')||route.endsWith('/host/fleet/operations')){reply({scope:'wsprrypi-wtp-fleet/1',maximum_outputs:32,controller_incarnation:host,publication_revision:'1',now_ms:'1000',replacement_pending:false,storage_error:'',assignments:[],outputs:{}});return;}
    if(route.startsWith('/api/control/v1/targets/'+pi+'/')){
     if(route.endsWith('/context')){const value=structuredClone(context);if(noSettings)for(const capability of value.capabilities){capability.read=false;capability.write=false;capability.activation='unsupported';}reply(value);return;}
     if(request.method==='GET'){
      if(route.endsWith('/status')){if(delayed)await new Promise(resolve=>{releaseStatus=resolve;});const value=sample('Pi closed conservative status');value.binding=structuredClone(context.binding);value.revision=settings.revision;value.schedule_enabled=settings.saved.scheduling.enabled;value.transmit_effective=settings.saved.configuration.Operation.Transmit;reply(value);return;}
      reply(route.includes('/operations/')?receipts.get(route.split('/').at(-1)):settings);return;
     }
     let raw='';for await(const chunk of request)raw+=chunk;const body=JSON.parse(raw);writes.push({route,body});assert.equal(body.binding.device_id,pi);
     const revision=(writes.length%10).toString().repeat(64);settings.revision=revision;context.configuration_revision=revision;
     if(route.endsWith('/enable'))settings.saved.scheduling.enabled=true;
     if(route.endsWith('/disable'))settings.saved.scheduling.enabled=false;
     if(route.endsWith('/stop'))settings.saved.configuration.Operation.Transmit=false;
     if(route.endsWith('/settings'))settings.saved=structuredClone(body.candidate);
     const receipt={contract,schema:'target-operation',binding:body.binding,operation_id:body.operation_id,request_digest:'d'.repeat(64),revision,state:'committed',error:null};receipts.set(receipt.operation_id,receipt);
     if(lost){reply({contract,schema:'target-error',error:{code:'transport_unavailable',retryable:true}},503);return;}reply(receipt);return;
    }
    if(route.startsWith('/api/control/v1/targets/'+pico+'/')){
     const binding={device_id:pico,boot_id:picoBoot,profile_generation:'1',connection_generation:'2'},headers={'X-WsprryPi-Target-Context':'e'.repeat(32)};
     if(route.endsWith('/context')){const value=sample('paired Pi context');value.target=structuredClone(picoTarget);value.binding=binding;value.capabilities=[];value.authorization={pair_id:'d'.repeat(32),authorization_generation:'2',renewal_required:false};value.gateway={parent:{device_id:pi,boot_id:boot,profile_generation:'1',connection_generation:'2'},child:binding,pair_id:'d'.repeat(32),authorization_generation:'2',resources:[]};reply(value,200,headers);return;}
     if(route.endsWith('/pico/management')){reply({contract,host:'',selection_id:'e'.repeat(32),carrier:'usb_physical',connected:true,session:null},200,headers);return;}
     if(route.endsWith('/pico/status')){reply({contract,job:{boot_id:picoBoot,state:stopped?'empty':'running',output_active:!stopped,owner_id:stopped?null:pico},standalone:{boot_id:picoBoot},network:{},transport:null},200,headers);return;}
     if(route.endsWith('/pico/schedules/stop')){
      if(request.method==='POST'){let raw='';for await(const chunk of request)raw+=chunk;const body=JSON.parse(raw);writes.push({route,body});assert.equal(body.target.device_id,pico);stopped=true;if(picoLost){reply({contract,schema:'target-error',error:{code:'transport_unavailable',retryable:true}},503,headers);return;}headers['X-WsprryPi-Operation-ID']=request.headers['x-wsprrypi-operation-id'];}
      reply({contract,schema:'schedule-stop',target:{scope:'member',device_id:pico,boot_id:picoBoot},message_enabled:!stopped,wspr_enabled:!stopped,output_active:!stopped,owned:!stopped,stopped},200,{...headers,ETag:'"pico-stop"'});return;
     }
    }
    const upstream=http.request({hostname:'127.0.0.1',port,path:request.url,method:request.method,headers:request.headers},other=>{response.writeHead(other.statusCode,other.headers);other.pipe(response);});upstream.on('error',()=>response.destroy());request.pipe(upstream);
   }catch(error){response.writeHead(500);response.end(JSON.stringify({error:String(error)}));}
  });await new Promise(resolve=>proxy.listen(0,'127.0.0.1',resolve));
  ({browser,client,profile}=await h.launchBrowser({args:['--headless=new','--disable-gpu','--no-first-run'],expectedDialogs:[
   'Enable saved scheduling on '+context.target.name+' ('+pi+')?', 'Pause scheduling on '+context.target.name+' ('+pi+')?', 'Enable transmission on '+context.target.name+' ('+pi+')?', 'Stop output on '+context.target.name+' ('+pi+')?', 'Stop output on '+picoTarget.name+' ('+pico+')?'
  ]}));
  const run=expression=>client.evaluate(expression),click=id=>run(`document.getElementById('operationTarget${id}').click()`);
  await client.send('Page.navigate',{url:`http://127.0.0.1:${proxy.address().port}/index.php?page=operation`});
  await h.waitFor(()=>run("!!window.TargetOperation&&document.getElementById('operationDevice').options.length===3"),'current paired devices without legacy assignments');
  const choose=async id=>{await run(`document.getElementById('operationDevice').value='target:${id}';document.getElementById('operationDevice').dispatchEvent(new Event('change'))`);await h.waitFor(()=>run(`TargetOperation.selected?.target.device_id==='${id}'&&!TargetOperation.busy&&!!TargetOperation.selected.status`),'target status read');};
  await choose(pi);assert.equal(writes.length,0,'selection never changes device settings');
  assert.equal(await run("FleetOperation.remote()&&document.getElementById('operationLocalView').inert"),true);
  await run('patchTransmitControl();stopTransmission();patchRebootBehaviorControl();openConfirmModal("reboot",{})');assert.equal(writes.length,0,'local handlers cannot act while a paired target is selected');
  await click('Enable');await h.waitFor(()=>run('!TargetOperation.busy&&TargetOperation.selected.status?.schedule_enabled'),'enable saved Pi schedule');assert.equal(writes.at(-1).route.endsWith('/enable'),true);
  await click('Disable');await h.waitFor(()=>run('!TargetOperation.busy&&TargetOperation.selected.status?.schedule_enabled===false'),'pause Pi schedule');
  await click('Transmit');await h.waitFor(()=>run('!TargetOperation.busy&&TargetOperation.selected.status?.transmit_effective'),'enable Pi transmit permission');
  lost=true;await click('Stop');await h.waitFor(()=>run('!TargetOperation.busy&&!!TargetOperation.selected.client.operation'),'lost Pi stop reply');const count=writes.length;
  await choose(pico);await choose(pi);assert.equal(writes.length,count);assert.equal(await run("document.getElementById('operationTargetStop').disabled"),true,'uncertain Pi operation remains fenced after switching');
  lost=false;await click('Check');await h.waitFor(()=>run('!TargetOperation.busy&&!TargetOperation.selected.client.operation&&!!TargetOperation.selected.status'),'Pi receipt recovery');assert.equal(writes.length,count,'receipt check does not replay stop');
  delayed=true;await click('Read');await h.waitFor(()=>!!releaseStatus,'delayed Pi read');await run(`document.getElementById('operationDevice').value='target:${pico}';document.getElementById('operationDevice').dispatchEvent(new Event('change'))`);assert.equal(await run('TargetOperation.selected.target.device_id'),pi,'selection frozen through an in-flight read');delayed=false;releaseStatus();await h.waitFor(()=>run('!TargetOperation.busy'),'delayed read settled');
  await choose(pico);picoLost=true;await click('Stop');await h.waitFor(()=>run('!TargetOperation.busy&&TargetOperation.selected.unknown'),'lost Pico stop reply');const picoCount=writes.length;
  await click('Read');await h.waitFor(()=>run('!TargetOperation.busy'),'ordinary Pico read');assert.equal(await run('TargetOperation.selected.unknown'),true,'ordinary status cannot clear stop hold');
  picoLost=false;await click('StopRead');await h.waitFor(()=>run('!TargetOperation.busy&&!TargetOperation.selected.unknown'),'explicit Pico stop readback');assert.equal(writes.length,picoCount,'stop readback never replays mutation');
  stopped=false;picoLost=true;await click('Stop');await h.waitFor(()=>run('!TargetOperation.busy&&TargetOperation.selected.unknown'),'second held stop');const beforeBootChange=writes.length;picoBoot='9'.repeat(32);await click('Read');await h.waitFor(()=>run('!TargetOperation.busy'),'changed boot read');await click('StopRead');await h.waitFor(()=>run('!TargetOperation.busy'),'changed boot stop readback');assert.equal(await run('TargetOperation.selected.unknown'),true,'new boot cannot resolve the old stop');assert.equal(writes.length,beforeBootChange);picoBoot=boot;picoLost=false;await click('Read');await h.waitFor(()=>run('!TargetOperation.busy'),'original boot read');await click('StopRead');await h.waitFor(()=>run('!TargetOperation.busy&&!TargetOperation.selected.unknown'),'original boot explicit stop readback');
  const output=process.env.WSPRRYPI_TARGET_OPERATION_SCREENSHOTS||fs.mkdtempSync(path.join(os.tmpdir(),'target-operation-captures-'));fs.mkdirSync(output,{recursive:true});
  async function capture(scene){for(const width of [1440,390])for(const theme of ['light','dark']){await client.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<500});await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');window.scrollTo(0,0)`);await client.settleRendering();assert(await run('document.documentElement.scrollWidth<=innerWidth'));assert(await run("Array.from(document.querySelectorAll('#operationTargetView .btn:disabled')).every(button=>getComputedStyle(button).opacity==='1')"),'Disabled target actions retain readable labels');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(path.join(output,`${scene}-${theme}-${width}.png`),Buffer.from(shot.data,'base64'));}}
  await capture('pico');await choose(pi);await capture('pi');
  noSettings=true;await click('Read');await h.waitFor(()=>run('!TargetOperation.busy&&TargetOperation.selected.readOnly&&!!TargetOperation.selected.status'),'paired controller without transmitter settings');assert.equal(await run("document.getElementById('operationTargetStop').disabled&&document.getElementById('operationTargetTransmit').disabled"),true);noSettings=false;await click('Read');await h.waitFor(()=>run('!TargetOperation.busy&&!TargetOperation.selected.readOnly'),'transmitter settings restored');
  advertised=false;await run('TargetOperation.refresh()');assert.equal(await run('TargetOperation.selected.status'),null);assert.equal(await run("document.getElementById('operationTargetStop').disabled"),true);await capture('removed');
  inventoryBad=true;await run('TargetOperation.refresh()');assert.equal(await run('TargetOperation.options().length'),0,'malformed inventory never populates selector');
  inventoryBad=false;advertised=true;await run('TargetOperation.refresh()');await choose(pi);await click('Setup');
  await h.waitFor(()=>run("!!window.SetupTargets&&SetupTargets.managingPi&&!!window.PiTargetEditors?.state&&!PiTargetEditors.state.paused"),'Operations handoff opens the same paired Pi in Setup');
  assert.equal(await run('PiTargetEditors.state.device'),pi);
  console.log('PASS Operations paired Pi/Pico: strict inventory, no legacy dependency, shipped target clients, local control fence, Pi schedule/transmit/stop, Pico stop, lost replies without replay, switching, delayed reads, removal, malformed inventory, twelve theme/viewport captures: '+output);
 }catch(error){failure=error;if(client)try{console.error(await client.evaluate("({feedback:document.getElementById('operationTargetFeedback')?.textContent,state:document.getElementById('operationTargetState')?.textContent,selected:TargetOperation?.selected?.target,options:Array.from(document.getElementById('operationDevice').options,o=>o.value)})"));}catch(_){} }
 finally{releaseStatus?.();await h.finishFixture({failure,php,browser,client,profile,cleanups:[async()=>{if(proxy)await h.closeServer(proxy);} ]});}
})().catch(error=>{console.error(error);process.exitCode=1});

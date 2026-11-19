// SPDX-License-Identifier: MIT
// Actual controller HTTP -> independent OpenSSL clients -> two actual Pico
// TLS/API/journal services. No WTP execution application or local output exists.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto'),readline=require('node:readline'),net=require('node:net'),http=require('node:http');
const {spawnFixtureChild:spawn,finishChildren}=require('./browser_fixture_process.js');
const [build,binary]=process.argv.slice(2);if(!build||!binary)throw Error('Explicit candidate Pico build and controller fixture required');
function lines(child){const queue=[],pending=[];readline.createInterface({input:child.stdout}).on('line',line=>pending.length?pending.shift()(line):queue.push(line));return()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Paired service reply deadline')),45000);pending.push(line=>{clearTimeout(timer);resolve(line);});});}
async function port(start){for(let n=start;n<start+100;++n){const server=net.createServer();if(await new Promise(resolve=>{server.once('error',()=>resolve(false));server.listen(n,'127.0.0.1',()=>server.close(()=>resolve(true)));}))return n;}throw Error('No fixture port');}
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'pico-controller-http-'));let a,b,controller,failure;
 try{
  const credentials=path.join(build,'credentials-v3');
  for(const id of ['a','b']){const dir=path.join(temporary,id);fs.mkdirSync(dir,{mode:0o700});for(const file of ['client-ca.crt','client.crt','client.key']){fs.copyFileSync(path.join(build,id==='a'?'credentials-v3':'credentials-second-v3',file),path.join(dir,file));fs.chmodSync(path.join(dir,file),0o600);}}
  a=spawn(path.join(build,'pico_tls_server'),['0','a','settings','18443'],{stdio:['pipe','pipe','inherit']});let lineA=lines(a);assert.match(await lineA(),/^READY 18443/);
  b=spawn(path.join(build,'pico_tls_server'),['1','b','settings','18444','second'],{stdio:['pipe','pipe','inherit']});const lineB=lines(b);assert.match(await lineB(),/^READY 18444/);
  const dump=async(child,line)=>{child.stdin.write('DUMP\n');return JSON.parse((await line()).replace(/^DUMP /,''));};
  const beforeA=await dump(a,lineA),beforeB=await dump(b,lineB);
  const httpPort=await port(39300),socketPort=await port(httpPort+1),origin=`http://127.0.0.1:${httpPort}`;
  const ini=path.join(temporary,'controller.ini'),original=`[Operation]\nWeb Port = ${httpPort}\nSocket Port = ${socketPort}\n[Security]\nPrivileged Network Safety = enforced\n`;
  fs.writeFileSync(ini,original,{mode:0o600});
  controller=spawn(binary,[temporary,'--controller-only','-i',ini],{stdio:['ignore','ignore','inherit']});
  const deadline=Date.now()+10000;while(true){try{const response=await fetch(origin+'/status');if(response.ok&&(await response.json()).controller_ready)break;}catch(_){}assert(Date.now()<deadline,'Controller ready deadline');await pause(50);}
  let cookie='',csrf='';
  async function call(resource,method='GET',body,headers={}){
   await pause(110);
   const response=await fetch(origin+resource,{method,headers:{...(!['GET','HEAD'].includes(method)?{'Origin':origin,'Content-Type':'application/json','X-WsprryPico-Request':'1','X-WsprryPi-CSRF':csrf}:{}),...(cookie?{Cookie:cookie}:{}),...headers},...(body!==undefined?{body:JSON.stringify(body)}:{})});
   // The production outer origin guard intentionally uses a plain-text denial.
   const text=await response.text();let value;try{value=JSON.parse(text);}catch(_){value={message:text};}
   return {status:response.status,body:value,etag:response.headers.get('ETag'),context:response.headers.get('X-WsprryPi-Target-Context'),response};
  }
  const bootstrap=await call('/api/control/v1/host/management-session','POST',{contract:'Wsprry-Control/1',schema:'controller-browser-session'});assert.equal(bootstrap.status,200);
  csrf=bootstrap.response.headers.get('X-WsprryPi-CSRF');cookie=bootstrap.response.headers.get('Set-Cookie').split(';')[0];
  const profiles={};
  for(const [id,tcp] of [['a',18443],['b',18444]]){
   const catalog=await call('/api/control/v1/host/devices');
   const settings={'Endpoint':'','USB Serial':'','USB Vendor ID':0,'USB Product ID':0,'TLS Server Identity':'','Transport':'network','Hostname':'wsprrypico-'+id.repeat(32)+'.local','Device ID':id.repeat(32),'TCP Port':tcp,
    'TLS CA File':path.join(temporary,id,'client-ca.crt'),'TLS Client Certificate':path.join(temporary,id,'client.crt'),'TLS Client Key':path.join(temporary,id,'client.key')};
   const saved=await call('/api/control/v1/host/devices','POST',{operation:'add',name:'Pico '+id,method:'manual',discovery_id:'',settings,consent_plain:false},{'If-Match':catalog.etag});assert.equal(saved.status,200,JSON.stringify(saved.body));profiles[id]=saved.body.profiles.at(-1).id;
  }
  const target=id=>'/api/control/v1/targets/'+id.repeat(32)+'/pico/';const headers={};
  for(const id of ['a','b']){
   const selected=await call(target(id)+'management','GET',undefined,{'X-WsprryPi-Profile-ID':profiles[id]});assert.equal(selected.status,200);assert(selected.context);
   headers[id]={'X-WsprryPi-Profile-ID':profiles[id],'X-WsprryPi-Target-Context':selected.context};
   const connected=await call(target(id)+'management','POST',{contract:'Wsprry-Control/1',action:'engineering_mtls'},headers[id]);assert.equal(connected.status,200,JSON.stringify(connected.body));
   headers[id]['X-WsprryPi-Target-Context']=connected.context;
  }
  const listed=await call('/api/control/v1/targets');assert.equal(listed.status,200);assert.equal(listed.body.targets.length,2);
  for(const resource of ['member/management','member/configuration','member/application','member/hostname','network'])
   assert.equal((await call('/api/control/v1/'+resource)).status,404,'Selected-backend settings route must be retired: '+resource);
  const hostCaps=await call('/api/control/v1/capabilities');assert(!Object.hasOwn(hostCaps.body,'member_resources'));assert(!Object.hasOwn(hostCaps.body,'management_carriers'));
  const snapshots={};for(const id of ['a','b']){snapshots[id]=await call(target(id)+'configuration','GET',undefined,headers[id]);assert.equal(snapshots[id].status,200,JSON.stringify(snapshots[id].body));}
  assert.deepEqual(await dump(a,lineA),beforeA);assert.deepEqual(await dump(b,lineB),beforeB);
  const payload=(id,callsign)=>({contract:'Wsprry-Control/1',schema:'pico-settings',target:snapshots[id].body.target,operation:'save',config:{station:{callsign}}});
  const operationHeaders=(id,operation,revision)=>({...headers[id],'X-WsprryPi-Operation-ID':operation,'If-Match':revision});
  const rejectedHeaders=()=>operationHeaders('a',crypto.randomBytes(16).toString('hex'),snapshots.a.etag);
  for(const name of ['X-WsprryPi-Profile-ID','X-WsprryPi-Target-Context','X-WsprryPi-Operation-ID','X-WsprryPi-CSRF']) {
   await pause(110);
   const selected={Host:`127.0.0.1:${httpPort}`,Origin:origin,'Content-Type':'application/json','X-WsprryPico-Request':'1',Cookie:cookie,'X-WsprryPi-CSRF':csrf,...rejectedHeaders()};
   // Exercise duplicated authorization metadata with valid bounded framing.
   // Node otherwise emits chunked framing, refused before the HTTP adapter.
   const body=JSON.stringify(payload('a','W1AW'));selected['Content-Length']=Buffer.byteLength(body);selected.Connection='close';
   const raw=Object.entries(selected).flat();raw.push(name,selected[name]);
   const denied=await new Promise((resolve,reject)=>{
    const request=http.request({hostname:'127.0.0.1',port:httpPort,path:target('a')+'configuration',method:'POST',headers:raw},response=>{
     let text='';response.on('data',part=>text+=part);response.on('end',()=>{try{resolve({status:response.statusCode,body:JSON.parse(text)});}catch(error){reject(error);}});
    });request.on('error',reject);request.end(body);
   });assert.equal(denied.status,400);assert.equal(denied.body.schema,'target-error');
  }
  for(const override of [{'X-WsprryPi-CSRF':''},{'X-WsprryPi-CSRF':'0'.repeat(64)},{Cookie:''},{Origin:'http://untrusted.invalid'}]) {
   const denied=await call(target('a')+'configuration','POST',payload('a','W1AW'),{...rejectedHeaders(),...override});
   assert.equal(denied.status,403,JSON.stringify(denied.body));
   assert.equal(denied.body.schema,'target-error');assert.deepEqual(Object.keys(denied.body).sort(),['contract','error','schema']);
  }
  const missingOperation=await call(target('a')+'configuration','POST',payload('a','W1AW'),{...headers.a,'If-Match':snapshots.a.etag});assert.equal(missingOperation.status,428);
  assert.deepEqual(missingOperation.body,{contract:'Wsprry-Control/1',schema:'target-error',error:{code:'revision_required',retryable:false}});
  let nested={};for(let i=0;i<18;++i)nested={nested};
  const tooDeep=await call(target('a')+'configuration','POST',nested,rejectedHeaders());assert.equal(tooDeep.status,400);
  assert.deepEqual(await dump(a,lineA),beforeA);assert.deepEqual(await dump(b,lineB),beforeB);
  assert.equal(fs.existsSync(ini+'.management'),false,'Security/shape refusals must not reserve a journal');
  const wrong=await call(target('a')+'configuration','POST',{...payload('a','W1AW'),target:snapshots.b.body.target},operationHeaders('a',crypto.randomBytes(16).toString('hex'),snapshots.a.etag));assert.equal(wrong.status,409);
  assert.equal(fs.existsSync(ini+'.management'),false,'Rejected native request must not reserve a journal');
  const network=await call(target('a')+'network','GET',undefined,headers.a);
  assert.equal(network.status,200,JSON.stringify(network.body));
  const networkHeaders={...headers.a,'If-Match':network.etag,'X-WsprryPi-Operation-ID':crypto.randomBytes(16).toString('hex')};
  const networkSaved=await call(target('a')+'network','PUT',{contract:'Wsprry-Control/1',enabled:true},networkHeaders);
  assert.equal(networkSaved.status,200,JSON.stringify(networkSaved.body));assert.equal(networkSaved.body.requested_enabled,true);
  const networkReplay=await call(target('a')+'network','PUT',{contract:'Wsprry-Control/1',enabled:true},networkHeaders);
  assert.equal(networkReplay.status,200,JSON.stringify({reply:networkReplay.body,before:headers.a['X-WsprryPi-Target-Context'],after:networkSaved.context}));assert.deepEqual(networkReplay.body,networkSaved.body);
  const otherNetwork=await call(target('b')+'network','GET',undefined,headers.b);
  assert.equal(otherNetwork.status,200);assert.equal(otherNetwork.body.enabled,true);
  snapshots.a=await call(target('a')+'configuration','GET',undefined,headers.a);assert.equal(snapshots.a.status,200);
  const opA=crypto.randomBytes(16).toString('hex'),opB=crypto.randomBytes(16).toString('hex');
  const [savedA,savedB]=await Promise.all([call(target('a')+'configuration','POST',payload('a','W1AW'),operationHeaders('a',opA,snapshots.a.etag)),call(target('b')+'configuration','POST',payload('b','W2AW'),operationHeaders('b',opB,snapshots.b.etag))]);
  assert.equal(savedA.status,200,JSON.stringify(savedA.body));assert.equal(savedB.status,200,JSON.stringify(savedB.body));
  let afterA=await dump(a,lineA),afterB=await dump(b,lineB);assert.equal(afterA.config.station.callsign,'W1AW');assert.equal(afterB.config.station.callsign,'W2AW');
  for(const [before,after]of [[beforeA,afterA],[beforeB,afterB]])for(const key of ['cursor','password_sha256','inhibit','prepares','disables','suspended'])assert.deepEqual(after[key],before[key],key);
  const replay=await call(target('a')+'configuration','POST',payload('a','W1AW'),operationHeaders('a',opA,snapshots.a.etag));assert.equal(replay.status,200);assert.deepEqual(replay.body,savedA.body);assert.deepEqual(await dump(a,lineA),afterA);
  const reuse=await call(target('a')+'configuration','POST',payload('a','W9AW'),operationHeaders('a',opA,snapshots.a.etag));assert.equal(reuse.status,409);assert.deepEqual(await dump(a,lineA),afterA);
  const receipt=await call('/api/control/v1/targets/'+'a'.repeat(32)+'/operations/'+opA,'GET',undefined,headers.a);assert.equal(receipt.status,200);assert.equal(receipt.body.state,'committed');
  assert.equal(receipt.response.headers.get('X-WsprryPi-Recovery-Resource'),'configuration');
  // A real producer storage failure creates uncertainty. Repair the fixture's
  // store, then recover through a fresh browser context without resending it.
  snapshots.a=await call(target('a')+'configuration','GET',undefined,headers.a);
  a.stdin.write('FAIL\n');assert.equal(await lineA(),'ACK');
  const uncertainId=crypto.randomBytes(16).toString('hex');
  const uncertain=await call(target('a')+'configuration','POST',payload('a','K2ABC'),operationHeaders('a',uncertainId,snapshots.a.etag));
  assert.equal(uncertain.status,503,JSON.stringify(uncertain.body));assert.equal(uncertain.body.state,'unknown');
  assert.equal(uncertain.response.headers.get('X-WsprryPi-Recovery-Resource'),'configuration');
  a.stdin.write('RELOAD_STORE\n');assert.equal(await lineA(),'ACK');
  const reopened=await call(target('a')+'management','GET',undefined,{'X-WsprryPi-Profile-ID':profiles.a});assert.equal(reopened.status,200);
  headers.a['X-WsprryPi-Target-Context']=reopened.context;
  snapshots.a=await call(target('a')+'configuration','GET',undefined,headers.a);assert.equal(snapshots.a.status,200);
  const heldBefore=await dump(a,lineA),newId=crypto.randomBytes(16).toString('hex');
  const held=await call(target('a')+'configuration','POST',payload('a','W3AW'),operationHeaders('a',newId,snapshots.a.etag));
  assert.equal(held.status,503,JSON.stringify(held.body));assert.equal(held.body.error.code,'recovery_required');
  const earlier='/api/control/v1/targets/'+'a'.repeat(32)+'/operations/'+uncertainId;
  assert.equal(held.response.headers.get('Location'),earlier);assert.equal(held.response.headers.get('X-WsprryPi-Recovery-Resource'),'configuration');
  assert.deepEqual(await dump(a,lineA),heldBefore);
  const heldHistory=JSON.parse(fs.readFileSync(path.join(ini+'.management','a'.repeat(32)+'.json'),'utf8'));
  assert(!heldHistory.receipts.some(entry=>entry.receipt.operation_id===newId),'A held action must not reserve another receipt');
  const earlierReceipt=await call(earlier,'GET',undefined,headers.a);assert.equal(earlierReceipt.body.state,'unknown');
  assert.equal(earlierReceipt.response.headers.get('X-WsprryPi-Recovery-Resource'),'configuration');
  const staleReview=await call(target('a')+'management','POST',{contract:'Wsprry-Control/1',action:'reconcile',operation_id:uncertainId,readback_revision:snapshots.a.response.headers.get('X-WsprryPi-Readback-Revision')},headers.a);
  assert.equal(staleReview.status,409,'A later attempted mutation invalidates its earlier read token');
  snapshots.a=await call(target('a')+'configuration','GET',undefined,headers.a);assert.equal(snapshots.a.status,200);
  const reviewed=await call(target('a')+'management','POST',{contract:'Wsprry-Control/1',action:'reconcile',operation_id:uncertainId,readback_revision:snapshots.a.response.headers.get('X-WsprryPi-Readback-Revision')},headers.a);
  assert.equal(reviewed.status,200,JSON.stringify(reviewed.body));assert.equal(reviewed.body.state,'unknown');
  assert.equal(reviewed.response.headers.get('X-WsprryPi-Recovery-Resource'),'configuration');
  assert.notEqual(reviewed.context,headers.a['X-WsprryPi-Target-Context']);
  headers.a['X-WsprryPi-Target-Context']=reviewed.context;
  snapshots.a=await call(target('a')+'configuration','GET',undefined,headers.a);assert.equal(snapshots.a.status,200);
  const explicitSave=await call(target('a')+'configuration','POST',payload('a','W3AW'),operationHeaders('a',crypto.randomBytes(16).toString('hex'),snapshots.a.etag));assert.equal(explicitSave.status,200,JSON.stringify(explicitSave.body));
  afterA=await dump(a,lineA);assert.equal(afterA.config.station.callsign,'W3AW');
  // Exercise the browser's target client against the same real service pair.
  // Node supplies the browser-owned Origin/cookie behavior explicitly.
  const browserClient=require('../data/pico-target-client.js').create({crypto:crypto.webcrypto,fetch:async(resource,options)=>{
   await pause(110);return fetch(origin+resource,{...options,headers:{Origin:origin,Cookie:cookie,...options.headers}});
  }});
  const browserB=await browserClient.open({id:profiles.b,settings:{'Device ID':'b'.repeat(32)}});
  const browserRead=await browserB.request('configuration');assert.equal(browserRead.response.status,200);
  const browserSave=await browserB.request('configuration',{method:'POST',revision:browserRead.response.headers.get('ETag'),body:{contract:'Wsprry-Control/1',schema:'pico-settings',target:browserRead.data.target,operation:'save',config:{station:{callsign:'W4AW'}}}});
  assert.equal(browserSave.response.status,200,JSON.stringify(browserSave.data));
  assert.equal(browserClient.operations()[0].device,'b'.repeat(32));assert.equal(browserClient.operations()[0].state,'committed');
  const browserReceipt=await browserB.lookup(browserSave.operation);assert.equal(browserReceipt.data.state,'committed');assert.equal(browserReceipt.recovery.resource,'configuration');
  headers.b['X-WsprryPi-Target-Context']=browserB.context;
  afterB=await dump(b,lineB);assert.equal(afterB.config.station.callsign,'W4AW');
  if(process.argv.includes('--browser')) {
   await require('./pico_target_editor_browser_test.js')({backendOrigin:origin,socketPort,profiles,
    failA:async()=>{a.stdin.write('FAIL\n');assert.equal(await lineA(),'ACK');},
    repairA:async()=>{a.stdin.write('RELOAD_STORE\n');assert.equal(await lineA(),'ACK');},dumpA:()=>dump(a,lineA)});
   afterA=await dump(a,lineA);afterB=await dump(b,lineB);
   for(const id of ['a','b']){const reopened=await call(target(id)+'management','GET',undefined,{'X-WsprryPi-Profile-ID':profiles[id]});assert.equal(reopened.status,200);headers[id]['X-WsprryPi-Target-Context']=reopened.context;}
  }
  if(process.argv.includes('--shared-browser')) {await require('./shared_setup_browser_test.js')({credentials:temporary,dumpA:()=>dump(a,lineA),dumpB:()=>dump(b,lineB),activeA:async()=>{a.stdin.write('ACTIVE_JOB\n');assert.equal(await lineA(),'ACK');},jobA:async()=>{a.stdin.write('JOB_STATE\n');return await lineA();},endA:async()=>{a.stdin.write('END_ACTIVE_JOB\n');assert.equal(await lineA(),'ACK');},rebootA:async()=>{const old=a;const ended=new Promise(resolve=>old.once('exit',resolve));old.kill('SIGTERM');await ended;a=spawn(path.join(build,'pico_tls_server'),['7','a','settings','18443'],{stdio:['pipe','pipe','inherit']});lineA=lines(a);assert.match(await lineA(),/^READY 18443/);}});afterA=await dump(a,lineA);afterB=await dump(b,lineB);}
  const badContext=await call(target('a')+'configuration','GET',undefined,headers.b);assert.equal(badContext.status,409);
  const catalog=await call('/api/control/v1/host/devices');assert.equal((await call('/api/control/v1/host/devices','POST',{operation:'rename',id:profiles.a,name:'Renamed A'},{'If-Match':catalog.etag})).status,200);
  assert.equal((await call(target('a')+'configuration','GET',undefined,headers.a)).status,409);
  assert.equal((await call(target('b')+'configuration','GET',undefined,headers.b)).status,200);
  const global=await call('/api/control/v1/status');assert.equal(global.body.host.selected,false);
  const status=await call('/status');assert.equal(status.body.local_output,null);assert.equal(status.body.local_transmission_available,false);
  assert.equal(fs.readFileSync(ini,'utf8'),original);
  const finalNetwork=await call(target('b')+'network','GET',undefined,headers.b);assert.equal(finalNetwork.status,200);
  const disableId=crypto.randomBytes(16).toString('hex');
  const networkDisabled=await call(target('b')+'network','PUT',{contract:'Wsprry-Control/1',enabled:false},operationHeaders('b',disableId,finalNetwork.etag));
  assert.equal(networkDisabled.status,200,JSON.stringify(networkDisabled.body));assert.equal(networkDisabled.body.requested_enabled,false);
  assert.equal((await call(target('b')+'network','GET',undefined,headers.b)).status,409,'Disabling Wi-Fi retires network management rather than authenticating from a cached receipt');
  const bHistory=JSON.parse(fs.readFileSync(path.join(ini+'.management','b'.repeat(32)+'.json')));
  assert.equal(bHistory.receipts.find(entry=>entry.receipt.operation_id===disableId).receipt.state,'committed','Accepted network disable remains durably recorded after carrier withdrawal');
  assert.deepEqual(await dump(b,lineB),afterB,'Network disable preserves configuration and output state');
  const exited=child=>new Promise((resolve,reject)=>{
   if(child.exitCode!==null)return resolve(child.exitCode);
   const timer=setTimeout(()=>{child.kill('SIGTERM');reject(Error('Controller recovery startup/shutdown deadline'));},10000);
   child.once('exit',code=>{clearTimeout(timer);resolve(code);});
  });
  const stopController=async()=>{const exit=exited(controller);controller.kill('SIGTERM');assert.equal(await exit,0);};
  const startController=()=>spawn(binary,[temporary,'--controller-only','-i',ini],{stdio:['ignore','ignore','inherit']});
  const history=ini+'.management',record=path.join(history,'a'.repeat(32)+'.json'),backup=fs.readFileSync(record);
  await stopController();fs.unlinkSync(record);
  controller=startController();assert.equal(await exited(controller),1,'Missing recorded history must inhibit a fresh controller before listeners');assert.equal(fs.existsSync(record),false);
  fs.writeFileSync(record,backup,{mode:0o600});controller=startController();
  const recoveredDeadline=Date.now()+10000;while(true){try{if((await call('/status')).body.controller_ready)break;}catch(_){}assert(Date.now()<recoveredDeadline,'Restored controller readiness deadline');}
  await stopController();
  const catalogPath=ini+'.wtp-devices.json',catalogBackup=fs.readFileSync(catalogPath);fs.unlinkSync(catalogPath);
  controller=startController();assert.equal(await exited(controller),1,'A missing catalog beside management authority must inhibit startup');
  assert.equal(fs.existsSync(catalogPath),false,'Do not replace lost enrollment/recovery authority with an empty catalog');assert.deepEqual(fs.readFileSync(record),backup);
  fs.writeFileSync(catalogPath,catalogBackup,{mode:0o600});controller=startController();
  const restoredCatalogDeadline=Date.now()+10000;while(true){try{if((await call('/status')).body.controller_ready)break;}catch(_){}assert(Date.now()<restoredCatalogDeadline,'Restored catalog readiness deadline');}
  await stopController();fs.rmSync(history,{recursive:true});
  controller=startController();assert.equal(await exited(controller),1,'A missing history directory must not become an empty authority');assert.equal(fs.existsSync(history),false);
  assert.deepEqual(await dump(a,lineA),afterA);assert.deepEqual(await dump(b,lineB),afterB);assert.equal(fs.readFileSync(ini,'utf8'),original);
  console.log('PASS: two actual Pico services through production controller HTTP/CSRF/catalog/crypto/context/journals; concurrent isolated saves, exact replay, conflict/profile fencing, no WTP selection or local output');
 }catch(error){failure=error;}
 try{await finishChildren({failure,children:[controller,a,b]});}finally{fs.rmSync(temporary,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});

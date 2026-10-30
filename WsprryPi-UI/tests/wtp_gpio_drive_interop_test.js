// Actual protected Pi -> engineering TLS / authenticated Consumer-Radio -> Pico.
'use strict';
const assert=require('node:assert/strict'),path=require('node:path'),readline=require('node:readline'),{spawn}=require('node:child_process');
if(!process.argv[2])throw Error('Usage: node wtp_gpio_drive_interop_test.js BUILD_DIRECTORY');
const {driveCases}=require('./gpio_drive_pair_cases.js');
const build=path.resolve(process.argv[2]),unsupported=process.argv.includes('--unsupported');
function lines(child){const queue=[],waiters=[];readline.createInterface({input:child.stdout}).on('line',l=>waiters.length?waiters.shift()(l):queue.push(l));return()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('GPIO pair deadline')),30000);waiters.push(l=>{clearTimeout(t);resolve(l);});});}
(async()=>{for(const carrier of process.argv.includes('--engineering-only')?['engineering']:['engineering','station','ap']){
 let server,client;
 try{
  let readServer;
  if(carrier==='engineering'){server=spawn(path.join(build,'pico_tls_server'),['0','a','settings'],{stdio:['pipe','pipe','inherit']});readServer=lines(server);assert.match(await readServer(),/^READY /);}
  client=spawn(path.join(build,carrier==='engineering'?'wtp_configuration_client':'consumer_interop'),carrier==='engineering'?[path.join(build,'credentials-v3')]:['--stdio',carrier],{stdio:['pipe','pipe','inherit']});const next=lines(client);if(carrier!=='engineering')assert.match(await next(),/^READY /);
  const call=async r=>{client.stdin.write(JSON.stringify(r)+'\n');return JSON.parse(await next());};
  const get=resource=>call({method:'GET',path:'/api/v1/member/'+resource});
  const post=(resource,body,revision)=>call({method:resource==='gpio-drive'?'PUT':'POST',path:'/api/v1/member/'+resource,body:JSON.stringify(body),revision});
  const connect=()=>call({method:'POST',path:'/api/v1/member/management',body:JSON.stringify(carrier==='engineering'?{action:'engineering_mtls'}:{action:'connect',carrier:carrier==='ap'?'consumer_ap':'consumer_station',host:carrier==='ap'?'192.168.4.1':'192.168.1.53',password:'test-password'})});
  const selection=await connect();
  assert.equal(selection.status,200,carrier+': '+JSON.stringify(selection));
  if(process.env.WSPRRYPI_INTEROP_TRACE)console.error(carrier,selection,await get('management'));
  const initial=await get('gpio-drive');
  if(unsupported){assert.equal(initial.status,409,JSON.stringify(initial));const settings=await get('configuration');assert.equal(settings.status,200,JSON.stringify(settings));const r=await post('configuration',{schema:'wsprrypi-pico-settings/1',target:settings.body.target,operation:'save',config:{expires_utc_s:0}},settings.etag);assert.equal(r.status,200,JSON.stringify(r));console.log(carrier+': old firmware drive unavailable; unrelated settings remain usable');continue;}
  assert.equal(initial.status,200,JSON.stringify(initial));const target=initial.body.target;
  await driveCases(call,{profileOwnedStation:carrier!=='engineering'});
  for(const drive of [2,4,8,12]){
   const before=await get('gpio-drive');const written=await post('gpio-drive',{schema:'transmitter-gpio-drive/1',target,gpio_drive_ma:drive},before.etag);
   assert.equal(written.status,200,JSON.stringify(written));assert.equal(written.body.saved,drive);assert.equal(written.body.pad_readback,null);
   const settings=await get('configuration');assert.equal(settings.status,200,JSON.stringify(settings));assert(!Object.hasOwn(settings.body.config,'gpio_drive_ma'));
   const unrelated=await post('configuration',{schema:'wsprrypi-pico-settings/1',target,operation:'save',config:{expires_utc_s:0}},settings.etag);assert.equal(unrelated.status,200,JSON.stringify(unrelated));
   const after=await get('gpio-drive');assert.equal(after.body.saved,drive);assert.equal(after.body.boot_active,4);
   const bad=await post('gpio-drive',{schema:'transmitter-gpio-drive/1',target,gpio_drive_ma:6},after.etag);assert.equal(bad.status,400,JSON.stringify(bad));
  }
  const draftRead=await get('gpio-drive-progress');assert.equal(draftRead.status,200,JSON.stringify(draftRead));
  const draftWrite=await call({method:'PUT',path:'/api/v1/member/gpio-drive-progress',revision:draftRead.etag,
    body:JSON.stringify({schema:'transmitter-progress/2',target,request_id:'4'.repeat(32),fields:{gpio_drive_ma:8}})});
  assert.equal(draftWrite.status,200,JSON.stringify(draftWrite));
  const freshDraft=await get('gpio-drive-progress');assert.equal(freshDraft.status,200,JSON.stringify(freshDraft));
  const applied=await call({method:'POST',path:'/api/v1/member/gpio-drive-progress',revision:freshDraft.etag,
    body:JSON.stringify({schema:'transmitter-progress/2',target,application:{draft_revision:freshDraft.body.draft_revision,domain:'drive'}})});
  assert.equal(applied.status,200,JSON.stringify(applied));assert.equal((await get('gpio-drive')).body.saved,8);
  const a=await get('gpio-drive');await call({control:'drop_mutation_reply'});
  const dropped=await post('gpio-drive',{schema:'transmitter-gpio-drive/1',target,gpio_drive_ma:8},a.etag);assert.notEqual(dropped.status,200);
  let reconciled=await get('gpio-drive');
  if(carrier!=='engineering'){assert.notEqual(reconciled.status,200);await call({control:'expire'});const reconnected=await connect();assert.equal(reconnected.status,200,JSON.stringify(reconnected));reconciled=await get('gpio-drive');}
  assert.equal(reconciled.status,200,JSON.stringify(reconciled));assert.equal(reconciled.body.saved,8);assert.equal(reconciled.body.pending_restart,true);
  console.log(carrier+': all strengths, legacy saves, invalid mutation and lost-reply reconciliation passed through actual protected runtime/carrier/Pico services');
 }finally{for(const child of [client,server])if(child&&child.exitCode===null){child.stdin.end();child.kill();}}
}})().catch(e=>{console.error(e);process.exitCode=1;});

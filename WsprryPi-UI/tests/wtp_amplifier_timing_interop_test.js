// Actual protected Pi -> engineering TLS / authenticated Consumer-Radio -> Pico.
'use strict';
const assert=require('node:assert/strict'),path=require('node:path'),readline=require('node:readline'),{spawn}=require('node:child_process');
const build=path.resolve(process.argv[2]),unsupported=process.argv.includes('--unsupported'),legacy=process.argv.includes('--legacy-ui');
function lines(child){const queue=[],waiters=[];readline.createInterface({input:child.stdout}).on('line',l=>waiters.length?waiters.shift()(l):queue.push(l));return()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('GPIO pair deadline')),30000);waiters.push(l=>{clearTimeout(t);resolve(l);});});}
(async()=>{for(const carrier of (legacy||process.argv.includes('--engineering-only'))?['engineering']:['engineering','station','ap']){
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
  await require('./amplifier_timing_pair_cases.js')(call);
  await require('./filter_selector_pair_cases.js')(call);
 }finally{for(const child of [client,server])if(child&&child.exitCode===null){child.stdin.end();child.kill();}}
}})().catch(e=>{console.error(e);process.exitCode=1;});

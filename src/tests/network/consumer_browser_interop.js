// SPDX-License-Identifier: MIT
// Shipped Noble bundle and native OpenSSL runtime share one real Pico manager.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {webcrypto}=require('node:crypto'),{spawn}=require('node:child_process'),readline=require('node:readline');
const {browser,response}=require('../../../WsprryPi-UI/tests/wtp_management_fixture.js');
const [build,pico]=process.argv.slice(2);
const child=spawn(path.join(build,'consumer_interop'),['--stdio','station'],{stdio:['pipe','pipe','inherit']});
const queue=[],waiting=[];
readline.createInterface({input:child.stdout}).on('line',line=>waiting.length?waiting.shift()(line):queue.push(line));
const next=()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{
 const timer=setTimeout(()=>reject(Error('Mixed-client fixture deadline')),30000);
 waiting.push(line=>{clearTimeout(timer);resolve(line);});
});
let work=Promise.resolve();
function command(value){const result=work.then(async()=>{child.stdin.write(JSON.stringify(value)+'\n');return JSON.parse(await next());});work=result.catch(()=>{});return result;}
const source=fs.readFileSync(path.join(pico,'src/network/bootstrap_web/radio-bundle.js'),'utf8');
function consumer(){
 const root={crypto:webcrypto,TextEncoder,TextDecoder,Uint8Array,Uint32Array,ArrayBuffer,AbortSignal,Date,atob,btoa,
  fetch:async(url,options={})=>{
   const bytes=typeof options.body==='string'?Buffer.from(options.body):Buffer.from(options.body||[]);
   const r=await command({consumer_packet:{path:url,method:options.method||'GET',media:options.headers?.['Content-Type']||'application/json',body_b64:bytes.toString('base64url')}});
   return new Response(Buffer.from(r.body_b64,'base64url'),{status:r.status,headers:{'Content-Type':r.media}});
  }};
 vm.runInNewContext(source,root);
 return root.WsprryPicoConsumerRadio.createClient();
}
(async()=>{
 try{
  assert.match(await next(),/^READY /);
  const calls=[];
  const ui=browser(async r=>{const result=await command({method:r.method||'GET',path:r.url,body:r.body?JSON.stringify(r.body):'',revision:r.headers?.['If-Match']||'',member_context:r.headers?.['X-WsprryPi-Member-Context']||'',headers:r.headers||{}});calls.push(result);return response(result.body,result.status,result.etag);});
  ui.element('wtp-management-carrier').value='consumer_station';ui.element('wtp-management-host').value='192.168.1.53';ui.element('wtp-management-password').value='test-password';
  await ui.click('wtp-management-connect');assert.equal(calls.at(-1).status,200);
  const a=consumer(),b=consumer();
  for(const client of [a,b]){await command({control:'advance'});await client.login('test-password',await client.identity());}
  for(let i=0;i<3;i++){
   await ui.click('wtp-remote-load');assert.equal(calls.at(-1).status,200);
   for(const client of [a,b])assert.equal((await client.request('status')).status,200);
  }
  await command({control:'advance'});const full=consumer();await assert.rejects(full.login('test-password',await full.identity()),/three radio sessions/);
  await a.logout();assert.equal((await b.request('status')).status,200);
  await ui.click('wtp-remote-load');assert.equal(calls.at(-1).status,200);
  await ui.click('wtp-management-disconnect');assert.equal(calls.at(-1).status,200);
  assert.equal((await b.request('status')).status,200);
  const state=await command({control:'dump'});assert.equal(state.wtp_io,0);assert.equal(state.mutations,0);
  console.log('Actual native Pi runtime plus two shipped-browser crypto contexts coexist; three-session quota and independent logout passed');
 }finally{child.stdin.end();if(child.exitCode===null)await new Promise(resolve=>child.once('exit',resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});

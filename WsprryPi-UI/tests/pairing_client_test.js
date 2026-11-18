// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),crypto=require('node:crypto');
const {create}=require('../data/pairing-client.js');
const contract='Wsprry-Control/1',self='a'.repeat(32),target='b'.repeat(32),boot='c'.repeat(32);
const state={contract,schema:'pairing-state',device_id:self,boot_id:boot,authorization_generation:'0',state:'unpaired',manager:null,window:null};
const binding={device_id:target,boot_id:boot,profile_generation:'1',connection_generation:'1'};
(async()=>{
 const records=new Map(),requests=[];let lost=true,operation='',status='pending';
 const storage={getItem:key=>records.get(key)||null,setItem:(key,value)=>records.set(key,value),removeItem:key=>records.delete(key)};
 const response=value=>new Response(JSON.stringify(value),{status:200,headers:{'X-WsprryPi-CSRF':'f'.repeat(64)}});
 const fetch=async(url,options)=>{
  requests.push({url,method:options.method||'GET'});
  if(url.endsWith('/management-session'))return response({contract,schema:'controller-browser-session'});
  if(url.endsWith('/connect')){
   const body=JSON.parse(options.body);operation=body.operation_id;assert.equal(body.code,'0123ABCD');assert.equal(body.expected_device_id,target);
   assert(!Array.from(records.values()).join('').includes(body.code),'No browser-persisted code');
   if(lost){lost=false;throw Error('Artificial lost reply after acceptance');}
   throw Error('A pairing command must never be replayed by this client');
  }
  if(url.endsWith('/operations/'+operation))return response({contract,schema:'target-operation',binding,operation_id:operation,request_digest:'e'.repeat(64),state:status,revision:status==='committed'?'d'.repeat(64):null,error:null});
  if(url.endsWith('/window'))return response({contract,schema:'pair-window',window_id:'d'.repeat(32),device_id:self,boot_id:boot,authorization_generation:'0',code:'0123-ABCD',expires_in_ms:120000});
  if(url.endsWith('/pairing'))return response(state);
  throw Error('Unexpected request');
 };
 const client=create({fetch,crypto:crypto.webcrypto,storage});
 assert.deepEqual(await client.state(),state);
 const window=await client.window(state);assert.equal(window.code,'0123-ABCD');assert.equal(records.size,0);
 assert.throws(()=>client.connect({discovery_id:'d'.repeat(32),device_id:target},'bad-code'),/invalid_code/);
 await assert.rejects(client.connect({discovery_id:'d'.repeat(32),device_id:target},'0123-abcd'));
 assert.deepEqual(Object.keys(client.pending()).sort(),['device','kind','operation']);assert.equal(records.size,1);
 const cold=create({fetch,crypto:crypto.webcrypto,storage});assert.equal(cold.pending().operation,operation);
 assert.equal((await cold.check()).state,'pending');assert(cold.pending());status='committed';
 assert.equal((await cold.check()).state,'committed');assert.equal(cold.pending(),null);assert.equal(records.size,0);
 assert.equal(requests.filter(request=>request.url.endsWith('/connect')).length,1,'Lost reply recovery must issue only reads');
 const corrupt={...storage,getItem:()=>'{"code":"DO-NOT-RETAIN"}'};assert.equal(create({fetch,crypto:crypto.webcrypto,storage:corrupt}).pending(),null);
 console.log('PASS pairing client: temporary-code confinement, durable redacted pending hint, fresh-browser exact receipt recovery and no mutation replay');
})().catch(error=>{console.error(error);process.exitCode=1;});

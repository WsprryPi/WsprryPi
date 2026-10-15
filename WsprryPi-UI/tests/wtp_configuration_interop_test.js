// SPDX-License-Identifier: MIT
// Actual UI producer -> Pi browser/coordinator/HTTPS -> exact Pico API/journal.
'use strict';
const assert=require('node:assert/strict'), fs=require('node:fs'), path=require('node:path');
const crypto=require('node:crypto'), readline=require('node:readline');
const {spawn}=require('node:child_process');
const {browser,response}=require('./wtp_management_fixture.js');
const [build,profile,pico]=process.argv.slice(2);
function lines(process) {
  const queued=[], waiting=[];
  readline.createInterface({input:process.stdout}).on('line',line=>waiting.length?waiting.shift()(line):queued.push(line));
  return ()=>queued.length?Promise.resolve(queued.shift()):new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error('Fixture response deadline')),45000);
    waiting.push(line=>{clearTimeout(timer);resolve(line);});
  });
}
async function stop(child) {
  if (!child || child.exitCode!==null) return;
  await new Promise(resolve=>{
    const timer=setTimeout(()=>child.kill('SIGKILL'),5000);
    child.once('exit',()=>{clearTimeout(timer);resolve();}); child.kill();
  });
}
(async()=>{
  let server,client;
  try {
    server=spawn(path.join(build,'pico_tls_server'),['0','a','settings'],{stdio:['pipe','pipe','inherit']});
    const serverLine=lines(server); assert.match(await serverLine(),/^READY /);
    client=spawn(path.join(build,'wtp_configuration_client'),[path.join(build,'credentials-v3')],{stdio:['pipe','pipe','inherit']});
    const clientLine=lines(client), calls=[];
    const call=async request=>{
      client.stdin.write(JSON.stringify(request)+'\n');
      const result=JSON.parse(await clientLine()); calls.push({request,result}); return result;
    };
    const control=async command=>{server.stdin.write(command+'\n');return serverLine();};
    const dump=async()=>JSON.parse((await control('DUMP')).replace(/^DUMP /,''));
    const exchange=async r=>{
      const result=await call({method:r.method,path:r.url,body:r.body?JSON.stringify(r.body):'',revision:r.headers?.['If-Match']||''});
      return response(result.body,result.status,result.etag);
    };
    const original=await dump(); assert(original.healthy);
    if(profile==='current') {
      const source=fs.readFileSync(path.join(pico,'tests/fixtures/pi-8454e21-wtp-management.js'),'utf8');
      assert.equal(crypto.createHash('sha256').update(source).digest('hex'),'08639d74049de9c9f66890423bd339cc86eaf328bce9a9004a42aeaec628f963');
      const old=browser(exchange,source); await old.click('wtp-remote-load');
      old.element('wtp-remote-callsign').value='K1ABC'; await old.click('wtp-remote-save');
      const captured=calls.at(-1); assert.equal(captured.request.method,'PUT');
      assert(!JSON.parse(captured.request.body).pins); assert.equal(captured.result.status,200);
      const persisted=await dump(); assert.equal(persisted.config.station.callsign,'K1ABC');
      assert.equal(persisted.config.pins.rf_gp,28); assert(persisted.config.allow_unqualified_frequency);
      assert(persisted.config.allow_non_amateur_frequency); assert.equal(persisted.password_sha256,original.password_sha256);
    }
    const b=browser(exchange);
    await b.click('wtp-remote-load');
    const context=calls.at(-1).result; assert.equal(context.status,200,JSON.stringify(context.body));
    assert.equal(context.body.update_strategy,profile==='current'?'merge':'legacy');
    const loaded=structuredClone(context.body.config);
    b.element('wtp-remote-callsign').value='W1AW';
    await b.click('wtp-remote-save');
    const saved=calls.at(-1).result; assert.equal(saved.status,200);
    const mutations=saved.exchanges.filter(e=>e.method!=='GET'); assert.equal(mutations.length,1);
    const wire=mutations[0], payload=JSON.parse(wire.body);
    if(profile==='current') {
      assert.equal(wire.method,'POST'); assert.equal(wire.resource,'config/update');
      assert.deepEqual(payload.config,{station:{callsign:'W1AW'}}); assert.equal(payload.target.device_id,'a'.repeat(32));
    } else {
      assert.equal(wire.method,'PUT'); assert.equal(wire.resource,'config');
      assert.equal(payload.wifi.password,null);
      if(profile==='legacy') assert(!Object.hasOwn(payload,'pins')&&!Object.hasOwn(payload,'allow_unqualified_frequency'));
      else { assert.equal(payload.pins.rf_gp,28); assert(payload.allow_unqualified_frequency&&payload.allow_non_amateur_frequency); }
    }
    let persisted=await dump(); assert.equal(persisted.config.station.callsign,'W1AW');
    assert.equal(persisted.cursor,original.cursor); assert.equal(persisted.password_sha256,original.password_sha256);
    if(profile!=='legacy') assert.deepEqual(persisted.config.pins,loaded.pins);
    // Schedule-only writes preserve unrelated operator drafts and settings.
    b.element('wtp-remote-password').value='explicit-new-password';
    b.element('wtp-remote-ssid').value='draft-network';
    b.element('wtp-remote-schedules').value='240 0';
    await b.click('wtp-schedules-save'); assert.equal(calls.at(-1).result.status,200);
    assert.equal(calls.at(-1).result.exchanges.filter(e=>e.method!=='GET')[0].resource,'schedules');
    assert.equal(b.element('wtp-remote-password').value,'explicit-new-password');
    persisted=await dump(); assert.equal(persisted.config.wifi.ssid,original.config.wifi.ssid);
    assert.deepEqual(persisted.config.schedules,[{period_s:240,phase_s:0}]);
    // Explicit credential/Wi-Fi/expiry changes are one compatible save.
    b.element('wtp-remote-expiry').value='1900000000';
    await b.click('wtp-remote-save'); assert.equal(calls.at(-1).result.status,200);
    assert.equal(b.element('wtp-remote-password').value,'');
    persisted=await dump(); assert.equal(persisted.config.wifi.ssid,'draft-network');
    assert.equal(persisted.config.expires_utc_s,1900000000);
    assert.equal(persisted.password_sha256,crypto.createHash('sha256').update('explicit-new-password').digest('hex'));
    // Ordinary enable/time-server edits and explicit reset preserve other fields.
    for (const enabled of [true,false]) {
      b.element('wtp-remote-enabled').checked=enabled;
      b.element('wtp-remote-ntp').value=enabled?'192.0.2.2':loaded.wifi.ntp_ipv4;
      await b.click('wtp-remote-save');
      const edited=calls.at(-1).result; assert.equal(edited.status,200);
      assert.equal(edited.exchanges.filter(e=>e.method!=='GET').length,1);
      persisted=await dump(); assert.equal(persisted.config.enabled,enabled);
      assert.equal(persisted.config.wifi.ntp_ipv4,enabled?'192.0.2.2':loaded.wifi.ntp_ipv4);
      assert.equal(persisted.cursor,original.cursor);
    }
    // Host-target and stale writers reject before any outbound mutation.
    const fresh=await call({method:'GET',path:'/api/v1/member/configuration'});
    const body={schema:'wsprrypi-pico-settings/1',target:fresh.body.target,operation:'save',config:{expires_utc_s:0}};
    let result=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify({...body,target:{...body.target,device_id:'b'.repeat(32)}}),revision:fresh.etag});
    assert.equal(result.status,409); assert.equal(result.exchanges.filter(e=>e.method!=='GET').length,0);
    result=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify(body),revision:fresh.etag}); assert.equal(result.status,200);
    result=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify(body),revision:fresh.etag}); assert.equal(result.status,412);
    if(profile!=='legacy') {
      const current=await call({method:'GET',path:'/api/v1/member/configuration'});
      const reset={...body,target:current.body.target,config:{allow_unqualified_frequency:false,allow_non_amateur_frequency:false,pins:{...loaded.pins,rf_gp:2}}};
      result=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify(reset),revision:current.etag}); assert.equal(result.status,200);
      const resetState=await dump(); assert(!resetState.config.allow_unqualified_frequency&&!resetState.config.allow_non_amateur_frequency);
      const pending=await call({method:'GET',path:'/api/v1/member/configuration'});
      assert.equal(pending.body.application.hardware.pending_restart,true);
      assert.equal(pending.body.application.hardware.saved.rf_gp,2);
      assert.equal(pending.body.application.hardware.active.rf_gp,28);
      const a=await call({method:'GET',path:'/api/v1/member/configuration'});
      const b=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify({...body,config:{expires_utc_s:1900000000}}),revision:a.etag});
      assert.equal(b.status,200);
      const back=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify(body),revision:b.etag});
      assert.equal(back.status,200);
      const aba=await call({method:'POST',path:'/api/v1/member/configuration',body:JSON.stringify(body),revision:a.etag});
      assert.equal(aba.status,412); assert.equal(aba.exchanges.filter(e=>e.method!=='GET').length,0);
    }
    // Actual Pico commit followed by a deliberately discarded client reply.
    await b.click('wtp-remote-load'); b.element('wtp-remote-callsign').value='K2ABC';
    b.element('wtp-remote-password').value='retained-password-draft';
    await call({control:'drop_mutation_reply'});
    await b.click('wtp-remote-save');
    assert.notEqual(calls.at(-1).result.status,200);
    assert.equal(calls.at(-1).result.exchanges.filter(e=>e.method!=='GET').length,1);
    assert(b.element('wtp-remote-save').disabled);
    assert.equal(b.element('wtp-remote-password').value,'retained-password-draft');
    assert.equal((await dump()).config.station.callsign,'K2ABC');
    const count=calls.length; await b.click('wtp-remote-save'); assert.equal(calls.length,count);
    await b.click('wtp-remote-load'); assert.equal(b.element('wtp-remote-callsign').value,'K2ABC');
    assert.equal(b.element('wtp-remote-password').value,'retained-password-draft');
    assert.match(b.element('wtp-management-feedback').textContent,/draft and password entry are preserved/);
    // Physical flash is replaced only by this test's deterministic RAM adapter.
    assert.equal(await control('FAIL'),'ACK');
    const before=await dump(); b.element('wtp-remote-callsign').value='K3ABC';
    await b.click('wtp-remote-save'); assert.equal(calls.at(-1).result.status,503);
    const after=await dump(); assert.deepEqual(after.config,before.config); assert.equal(after.cursor,before.cursor);
    console.log(`${profile}: actual UI/Pi adapters/HTTPS/Pico API/journal preservation, resets, revisions and ambiguous saves passed`);
  } finally {await stop(client);await stop(server);}
})().catch(error=>{console.error(error);process.exitCode=1;});

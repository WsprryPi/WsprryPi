// SPDX-License-Identifier: MIT
// UI producer -> protected classification/browser -> production runtime/gate ->
// actual engineering TLS or Consumer-Radio station/AP crypto -> Pico/journal.
'use strict';
const assert=require('node:assert/strict'), fs=require('node:fs'), path=require('node:path'), readline=require('node:readline');
const {spawn}=require('node:child_process');
const {browser,response}=require('./wtp_management_fixture.js');
const build=path.resolve(process.argv[2]);
const trace=(...args)=>{if(process.env.WSPRRYPI_INTEROP_TRACE)console.error(...args);};
function lines(child) {
  const queue=[],waiters=[];
  readline.createInterface({input:child.stdout}).on('line',line=>waiters.length?waiters.shift()(line):queue.push(line));
  return ()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(Error('Resource fixture deadline')),45000);
    waiters.push(line=>{clearTimeout(timer);resolve(line);});
  });
}
async function stop(child) {
  if (!child || child.exitCode!==null) return;
  await new Promise(resolve=>{const timer=setTimeout(()=>child.kill('SIGKILL'),3000);child.once('exit',()=>{clearTimeout(timer);resolve();});child.kill();});
}
async function cases(carrier, failure="storage") {
  const consumer=carrier!=='engineering_mtls'; let server,client;
  try {
    let serverLine;
    if (!consumer) {
      server=spawn(path.join(build,'pico_tls_server'),['0','a',failure==='blank'?'blank':'settings'],{stdio:['pipe','pipe','inherit']});
      serverLine=lines(server); assert.match(await serverLine(),/^READY /);
    }
    client=spawn(path.join(build,consumer?'consumer_interop':'wtp_configuration_client'),consumer?['--stdio',(carrier==='consumer_ap'?'ap':'station')+(failure==='blank'?'-blank':'')]:[path.join(build,'credentials-v3')],{stdio:['pipe','pipe','inherit']});
    const next=lines(client),calls=[];
    if (consumer) assert.match(await next(),/^READY /);
    const call=async request=>{trace(carrier,request.method||request.control,request.path||request.corruption);if(request.headers){request.headers={...request.headers};if(Object.hasOwn(request,'revision'))request.headers['If-Match']=request.revision;if(Object.hasOwn(request,'member_context'))request.headers['X-WsprryPi-Member-Context']=request.member_context;}client.stdin.write(JSON.stringify(request)+'\n');const result=JSON.parse(await next());calls.push({request,result});return result;};
    const control=async name=> {
      trace(carrier,"server",name);
      if (consumer) return call({control:name});
      server.stdin.write(({dump:'DUMP',stop:'STOP',fail_storage:'FAIL',fail_disable:'DISABLE_FAIL'}[name]||name)+'\n');
      const value=await serverLine();return value.startsWith('DUMP ')?JSON.parse(value.slice(5)):value;
    };
    const b=browser(async r=>{
      const result=await call({method:r.method,path:r.url,body:r.body?JSON.stringify(r.body):'',revision:r.headers?.['If-Match']||'',member_context:r.headers?.['X-WsprryPi-Member-Context']||'',headers:r.headers||{}});
      return response(result.body,result.status,result.etag);
    });
    if (consumer) {
      b.element('wtp-management-carrier').value=carrier;
      b.element('wtp-management-host').value=carrier==='consumer_ap'?'192.168.4.1':'192.168.1.53';
      b.element('wtp-management-password').value='test-password';await b.click('wtp-management-connect');
      assert.equal(calls.at(-1).result.status,200);
    }
    const load=async()=>{await b.click('wtp-member-load');const r=calls.at(-1).result;assert.equal(r.status,200,JSON.stringify(r));return r;};
    let loaded=await load(); if(failure==="storage")fs.writeFileSync(path.join(build,`resources-loaded-${carrier}.json`),JSON.stringify(loaded.body)); const initial=await control('dump');
    assert.equal(loaded.body.profile_owned_station,consumer);
    const extendedFormats=loaded.body.application.schema==='transmitter-application/2';
    assert.match(b.element('wtp-member-station-hint').textContent, extendedFormats ? /automatically selects Type 1, Type 2 or Type 3/ : /Type 1 station writes only/);
    assert.equal(b.element('wtp-member-station-hint').textContent.includes('measured RF output'),true);
    const remoteResource=r=>extendedFormats&&['application','station'].includes(r)?r+'/2':r;
    if (consumer) {
      assert.match(b.element('wtp-member-active').textContent,/Unknown/);
      assert(b.element('wtp-member-station-save').disabled);assert.match(b.element('wtp-member-station-feedback').textContent,/setup owns/);
      assert.equal(initial.wtp_io,0);
    }
    if(failure==='blank') {
      assert.equal(loaded.body.application.hardware.active,null);assert.equal(loaded.body.application.hardware.saved,null);
      assert.match(b.element('wtp-member-active').textContent,/Unknown/);assert(b.element('wtp-member-hardware-save').disabled);
      assert(!b.element('wtp-member-validate').disabled);assert.equal(b.element('wtp-member-rf').value,'');
      b.element('wtp-member-rf').value='3';b.element('wtp-member-button').value='';b.element('wtp-member-indicator').value='onboard_led';b.element('wtp-member-polarity').value='true';
      const before=await control('dump');await b.click('wtp-member-validate');assert.equal(calls.at(-1).result.status,200,JSON.stringify(calls.at(-1)));
      assert.deepEqual(await control('dump'),before);assert(b.element('wtp-member-hardware-save').disabled);
      console.log(`${carrier}: actual unconfigured/null active resources, explicit candidate preview without defaults or persistence, unavailable save passed`);
      return;
    }
    if(failure==='disable') {
      b.element('wtp-member-rf').value='3';await b.click('wtp-member-validate');assert.equal(calls.at(-1).result.status,200);
      const before=await control('dump');await control('fail_disable');await b.click('wtp-member-hardware-save');
      assert.equal(calls.at(-1).result.status,503,JSON.stringify(calls.at(-1)));assert.equal(calls.at(-1).result.body.error.code,'output_disable_failed');
      assert.match(b.element('wtp-member-hardware-feedback').textContent,/may be saved/);assert(b.element('wtp-member-hardware-save').disabled);
      const saved=await control('dump');assert.equal(saved.config.pins.rf_gp,3);assert.notEqual(saved.generation,before.generation);
      assert(saved.pending_restart&&saved.inhibit);assert.equal(saved.cursor,before.cursor);
      loaded=await load();assert.equal(loaded.body.application.hardware.application_error,'output_disable_failed');
      assert.equal(loaded.body.application.hardware.active,consumer?null:loaded.body.application.hardware.active);
      if(!consumer)assert.equal(loaded.body.application.hardware.active.rf_gp,28);
      assert(b.element('wtp-member-hardware-save').disabled);assert(!b.element('wtp-member-validate').disabled);
      console.log(`${carrier}: actual post-commit output_disable_failed, saved/active readback, preserved cursor, inhibit and UI reconciliation passed`);
      return;
    }
    if (!consumer) {
      // Corrupt actual authenticated handler readbacks, then prove zero mutation.
      const a=loaded.body.application;
      const variants=[
        ['application','/target/boot_id','b'.repeat(32)],
        ['application','/station/station/callsign','ABC'],
        ['application','/station/station/locator','FN42ab'],
        ['application','/station/station/power_dbm',20.5],
        ['application','/station/saved_generation',9007199254740993],
        ['application','/station/saved_generation','18446744073709551616'],
        ['application','/station/saved_generation','01'],
        ['application','/station/saved_generation','2'],
        ['application','/hardware/storage_healthy',false],
        ['application','/hardware/active_revision',null],
        ['application','/hardware/pending_restart',false],
        ['application','/hardware/active/rf_gp',7],
        ['application','/hardware/saved/rf_gp',14],
        ['application','/hardware/active/indicator_active_high',1],
        ['application','/hardware/application_error','unknown_fault'],
        ['application','/recurrence/expires_utc_s','18446744073709551616'],
        ['application','/recurrence',{...a.recurrence,enabled:true,schedules:[]}],
        ['application','/recurrence/schedules',[{period_s:240,phase_s:0},{period_s:120,phase_s:0}]],
        ['application','/capabilities/station_encoding','wspr_type2'],
        ['capabilities',extendedFormats?'/application_resources_v2/schema':'/application_resources/schema',extendedFormats?'transmitter-application/99':'transmitter-application/2'],
        ['capabilities',extendedFormats?'/application_resources_v2':'/application_resources',null],
        ['capabilities','/features/consumer_setup_identity',true],
        ['management','/selected','consumer_ap'],
        ['pins','/active/rf_gp',14],
        ['pins','/choices/adapter_support/i2c_rf',true],
        ['pins','/choices/i2c_pairs/0/controller',1],
      ];
      for (const [resource,pointer,value] of variants) {
        // Unchanged false is valid; exercise the impossible active/saved divergence separately.
        if(resource==='application'&&pointer==='/hardware/pending_restart')continue;
        await call({control:'corrupt',corruption:{resource:remoteResource(resource),pointer,value}});
        await b.click('wtp-member-load');assert.notEqual(calls.at(-1).result.status,200,`${resource}${pointer}`);
        assert.deepEqual(await control('dump'),initial);
      }
      for(const [resource,pointer,value] of [['station','/station/power_dbm',20],['hardware','/saved/rf_gp',28],['pins','/choices/eligible_gps/0',0],['application','/capabilities/max_update_bytes',1024]]) {
        await call({control:'corrupt',corruption:{resource:remoteResource(resource),pointer,value,as_float:true}});
        await b.click('wtp-member-load');assert.notEqual(calls.at(-1).result.status,200,`${resource}${pointer} float`);assert.deepEqual(await control('dump'),initial);
      }
      for(const option of ['duplicate','oversized']) {
        await call({control:'corrupt',corruption:{resource:remoteResource('application'),pointer:'/schema',value:'transmitter-application/1',[option]:true}});
        await b.click('wtp-member-load');assert.notEqual(calls.at(-1).result.status,200);assert.deepEqual(await control('dump'),initial);
      }
      await call({control:'clear'});loaded=await load();
    }
    // Preview the actual UI-produced candidate; state and byte-level journal are unchanged.
    b.element('wtp-member-rf').value='3';b.element('wtp-member-button').value='14';
    b.element('wtp-member-indicator').value='external';b.element('wtp-member-indicator-gp').value='4';b.element('wtp-member-polarity').value='false';
    const before=await control('dump');await b.click('wtp-member-validate');
    let last=calls.at(-1);assert.equal(last.result.status,200,JSON.stringify(last.result));
    assert.deepEqual(last.result.body.applied,false);assert(last.result.body.valid&&last.result.body.operational_supported);
    assert.deepEqual(await control('dump'),before);assert(!b.element('wtp-member-hardware-save').disabled);
    const preview=structuredClone(last.request);
    if(!consumer) {
      await call({control:'corrupt',corruption:{resource:'pins/validate',method:'POST',pointer:'/applied',value:false,oversized:true}});
      const oversized=await call(preview);assert.notEqual(oversized.status,200);
      assert.equal(oversized.body.error.code,'invalid_pin_validation');assert.equal(oversized.body.mutation_attempted,false);
      assert.deepEqual(await control('dump'),before);
      await call({control:'clear'});
    }
    b.element('wtp-member-rf').value='14';await b.click('wtp-member-validate');assert.equal(calls.at(-1).result.body.valid,false);
    assert(b.element('wtp-member-hardware-save').disabled);assert.deepEqual(await control('dump'),before);
    let plan=JSON.parse(preview.body);
    const unsupported={...plan,engine:'i2c',rf_gp:null,i2c_pair:0};
    let r=await call({...preview,body:JSON.stringify(unsupported)});assert.equal(r.status,200);assert(r.body.valid&&!r.body.operational_supported);
    assert.deepEqual(await control('dump'),before);
    // Missing protected mutation intent is refused before runtime/carrier dispatch.
    r=await call({...preview,mutation_context:false});assert.equal(r.status,403);assert.deepEqual(await control('dump'),before);
    for(const negative of [{peer:'198.51.100.2'},{extra_headers:[['X-WsprryPi-Member-Context',preview.member_context]]},{extra_headers:[['Origin','http://foreign.local']]}]) {
      r=await call({...preview,...negative});assert.notEqual(r.status,200);assert.deepEqual(await control('dump'),before);
    }
    // Unknown, duplicate, oversized and malformed candidate bodies have zero mutations.
    for(const body of [JSON.stringify({...plan,unknown:true}),'{"engine":"direct","engine":"i2c"}', ' '.repeat(2049), JSON.stringify({...plan,rf_gp:3.5})]) {
      r=await call({...preview,body});assert.notEqual(r.status,200);assert.deepEqual(await control('dump'),before);
    }
    // The UI never autosaves preview. Explicit complete hardware save consumes one journal generation.
    b.element('wtp-member-rf').value='3';await b.click('wtp-member-validate');
    await b.click('wtp-member-hardware-save');last=calls.at(-1);assert.equal(last.result.status,200,JSON.stringify(last.result));
    if(!consumer)assert.equal(last.result.host_ready,false);
    const hardwareWire=structuredClone(last.request);assert.equal(JSON.parse(hardwareWire.body).pins.indicator_active_high,false);
    const saved=await control('dump');assert.equal(saved.config.pins.rf_gp,3);assert.equal(saved.config.pins.indicator_active_high,false);
    assert.equal(saved.cursor,initial.cursor);assert.equal(saved.config.station.callsign,initial.config.station.callsign);
    assert.deepEqual(saved.config.schedules,initial.config.schedules);assert(saved.inhibit&&saved.pending_restart);
    assert(b.element('wtp-member-station-save').disabled&&b.element('wtp-member-hardware-save').disabled);
    r=await call(hardwareWire);assert.equal(r.status,409);assert.deepEqual(await control('dump'),saved);
    loaded=await load();assert(loaded.body.application.hardware.pending_restart);
    if(consumer)assert.equal(loaded.body.application.hardware.active,null);
    else {assert.equal(loaded.body.application.hardware.active.rf_gp,initial.config.pins.rf_gp);assert.equal(loaded.body.application.hardware.saved.rf_gp,3);assert.match(b.element('wtp-member-active').textContent,/GP28/);assert.match(b.element('wtp-member-saved').textContent,/GP3/);}
    assert.match(b.element('wtp-member-restart').textContent,/latched/);
    fs.writeFileSync(path.join(build,`resources-pending-${carrier}.json`),JSON.stringify(loaded.body));
    // Cross-resource stale contexts, target mismatch and no Config fallback.
    for(const changes of [p=>{p.target.device_id='b'.repeat(32);},p=>{p.target.boot_id='b'.repeat(32);},p=>{p.target.boot_id=9007199254740993;},p=>{p.schema='transmitter-hardware/2';},p=>{p.pins.amplifier_gp=5;}]) {
      const fresh=await load(),payload=JSON.parse(hardwareWire.body);changes(payload);
      r=await call({...hardwareWire,body:JSON.stringify(payload),revision:fresh.etag,member_context:fresh.body.member_context});
      assert.notEqual(r.status,200);assert.deepEqual(await control('dump'),saved);
    }
    // Existing complete station and legacy editors remain distinct.
    await b.click('wtp-remote-load');assert.equal(calls.at(-1).result.status,200);
    b.element('wtp-remote-password').value='preserved-credential-draft';
    loaded=await load();
    b.element('wtp-member-callsign').value='K1ABC';b.element('wtp-member-locator').value='FN42AB';b.element('wtp-member-power').value='10';
    if (!consumer) {
      await b.click('wtp-member-station-save');assert.equal(calls.at(-1).result.status,200,JSON.stringify(calls.at(-1)));
      const stationWire=structuredClone(calls.at(-1).request);assert.equal(b.element('wtp-remote-password').value,'preserved-credential-draft');
      assert(b.element('wtp-remote-save').disabled);assert(b.element('wtp-member-hardware-save').disabled);
      let state=await control('dump');assert.equal(state.config.station.locator,'FN42AB');assert(state.pending_restart&&state.inhibit);
      assert.equal(state.password_sha256,initial.password_sha256);
      loaded=await load();
      if(extendedFormats) {
        for(const [scenario,callsign,locator,types] of [['type1','AA0NT','EM18',['type1']],['ordinary-six','AA0NT','EM18IG',['type1']],['type2','K1ABC/P','FN42',['type2']],['type3','<AA0NT>','EM18IG',['type3']],['pair','PJ4/AA0NT','EM18IG',['type2','type3']],['w1a','W1A/AA0NT','EM18IG',['type2','type3']],['w1a-type3','<W1A/AA0NT>','EM18IG',['type3']],['ambiguity','AA0NT/09','EM18IG',['type2','type3']]]) {
          loaded=await load();b.element('wtp-member-callsign').value=callsign;b.element('wtp-member-locator').value=locator;
          await b.click('wtp-member-station-save');assert.equal(calls.at(-1).result.status,200,JSON.stringify(calls.at(-1)));
          assert.equal(JSON.parse(calls.at(-1).request.body).schema,'transmitter-station/2');
          state=await control('dump');assert.equal(state.config.station.callsign,callsign);assert.equal(state.config.station.locator,locator);
          assert.equal(state.cursor,initial.cursor);assert.equal(state.password_sha256,initial.password_sha256);assert.deepEqual(state.config.schedules,initial.config.schedules);
          loaded=await load();assert.deepEqual(loaded.body.application.wspr_plan.frame_types,types);
          fs.writeFileSync(path.join(build,`resources-wspr-${scenario}.json`),JSON.stringify(loaded.body));
          if(callsign==='AA0NT/09'){assert.equal(loaded.body.application.wspr_plan.type2_ambiguous,true);assert.equal(loaded.body.application.wspr_plan.alternate_callsign,'AA0NT/Z');}
        }
      } else {
        const extended=JSON.parse(stationWire.body);extended.station.callsign='K1ABC/P';
        r=await call({...stationWire,body:JSON.stringify(extended),revision:loaded.etag,member_context:loaded.body.member_context});
        assert.equal(r.status,400);assert.equal(r.body.error.code,'unsupported_station_encoding');assert.deepEqual(await control('dump'),state);
      }
      // A successful legacy write invalidates the shared editor context.
      loaded=await load();await b.click('wtp-remote-load');b.element('wtp-remote-callsign').value='W1AW';
      b.element('wtp-remote-password').value='';await b.click('wtp-remote-save');assert.equal(calls.at(-1).result.status,200);
      r=await call({...hardwareWire,revision:loaded.etag,member_context:loaded.body.member_context});assert.equal(r.status,409);
      // The supported aliases can refresh and write after an invalidated shared context.
      const alias=await call({method:'GET',path:'/api/v1/schedules'});
      r=await call({method:'PUT',path:'/api/v1/schedules',body:JSON.stringify({schedules:state.config.schedules}),revision:alias.etag});assert.equal(r.status,200);
    } else {
      const count=calls.length;await b.click('wtp-member-station-save');assert.equal(calls.length,count);
      const payload={schema:extendedFormats?'transmitter-station/2':'transmitter-station/1',target:loaded.body.target,station:{callsign:'K1ABC',locator:'FN42AB',power_dbm:10}};
      r=await call({method:'PUT',path:'/api/v1/member/station',body:JSON.stringify(payload),revision:loaded.etag,member_context:loaded.body.member_context});
      assert.equal(r.status,409);assert.equal(r.body.error.code,'consumer_setup_identity_required');
    }
    // Restore the old pins: restart/output inhibition must remain latched.
    loaded=await load();b.element('wtp-member-rf').value=String(consumer?2:28);
    await b.click('wtp-member-validate');await b.click('wtp-member-hardware-save');assert.equal(calls.at(-1).result.status,200);
    assert((await control('dump')).pending_restart);
    // STOP suspension survives a complete supported hardware save.
    await control('stop');loaded=await load();await b.click('wtp-member-validate');
    await b.click('wtp-member-hardware-save');assert.equal(calls.at(-1).result.status,200);
    const stopped=await control('dump');assert.equal(consumer?stopped.scheduler.suspended:stopped.suspended,true);assert.equal(stopped.cursor,initial.cursor);
    if(consumer) {
      loaded=await load();b.element('wtp-member-rf').value='6';
      const preExpiry=await control('dump');await control('expire');await b.click('wtp-member-load');
      assert.notEqual(calls.at(-1).result.status,200);assert.match(b.element('wtp-member-feedback').textContent,/expired|unconfirmed/);
      assert.equal(b.element('wtp-member-rf').value,'6');assert.deepEqual(await control('dump'),preExpiry);
      b.element('wtp-management-password').value='test-password';await b.click('wtp-management-connect');assert.equal(calls.at(-1).result.status,200);
    }
    if(consumer) {
      loaded=await load();const before=await control('dump');await control('change_profile');await b.click('wtp-member-load');
      assert.notEqual(calls.at(-1).result.status,200);assert.deepEqual(await control('dump'),before);assert.equal(b.element('wtp-member-rf').value,'6');
      await control('advance');b.element('wtp-management-password').value='test-password';await b.click('wtp-management-connect');assert.equal(calls.at(-1).result.status,200);
      loaded=await load();assert.equal(loaded.body.profile_generation,'2');
    }
    // A dropped post-commit response never causes replay or fallback.
    loaded=await load();b.element('wtp-member-rf').value='5';await b.click('wtp-member-validate');
    await call({control:'drop_mutation_reply'});await b.click('wtp-member-hardware-save');assert.notEqual(calls.at(-1).result.status,200);
    assert.equal((await control('dump')).config.pins.rf_gp,5);assert(b.element('wtp-member-hardware-save').disabled);
    const count=calls.length;await b.click('wtp-member-hardware-save');assert.equal(calls.length,count);
    if(consumer) {
      await control('expire');b.element('wtp-management-password').value='test-password';await b.click('wtp-management-connect');assert.equal(calls.at(-1).result.status,200);
    }
    loaded=await load();assert.equal(loaded.body.application.hardware.saved.rf_gp,5);
    assert.equal(b.element('wtp-member-rf').value,'5');assert.equal(b.element('wtp-remote-password').value,consumer?'preserved-credential-draft':'');
    // Carrier selection invalidates an otherwise unchanged revision/context.
    const old=loaded;
    await call({method:'POST',path:'/api/v1/member/management',body:JSON.stringify({action:consumer?'disconnect':'engineering_mtls'})});
    r=await call({...hardwareWire,revision:old.etag,member_context:old.body.member_context});assert.equal(r.status,409);
    if(consumer) {await control('advance');b.element('wtp-management-password').value='test-password';await b.click('wtp-management-connect');assert.equal(calls.at(-1).result.status,200,JSON.stringify(calls.at(-1).result));}
    if(!consumer) {
      loaded=await load();b.element('wtp-member-power').value='13';
      await call({control:'corrupt',corruption:{resource:remoteResource('station'),method:'PUT',pointer:'/station/power_dbm',value:13,as_float:true}});
      await b.click('wtp-member-station-save');assert.notEqual(calls.at(-1).result.status,200);assert.equal(calls.at(-1).result.body.mutation_attempted,true);
      assert.equal((await control('dump')).config.station.power_dbm,13);assert(b.element('wtp-member-station-save').disabled);
      await call({control:'clear'});loaded=await load();assert.equal(loaded.body.application.station.station.power_dbm,13);
    }
    loaded=await load();await b.click('wtp-member-validate');const bytes=(await control('dump')).bytes_sha256;
    await control('fail_storage');await b.click('wtp-member-hardware-save');assert.equal(calls.at(-1).result.status,503);
    assert.equal((await control('dump')).bytes_sha256,bytes);
    loaded=await load();fs.writeFileSync(path.join(build,`resources-fault-${carrier}.json`),JSON.stringify(loaded.body));assert.equal(loaded.body.application.hardware.storage_healthy,false);assert(b.element('wtp-member-hardware-save').disabled);
    if(consumer) assert.equal((await control('dump')).wtp_io,0);
    console.log(`${carrier}: UI, protected routes, production runtime/coordinator, actual Pico/journal, preview, saved/active, stale context, profile/encoder, STOP/cursors, dropped reply and storage failure passed`);
  } finally {await stop(client);await stop(server);}
}
(async()=>{for(const carrier of ['engineering_mtls','consumer_station','consumer_ap']){await cases(carrier);await cases(carrier,'disable');await cases(carrier,'blank');}})().catch(e=>{console.error(e);process.exitCode=1;});

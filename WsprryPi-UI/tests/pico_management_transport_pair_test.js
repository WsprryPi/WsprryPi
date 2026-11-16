// SPDX-License-Identifier: MIT
// Production independent management transport -> actual Pico TLS/API/journals.
// This fixture does not expose or qualify the unfinished target-bound HTTP API.
'use strict';
const crypto=require('node:crypto');
const assert=require('node:assert/strict'),path=require('node:path'),readline=require('node:readline');
const {spawnFixtureChild:spawn,finishChildren}=require('./browser_fixture_process.js');
const [build,clientBinary]=process.argv.slice(2);
if(!build||!clientBinary)throw Error('Explicit Pico build and Pi management client paths required');
function lines(child) {
    const queued=[],pending=[];
    readline.createInterface({input:child.stdout}).on('line',line=>pending.length?pending.shift()(line):queued.push(line));
    return (timeout=45000)=>queued.length?Promise.resolve(queued.shift()):new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(Error('Independent management reply deadline')),timeout);
        pending.push(line=>{clearTimeout(timer);resolve(line);});
    });
}
(async()=>{
    let server,client,primary;
    try {
        server=spawn(path.join(build,'pico_tls_server'),['0','a','settings'],{stdio:['pipe','pipe','inherit']});
        const serverLine=lines(server);assert.match(await serverLine(),/^READY /);
        const dump=async()=>{server.stdin.write('DUMP\n');return JSON.parse((await serverLine()).replace(/^DUMP /,''));};
        const before=await dump();
        client=spawn(clientBinary,[path.join(build,'credentials-v3')],{stdio:['pipe','pipe','inherit']});
        const clientLine=lines(client);assert.equal(await clientLine(),'READY');
        const call=async request=>{
            if(request.method&&request.method!=='GET'&&!request.operation_id)request.operation_id=crypto.randomBytes(16).toString('hex');
            client.stdin.write(JSON.stringify(request)+'\n');
            const result=JSON.parse(await clientLine());
            assert.equal(result.execution_ready,false,'Management must never construct/select an execution runtime');
            assert.equal(result.binding.device_id,'a'.repeat(32));
            return result;
        };
        const first=await call({resource:'configuration',method:'GET'});
        assert.equal(first.status,200,JSON.stringify(first));
        assert.equal(first.body.config.station.callsign,before.config.station.callsign);
        assert.deepEqual(await dump(),before,'Settings reads must preserve the exact journal and output state');
        const settings={contract:'Wsprry-Control/1',schema:'pico-settings',target:first.body.target,operation:'save',config:{station:{callsign:'W1AW'}}};
        const wrong=await call({resource:'configuration',method:'POST',body:JSON.stringify({...settings,target:{...settings.target,device_id:'b'.repeat(32)}}),revision:first.etag});
        assert.equal(wrong.status,409);assert.deepEqual(await dump(),before);
        assert.equal((await call({resource:'configuration',method:'GET'})).status,200);
        const stale=await call({resource:'configuration',method:'POST',body:JSON.stringify(settings),revision:'"stale"'});
        assert.equal(stale.status,412);assert.deepEqual(await dump(),before);
        assert.equal((await call({resource:'configuration',method:'GET'})).status,200);
        const savedRequest={resource:'configuration',method:'POST',body:JSON.stringify(settings),revision:first.etag};
        const saved=await call(savedRequest);
        assert.equal(saved.status,200,JSON.stringify(saved));
        const repeated=await call(savedRequest);assert.equal(repeated.status,200);assert.equal(repeated.mutations,saved.mutations);assert.deepEqual(repeated.body,saved.body);
        const conflict=await call({...savedRequest,body:JSON.stringify({...settings,config:{station:{callsign:'W2AW'}}})});assert.equal(conflict.status,409);assert.equal(conflict.mutations,saved.mutations);
        const after=await dump();assert.equal(after.config.station.callsign,'W1AW');
        for(const key of ['cursor','password_sha256','inhibit','pending_restart','disables','prepares','suspended'])assert.deepEqual(after[key],before[key],key);
        const readback=await call({resource:'configuration',method:'GET'});
        assert.equal(readback.status,200);assert.equal(readback.body.config.station.callsign,'W1AW');
        const unsupported=await call({resource:'jobs',method:'POST',body:'{}'});
        assert.equal(unsupported.status,404);assert.deepEqual(await dump(),after);
        const badContext=await call({resource:'configuration',method:'GET',context:'0'.repeat(32)});assert.equal(badContext.status,409);
        client.stdin.write(JSON.stringify({control:'drop_reply'})+'\n');assert.deepEqual(JSON.parse(await clientLine()),{});
        const lostRequest={resource:'configuration',method:'POST',body:JSON.stringify({...settings,config:{station:{callsign:'K2ABC'}}}),revision:readback.etag};
        const lost=await call(lostRequest);assert.equal(lost.status,503);assert.equal(lost.body.state,'unknown');
        assert.equal(lost.recovery_operation,lostRequest.operation_id);assert.equal(lost.recovery_resource,'configuration');
        const blocked=await call({...lostRequest,operation_id:crypto.randomBytes(16).toString('hex')});
        assert.equal(blocked.status,503);assert.equal(blocked.body.error.code,'recovery_required');assert.equal(blocked.mutations,lost.mutations);
        assert.equal(blocked.recovery_operation,lostRequest.operation_id);assert.equal(blocked.recovery_resource,'configuration');
        const lostAgain=await call(lostRequest);assert.equal(lostAgain.status,409);assert.equal(lostAgain.mutations,lost.mutations);
        assert.equal((await dump()).config.station.callsign,'K2ABC');
        const staleReview=await call({control:'reconcile',operation_id:lostRequest.operation_id,readback_revision:readback.readback_revision});assert.equal(staleReview.status,409);
        const unrelated=await call({resource:'application',method:'GET'});assert.equal(unrelated.status,200);
        const wrongReview=await call({control:'reconcile',operation_id:lostRequest.operation_id,readback_revision:unrelated.readback_revision});assert.equal(wrongReview.status,409);
        const fresh=await call({resource:'configuration',method:'GET'});assert.equal(fresh.body.config.station.callsign,'K2ABC');
        const reviewed=await call({control:'reconcile',operation_id:lostRequest.operation_id,readback_revision:fresh.readback_revision});
        assert.equal(reviewed.status,200);assert.equal(reviewed.body.state,'unknown');assert.equal(reviewed.mutations,lost.mutations);
        const afterReview=await call({resource:'configuration',method:'POST',body:JSON.stringify({...settings,config:{station:{callsign:'W3AW'}}}),revision:fresh.etag});
        assert.equal(afterReview.status,200);assert.equal(afterReview.mutations,lost.mutations+1);
        client.stdin.write(JSON.stringify({control:'seed_terminal_capacity'})+'\n');assert.deepEqual(JSON.parse(await clientLine(240000)),{});
        let capacityRead=await call({resource:'configuration',method:'GET'});
        const oldestSeed=crypto.createHash('sha256').update('seed-0').digest('hex').slice(0,32);
        assert.equal((await call({control:'receipt',operation_id:oldestSeed})).status,200);
        client.stdin.write(JSON.stringify({control:'drop_reply'})+'\n');assert.deepEqual(JSON.parse(await clientLine()),{});
        const atCapacity={resource:'configuration',method:'POST',body:JSON.stringify({...settings,config:{station:{callsign:'W6AW'}}}),revision:capacityRead.etag};
        const capacityUnknown=await call(atCapacity);assert.equal(capacityUnknown.status,503);assert.equal(capacityUnknown.body.state,'unknown');
        assert.equal(capacityUnknown.context,capacityRead.context,'An unresolved new operation must not evict a retained terminal receipt');
        assert.equal((await call({control:'receipt',operation_id:oldestSeed})).status,200,'Keep all 256 terminal receipts while the new outcome is unresolved');
        const capacityFresh=await call({resource:'configuration',method:'GET'});
        const capacityReviewed=await call({control:'reconcile',operation_id:atCapacity.operation_id,readback_revision:capacityFresh.readback_revision});
        assert.equal(capacityReviewed.status,200);assert.equal(capacityReviewed.body.state,'unknown');assert.notEqual(capacityReviewed.context,capacityFresh.context,'Review that evicts a receipt must retire the edit context');
        assert.equal((await call({control:'receipt',operation_id:oldestSeed})).status,404);
        assert.equal((await call({resource:'configuration',method:'GET',context:capacityFresh.context})).status,409);
        capacityRead=await call({resource:'configuration',method:'GET'});

        const capacitySave=await call({resource:'configuration',method:'POST',body:JSON.stringify({...settings,config:{station:{callsign:'W4AW'}}}),revision:capacityRead.etag});
        assert.equal(capacitySave.status,200);assert.notEqual(capacitySave.context,capacityRead.context);
        const expiredRequest=await call({...savedRequest,context:capacityRead.context});
        assert.equal(expiredRequest.status,409);assert.equal(expiredRequest.mutations,capacitySave.mutations);
        const beforeStorageFault=await call({resource:'configuration',method:'GET'});
        client.stdin.write(JSON.stringify({control:'fail_receipt_storage'})+'\n');assert.deepEqual(JSON.parse(await clientLine()),{});
        const faultRequest={resource:'configuration',method:'POST',body:JSON.stringify({...settings,config:{station:{callsign:'W5AW'}}}),revision:beforeStorageFault.etag};
        const fault=await call(faultRequest);assert.equal(fault.status,503);assert.equal(fault.body.error.code,'storage_unavailable');
        assert.equal(fault.recovery_operation,faultRequest.operation_id);assert.equal(fault.recovery_resource,'configuration');
        assert.equal((await dump()).config.station.callsign,'W5AW','A receipt persistence failure must not be mistaken for a refused remote write');
        client.stdin.write(JSON.stringify({control:'repair_receipt_storage'})+'\n');assert.deepEqual(JSON.parse(await clientLine()),{});
        const faultRead=await call({resource:'configuration',method:'GET'});assert.equal(faultRead.status,200);
        const faultHeld=await call({...faultRequest,operation_id:crypto.randomBytes(16).toString('hex')});assert.equal(faultHeld.status,503);assert.equal(faultHeld.body.error.code,'storage_unavailable');assert.equal(faultHeld.mutations,fault.mutations);
        // Restoring file permissions does not erase the warm authority's change
        // detection. The service stays held until cold authoritative recovery.
        console.log('PASS: independent Pico settings transport, authoritative save/readback, wrong-target/revision refusal, unchanged execution/output and journals on rejection');
    } catch(error){primary=error;}
    await finishChildren({failure:primary,children:[client,server]});
})().catch(error=>{console.error(error);process.exitCode=1;});

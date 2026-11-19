// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict');
const {webcrypto}=require('node:crypto');
const {create}=require('../data/configuration-recovery-client.js');
const vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=family=>structuredClone(vectors.find(item=>item.family===family&&item.valid).value);
async function main() {
    const intent=sample('target-migration'),device=intent.binding.device_id,calls=[];
    const context=sample('target-context'),migration=sample('target-migration-read');
    const receipt=sample('target-operation');
    receipt.binding=structuredClone(intent.binding);receipt.operation_id=intent.operation_id;
    const csrf='c'.repeat(64);
    const client=create({crypto:webcrypto,fetch:async(url,options)=>{
        calls.push({url,options});
        if(url.endsWith('/host/management-session'))return new Response(JSON.stringify(sample('controller-browser-session')),{headers:{'X-WsprryPi-CSRF':csrf}});
        const value=url.endsWith('/context')?context:options.method==='POST'||url.includes('/operations/')?receipt:migration;
        return new Response(JSON.stringify(value),{status:200,headers:{ETag:'"'+'a'.repeat(64)+'"'}});
    }});
    await client.targetRequest(device,'context');await client.targetRequest(device,'migration');
    assert.equal(calls.length,2,'Reads must not create a browser session or save');
    await assert.rejects(client.targetRequest(device,'../migration'),/unsupported_resource/);
    await assert.rejects(client.targetRequest(device,'migration',{method:'PUT',body:intent}),/unsupported_resource/);
    await assert.rejects(client.targetRequest('bad','migration'),/target_mismatch/);
    const wrong=structuredClone(intent);wrong.binding.device_id='f'.repeat(32);
    await assert.rejects(client.targetRequest(device,'migration',{method:'POST',body:wrong}),/candidate_invalid/);
    const before=JSON.stringify(intent);
    await client.targetRequest(device,'migration',{method:'POST',body:intent});
    await client.targetRequest(device,'migration',{method:'POST',body:intent});
    const writes=calls.filter(item=>item.url.endsWith('/migration')&&item.options.method==='POST');
    assert.equal(writes.length,2);assert(writes.every(item=>item.options.body===before));
    assert(writes.every(item=>item.options.headers['If-Match']==='"'+intent.expected_revision+'"'&&item.options.headers['X-WsprryPi-CSRF']===csrf));
    assert(writes.every(item=>item.options.credentials==='same-origin'&&item.options.redirect==='error'));
    assert.equal(JSON.stringify(intent),before,'Retry cannot rebase the accepted intent');
    const lookup=await client.targetRequest(device,'operations/'+intent.operation_id);assert.deepEqual(lookup.data,receipt);
    const malformed=create({crypto:webcrypto,fetch:async()=>new Response(JSON.stringify(migration).replace('"schema":"target-migration"','"schema":"target-migration","schema":"target-migration"'))});
    await assert.rejects(malformed.targetRequest(device,'migration'),/invalid_response/);
    console.log('PASS: bounded closed migration reads, protected immutable POST/retry, exact target paths, receipt lookup, and duplicate-key refusal');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

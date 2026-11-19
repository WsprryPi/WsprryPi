// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),{webcrypto}=require('node:crypto');
const {create}=require('../data/configuration-recovery-client.js');
const vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const sample=family=>structuredClone(vectors.find(item=>item.family===family&&item.valid).value);
async function main(){
    const intent=sample('controller-retirement'),book=sample('controller-retirements'),receipt=sample('controller-retirement-operation');
    const calls=[],etag='"'+'a'.repeat(64)+'"',csrf='b'.repeat(64);
    const client=create({crypto:webcrypto,fetch:async(url,options)=>{
        calls.push({url,options});
        if(url.endsWith('/management-session'))return new Response(JSON.stringify(sample('controller-browser-session')),{headers:{'X-WsprryPi-CSRF':csrf}});
        assert(url.endsWith('/host/retirements'));
        return new Response(JSON.stringify(options.method==='POST'?receipt:book),{status:options.method==='POST'?202:200,headers:{ETag:etag}});
    }});
    assert.deepEqual((await client.retirements()).data,book);assert.equal(calls.length,1,'Read cannot create a browser session');
    await assert.rejects(client.retirements({...intent,extra:true},etag),/candidate_invalid/);
    await assert.rejects(client.retirements(intent,'*'),/candidate_invalid/);
    assert.equal(calls.length,1,'Invalid intent refuses before all transport');
    const bytes=JSON.stringify(intent);
    for(let n=0;n<2;n++)assert.deepEqual((await client.retirements(intent,etag)).data,receipt);
    const writes=calls.filter(c=>c.options.method==='POST'&&c.url.endsWith('/retirements'));
    assert.equal(writes.length,2);assert(writes.every(c=>c.options.body===bytes&&c.options.headers['If-Match']===etag&&c.options.headers['X-WsprryPi-CSRF']===csrf));
    assert(writes.every(c=>c.options.redirect==='error'&&c.options.credentials==='same-origin'));
    const malformed=create({crypto:webcrypto,fetch:async()=>new Response(JSON.stringify(book).replace('"schema":"controller-retirements"','"schema":"controller-retirements","schema":"controller-retirements"'))});
    await assert.rejects(malformed.retirements(),/invalid_response/);
    console.log('PASS: closed bounded retirement read, protected immutable intent/replay, strong revision, duplicate-key refusal, and no read-side writes');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

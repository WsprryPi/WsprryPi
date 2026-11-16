// SPDX-License-Identifier: MIT
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const crypto = require('node:crypto').webcrypto;
const control = require('../data/control-contract.js');
const {create} = require('../data/configuration-recovery-client.js');
const vectors = JSON.parse(fs.readFileSync(require('node:path').join(__dirname,'../../src/wtp_integration/contract/control-controller-vectors.json')));
const sample = name => structuredClone(vectors.cases.find(item => item.name === name).value);
const response = (value,status = 200,headers = {}) => new Response(JSON.stringify(value),{status,headers});
const refusal = (code,status) => response({contract:control.contract,schema:'target-error',error:{code,retryable:false}},status);
const receipt = command => ({contract:control.contract,schema:'target-operation',binding:command.binding,
    operation_id:command.operation_id,request_digest:'1'.repeat(64),state:'committed',revision:'2'.repeat(64),error:null});
async function main() {
    const original = sample('P209 redacted unconfirmed draft'), calls = [];
    for (const [section,values] of Object.entries(original.draft)) for (const key of Object.keys(values))
        assert(control.controllerPiConfigurationField(section,key),section + '.' + key);
    assert.equal(control.controllerPiConfigurationField('__proto__','bad'),null);
    const detachedRule = control.controllerPiConfigurationField('CW','Fade Shape');detachedRule.enum.push('invented');
    assert(!control.controllerPiConfigurationField('CW','Fade Shape').enum.includes('invented'),'Rendered rules cannot mutate the authoritative validator');
    let mode = 'commit', sent = null;
    const fetch = async (url,options) => {
        assert.equal(options.credentials,'same-origin');assert.equal(options.redirect,'error');assert.equal(options.cache,'no-store');
        calls.push({url,...options});
        if (url.endsWith('/management-session')) return response({contract:control.contract,schema:'controller-browser-session'},200,{'X-WsprryPi-CSRF':'3'.repeat(64)});
        if (!options.method) return response(original);
        assert.equal(options.headers['X-WsprryPi-CSRF'],'3'.repeat(64));
        assert.equal(options.headers['If-Match'],'"' + original.revision + '"');
        const command = JSON.parse(options.body);
        assert(control.validControllerDocument(command,command.schema));sent = command;
        if (mode === 'lost') throw new Error('private transport detail');
        if (mode === 'invalid') return response({...receipt(command),extra:'forbidden'});
        if (mode === 'replaced') return response({...receipt(command),binding:{...command.binding,boot_id:'f'.repeat(32)}});
        if (mode === 'storage') return refusal('storage_unavailable',503);
        if (mode === 'refused') return refusal('candidate_invalid',422);
        if (mode === 'busy') return refusal('capacity',409);
        return response(receipt(command));
    };
    const client = create({fetch,crypto});await client.read();
    assert.equal(client.dirty,false);assert.deepEqual(client.draft,original.draft);
    assert.throws(() => client.set('WTP','TLS Client Key','/private/key'),{code:'read_only_field'});
    assert.throws(() => client.set('CW','Fade Shape','invalid'),{code:'candidate_invalid'});
    assert.throws(() => client.set('CW','Message',{state:'unavailable',reason:'missing',saved_text:null}),{code:'candidate_invalid'});
    assert.throws(() => client.set('__proto__','Message','bad'),{code:'read_only_field'});
    client.set('CW','Message','EXPLICIT DRAFT');assert(client.dirty);
    const detached = client.draft;detached.CW.Message = 'NOT AN EDIT';assert.equal(client.draft.CW.Message,'EXPLICIT DRAFT');
    const changed = structuredClone(original);changed.revision = 'c'.repeat(64);
    assert.throws(() => client.accept(changed),{code:'revision_conflict'});assert.equal(client.draft.CW.Message,'EXPLICIT DRAFT');
    mode = 'lost';await assert.rejects(client.save(),{code:'request_unconfirmed'});
    const operation = client.operation, firstBody = calls.at(-1).body;
    assert.equal(operation.state,'unconfirmed');client.set('CW','Message','LATER DRAFT');
    assert(client.laterEdits,'Accepted-operation tracking distinguishes later edits from the submitted draft');
    await assert.rejects(client.action('reset'),{code:'operation_pending'});
    mode = 'refused';await assert.rejects(client.retry(),{code:'candidate_invalid'});
    assert.equal(client.operation.state,'unconfirmed','A later context/refusal cannot prove that an earlier lost request was not committed');
    mode = 'commit';const done = await client.retry();assert.equal(done.state,'committed');
    assert.equal(calls.at(-1).body,firstBody);assert.equal(sent.configuration.CW.Message,'EXPLICIT DRAFT');
    assert.equal(client.draft.CW.Message,'LATER DRAFT','Late accepted writes never erase a newer browser draft');
    for (const key of ['TLS CA File','TLS Client Certificate','TLS Client Key']) assert.equal(sent.configuration.WTP[key],'');
    const before = calls.length;assert.deepEqual(await client.retry(),done);assert.equal(calls.length,before,'A confirmed receipt is not dispatched again');
    for (const failure of ['invalid','replaced','storage']) {
        const isolated = create({fetch,crypto});isolated.accept(original);mode = failure;
        await assert.rejects(isolated.save());assert.equal(isolated.operation.state,'unconfirmed',failure);
        await assert.rejects(isolated.save(),{code:'operation_pending'});
    }
    const rejected = create({fetch,crypto});rejected.accept(original);rejected.set('CW','Message','KEEP ME');mode = 'refused';
    await assert.rejects(rejected.save(),{code:'candidate_invalid'});assert.equal(rejected.operation,null);assert.equal(rejected.draft.CW.Message,'KEEP ME');
    const busy = create({fetch,crypto});busy.accept(original);mode = 'busy';await assert.rejects(busy.save(),{code:'capacity'});
    assert.equal(busy.operation.state,'unconfirmed','A first visible busy refusal may follow a browser-internal retry of an accepted PUT');
    const busyBody = calls.at(-1).body;mode = 'commit';await busy.retry();assert.equal(calls.at(-1).body,busyBody);
    const readOnly = create({fetch,crypto});readOnly.accept({...original,writable:false});await assert.rejects(readOnly.save(),{code:'authority_pending'});
    const unavailable = sample('P209 invalid saved scalar represented explicitly'), missing = create({fetch,crypto});missing.accept(unavailable);
    await assert.rejects(missing.save(),{code:'candidate_invalid'});assert.equal(missing.operation,null);
    const duplicate = create({crypto,fetch:async () => new Response(JSON.stringify(original).replace('"schema":"configuration-recovery"','"schema":"wrong","schema":"configuration-recovery"'))});
    await assert.rejects(duplicate.read(),{code:'invalid_response'});
    const oversized = create({crypto,fetch:async () => new Response(' '.repeat(32769))});
    await assert.rejects(oversized.read(),{code:'invalid_response'});
    assert.throws(() => create({fetch,crypto,base:'https://outside.invalid/api/control/v1'}),{code:'invalid_base'});
    console.log('PASS: closed protected recovery client, retained typed drafts, private-reference refusal, bounded/duplicate replies, immutable accepted-operation retry, stale-binding refusal, and no new action after uncertainty');
}
main().catch(error => {console.error(error);process.exitCode = 1;});

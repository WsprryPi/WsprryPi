// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=process.argv[2]||path.join(__dirname,'../data/control-contract.js');
const vectors=process.argv[3]||path.join(__dirname,'../../src/wtp_integration/contract/control-controller-vectors.json');
const root={module:{exports:{}}};vm.runInNewContext(fs.readFileSync(source,'utf8'),root);
const control=root.module.exports;
const samples=JSON.parse(fs.readFileSync(vectors,'utf8'));
const spec=JSON.parse(fs.readFileSync(path.join(path.dirname(vectors),'control-1.schema.json'),'utf8'));
function canonical(value) {
    if (Array.isArray(value)) return '['+value.map(canonical).join(',')+']';
    if (value !== null && typeof value === 'object')
        return '{'+Object.keys(value).sort().map(key=>JSON.stringify(key)+':'+canonical(value[key])).join(',')+'}';
    return JSON.stringify(value);
}
const digest=require('node:crypto').createHash('sha256').update(canonical(spec)).digest('hex');
const discovery=samples.cases.find(item=>item.name==='current bounded management discovery');
const vectorDigest=discovery.value.discoveries[0].control_schema_digest;
// Shared vectors retain a historical fixture digest. Bind those fixture rows
// to the selected schema so each test still exercises its named condition.
if (vectorDigest!==digest)
    assert.equal(control.validControllerDocument(discovery.value,'pair-discovery'),false,'Superseded discovery digest must refuse');
for (const item of samples.cases) {
    const value=structuredClone(item.value);
    if (item.family==='pair-discovery')
        for (const row of value.discoveries ?? [])
            if (row.control_schema_digest===vectorDigest) row.control_schema_digest=digest;
    assert.equal(control.validControllerDocument(value,item.family),item.valid,item.name);
}
const piSave=samples.cases.find(item=>item.name==='Pi closed structural save');
assert(piSave,'The current Pi structural save vector is required');
for(const number of [NaN,Infinity,-Infinity]){
    const value=JSON.parse(JSON.stringify(piSave.value));value.candidate.configuration.CW['Base Frequency']=number;
    assert.equal(control.validControllerDocument(value,'target-save'),false,'Nonfinite Pi configuration');
}
const recoverySave=structuredClone(samples.cases.find(item=>item.name==='P209 complete recovery save').value);
recoverySave.configuration.Operation['Transmit Backend']='wtp';
assert.equal(control.validControllerDocument(recoverySave,'configuration-recovery-save'),false,'Retired recovery backend');
assert.equal(control.validControllerDocument({},'unknown'),false);
for(const item of samples.recovery_cases)assert.equal(control.controllerRecoveryRead(item.resource),item.read,item.resource);
assert.equal(control.controllerAdapter.headers.recovery,'X-WsprryPi-Recovery-Resource');
for(const [resource,descriptor] of Object.entries(control.controllerPiDescriptors)){
 const capability={resource,...descriptor,read:true,write:false,activation:'unsupported',maximum_bytes:32768};
 assert.equal(control.supportedPiResource(capability),true,resource);
 for(const changed of [{schema_id:'urn:other:pi-config'},{schema_digest:'0'.repeat(64)},{maximum_bytes:32769},{write:true},{extra:true}])
  assert.equal(control.supportedPiResource({...capability,...changed}),false,JSON.stringify(changed));
}
console.log('PASS: authoritative controller documents, closed shapes, identity/generation bounds and native method registry');

// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=process.argv[2]||path.join(__dirname,'../data/control-contract.js');
const vectors=process.argv[3]||path.join(__dirname,'../../src/wtp_integration/contract/control-controller-vectors.json');
const root={module:{exports:{}}};vm.runInNewContext(fs.readFileSync(source,'utf8'),root);
const control=root.module.exports;
const samples=JSON.parse(fs.readFileSync(vectors,'utf8'));
for(const item of samples.cases)
    assert.equal(control.validControllerDocument(item.value,item.family),item.valid,item.name);
const piSave=samples.cases.find(item=>item.name==='Pi closed structural save');
assert(piSave,'The current Pi structural save vector is required');
for(const number of [NaN,Infinity,-Infinity]){
    const value=JSON.parse(JSON.stringify(piSave.value));value.candidate.configuration.CW['Base Frequency']=number;
    assert.equal(control.validControllerDocument(value,'target-save'),false,'Nonfinite Pi configuration');
}
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

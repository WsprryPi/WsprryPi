// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const control=require('../data/control-contract.js'),vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const source=fs.readFileSync(require.resolve('../data/controller-retirement.js'),'utf8');
const sample=()=>structuredClone(vectors.find(c=>c.family==='controller-retirements'&&c.valid).value);
async function main(){
 const nodes=new Map(),events={},requests=[];let book=sample(),release,delay=false,badRevision=false;
 function node(id){if(!nodes.has(id))nodes.set(id,{hidden:true,disabled:false,textContent:'',events:{},addEventListener(name,fn){this.events[name]=fn;}});return nodes.get(id);}
 const target=book.assignments[0].target_id;
 const root={document:{getElementById:node},addEventListener(name,fn){events[name]=fn;},WsprryControl:control,
  ConfigurationRecoveryClient:{create:()=>({retirements:async(...args)=>{requests.push(args);const captured=structuredClone(book);if(delay)await new Promise(resolve=>{release=resolve;});return {data:captured,etag:badRevision?'"wrong"':'"'+captured.revision+'"'};}})}};
 vm.runInNewContext(source,{window:root});const settle=()=>new Promise(resolve=>setImmediate(resolve));
 const feedback=()=>node('controllerRetirementFeedback').textContent;
 for(const state of ['legacy','retiring','held','rollback']){
  book=sample();book.assignments[0].state=state;if(state==='retiring')book.assignments[0].receipt.state=state;else book.assignments[0].receipt=null;
  root.ControllerRetirement.select(target,'Retained output');await settle();
  assert(root.ControllerRetirement.fenced,state+' cannot establish retirement');assert.match(feedback(),/WTP-capable software/);
 }
 book=sample();book.assignments[0].state='retired';book.assignments[0].receipt.state='retired';
 root.ControllerRetirement.select(target,'Retained output');await settle();assert.equal(root.ControllerRetirement.fenced,false);assert.match(feedback(),/retirement is confirmed/);
 badRevision=true;node('controllerRetirementRefresh').events.click();await settle();assert(root.ControllerRetirement.fenced);assert.match(feedback(),/No retirement is inferred/);badRevision=false;
 book={...sample(),unexpected:true};node('controllerRetirementRefresh').events.click();await settle();assert(root.ControllerRetirement.fenced);assert.match(feedback(),/could not be verified/);
 book=sample();book.assignments=[];node('controllerRetirementRefresh').events.click();await settle();assert(root.ControllerRetirement.fenced);assert.match(feedback(),/No matching retirement receipt/);
 book=sample();book.assignments[0].state='retired';book.assignments[0].receipt.state='retired';delay=true;root.ControllerRetirement.select(target,'Old selection');await settle();
 root.ControllerRetirement.select('0'.repeat(32),'New selection');delay=false;release();await settle();await settle();assert(root.ControllerRetirement.fenced,'Late old receipt cannot confirm another selected target');assert.match(feedback(),/No matching retirement receipt/);
 events.pagehide();assert.equal(node('controllerRetirementRefresh').disabled,true);events.pageshow();await settle();
 assert(requests.every(args=>args.length===0),'Retirement view never submits, retries, rebases, or invents an intent');
 assert.equal(nodes.has('controllerRetirementConfirm'),false,'Retired software has no confirmation/write controls');
 console.log('PASS read-only retirement receipts, strict shape/revision, retained holds, missing receipt, late target binding, page lifecycle, and zero mutation');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

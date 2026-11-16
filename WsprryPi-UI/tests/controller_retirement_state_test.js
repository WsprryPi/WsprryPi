// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),{webcrypto}=require('node:crypto');
const control=require('../data/control-contract.js'),vectors=require('../../src/wtp_integration/contract/control-controller-vectors.json').cases;
const source=fs.readFileSync(require.resolve('../data/controller-retirement.js'),'utf8');
const sample=family=>structuredClone(vectors.find(c=>c.family===family&&c.valid).value);
async function scenario(mode){
    const values=new Map(),nodes=new Map();let active=null,posts=[],releasePost;
    function node(id){if(!nodes.has(id))nodes.set(id,{id,hidden:true,disabled:false,checked:false,textContent:'',events:{},
        addEventListener(name,handler){this.events[name]=handler;},setAttribute(){},focus(){active=this;},
        click(){if(!this.disabled)this.events.click?.();},change(){this.events.change?.();}});return nodes.get(id);}
    let book=sample('controller-retirements');book.generation='1';book.assignments[0].state='legacy';book.assignments[0].receipt=null;
    const row=book.assignments[0],target=row.target_id,other='0'.repeat(32);
    book.assignments.push({...structuredClone(row),target_id:other,assignment_id:'1'.repeat(32)});
    const root={setTimeout:()=>1,clearTimeout(){},addEventListener(){},document:{getElementById:node,get activeElement(){return active;}},crypto:webcrypto,WsprryControl:control,
        sessionStorage:{getItem:k=>values.get(k)||null,setItem:(k,v)=>values.set(k,v),removeItem:k=>values.delete(k)},
        ConfigurationRecoveryClient:{create:()=>({retirements:async(body,etag)=>{
            if(!body)return {data:structuredClone(book),etag:'"'+book.revision+'"'};
            posts.push({body:structuredClone(body),etag});
            if(mode==='switch')await new Promise(resolve=>{releasePost=resolve;});
            if(mode==='conflict'||mode==='other'&&posts.length>1)throw Object.assign(new Error('revision_conflict'),{code:'revision_conflict',status:409});
            const receipt={contract:control.contract,schema:'controller-retirement-operation',controller_id:body.controller_id,
                target_id:body.target_id,assignment_id:body.assignment_id,operation_id:body.operation_id,transition_id:body.transition_id,
                state:mode==='terminal'?'retired':'retiring',generation:'2'};
            if(mode==='other')receipt.operation_id='8'.repeat(32);
            book={...book,generation:'2',revision:'9'.repeat(64),assignments:book.assignments.map(saved=>saved.target_id===body.target_id?{...saved,state:receipt.state,receipt}:saved)};
            if(mode==='unknown'||mode==='other')throw Object.assign(new Error('storage_unavailable'),{code:'storage_unavailable',status:503});
            return {data:receipt,status:200};
        }})}};
    vm.runInNewContext(source,{window:root},{filename:'controller-retirement.js'});
    const byId=suffix=>node('controllerRetirement'+suffix);
    const settle=()=>new Promise(resolve=>setImmediate(resolve));
    if(['held','rollback'].includes(mode))book.assignments[0].state=mode;
    root.ControllerRetirement.select(target,'Example transmitter');await settle();
    if(['held','rollback'].includes(mode)){assert.equal(byId('Review').disabled,true);assert(root.ControllerRetirement.fenced);assert.match(byId('Feedback').textContent,/requires recovery/);assert.equal(posts.length,0);return;}
    assert.equal(byId('Review').disabled,false);assert.equal(posts.length,0);
    byId('Review').click();assert.equal(byId('Confirm').disabled,true);
    byId('Consent').checked=true;byId('Consent').change();byId('Confirm').click();await settle();
    assert.equal(posts.length,1,'Only explicit confirmation submits an intent');
    if(mode==='switch'){
        root.ControllerRetirement.select(other,'Second output');releasePost();await settle();await settle();
        assert.match(byId('Feedback').textContent,/unconfirmed for Example transmitter/);
        assert.equal(byId('Review').disabled,true);assert.equal(posts[0].body.target_id,target);
        assert.equal(JSON.parse([...values.values()][0]).intent.target_id,target);
        assert.equal(posts.length,1,'Late completion cannot submit a request for another selected device');return;
    }
    if(mode==='other'){
        assert.equal(values.size,1);byId('Refresh').click();await settle();
        assert.match(byId('Feedback').textContent,/request was refused/);assert.equal(values.size,0);
        assert.equal(posts.length,2);assert.deepEqual(posts[1],posts[0],'Concurrent retirement recovery cannot rebase the old intent');
        byId('Refresh').click();await settle();assert.match(byId('Feedback').textContent,/work remains pending/);
        assert(root.ControllerRetirement.fenced);return;
    }
    if(mode==='conflict'){
        assert.match(byId('Feedback').textContent,/request was refused/);assert.equal(values.size,0);
        assert.equal(byId('Review').disabled,true,'A refused stale review is not current authority');
        byId('Refresh').click();await settle();assert.equal(byId('Review').disabled,false,'Explicit fresh read makes another review possible');
    }else{
        assert.equal(byId('Review').disabled,true);assert(root.ControllerRetirement.fenced);
        if(mode==='unknown'){assert.equal(values.size,1);assert.match(byId('Feedback').textContent,/unconfirmed/);}
        else {assert.equal(values.size,0);assert.match(byId('Feedback').textContent,/confirmed/);}
        byId('Refresh').click();await settle();
        assert.match(byId('Feedback').textContent,mode==='unknown'?/work remains pending/:/confirmed/);
        assert.equal(byId('Review').disabled,true);assert.equal(node('fleet-schedule-save').disabled,true);
    }
    assert.equal(posts.length,1,'Readback and precondition refusal cannot silently retry or rebase a mutation');
}
(async()=>{for(const mode of ['conflict','unknown','terminal','switch','held','rollback','other'])await scenario(mode);console.log('PASS: definite pre-write conflict requires fresh review, uncertain intent retained, terminal fence retained, late target binding, read-only holds, and lookup without automatic mutation');})().catch(e=>{console.error(e);process.exitCode=1;});

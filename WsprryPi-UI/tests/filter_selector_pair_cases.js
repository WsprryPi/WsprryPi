// Shipped shared editor -> protected Pi coordinator -> actual Pico persistence.
'use strict';
const assert=require('node:assert/strict'),filters=require('../data/filter-selector.js'),dom=require('./filter_dom.cjs');
module.exports=async request=>{
 const path='/api/control/v1/member/filter-selector',get=()=>request({method:'GET',path,body:''});
 let before=await get();assert.equal(before.status,200,JSON.stringify(before));assert.equal(before.body.contract,'Wsprry-Control/1');const {contract,...first}=before.body;assert(filters.valid(first));
 const target=before.body.target,b=dom(),view=filters.mount(b.host,{exchange:async(method,body,revision)=>{const r=await request({method,path,body:body?JSON.stringify(body):'',revision});if(r.status!==200)throw Error(JSON.stringify(r));assert.equal(r.body.contract,'Wsprry-Control/1');const {contract,...data}=r.body;return {data,revision:r.etag};}});
 view.setAvailability(true);await view.load();
 b.button('Add selector').onclick();b.change('Selector name','HF');const reserved=new Set(before.body.reserved.map(o=>o.gp));const pin=[0,1,3,4,5,6,7,8,9,10,11,12,13,15,16,17,18,19,20,21,22,26,27,28].find(gp=>!reserved.has(gp));b.change('Pico GPIO',pin);b.change('Polarity','false');
 b.button('Add frequency range').onclick();b.change('Range name','40m');b.change('Selector',1);b.change('From (Hz)',7000000);b.change('Through (Hz)',7300000);b.change('Selection policy','required');
 assert.equal(b.button('Save selectors').disabled,false);await b.button('Save selectors').onclick();assert.match(b.status(),/Selectors saved/,b.status());
 const saved=(await get()).body;assert.equal(saved.saved.policy,'required');assert.equal(saved.saved.selectors[0].gp,pin);assert.equal(saved.saved.selectors[0].active_high,false);assert(saved.pending_restart);assert.equal(saved.selected_id,null);assert.equal(saved.output_active,false);
 const unchanged=saved.saved;
 for(const patch of [{schema:'unknown-filter-selector'},{extra:0},{target:[]},{target:{...target,boot_id:'f'.repeat(32)}},{settings:{...unchanged,settle_ms:251}},{settings:{...unchanged,selectors:[{...unchanged.selectors[0],gp:saved.reserved[0].gp}]}}]){
   before=await get();const r=await request({method:'PUT',path,revision:before.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:filters.descriptor.schema,target,settings:unchanged,...patch})});assert.equal(r.status,400,JSON.stringify(r));const after=await get();assert.equal(after.etag,before.etag);assert.deepEqual(after.body.saved,unchanged);
 }
 // Existing config/logging edits must not accidentally erase selector settings.
 const config=await request({method:'GET',path:'/api/control/v1/member/configuration',body:''});assert.equal(config.status,200,JSON.stringify(config));
 const written=await request({method:'POST',path:'/api/control/v1/member/configuration',revision:config.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'pico-settings',target,operation:'save',config:{expires_utc_s:0}})});assert.equal(written.status,200,JSON.stringify(written));assert.deepEqual((await get()).body.saved,unchanged);
 before=await get();const restored=await request({method:'PUT',path,revision:before.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:filters.descriptor.schema,target,settings:{policy:'fixed_manual',release_ms:20,settle_ms:20,selectors:[],ranges:[]}})});assert.equal(restored.status,200,JSON.stringify(restored));assert(restored.body.pending_restart);
 console.log('Selectors: shipped UI producer, pin/limit/identity refusals, atomic persistence and A/B/A restart passed through production pair');
};

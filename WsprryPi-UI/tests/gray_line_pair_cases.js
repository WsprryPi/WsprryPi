// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict');
const gray=require('../data/gray-line.js');
async function grayLineCases(request) {
  const path='/api/control/v1/member/gray-line';
  const read=()=>request({method:'GET',path,body:''});
  const before=await read();assert.equal(before.status,200,JSON.stringify(before));
  assert(gray.readback(before.body));assert.equal(before.body.saved.morning.enabled,false);
  const target=before.body.target,settings=gray.defaults();
  settings.morning={enabled:true,lower_mdeg:-6500,upper_mdeg:7000,wspr_dial_frequency_hz:7038600,message_frequency_nhz:'7039000000000000'};
  settings.evening={enabled:true,lower_mdeg:-5000,upper_mdeg:5500,wspr_dial_frequency_hz:10138700,message_frequency_nhz:null};
  const write=(body,revision=before.etag)=>request({method:'PUT',path,revision,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'gray-line',target,settings,...body})});
  for(const patch of [
    {target:{...target,device_id:'f'.repeat(32)}},
    {target:{...target,boot_id:'f'.repeat(32)}},
    {contract:'Wsprry-Control/0'},
    {settings:{...settings,extra:true}},
    {settings:{...settings,morning:{...settings.morning,upper_mdeg:-6500}}},
    {settings:{...settings,morning:{...settings.morning,lower_mdeg:-90001}}},
    {settings:{...settings,morning:{...settings.morning,enabled:1}}},
    {settings:{...settings,morning:{...settings.morning,wspr_dial_frequency_hz:1234567}}},
    {settings:{...settings,evening:{...settings.evening,message_frequency_nhz:'1'}}},
    {settings:{...settings,evening:{...settings.evening,message_frequency_nhz:'18446744073709551615'}}},
    {settings:{...settings,morning:{...settings.morning,message_frequency_nhz:'18446744073709551616'}}}
  ]) {
    const refused=await write(patch);assert(refused.status>=400,JSON.stringify(refused));
    const unchanged=await read();assert.equal(unchanged.etag,before.etag);assert.deepEqual(unchanged.body.saved,before.body.saved);
  }
  const saved=await write({});assert.equal(saved.status,200,JSON.stringify(saved));
  assert.deepEqual(saved.body.saved,settings);assert.notEqual(saved.etag,before.etag);
  const stale=await write({});assert.equal(stale.status,409,JSON.stringify(stale));
  assert.equal(stale.body.error.code,'member_read_required');assert.deepEqual(stale.exchanges,[]);
  const afterStale=await read();assert.deepEqual(afterStale.body.saved,settings);
  const restored=await write({settings:gray.defaults()},afterStale.etag);
  assert.equal(restored.status,200,JSON.stringify(restored));assert.deepEqual(restored.body.saved,gray.defaults());
  console.log('Gray-line production source-pair cases passed');
}
module.exports={grayLineCases};

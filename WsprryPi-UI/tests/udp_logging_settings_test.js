// SPDX-License-Identifier: MIT
const assert=require('node:assert/strict');
const udp=require('../data/udp-logging.js');
const counters=Object.fromEntries(['generated','filtered','redacted_rejected','queued','overflow','expired','reconfigured_drops','truncated','attempted','locally_submitted','locally_failed'].map(k=>[k,'0']));
const target={scope:'member',device_id:'a'.repeat(32),boot_id:'b'.repeat(32)};
const capabilities={contract:'Wsprry-Control/1',schema:'udp-logging-capabilities',target,resource:'/api/control/v1/udp-logging',settings_schema:'udp-logging',wire_schema:'wsprry-udp/1',limits:{payload_bytes:480,queue_records:16,maximum_sources:0,maximum_peers:0,maximum_rate:8,maximum_burst:16,address_family:'ipv4'}};
const saved={enabled:false,destination_ipv4:null,destination_port:5514,maximum_severity:6,event_codes:[],rate:4,burst:8};
const value={contract:'Wsprry-Control/1',schema:'udp-logging',target,saved,active:{state:'disabled',applied_revision:'one',receipt:'unknown',queued:0,counters}};
assert(udp.capabilities(capabilities));assert(udp.readback(value,capabilities));
for(const address of ['127.0.0.1','localhost','0.0.0.0','224.1.2.3','255.255.255.255','192.168.01.1','192.168.1.1:514','::1'])assert(!udp.ip(address));
assert(udp.ip('192.168.1.20'));
assert(!udp.decimal('18446744073709551616'));assert(udp.decimal('18446744073709551615'));
for(const patch of [{rate:true},{destination_port:'514'},{burst:17},{enabled:true},{event_codes:['BOOT','BOOT']},{extra:true}])assert(!udp.settings({...saved,...patch}));
for(const version of ['udp-logging-capabilities/0','unknown-udp-logging-capabilities'])assert(!udp.capabilities({...capabilities,schema:version}));
assert(!udp.capabilities({...capabilities,limits:{...capabilities.limits,payload_bytes:481}}));
assert(!udp.readback({...value,target:{...target,boot_id:'c'.repeat(32)}},capabilities));
assert(!udp.readback({...value,active:{...value.active,receipt:'delivered'}},capabilities));
const hostTarget={scope:'host',collector_id:'c'.repeat(32)};
const hostCaps={...capabilities,schema:'udp-logging-capabilities/1',target:hostTarget,resource:'/api/control/v1/host/udp-logging',settings_schema:'host-udp-logging/1',limits:{...capabilities.limits,queue_records:256,maximum_sources:8,maximum_peers:32,maximum_rate:64,maximum_burst:128}};
const hostValue={schema:'host-udp-logging/1',target:hostTarget,saved:{sender:{...saved,rate:16,burst:32,interface:null,native_enabled:false,usb_sources:[]},collector:{enabled:false,bind_ipv4:null,interface:null,port:5514,peers:[{slot:'member',ipv4:'192.0.2.20'}]}},active:{applied_revision:null,sender:{state:'disabled',receipt:'unknown',queued:0,counters},collector:{state:'disabled',bind_ipv4:null,port:null,counters:Object.fromEntries(['received','oversize','disallowed','malformed','duplicate','conflict','limited','overflow','journal_failed','journal_submitted','discontinuity'].map(k=>[k,'0'])),peers:[{slot:'member',received:'0',last_receipt_us:null}]}}};
assert(udp.readback(hostValue,hostCaps,true));
for(const slots of [['unknown'],['member','member'],['not a slot']]){
 const response=structuredClone(hostValue);response.active.collector.peers=slots.map(slot=>({slot,received:'0',last_receipt_us:null}));
 assert(!udp.readback(response,hostCaps,true),'Refuse duplicate or unconfigured active peer scopes');
}
assert.deepEqual(udp.parseJson('{"settings":{"rate":4}}'),{settings:{rate:4}});
for(const body of ['{"rate":1,"rate":2}','{"rate":1,"\\u0072ate":2}','{"settings":{"a":1,"a":2}}'])assert.throws(()=>udp.parseJson(body));
console.log('UDP settings, closed negotiation, readback and duplicate-key checks passed');

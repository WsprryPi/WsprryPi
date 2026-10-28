// SPDX-License-Identifier: MIT
// Shared UI contract checks and explicit read/save form. No transport authority.
(function(root){
'use strict';
const keys=(v,n)=>v!==null&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===n.length&&n.every(k=>Object.hasOwn(v,k));
const id=v=>typeof v==='string'&&/^[0-9a-f]{32}$/.test(v);
const integer=(v,a,b)=>Number.isInteger(v)&&v>=a&&v<=b;
const printable=(v,n,empty=false)=>typeof v==='string'&&(empty||v.length>0)&&v.length<=n&&/^[\x20-\x7e]*$/.test(v);
const token=v=>printable(v,32)&&/^[\x21\x23-\x5b\x5d-\x7e]+$/.test(v);
const decimal=v=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=18446744073709551615n;
const ip=v=>{
 if(typeof v!=='string'||!v.split('.').every(x=>/^(0|[1-9][0-9]{0,2})$/.test(x)&&Number(x)<=255)||v.split('.').length!==4)return false;
 const p=v.split('.').map(Number);return p[0]>0&&p[0]!==127&&p[0]<224;
};
const counterKeys=['generated','filtered','redacted_rejected','queued','overflow','expired','reconfigured_drops','truncated','attempted','locally_submitted','locally_failed'];
const collectorKeys=['received','oversize','disallowed','malformed','duplicate','conflict','limited','overflow','journal_failed','journal_submitted','discontinuity'];
const counters=(v,n)=>keys(v,n)&&n.every(k=>decimal(v[k]));
const settings=(v,host=false)=>keys(v,['enabled','destination_ipv4','destination_port','maximum_severity','event_codes','rate','burst',...(host?['interface','native_enabled','usb_sources']:[])])&&
 typeof v.enabled==='boolean'&&(v.destination_ipv4===null||ip(v.destination_ipv4))&&(!v.enabled||v.destination_ipv4!==null)&&integer(v.destination_port,1,65535)&&integer(v.maximum_severity,0,7)&&
 integer(v.rate,1,host?64:8)&&integer(v.burst,1,host?128:16)&&Array.isArray(v.event_codes)&&v.event_codes.length<=16&&v.event_codes.every(token)&&new Set(v.event_codes).size===v.event_codes.length&&
 (!host||((v.interface===null||iface(v.interface))&&(!v.enabled||v.interface!==null)&&typeof v.native_enabled==='boolean'&&Array.isArray(v.usb_sources)&&v.usb_sources.length<=8&&v.usb_sources.every(s=>keys(s,['profile_id','device_id'])&&id(s.profile_id)&&id(s.device_id))&&new Set(v.usb_sources.map(s=>s.device_id)).size===v.usb_sources.length&&new Set(v.usb_sources.map(s=>s.profile_id)).size===v.usb_sources.length));
const iface=v=>typeof v==='string'&&/^[A-Za-z0-9_.:-]{1,15}$/.test(v);
const target=(t,host)=>host?keys(t,['scope','collector_id'])&&t.scope==='host'&&id(t.collector_id):keys(t,['scope','device_id','boot_id'])&&t.scope==='member'&&id(t.device_id)&&id(t.boot_id);
const equalTarget=(a,b)=>keys(a,Object.keys(b))&&Object.keys(b).every(k=>a[k]===b[k]);
const collector=v=>keys(v,['enabled','bind_ipv4','interface','port','peers'])&&typeof v.enabled==='boolean'&&(v.bind_ipv4===null||ip(v.bind_ipv4))&&(v.interface===null||iface(v.interface))&&(!v.enabled||(v.bind_ipv4!==null&&v.interface!==null))&&integer(v.port,1,65535)&&Array.isArray(v.peers)&&v.peers.length<=32&&v.peers.every(p=>keys(p,['slot','ipv4'])&&typeof p.slot==='string'&&/^[A-Za-z0-9_-]{1,32}$/.test(p.slot)&&ip(p.ipv4))&&new Set(v.peers.map(p=>p.slot)).size===v.peers.length&&new Set(v.peers.map(p=>p.ipv4)).size===v.peers.length;
function capabilities(c,host=false){
 if(!keys(c,['schema','target','resource','settings_schema','wire_schema','limits'])||c.schema!=='udp-logging-capabilities/1'||!target(c.target,host)||c.resource!==(host?'/api/v1/host/udp-logging':'/api/v1/udp-logging')||c.settings_schema!==(host?'host-udp-logging/1':'transmitter-udp-logging/1')||c.wire_schema!=='wsprry-udp/1')return false;
 const limits={payload_bytes:480,queue_records:host?256:16,maximum_sources:host?8:0,maximum_peers:host?32:0,maximum_rate:host?64:8,maximum_burst:host?128:16,address_family:'ipv4'};
 return keys(c.limits,Object.keys(limits))&&Object.keys(limits).every(k=>c.limits[k]===limits[k]);
}
function senderActive(a,host=false){return keys(a,['state',...(host?[]:['applied_revision']),'receipt','queued','counters'])&&['disabled','awaiting_network','invalid_destination','ready','submission_failed','unavailable'].includes(a.state)&&a.receipt==='unknown'&&integer(a.queued,0,host?256:16)&&counters(a.counters,counterKeys)&&(host||a.applied_revision===null||printable(a.applied_revision,128));}
function readback(v,c,host=false){
 if(!capabilities(c,host)||!keys(v,['schema','target','saved','active'])||v.schema!==c.settings_schema||!equalTarget(v.target,c.target))return false;
 if(!host)return settings(v.saved)&&senderActive(v.active);
 const a=v.active;
 return keys(v.saved,['sender','collector'])&&settings(v.saved.sender,true)&&collector(v.saved.collector)&&keys(a,['applied_revision','sender','collector'])&&(a.applied_revision===null||printable(a.applied_revision,128))&&senderActive(a.sender,true)&&keys(a.collector,['state','bind_ipv4','port','counters','peers'])&&['disabled','awaiting_network','ready','bind_failed','unavailable','policy_invalid'].includes(a.collector.state)&&(a.collector.bind_ipv4===null||ip(a.collector.bind_ipv4))&&(a.collector.port===null||integer(a.collector.port,1,65535))&&counters(a.collector.counters,collectorKeys)&&Array.isArray(a.collector.peers)&&a.collector.peers.length<=32&&a.collector.peers.every(p=>keys(p,['slot','received','last_receipt_us'])&&printable(p.slot,32)&&v.saved.collector.peers.some(saved=>saved.slot===p.slot)&&decimal(p.received)&&(p.last_receipt_us===null||decimal(p.last_receipt_us)))&&new Set(a.collector.peers.map(p=>p.slot)).size===a.collector.peers.length;
}
function testCapabilities(v,c,host=false){return capabilities(c,host)&&keys(v,['schema','target','resource','request_schema','event_code','maximum_rate'])&&v.schema==='udp-logging-test-capabilities/1'&&equalTarget(v.target,c.target)&&v.resource===c.resource+'/test'&&v.request_schema==='udp-logging-test/1'&&v.event_code===(host?'PI_LOG':'UDP_TEST')&&v.maximum_rate===2;}
let serial=0;
function mount(element,options){
 if(!element||!root.document)return null;
 element.classList.add('udp-logging-controls');
 const host=options.host===true,prefix=`udp-log-${++serial}-`,doc=root.document;
 let snapshot=null,revision='',epoch=0,available=true,writable=true,busy=false,dirty=false,diagnostic=null,reviewRequired=false;
 const el=(tag,text)=>{const node=doc.createElement(tag);if(text!==undefined)node.textContent=text;return node;};
 const read=el('button','Read UDP logging settings');read.type='button';read.className=options.buttonClass||'';
 const test=el('button','Test saved UDP export');test.type='button';test.className=options.buttonClass||'';
 const form=el('form'),fieldset=el('fieldset');fieldset.disabled=true;form.append(fieldset);
 const status=el('p','Read settings to see saved and active UDP logging state.');status.setAttribute('role','status');status.setAttribute('aria-live','polite');
 const identity=el('p');identity.className='text-break';const nodes={};
 const comparison=el('details'),savedList=el('ul'),review=el('button','Confirm review');
 comparison.append(el('summary','Saved UDP logging settings'),savedList,review);comparison.hidden=true;
 review.type='button';review.className=options.buttonClass||'';review.hidden=true;
 function showSaved(value){
  savedList.replaceChildren();const s=host?value.sender:value;
  const row=text=>savedList.append(el('li',text));
  row(`Export: ${s.enabled?'enabled':'disabled'}`);row(`Destination: ${s.destination_ipv4||'not set'}; port ${s.destination_port}`);
  row(`Maximum severity: ${s.maximum_severity}`);row(`Event filter: ${s.event_codes.length?s.event_codes.join(', '):'all export-safe events'}`);row(`Rate: ${s.rate} events/second; burst ${s.burst}`);
  if(host){row(`Send interface: ${s.interface||'not set'}`);row(`Native Pi events: ${s.native_enabled?'included':'excluded'}`);
   if(!s.usb_sources.length)row('Forwarded Picos: none');
   for(const source of s.usb_sources)row(`Forwarded Pico ${source.device_id}; Fleet profile ${source.profile_id}`);
   const c=value.collector;row(`Network collection: ${c.enabled?'enabled':'disabled'}`);row(`Listen address: ${c.bind_ipv4||'not set'}; interface ${c.interface||'not set'}; port ${c.port}`);
   if(!c.peers.length)row('Allowed senders: none');for(const peer of c.peers)row(`Allowed sender ${peer.slot}: ${peer.ipv4}`);
  }
 }
 function input(key,label,type='text',parent=fieldset){
  const row=el('label',label),field=el('input');field.type=type;field.id=prefix+key;field.name=key;row.htmlFor=field.id;row.append(field);parent.append(row);nodes[key]=field;
  if(type==='checkbox')row.className=options.checkClass||'check';else field.className=options.inputClass||'';
  if(type!=='checkbox')field.autocomplete='off';return field;
 }
 function number(key,label,min,max){const f=input(key,label,'number');f.min=min;f.max=max;f.step=1;return f;}
 const warning='UDP logs are sent in plaintext without authentication. Use a deliberately configured managed network. A successful send does not confirm receipt.';
 input('enabled','Enable UDP log export','checkbox');fieldset.append(el('p',warning));
 input('destination_ipv4','Collector IPv4 address').placeholder='192.168.1.20';number('destination_port','Collector port',1,65535);
 number('maximum_severity','Maximum severity (0 emergency to 7 debug)',0,7);
 if(host){input('interface','Send through interface').maxLength=15;input('native_enabled','Include native Pi events','checkbox');}
 const advanced=el('details'),summary=el('summary','Export limits and event filter');advanced.append(summary);fieldset.append(advanced);
 for(const [key,label,max] of [['rate','Events per second',host?64:8],['burst','Maximum burst',host?128:16]]){const f=input(key,label,'number',advanced);f.min=1;f.max=max;f.step=1;}
 input('event_codes','Event codes (comma separated; blank includes all export-safe events)','text',advanced);
 let sources=null,peers=null;
 if(host){
  sources=el('div');sources.append(el('p','Forward selected attached-Pico logs. USB collection and Pico export must already be enabled.'));fieldset.append(sources);
  const heading=el('h3','Receive network logs on this Pi');fieldset.append(heading);
  input('collector_enabled','Enable network log collection','checkbox');fieldset.append(el('p',warning));
  input('bind_ipv4','Listen on this Pi’s IPv4 address');input('collector_interface','Receive through interface').maxLength=15;
  number('collector_port','Listen on port',1,65535);
  const label=el('label','Allowed senders (one slot and IPv4 address per line)'),field=el('textarea');field.rows=4;field.id=prefix+'peers';label.htmlFor=field.id;field.className=options.inputClass||'';label.append(field);fieldset.append(label);peers=field;
  field.placeholder='member-1 192.168.1.21';
 }
 const save=el('button',host?'Save logging on this Pi':'Save Pico UDP logging');save.type='submit';save.className=options.buttonClass||'';fieldset.append(save);
 element.append(read,test,identity,comparison,form,status);
 const show=()=>{const policy=snapshot&&(host?snapshot.saved.sender:snapshot.saved);const testable=diagnostic&&policy?.enabled&&(!host||policy.native_enabled)&&policy.maximum_severity>=(host?6:5)&&(!policy.event_codes.length||policy.event_codes.includes(diagnostic.event_code));test.disabled=busy||!available||!writable||!testable||dirty;test.title=testable?'Send one fixed event using the saved policy; receipt remains unknown.':diagnostic?'Enable and save export with the test event included before testing.':'Test events are unavailable until compatible settings are read.';read.disabled=busy||!available;fieldset.disabled=busy||!available||!snapshot;save.disabled=busy||!writable||!available||!snapshot||reviewRequired;review.hidden=!snapshot||!reviewRequired;review.disabled=busy||!available||!writable||!snapshot;};
 const invalidate=()=>{diagnostic=null;snapshot=null;revision='';comparison.hidden=true;identity.textContent='';reviewRequired=dirty;++epoch;show();};
 const fromForm=()=>{
  const s={enabled:nodes.enabled.checked,destination_ipv4:nodes.destination_ipv4.value||null,destination_port:Number(nodes.destination_port.value),maximum_severity:Number(nodes.maximum_severity.value),event_codes:nodes.event_codes.value.split(',').map(x=>x.trim()).filter(Boolean),rate:Number(nodes.rate.value),burst:Number(nodes.burst.value)};
  if(host){s.interface=nodes.interface.value||null;s.native_enabled=nodes.native_enabled.checked;s.usb_sources=[...sources.querySelectorAll('input:checked')].map(n=>({profile_id:n.dataset.profile,device_id:n.dataset.device}));}
  if(!settings(s,host))throw Error('Check the IPv4 address, port, severity, event codes and export limits. DNS names and IPv6 are unsupported.');
  if(!host)return s;
  const c={enabled:nodes.collector_enabled.checked,bind_ipv4:nodes.bind_ipv4.value||null,interface:nodes.collector_interface.value||null,port:Number(nodes.collector_port.value),peers:peers.value.split('\n').map(x=>x.trim()).filter(Boolean).map(line=>{const pair=line.split(/\s+/);return {slot:pair[0],ipv4:pair.length===2?pair[1]:''};})};
  if(!collector(c))throw Error('Check the listen address/interface/port and each unique sender slot and IPv4 address.');return {sender:s,collector:c};
 };
 async function populate(v,current,context){
  const s=host?v.sender:v;
  for(const key of ['enabled','destination_ipv4','destination_port','maximum_severity','rate','burst']){if(key==='enabled')nodes[key].checked=s[key];else nodes[key].value=s[key]===null?'':String(s[key]);}
  nodes.event_codes.value=s.event_codes.join(', ');
  if(host){
   nodes.interface.value=s.interface||'';nodes.native_enabled.checked=s.native_enabled;const c=v.collector;
   nodes.collector_enabled.checked=c.enabled;nodes.bind_ipv4.value=c.bind_ipv4||'';nodes.collector_interface.value=c.interface||'';nodes.collector_port.value=String(c.port);peers.value=c.peers.map(p=>`${p.slot} ${p.ipv4}`).join('\n');
   while(sources.children.length>1)sources.lastChild.remove();
   const selected=new Map(s.usb_sources.map(x=>[x.device_id,x]));let candidates=[];
   try{const response=await options.usbSources();candidates=Array.isArray(response?.candidates)?response.candidates.slice(0,8):[];}catch(_){}
   if(current!==epoch||context!==options.context?.())return;
   for(const selectedSource of selected.values())if(!candidates.some(x=>x.device_id===selectedSource.device_id))candidates.push({catalog_id:selectedSource.profile_id,device_id:selectedSource.device_id,name:'Unavailable saved Pico'});
   for(const item of candidates.slice(0,16)){
    const label=el('label',`${item.name||'Pico'} · ${item.device_id}`),check=el('input');label.className=options.checkClass||'check';check.type='checkbox';check.checked=selected.has(item.device_id);check.dataset.profile=item.catalog_id;check.dataset.device=item.device_id;label.prepend(check);sources.append(label);
   }
  }
 }
 async function perform(saveRequested=false){
  if(busy||!available||saveRequested&&(!snapshot||!writable||reviewRequired))return;
  const current=epoch,context=options.context?.(),expected=snapshot;
  let draft;
  try{if(saveRequested)draft=fromForm();}catch(error){status.textContent=error.message;return;}
  busy=true;show();
  try{
   const caps=(await options.exchange(options.capabilitiesPath||'udp-logging/capabilities')).value;
   if(!capabilities(caps,host))throw Error('This device does not support the required UDP logging contract. Update it and read settings again.');
   if(expected&&!equalTarget(expected.target,caps.target))throw Error('The selected device or boot changed. Read settings again.');
   if(current!==epoch||context!==options.context?.())return;
   if(saveRequested){snapshot=null;await options.exchange(options.resourcePath||'udp-logging','PUT',{schema:caps.settings_schema,target:expected.target,settings:draft},revision);}
   const response=await options.exchange(options.resourcePath||'udp-logging');
   if(current!==epoch||context!==options.context?.())return;
   if(!readback(response.value,caps,host)||!printable(response.revision,128))throw Error('UDP logging readback is inconsistent. Read settings again before saving.');
   if(!saveRequested&&dirty&&(!expected||response.revision!==revision))reviewRequired=true;
   snapshot=response.value;revision=response.revision;
   diagnostic=null;
   try {const d=(await options.exchange((options.resourcePath||'udp-logging')+'/test/capabilities')).value;if(testCapabilities(d,caps,host))diagnostic=d;}catch(_){}
   if(current!==epoch||context!==options.context?.())return;
   identity.textContent=host?`Settings on this Pi · ${caps.target.collector_id}`:`Pico · ${caps.target.device_id} · boot ${caps.target.boot_id}`;
   if(saveRequested||!dirty){await populate(snapshot.saved,current,context);dirty=false;reviewRequired=false;}
   if(current!==epoch||context!==options.context?.())return;
   showSaved(snapshot.saved);comparison.hidden=!dirty;comparison.open=reviewRequired;
   const active=host?snapshot.active.sender:snapshot.active;
   status.textContent=`Saved revision ${revision}; applied ${snapshot.active.applied_revision||'none'}. Export: ${(host?snapshot.saved.sender:snapshot.saved).enabled?'enabled':'disabled'}. Active: ${active.state.replaceAll('_',' ')}. Locally submitted: ${active.counters.locally_submitted}; local failures: ${active.counters.locally_failed}; queued: ${active.queued}; overflow: ${active.counters.overflow}; expired: ${active.counters.expired}. Collector receipt is unknown.${host?` Collection: ${snapshot.active.collector.state.replaceAll('_',' ')} at ${snapshot.active.collector.bind_ipv4||'no bound address'}:${snapshot.active.collector.port||'no port'}. Accepted into journal: ${snapshot.active.collector.counters.journal_submitted}; duplicates: ${snapshot.active.collector.counters.duplicate}; conflicts: ${snapshot.active.collector.counters.conflict}; discontinuities: ${snapshot.active.collector.counters.discontinuity}.`:''}${dirty?' Your draft is preserved; review it against the saved state before saving.':''}`;
   if(reviewRequired)status.textContent+=' Compare the saved settings above with your preserved draft, then confirm review before saving.';
  }catch(error){snapshot=null;revision='';dirty=dirty||saveRequested;reviewRequired=dirty;comparison.hidden=true;status.textContent=`${saveRequested?'Save unconfirmed. ':''}${error.message||'UDP logging unavailable.'} Read settings and review saved state before another save. Your draft is preserved.`;}
  finally{busy=false;show();}
 }
 form.addEventListener('input',()=>{dirty=true;show();});form.addEventListener('submit',e=>{e.preventDefault();perform(true);});read.addEventListener('click',()=>perform(false));
 review.addEventListener('click',()=>{if(!snapshot||busy||!writable)return;reviewRequired=false;status.textContent='Saved settings reviewed. Your draft is unchanged. Save explicitly to apply it.';show();});
 test.addEventListener('click',async()=>{
  if(test.disabled)return;
  const current=epoch,context=options.context?.(),expected=snapshot;busy=true;show();
  try {
   const r=(await options.exchange((options.resourcePath||'udp-logging')+'/test','POST',{schema:'udp-logging-test/1',target:expected.target},revision)).value;
   if(current!==epoch||context!==options.context?.())return;
   if(!keys(r,['schema','target','accepted','receipt'])||r.schema!=='udp-logging-test/1'||!equalTarget(r.target,expected.target)||typeof r.accepted!=='boolean'||r.receipt!=='unknown')throw Error('Inconsistent test reply');
   busy=false;await perform(false);
   status.textContent+=(r.accepted?' One test event was admitted locally.':' The test event was dropped locally.')+' Collector receipt is unknown.';
  }catch(error){snapshot=null;revision='';status.textContent='Test result unconfirmed. Read settings before another test or save. '+error.message;}
  finally{busy=false;show();}
 });show();
 return {invalidate,setAvailability(value){if(available!==value){available=value;if(!value)invalidate();}show();},setWritable(value){writable=value;show();},read:()=>perform(false)};
}
function parseJson(text,limit=16384){
 if(typeof text!=='string'||text.length>limit)throw Error('UDP document exceeds its limit.');
 const stack=[];
 for(let i=0;i<text.length;i++){
  const c=text[i];
  if(c==='{'||c==='['){stack.push(c==='{'?new Set():null);if(stack.length>8)throw Error('UDP document is too deeply nested.');}
  else if(c==='}'||c===']')stack.pop();
  else if(c==='"'){
   const start=i;let closed=false;
   while(++i<text.length){if(text[i]==='\\'){i++;continue;}if(text[i]==='"'){closed=true;break;}}
   if(!closed)throw Error('Invalid UDP JSON string.');
   let next=i+1;while(/\s/.test(text[next]||'x'))next++;
   if(text[next]===':'&&stack.at(-1) instanceof Set){const key=JSON.parse(text.slice(start,i+1));if(stack.at(-1).has(key))throw Error('Duplicate UDP document field.');stack.at(-1).add(key);}
  }
 }
 return JSON.parse(text);
}
const exported={parseJson,keys,id,integer,decimal,ip,settings,collector,capabilities,readback,testCapabilities,mount};root.WsprryUdp=exported;if(typeof module!=='undefined')module.exports=exported;
})(typeof window==='undefined'?globalThis:window);

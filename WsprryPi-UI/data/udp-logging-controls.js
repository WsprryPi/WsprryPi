// SPDX-License-Identifier: MIT
(function(root){
'use strict';
if(!root.document)return;
const base=()=>root.WSPRRYPI_PATHS?.sharedApiPath||'/api/v1';
async function exchange(path,method='GET',body,revision){
 const response=await root.fetch(`${base()}/${path}`,{method,cache:'no-store',signal:AbortSignal.timeout(15000),...(body?{headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','If-Match':revision},body:JSON.stringify(body)}:{})});
 const value=root.WsprryUdp.parseJson(await response.text());
 if(!response.ok && value?.error?.code==='udp_resource_limits_unavailable')throw Error('This host cannot enforce the required memory and process limits. Ask the host administrator to enable its memory and PID cgroup controllers, then read settings again.');
 if(!response.ok)throw Error(response.status===404?'UDP logging is unsupported by this software. Update the selected device.':response.status===412?'Settings changed elsewhere.':response.status===401||response.status===403?'Unlock the selected Pico or sign in before reading its settings.':response.status===409?'Select and unlock the intended member, reconcile any active job, then read settings.':'The logging request was not confirmed.');
 if(method==='PUT'&&path==='host/udp-logging')root.dispatchEvent(new CustomEvent('wsprry-udp-policy-changed'));
 return {value,revision:response.headers.get('ETag')||''};
}
function init(){
 const common={inputClass:'form-control',buttonClass:'btn btn-outline-secondary',checkClass:'form-check'};
 const member=root.document.getElementById('member-udp-logging-controls');
 if(member){
  root.WsprryMemberUdp=root.WsprryUdp.mount(member,{...common,exchange:(path,...rest)=>exchange(`member/${path}`,...rest),context:()=>{
   const s=root.WtpUi?.read()||{};return [s.Transport,s.Hostname,s['TCP Port'],s['USB Serial'],s['Device ID']].join('|');
  }});
 }
 const host=root.document.getElementById('host-udp-logging-controls');
 if(host){
 const installation=root.document.createElement('p');installation.setAttribute('role','status');installation.setAttribute('aria-live','polite');host.append(installation);
 async function hostExchange(path,...args){
  const result=await exchange(path,...args);
  if(path==='host/udp-logging'&&(!args[0]||args[0]==='GET')){
   try{const {value:v}=await exchange('host/udp-logging/installation');
    if(!root.WsprryUdp.keys(v,['schema','target','worker_state','native_journal_state','policy_state','service_state'])||v.schema!=='host-udp-installation/1'||!root.WsprryUdp.keys(v.target,['scope','collector_id'])||v.target.scope!=='host'||v.target.collector_id!==result.value.target.collector_id||!['missing','unsafe','installed'].includes(v.worker_state)||!['available','unavailable'].includes(v.native_journal_state)||!['valid','invalid','unavailable'].includes(v.policy_state)||!['inactive','running','bind_error','ready'].includes(v.service_state))throw Error('Invalid installation status');
    installation.textContent=`Logging worker: ${v.worker_state}. Native journal: ${v.native_journal_state}. Policy: ${v.policy_state}. Service: ${v.service_state.replaceAll('_',' ')}.`;
   }catch(_){installation.textContent='Logging installation status is unavailable. Reopen the Logs page to retry.';}
  }return result;
 }
 root.WsprryHostUdp=root.WsprryUdp.mount(host,{...common,host:true,autoRead:true,capabilitiesPath:'udp-logging/capabilities',resourcePath:'host/udp-logging',exchange:hostExchange,
  usbSources:async()=>{const r=await exchange('host/logging');if(r.value.schema!=='host-usb-logging/1')throw Error('USB collection settings unavailable');return r.value;}});
 }
}
if(root.document.readyState==='loading')root.document.addEventListener('DOMContentLoaded',init);else init();
})(typeof window==='undefined'?globalThis:window);

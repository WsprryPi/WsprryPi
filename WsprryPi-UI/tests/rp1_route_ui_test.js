"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const root=path.resolve(__dirname,"..");
const view=fs.readFileSync(path.join(root,"data/views/config.php"),"utf8");
const script=fs.readFileSync(path.join(root,"data/index.js"),"utf8");
const siteScript=fs.readFileSync(path.join(root,"data/site.js"),"utf8");
const styles=fs.readFileSync(path.join(root,"data/index.css"),"utf8");
const header=fs.readFileSync(path.join(root,"data/header.php"),"utf8");
assert.match(view,/Requested[\s\S]*Persisted[\s\S]*Configured[\s\S]*Active[\s\S]*Module reported[\s\S]*Reconciled[\s\S]*Boot ownership[\s\S]*Pending transaction[\s\S]*Fixed services[\s\S]*Endpoint[\s\S]*Output inhibited[\s\S]*Operational readiness[\s\S]*Development policy[\s\S]*Operation lifecycle[\s\S]*Product qualification/);
assert.match(view,/>Check route</); assert.match(view,/>Cancel</);
assert.match(view,/role="status" aria-live="polite" aria-atomic="true"/);
assert.match(view,/<option value="">None<\/option>/);
assert.match(view,/<input\s+(?=[^>]*id="transmit_backend")(?=[^>]*type="checkbox")(?=[^>]*role="switch")[^>]*>/);
assert.doesNotMatch(view,/<select\s+(?=[^>]*id="transmit_backend")[^>]*>/);
assert.match(view,/id="rp1-route-panel" data-debug-retained="true" hidden/);
assert.match(view,/id="rp1-route-progress-modal"[\s\S]*aria-describedby="rp1-route-progress-message"/);
assert.match(view,/id="rp1-route-progress-close"[\s\S]*disabled/);
for(const state of ["checking","active","reboot_required","applying","staged","mismatch","unavailable","rollback","rollback_required"])
 assert.match(script,new RegExp(`${state}:`),`missing ${state} state`);
for(const state of ["runtime_preflight_ready","runtime_preflight_failed","runtime_switch_queued","runtime_remove_queued","runtime_recovery_queued","runtime_neutral_running","runtime_neutral_stopped","runtime_neutral_restoration_failed"])
 assert.match(script,new RegExp(`${state}:`),`missing ${state} state`);
assert.match(script,/body:JSON\.stringify\(\{operation, route:requested, generation:this\.generation\}\)/);
assert.match(script,/operation:"preflight"[\s\S]*operation:"apply-and-reboot"/);
assert.match(script,/if \(rp1RouteUi && rp1RouteUi\.visible\(\)\)[\s\S]*return;[\s\S]*scheduleAutosave\(\)/);
assert.match(script,/draft==="None" \? "Remove route"/);
assert.match(script,/maxAutomaticPolls=12/);
assert.match(script,/requestWithTimeout\(options,timeoutMs=8000\)/);
assert.match(script,/this\.pollStatus\(\)/);
assert.match(script,/select:not\(#tx_pin\)/);
assert.match(siteScript,/isRp1RouteStatusModalActive/);
assert.match(script,/rp1-development-policy/);
assert.match(view,/id="rp1-development-policy">Disabled/);
assert.match(view,/id="rp1-route-eligible">Unqualified/);
assert.doesNotMatch(view,/Package|Predecessor evidence|Module \/ ABI|Compatibility/);
assert.match(view,/Installation leaves RP1 route-neutral/);
assert.match(script,/function rp1GpioRouteSelectable\(\)/);
assert.match(script,/function transmitBackendForPersistence\(\)/);
assert.match(script,/function rp1RouteUnavailableMessage\(\)/);
assert.match(script,/function rp1RouteSelectorHint\(\)/);
assert.match(script,/Off uses GPIO through the RP1 GPCLK provider\./);
assert.match(siteScript,/if \(typeof initializeRp1RouteUi === "function"\) \{\s*initializeRp1RouteUi\(\);/);
assert.match(siteScript,/\["gpio",\s*"rp1-gpclk",\s*"si5351", "wtp"\]\.includes\(transmitBackend\)/);
assert.match(siteScript,/setTransmitBackendSelection\(transmitBackend, true, false\)/);
assert.match(script,/window\.confirm/);
assert.match(script,/wsprrypi\.service and soapyremote-server\.service/);
assert.match(header,/'rp1RoutePath' => \$basePath \. '\/api\/rp1-gpclk-route'/);
assert.match(styles,/@media \(max-width: 575\.98px\)[\s\S]*\.rp1-route-actions > \.btn/);
assert.match(styles,/runtime_preflight_failed[^}]*--wspr-state-danger/);
console.log("rp1_route_ui_test passed");

// Execute the real controller without a browser or external dependencies.
const vm=require("node:vm");
const nodes=new Map();
function element(id){
 if(!nodes.has(id)) nodes.set(id,{dataset:{},attributes:{},setAttribute(name,value){this.attributes[name]=value},getAttribute(name){return this.attributes[name]},querySelector(selector){return element(`${id}:${selector}`)},disabled:false,hidden:false,textContent:""});
 return nodes.get(id);
}
let pin=20;
const timers=[];
const context={window:{fetch:async()=>{throw Error("no fixture")},confirm:()=>true,setTimeout:callback=>{timers.push(callback);return timers.length},clearTimeout:()=>{}},
 document:{getElementById:element,querySelector:element},
 getTxPin:()=>pin,setTxPin:value=>{pin=value},
 bootstrap:{Modal:{getOrCreateInstance:()=>({show(){}})}},
 $:selector=>({prop(name,value){element(selector.slice(1))[name]=value;return this},
 text(value){element(selector.slice(1)).textContent=value;return this}})};
vm.createContext(context);
vm.runInContext(script.slice(script.indexOf("const RP1_ROUTE_STATES"),script.indexOf("function initializeRp1RouteUi"))+
 ';globalThis.controller=new Rp1RouteUiController("/offline");',context);
(async()=>{
 const controller=context.controller;
 controller.render({profile:"legacy",ok:true,state:"active",persisted:"GPIO20",active:"GPIO20",compatible:true});
 assert.equal(element('#tx_pin option[value=""]').hidden,true,"None stays unavailable to the legacy development profile");
 controller.render({profile:"runtime",ok:true,state:"runtime_inhibited",persisted:"GPIO20",active:"GPIO4",compatible:true});
 assert.equal(element('#tx_pin option[value=""]').hidden,false,"None is available to the runtime profile");
 assert.equal(element("rp1-route-apply").textContent,"Switch route");
 let confirmations=0,requests=[];
 context.window.confirm=()=>{confirmations++;return true};
 controller.request=async(url,options)=>{requests.push(JSON.parse(options.body));return {
  ok:false,json:async()=>({ok:false,profile:"runtime",state:"runtime_preflight_failed",
   message:"Route not switched. The provider route plan was rejected before any change began. Transmission remains disabled.",
   changeStarted:false,recoveryRequired:false})};};
 await controller.applyAndReboot();
 assert.equal(confirmations,0,"failed preflight cannot request confirmation");
 assert.deepEqual(requests,[{operation:"preflight",route:"GPIO20",generation:0}],
  "failed preflight cannot send a switch request");
 assert.equal(element("rp1-route-state").textContent,"Route not switched");
 assert.equal(element("rp1-route-active").textContent,"GPIO4",
  "failed preflight preserves the last confirmed route facts");
 assert.equal(element("rp1-route-rollback").hidden,true,
  "failed preflight does not expose transaction recovery");
 controller.completionUnknown=false;
 controller.request=async()=>({ok:false,json:async()=>({ok:false,profile:"runtime",
  state:"runtime_unknown",message:"Route plan evidence was inconsistent.",
  changeStarted:false})});
 await controller.applyAndReboot();
 assert.equal(element("rp1-route-state").textContent,"Route not switched",
  "known no-change preflight rejection is not presented as reconnecting");
 assert.equal(controller.completionUnknown,false,
  "known no-change preflight rejection remains retryable");
 controller.request=async()=>({ok:false,json:async()=>({ok:false,profile:"runtime",
  state:"runtime_recovery",message:"Existing transaction requires recovery."})});
 await controller.applyAndReboot();
 assert.equal(element("rp1-route-state").textContent,"Recovery required",
  "preflight handling preserves independently reported recovery evidence");
 assert.equal(element("rp1-route-rollback").hidden,false,
  "reported recovery evidence keeps explicit recovery available");
 requests=[];
 controller.request=async(url,options)=>{requests.push(JSON.parse(options.body));throw Error("disconnected")};
 await controller.operate("switch");
 assert.equal(requests.length,1,"no automatic effect retries");
 assert.equal(element("rp1-route-state").dataset.state,"runtime_unknown");
 controller.select("GPIO20");
 assert.equal(element("rp1-route-apply").disabled,true,"selection cannot clear unknown completion");
 controller.render({profile:"runtime",ok:true,state:"runtime_recovery",persisted:"GPIO20",active:"GPIO4"});
 assert.equal(element("rp1-route-rollback").hidden,false);
 context.window.confirm=()=>false;
 const beforeRollbackRequests=requests.length;
 await controller.operate("rollback");
 assert.equal(requests.length,beforeRollbackRequests,"cancelled debug recovery performs no mutation");
 controller.render({profile:"runtime",ok:true,state:"runtime_ready",persisted:"GPIO20",active:"GPIO20"});
 assert.equal(element("rp1-route-state").textContent,"Route selected");
 for(const id of ["rp1-route-module-fact","rp1-route-endpoint-fact",
  "rp1-route-output-inhibited-fact","rp1-route-operational-ready-fact",
  "rp1-development-policy-fact","rp1-operation-lifecycle-fact","rp1-route-eligible-fact"])
  assert.equal(element(id).hidden,true,`${id} must not show unavailable provider facts in runtime administration`);
 controller.render({profile:"runtime",ok:false,state:"runtime_restoration_failed",persisted:"GPIO20",active:"GPIO4"});
 assert.equal(element("rp1-route-apply").disabled,true,"restoration failure cannot launch another switch");
 assert.match(element("rp1-route-feedback").textContent,/restore --execute/);
 controller.render({profile:"runtime",ok:true,state:"runtime_recovery_queued",persisted:"GPIO20",active:"GPIO4"});
 assert.equal(element("rp1-route-state").textContent,"Recovery queued");
 assert.equal(element("rp1-route-apply").disabled,true,"queued recovery cannot launch another switch");

 controller.render({profile:"runtime",ok:true,state:"runtime_inhibited",persisted:"GPIO20",active:"GPIO20",compatible:true});
 pin=null;controller.select("None");
 assert.equal(element("rp1-route-apply").textContent,"Remove route");
 assert.equal(element("rp1-route-state").textContent,"Route Unsaved",
  "an unapplied route selection is distinguished from application idle state");
 pin=20;controller.select("GPIO20");
 assert.equal(element("rp1-route-state").textContent,"Application idle",
  "restoring the active selection restores the confirmed provider state");
 pin=null;controller.select("None");
 controller.progressActive=true;let duplicateRequests=0;controller.request=async()=>{duplicateRequests++;throw Error("duplicate")};
 await controller.applyAndReboot();
 assert.equal(duplicateRequests,0,"an active progress cycle blocks duplicate route actions");
 controller.progressActive=false;
 let cancelRequestCount=0;context.window.confirm=()=>false;requests=[];controller.request=async()=>{cancelRequestCount++;throw Error("cancelled removal must not request")};
 await controller.applyAndReboot();
 assert.equal(pin,20,"cancelled removal restores the confirmed active route");
 assert.equal(cancelRequestCount,0,"cancelled removal performs no request");
 pin=null;controller.select("None");
 requests=[];context.window.confirm=()=>true;
 controller.request=async(url,options)=>{requests.push(JSON.parse(options.body));return {ok:true,json:async()=>({ok:true,profile:"runtime",state:"runtime_remove_queued",persisted:"GPIO20",active:"GPIO20",compatible:true})};};
 await controller.applyAndReboot();
 assert.deepEqual(requests,[{operation:"remove",route:"GPIO20",generation:0}],"None launches exactly one removal for the confirmed active route");
 assert.equal(controller.progressActive,true,"route removal keeps the status modal active");
 assert.equal(timers.length>0,true,"queued removal schedules read-only status polling");
 controller.request=async(url,options)=>{assert.equal(options.method,undefined,"polling is read-only");return {ok:true,json:async()=>({ok:true,profile:"runtime",state:"runtime_neutral_running",persisted:"GPIO20",active:"None",compatible:true,journal:"recovered-inhibited"})};};
 await controller.pollStatus();
 assert.equal(element("rp1-route-progress-state").textContent,"Route removed");
 assert.equal(element("rp1-route-progress-close").disabled,false,"confirmed neutral state makes the modal dismissible");
 assert.equal(pin,null,"confirmed neutral state retains None in the selector");
 controller.closeProgress();
 for(const persisted of ["GPIO4","GPIO20"]){
  for(const state of ["runtime_neutral_running","runtime_neutral_stopped"]){
   pin=Number(persisted.slice(4)); // Configuration refresh retains a valid fallback pin.
   controller.request=async()=>({ok:true,json:async()=>({ok:true,profile:"runtime",
    state,persisted,requested:persisted,configured:"None",active:"None",compatible:true})});
   await controller.query();
   assert.equal(pin,null,"status refresh after dismissal must preserve the confirmed removed route");
   assert.equal(element("rp1-route-apply").disabled,true,"removed route must not enable a redundant route action");
   assert.equal(controller.lastPersistableRoute,persisted,"removal retains the saved fallback pin for configuration persistence");
  }
  controller.render({ok:true,profile:"runtime",state:"runtime_ready",persisted,
   requested:persisted,configured:persisted,active:persisted,compatible:true});
  assert.equal(pin,Number(persisted.slice(4)),"a completed switch selects the confirmed GPIO route");
  assert.equal(element("rp1-route-apply").disabled,true,"a completed switch has no pending action");
 }
 console.log("runtime route UI behavior: PASS");
})().catch(error=>{console.error(error);process.exitCode=1});

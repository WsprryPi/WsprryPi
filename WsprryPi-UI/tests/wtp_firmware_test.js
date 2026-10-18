// SPDX-License-Identifier: MIT
"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm"),path=require("node:path");
const source=fs.readFileSync(path.join(__dirname,"../data/wtp-firmware.js"),"utf8");
async function main() {
    const fields=new Map(),requests=[];
    const element=id=>{if(!fields.has(id))fields.set(id,{value:id.endsWith("branch")?"main":id.endsWith("source")?"latest":"",listeners:{},disabled:false,hidden:false,textContent:"",setAttribute(){},addEventListener(n,f){this.listeners[n]=f;}});return fields.get(id);};
    let state={serial:"1545B6EFBF78028C",device_id:"c".repeat(32),selection_id:"fresh-selection",installed_version:"old",running:false,maintenance:false},fail=false;
    const root={document:{readyState:"complete",getElementById:element},AbortSignal,confirm:()=>true,setTimeout:()=>0,clearTimeout(){},fetch:async(url,options)=>{
        requests.push({url,...options});if(fail)throw Error("Disconnected");
        if(options.method==="POST") {const p=JSON.parse(options.body);assert.equal(p.selection_id,"fresh-selection");
            if(p.action==="plan")state={...state,operation:"plan",plan_id:"bound",result:{ok:true,version:"checked",branch:p.branch,source:"a".repeat(40),workflow:{run_id:7,attempt:1}}};
            else state={...state,operation:p.action,running:true,maintenance:true};}
        return {ok:true,json:async()=>state};}};
    root.window=root;vm.runInNewContext(source,root);root.WtpFirmware.setAvailability(true,"Pico C","fresh-selection");
    const el=name=>element("wtp-firmware-"+name), click=name=>el(name).listeners.click();
    assert(el("plan").disabled);await click("read");assert.match(el("read-status").textContent,/Status read at/);await click("plan");
    assert.equal(JSON.parse(requests.at(-1).body).branch,"main");assert.equal(el("upgrade").disabled,false);
    el("branch").value="devel";el("branch").listeners.change();assert(el("upgrade").disabled);
    await click("plan");assert.equal(JSON.parse(requests.at(-1).body).branch,"devel");
    root.confirm=()=>false;let count=requests.length;await click("upgrade");assert.equal(requests.length,count);
    root.confirm=()=>true;await click("upgrade");assert.equal(JSON.parse(requests.at(-1).body).plan_id,"bound");assert(el("upgrade").disabled);assert.match(el("feedback").textContent,/Keep the cable/);
    state={...state,running:false,manual_bootsel_required:true,recovery_plan_id:"bound",recovery_version:"checked",result:{ok:true,stage:"awaiting_bootsel"}};
    await click("read");assert.equal(el("recover").textContent,"Continue after BOOTSEL");
    assert.equal(el("recover").disabled,false);assert.equal(el("migration").hidden,false);
    assert.match(el("feedback").textContent,/No firmware has been written/);assert(el("plan").disabled);
    await click("recover");assert.equal(JSON.parse(requests.at(-1).body).action,"recover");
    state={...state,running:false,manual_bootsel_required:false};
    state={...state,running:false,result:{ok:false,error:"Verification failed",recovery_required:true},recovery_plan_id:"bound",recovery_version:"checked"};await click("read");assert.equal(el("recover").hidden,false);assert.match(el("feedback").textContent,/remains paused/);
    await click("recover");assert.equal(JSON.parse(requests.at(-1).body).action,"recover");
    state={...state,running:false,programming_verified:true};await click("read");assert.match(el("feedback").textContent,/without reflashing/);
    fail=true;await click("read");assert(el("plan").disabled);assert.match(el("feedback").textContent,/may already be running/);
    assert.match(el("read-status").textContent,/Status read failed/);
    root.WtpFirmware.setAvailability(true,"Pico C","another-selection");
    assert(el("upgrade").disabled);assert(el("plan").disabled);assert.match(el("feedback").textContent,/Selected Pico changed/);
    let release;
    root.fetch=()=>new Promise(resolve=>{release=resolve;});
    const pending=click("read");
    assert.equal(el("read").textContent,"Reading status…");
    assert.match(el("read-status").textContent,/Reading firmware status/);
    root.WtpFirmware.setAvailability(true,"Pico B","new-selection");
    release({ok:true,json:async()=>({...state,operation:"plan",result:{ok:true,version:"old member"}})});
    await pending;
    assert(el("upgrade").disabled);assert(el("plan").disabled);assert.match(el("identity").textContent,/Read the selected Pico/);
    assert.equal(el("read-status").textContent,"");
    let rejectOld;
    root.fetch=()=>new Promise((_,reject)=>{rejectOld=reject;});
    const oldFailure=click("read");
    root.WtpFirmware.setAvailability(true,"Pico C","latest-selection");
    rejectOld(Error("Old member timed out"));await oldFailure;
    assert.match(el("feedback").textContent,/Selected Pico changed/);
    assert(el("upgrade").disabled);assert(el("plan").disabled);
    root.WtpFirmware.setAvailability(false);assert(element("wtp-firmware").hidden);
    console.log("Firmware UI branch defaults, plan invalidation, confirmation, pending, refusal and recovery passed");
}
main().catch(error=>{console.error(error);process.exitCode=1;});

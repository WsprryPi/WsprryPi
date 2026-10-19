// SPDX-License-Identifier: MIT
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), vm = require("node:vm"), path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "../data/wtp-firmware.js"), "utf8");
function fixture() {
    const fields = new Map(), timers = new Map(), events = {}, requests = [], safety = {pending:false};
    let now = 10000, nextTimer = 0, failRead = false, losePost = false, holdPost = null, holdRead = null;
    let state = {serial:"1545B6EFBF78028C", device_id:"c".repeat(32), selection_id:"selection", installed_version:"old", running:false, maintenance:false};
    const el = name => {
        const id = "wtp-firmware" + (name ? "-" + name : "");
        if (!fields.has(id)) fields.set(id, {value:name === "branch" ? "devel" : name === "source" ? "latest" : "", listeners:{}, disabled:false,
            hidden:false, textContent:"", setAttribute(){}, addEventListener(event, callback){this.listeners[event]=callback;}});
        return fields.get(id);
    };
    const root = {PicoFirmwareSafety:{begin(){safety.pending=true;},observeFirmware(value){safety.pending=value.running===true||value.maintenance===true;}},
        document:{readyState:"complete",visibilityState:"visible",getElementById:id=>el(id.replace(/^wtp-firmware-?/,"")),
        addEventListener:(name, callback)=>{events[name]=callback;}}, AbortSignal, confirm:()=>true,
        Date:class extends Date {constructor(...args){super(...(args.length?args:[now]));}static now(){return now;}},
        setTimeout:(callback, delay)=>{timers.set(++nextTimer,{callback,at:now+delay});return nextTimer;},clearTimeout:id=>timers.delete(id),
        addEventListener:(name,callback)=>{events[name]=callback;}, fetch:async(url,options)=>{
            requests.push({method:options.method,body:options.body});
            if (options.method === "POST") {
                const action=JSON.parse(options.body).action;
                state={...state, operation:action, running:true, maintenance:action!=="plan", result:null, plan_id:"plan"};
                if (losePost) {losePost=false;throw Error("Reply lost");}
                if (holdPost) await new Promise(resolve=>{holdPost.release=resolve;});
            } else {
                if(failRead) {failRead=false;throw Error("Status temporarily unavailable");}
                if(holdRead) await new Promise(resolve=>{holdRead.release=resolve;});
            }
            return {ok:true,json:async()=>JSON.parse(JSON.stringify(state))};
        }};
    root.window=root;vm.runInNewContext(source,root);
    const advance=async ms=>{
        const end=now+ms;
        for (;;) {
            const due=[...timers].filter(([,timer])=>timer.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];
            if(!due)break;
            timers.delete(due[0]);now=due[1].at;await due[1].callback();
        }
        now=end;
    };
    const complete=()=>{state={...state,running:false,maintenance:false,result:{ok:true,version:"checked",branch:"devel",source:"a".repeat(40),workflow:{run_id:7,attempt:1}}};};
    return {root,el,events,requests,timers,advance,complete,safety,click:name=>el(name).listeners.click(),
        failRead:()=>{failRead=true;},losePost:()=>{losePost=true;},holdPost:value=>{holdPost=value;},holdRead:value=>{holdRead=value;},
        setState:value=>{state={...state,...value};}};
}
async function main() {
    const f=fixture();
    f.root.WtpFirmware.setAvailability(true,"C","selection","1545B6EFBF78028C");
    await f.advance(0);
    assert.match(f.el("read-status").textContent,/updated automatically/);
    const held={};f.holdPost(held);const pending=f.click("plan");
    assert(!f.el("activity").hidden);assert.match(f.el("phase").textContent,/Requesting image check/);
    held.release();await pending;f.holdPost(null);
    assert.match(f.el("phase").textContent,/Downloading and checking/);
    await f.advance(5000);
    assert.match(f.el("timing").textContent,/Monitoring for 0:05/);
    assert.match(f.el("timing").textContent,/percentage is not available/);
    f.complete();await f.advance(1500);
    assert.match(f.el("feedback").textContent,/Checked checked/);assert(!f.el("upgrade").disabled);assert(f.el("activity").hidden);
    assert.equal(f.requests.filter(r=>r.method==="POST").length,1,"polling never repeats a mutation");

    await f.click("plan");f.failRead();await f.advance(1500);
    assert.match(f.el("phase").textContent,/Retrying automatically/);assert(f.el("upgrade").disabled);
    f.complete();await f.advance(3000);
    assert.match(f.el("feedback").textContent,/Checked checked/);assert(!f.el("upgrade").disabled);

    f.losePost();await f.click("plan");const posts=f.requests.filter(r=>r.method==="POST").length;
    assert.match(f.el("feedback").textContent,/never repeated automatically/);
    f.complete();await f.advance(3000);assert(!f.el("upgrade").disabled);
    assert.equal(f.requests.filter(r=>r.method==="POST").length,posts,"lost POST replies recover through GET only");
    const heldRead={};f.holdRead(heldRead);const backgroundRead=f.advance(3000);
    assert.equal(f.el("branch").disabled,false,"idle background reads must not disrupt editing or close selectors");
    f.el("branch").value="main";f.el("branch").listeners.change();await f.advance(3000);
    heldRead.release();f.holdRead(null);await backgroundRead;
    assert(f.el("upgrade").disabled,"background reads must not restore an invalidated plan");
    assert.match(f.el("feedback").textContent,/selection changed/);assert.equal(f.el("branch").value,"main");
    f.root.WtpFirmware.setAvailability(false);assert.equal(f.timers.size,0);
    f.root.WtpFirmware.setAvailability(true,"C","selection","1545B6EFBF78028C");await f.advance(0);
    assert.match(f.el("feedback").textContent,/Checked checked/);
    f.events.pagehide();assert.equal(f.timers.size,0);
    f.events.pageshow();await f.advance(0);assert.match(f.el("read-status").textContent,/updated automatically/);
    f.setState({serial:"OTHER"});await f.advance(3000);assert(f.el("upgrade").disabled,"wrong USB serial remains locked");
    f.events.pagehide();assert.equal(f.timers.size,0);

    const g=fixture();g.setState({operation:"upgrade",running:true,maintenance:true});
    g.root.WtpFirmware.setAvailability(true,"C","selection");await g.advance(0);
    assert.match(g.el("phase").textContent,/Backing up, installing and verifying/);
    g.complete();await g.advance(1500);
    assert.match(g.el("feedback").textContent,/Verified checked/);assert(g.el("activity").hidden);
    assert(g.requests.every(r=>r.method==="GET"),"another tab observes completion without any mutation");
    g.events.pagehide();
    const h=fixture();h.root.WtpFirmware.setAvailability(true,"C","selection");await h.advance(0);
    await h.click("plan");h.complete();await h.advance(1500);
    h.losePost();await h.click("upgrade");assert(h.safety.pending,"an ambiguous installation keeps maintenance held");
    h.complete();await h.advance(3000);
    assert.match(h.el("feedback").textContent,/Verified checked/);assert(!h.safety.pending,"automatic completion releases the local pending hold");
    assert.deepEqual(h.requests.filter(r=>r.method==="POST").map(r=>JSON.parse(r.body).action),["plan","upgrade"]);
    h.events.pagehide();
    const limited=fixture();limited.setState({operation:"plan",result:{ok:false,error:"GitHub's anonymous API request limit is exhausted (HTTP 403). Retry after 2026-10-18 21:00:00 UTC.",error_code:"github_rate_limited",retry_at:70,recovery_required:false}});
    limited.root.WtpFirmware.setAvailability(true,"C","selection");await limited.advance(0);
    assert.match(limited.el("feedback").textContent,/GitHub.*limit.*UTC/);
    assert(!limited.el("retry").hidden);assert.match(limited.el("retry").textContent,/Try again in 1:00/);
    assert(limited.el("plan").disabled);assert(!limited.el("read").disabled);
    await limited.click("plan");assert(limited.requests.every(r=>r.method==="GET"),"cooldown cannot send an early POST");
    limited.el("branch").value="main";limited.el("branch").listeners.change();await limited.advance(3000);
    assert.match(limited.el("feedback").textContent,/GitHub.*limit/);assert(limited.el("plan").disabled,"changing branches does not bypass a shared API limit");
    await limited.advance(57000);assert(!limited.el("plan").disabled);
    assert.match(limited.el("retry").textContent,/retry time has passed/);
    assert(limited.requests.every(r=>r.method==="GET"),"expiry never restarts a download");
    await limited.click("plan");assert.equal(limited.requests.filter(r=>r.method==="POST").length,1);
    limited.events.pagehide();
    const optional=fixture();optional.setState({operation:"plan",result:{ok:true,version:"checked",recent_ci:{available:false,error_code:"github_rate_limited",reason:"GitHub temporarily rate-limited this API request. Retry after 2026-10-18 21:00:00 UTC.",retry_at:70}}});
    optional.root.WtpFirmware.setAvailability(true,"C","selection");await optional.advance(0);
    assert.match(optional.el("feedback").textContent,/Additional CI status: GitHub.*UTC/);
    assert(!optional.el("upgrade").disabled,"optional CI status cannot invalidate a verified checked image");
    assert(optional.el("retry").hidden);optional.events.pagehide();
    console.log("Firmware polling: completion, retry cooldown/expiry, optional CI limits, lost replies, invalidation, lifecycle and exact USB binding passed");
}
main().catch(error=>{console.error(error);process.exitCode=1;});

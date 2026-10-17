// SPDX-License-Identifier: MIT
"use strict";
const assert=require("node:assert/strict"), readline=require("node:readline"),{spawn}=require("node:child_process");
const {browser,reply,target}=require("./wtp_usb_settings_test.js");
(async()=>{
    const args=process.argv.slice(2);if(!args.length)throw new Error("Usage: node wtp_usb_pair_test.js CLIENT_COMMAND [arguments]");
    const child=spawn(args[0],args.slice(1),{stdio:["pipe","pipe","inherit"]});
    let pending=null, exited=false;
    const lines=readline.createInterface({input:child.stdout});
    lines.on("line",line=>{if(!pending)return;const callback=pending;pending=null;callback.resolve(JSON.parse(line));});
    child.on("exit",code=>{exited=true;if(pending){pending.reject(new Error(`Pair client exited ${code}`));pending=null;}});
    child.on("error",error=>{if(pending){pending.reject(error);pending=null;}});
    const request=command=>new Promise((resolve,reject)=>{
        assert.equal(pending,null);const timer=setTimeout(()=>{if(pending){pending=null;reject(new Error("Pair exchange deadline"));child.kill("SIGTERM");}},15000);
        pending={resolve:value=>{clearTimeout(timer);resolve(value);},reject:error=>{clearTimeout(timer);reject(error);}};
        child.stdin.write(JSON.stringify(command)+"\n");
    });
    try {
        const b=browser(async(url,options)=>{
            const result=await request({method:options.method,path:url,body:options.body||"",headers:options.headers||{}});
            assert(!result.failure,JSON.stringify(result));return reply(result.body,result.status,result.etag);
        });
        await b.click("load");assert.match(b.element("wtp-usb-feedback").textContent,/No saved Wi-Fi/);
        b.element("wtp-usb-hostname").value="usb-bench.local";await b.click("name-save");
        assert.match(b.element("wtp-usb-feedback").textContent,/Settings saved/);await b.click("load");assert.equal(b.element("wtp-usb-hostname").value,"usb-bench.local");
        b.element("wtp-usb-ssid").value="synthetic-network";b.element("wtp-usb-password").value="synthetic-password";b.element("wtp-usb-time-server").value="192.0.2.1";
        await b.click("wifi-save");assert.match(b.element("wtp-usb-feedback").textContent,/Settings saved/);await b.click("load");assert.equal(b.element("wtp-usb-ssid").value,"synthetic-network");
        b.element("wtp-usb-ssid").value="replacement-network";b.element("wtp-usb-password").value="replacement-synthetic-password";await b.click("wifi-save");assert.match(b.element("wtp-usb-feedback").textContent,/Settings saved/);await b.click("load");assert.equal(b.element("wtp-usb-ssid").value,"replacement-network");
        await b.click("wifi-remove");assert.match(b.element("wtp-usb-feedback").textContent,/removed/);await b.click("load");assert.equal(b.element("wtp-usb-ssid").value,"");assert.equal(b.element("wtp-usb-hostname").value,"usb-bench.local");
        const clock=await request({synchronize:true});assert.equal(clock.accepted,true,JSON.stringify(clock));
        const progress=await request({method:"GET",path:"/api/v1/member/progress",body:""});assert.equal(progress.status,200,JSON.stringify(progress));
        const partial=await request({method:"POST",path:"/api/v1/member/progress/update",body:JSON.stringify({schema:"transmitter-progress/1",target,request_id:"1".repeat(32),fields:{callsign:"AA0NT"}}),revision:progress.etag});
        assert.equal(partial.status,200,JSON.stringify(partial));assert.equal(partial.body.station_complete,false);
        const initial=await request({method:"GET",path:"/api/v1/member/configuration",body:""});assert.equal(initial.status,200,JSON.stringify(initial));assert.equal(initial.body.config,null);
        const reread=await request({method:"GET",path:"/api/v1/member/progress",body:""});
        const complete=await request({method:"POST",path:"/api/v1/member/progress/update",body:JSON.stringify({schema:"transmitter-progress/1",target,request_id:"2".repeat(32),fields:{locator:"EM18",power_dbm:20}}),revision:reread.etag});
        assert.equal(complete.status,200,JSON.stringify(complete));assert.equal(complete.body.station_complete,true);
        const current=await request({method:"GET",path:"/api/v1/member/progress",body:""});
        const applied=await request({method:"POST",path:"/api/v1/member/progress/apply",body:JSON.stringify({schema:"transmitter-progress/1",target,application:{draft_revision:current.body.draft_revision,domain:"station"}}),revision:current.etag});
        assert.equal(applied.status,200,JSON.stringify(applied));
        const resources=await request({method:"GET",path:"/api/v1/member/application",body:""});
        assert.equal(resources.status,200,JSON.stringify(resources));assert.equal(resources.body.profile_owned_station,false);
        const station=await request({method:"PUT",path:"/api/v1/member/station",body:JSON.stringify({schema:"transmitter-station/2",target,station:{callsign:"K1ABC",locator:"EM18",power_dbm:20}}),revision:resources.etag,member_context:resources.body.member_context});
        assert.equal(station.status,200,JSON.stringify(station));
        const settings=await request({method:"GET",path:"/api/v1/member/configuration",body:""});assert.equal(settings.status,200,JSON.stringify(settings));
        assert.equal(settings.body.config.wifi.ssid,"");assert.equal(settings.body.config.station.callsign,"K1ABC");
        // Removal is not a reset: unrelated station and schedules are retained.
        await b.click("load");b.element("wtp-usb-ssid").value="recreated-network";b.element("wtp-usb-password").value="different-synthetic-password";await b.click("wifi-save");
        assert.match(b.element("wtp-usb-feedback").textContent,/Settings saved/);await b.click("load");assert.equal(b.element("wtp-usb-ssid").value,"recreated-network");
        console.log("Actual production pair: UMP framing+ACK, protected UI requests, blank USB name/Wi-Fi create/replace/delete-last/recreate, partial station progress vs applied configuration, preserved station and UTC synchronization passed (no network/hardware)");
    } finally {
        child.stdin.end();lines.close();if(!exited)child.kill("SIGTERM");
    }
})().catch(error=>{console.error(error);process.exitCode=1;});

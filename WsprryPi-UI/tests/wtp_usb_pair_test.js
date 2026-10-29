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
        await require('./udp_logging_pair_cases.js')(request);
        const preclock=await request({method:"GET",path:"/api/v1/member/progress",body:""});
        assert.equal(preclock.status,200,JSON.stringify(preclock));
        const preclockSave=await request({method:"POST",path:"/api/v1/member/progress/update",revision:preclock.etag,
            body:JSON.stringify({schema:"transmitter-progress/1",target,request_id:"8".repeat(32),fields:{callsign:"AA0NT",locator:null,power_dbm:0}})});
        assert.equal(preclockSave.status,200,JSON.stringify(preclockSave));
        assert.equal(preclockSave.body.station_complete,false);assert.equal(preclockSave.body.station.power_dbm,0);
        const clock=await request({synchronize:true});assert.equal(clock.accepted,true,JSON.stringify(clock));
        const blankDriveProgress=await request({method:"GET",path:"/api/v1/member/gpio-drive-progress",body:""});
        if(process.env.WSPRRYPI_GPIO_DRIVE_REQUIRED==='1')assert.equal(blankDriveProgress.status,200,JSON.stringify(blankDriveProgress));
        if(blankDriveProgress.status===200){
            const independent=await request({method:'PUT',path:'/api/v1/member/gpio-drive-progress',revision:blankDriveProgress.etag,
                body:JSON.stringify({schema:'transmitter-progress/2',target,request_id:'9'.repeat(32),fields:{gpio_drive_ma:8}})});
            assert.equal(independent.status,200,JSON.stringify(independent));assert.equal(independent.body.station_complete,false);assert.equal(independent.body.fields.gpio_drive_ma,8);
        }
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
        const dial=await request({raw_resource:'standalone-frequency'});
        assert.equal(dial.status,200,JSON.stringify(dial)); assert.equal(dial.body.frequency_hz,null);
        const selectedDial=await request({raw_resource:'standalone-frequency',method:'PUT',revision:dial.etag,
            body:JSON.stringify({schema:'transmitter-standalone-frequency/1',target,frequency_hz:3568737})});
        assert.equal(selectedDial.status,200,JSON.stringify(selectedDial));assert.equal(selectedDial.body.frequency_hz,3568737);
        // An incomplete saved draft and Pico-owned hostname are independent of applied station settings.
        const independentDraft=await request({raw_resource:'progress/2'});
        const savedDraft=await request({raw_resource:'progress/update/2',method:'POST',revision:independentDraft.etag,
            body:JSON.stringify({schema:'transmitter-progress/2',target,request_id:'a'.repeat(32),fields:{callsign:'BB1BB',locator:null}})});
        assert.equal(savedDraft.status,200,JSON.stringify(savedDraft));assert.equal(savedDraft.body.station_complete,false);
        const hostname=await request({method:'GET',path:'/api/v1/member/hostname',body:''});
        assert.equal(hostname.status,200,JSON.stringify(hostname));assert.match(JSON.stringify(hostname.body),/usb-bench\.local/);
        for(const mode of ['put','merge','replace']) {
            const read=await request({raw_resource:'config/2'});assert.equal(read.status,200,JSON.stringify(read));
            const config=mode==='merge'?{expires_utc_s:1900000000}:read.body.config;
            config.expires_utc_s=1900000000;
            const saved=await request({raw_resource:mode==='put'?'config/2':'config/update/2',method:mode==='put'?'PUT':'POST',revision:read.etag,
                body:JSON.stringify(mode==='put'?config:{schema:'transmitter-config-update/1',target,mode,config})});
            assert.equal(saved.status,200,JSON.stringify(saved));
            assert.equal((await request({raw_resource:'standalone-frequency'})).body.frequency_hz,3568737);
            const view=await request({raw_resource:'application/4'});
            assert.equal(view.body.recurrence.schedule_dial_frequency_nhz,'3568737000000000');
            assert.equal(view.body.recurrence.enabled,false);
            assert(!Object.hasOwn(saved.body.config,'wspr_dial_frequency_hz'));
            assert.equal(saved.body.config.station.callsign,'K1ABC');
            const retainedDraft=await request({raw_resource:'progress/2'});
            assert.equal(retainedDraft.status,200,JSON.stringify(retainedDraft));
            assert.deepEqual(retainedDraft.body.fields,savedDraft.body.fields);
            assert.equal(retainedDraft.body.draft_revision,savedDraft.body.draft_revision);
            const retainedHostname=await request({method:'GET',path:'/api/v1/member/hostname',body:''});
            assert.equal(retainedHostname.status,200,JSON.stringify(retainedHostname));
            assert.deepEqual(retainedHostname.body,hostname.body);

            const stale=await request({raw_resource:'standalone-frequency',method:'PUT',revision:read.etag,
                body:JSON.stringify({schema:'transmitter-standalone-frequency/1',target,frequency_hz:14095600})});
            assert.equal(stale.status,412,JSON.stringify(stale));
        }
        const driveInitial=await request({method:"GET",path:"/api/v1/member/gpio-drive",body:""});
        if(process.env.WSPRRYPI_GPIO_DRIVE_REQUIRED === "1") assert.equal(driveInitial.status,200,JSON.stringify(driveInitial));
        if(driveInitial.status===200) { assert.equal(driveInitial.body.saved,4);
        for (const value of [2,8,12,4]) {
            const read=await request({method:"GET",path:"/api/v1/member/gpio-drive",body:""});
            const write=await request({method:"PUT",path:"/api/v1/member/gpio-drive",body:JSON.stringify({schema:"transmitter-gpio-drive/1",target,gpio_drive_ma:value}),revision:read.etag});
            assert.equal(write.status,200,JSON.stringify(write)); assert.equal(write.body.saved,value); assert.equal(write.body.pad_readback,null);
            const legacy=await request({method:"GET",path:"/api/v1/member/configuration",body:""});
            assert.equal(legacy.status,200,JSON.stringify(legacy));assert(!Object.hasOwn(legacy.body.config,'gpio_drive_ma'));
        }
        for (const resource of ['gpio-drive','application/3','hardware/2','config/3','progress/2']) {
            const wire=await request({raw_resource:resource});assert.equal(wire.status,200,JSON.stringify(wire));assert(wire.bytes<65536);
            if(resource==='config/3')assert.equal(wire.body.config.gpio_drive_ma,4);
        }
        const draft=await request({raw_resource:'progress/2'});
        const driveDraft=await request({raw_resource:'progress/update/2',method:'POST',revision:draft.etag,
            body:JSON.stringify({schema:'transmitter-progress/2',target,request_id:'3'.repeat(32),fields:{gpio_drive_ma:8}})});
        assert.equal(driveDraft.status,200,JSON.stringify(driveDraft));assert.equal(driveDraft.body.fields.gpio_drive_ma,8);
        const applyDrive=await request({raw_resource:'progress/apply/2',method:'POST',revision:driveDraft.etag,
            body:JSON.stringify({schema:'transmitter-progress/2',target,application:{draft_revision:driveDraft.body.draft_revision,domain:'drive'}})});
        assert.equal(applyDrive.status,200,JSON.stringify(applyDrive));assert.equal(applyDrive.body.config.gpio_drive_ma,8);
        } else assert.equal(driveInitial.status,409,JSON.stringify(driveInitial));
        await require('./amplifier_timing_pair_cases.js')(request);
  await require('./filter_selector_pair_cases.js')(request);
        // Removal is not a reset: unrelated station and schedules are retained.
        await b.click("load");b.element("wtp-usb-ssid").value="recreated-network";b.element("wtp-usb-password").value="different-synthetic-password";await b.click("wifi-save");
        assert.match(b.element("wtp-usb-feedback").textContent,/Settings saved/);await b.click("load");assert.equal(b.element("wtp-usb-ssid").value,"recreated-network");
        console.log("Actual production pair: UMP framing+ACK, protected UI requests, blank USB name/Wi-Fi create/replace/delete-last/recreate, partial station progress vs applied configuration, preserved station and UTC synchronization passed (no network/hardware)");
    } finally {
        child.stdin.end();lines.close();if(!exited)child.kill("SIGTERM");
    }
})().catch(error=>{console.error(error);process.exitCode=1;});

// SPDX-License-Identifier: MIT
// Actual Pi UI -> protected API/runtime -> unmodified Pico persistence/crypto.
"use strict";
const assert=require("node:assert/strict"),path=require("node:path"),{spawn}=require("node:child_process"),readline=require("node:readline");
const {browser,response}=require("./wtp_management_fixture.js");
const build=path.resolve(process.argv[2]);
function channel(child) {
    const queue=[],waiting=[];
    readline.createInterface({input:child.stdout}).on("line",line=>waiting.length?waiting.shift()(line):queue.push(line));
    return ()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(new Error("Progress pair deadline")),45000);
        waiting.push(line=>{clearTimeout(timer);resolve(line);});
    });
}
async function stop(child) {
    if(!child || child.exitCode!==null)return;
    await new Promise(resolve=>{const timer=setTimeout(()=>child.kill("SIGKILL"),3000);child.once("exit",()=>{clearTimeout(timer);resolve();});child.kill("SIGTERM");});
}
async function scenario(carrier) {
    let server,client;
    try {
        const consumer=carrier!=="engineering_mtls";
        let serverLine;
        if(!consumer) {
            server=spawn(path.join(build,"pico_tls_server"),["0","a","settings"],{stdio:["pipe","pipe","inherit"]});
            serverLine=channel(server);assert.match(await serverLine(),/^READY /);
            client=spawn(path.join(build,"wtp_configuration_client"),[path.join(build,"credentials-v3")],{stdio:["pipe","pipe","inherit"]});
        } else client=spawn(path.join(build,"consumer_interop"),["--stdio",carrier==="consumer_ap"?"ap":"station"],{stdio:["pipe","pipe","inherit"]});
        const next=channel(client),calls=[];
        if(consumer)assert.match(await next(),/^READY /);
        const command=async value=>{client.stdin.write(JSON.stringify(value)+"\n");return JSON.parse(await next());};
        const dump=async()=>{
            if(consumer)return command({control:"dump"});
            server.stdin.write("DUMP\n");return JSON.parse((await serverLine()).replace(/^DUMP /,""));
        };
        const b=browser(async r=>{
            const result=await command({method:r.method,path:r.url,body:r.body?JSON.stringify(r.body):"",revision:r.headers?.["If-Match"]||"",headers:r.headers||{}});
            calls.push(result);return response(result.body,result.status,result.etag);
        });
        if(consumer) {
            b.element("wtp-management-carrier").value=carrier;b.element("wtp-management-host").value=carrier==="consumer_ap"?"192.168.4.1":"192.168.1.53";
            b.element("wtp-management-password").value="test-password";
            await b.click("wtp-management-connect");assert.equal(calls.at(-1).status,200,JSON.stringify(calls.at(-1)));
        }
        const initial=await dump();
        // Preserve the established API's non-station units while the new UI
        // edits station only. Pins normalize their omitted default polarity.
        const originalProgress=await command({method:"GET",path:"/api/v1/member/progress",body:""});
        assert.equal(originalProgress.status,200,JSON.stringify(originalProgress));
        const pins={engine:"direct",rf_gp:28,i2c_pair:null,button_gp:14,amplifier_gp:null,lpf_gps:[],indicator:"onboard_led",indicator_gp:null};
        const independent=await command({method:"POST",path:"/api/v1/member/progress/update",revision:originalProgress.etag,
            body:JSON.stringify({schema:"transmitter-progress/1",target:originalProgress.body.target,request_id:"7".repeat(32),fields:{pins,enabled:false,schedules:[]}})});
        assert.equal(independent.status,200,JSON.stringify(independent));
        assert.equal(independent.body.fields.enabled,false);assert.deepEqual(independent.body.fields.schedules,[]);
        assert.equal(independent.body.fields.pins.indicator_active_high,true);
        const edit=(name,value)=>{b.element("wtp-member-"+name).value=value;b.element("wtp-member-"+name).listeners.input();};
        await b.click("wtp-usb-progress-load");assert.equal(calls.at(-1).status,200,JSON.stringify(calls.at(-1)));
        edit("callsign","K1ABC");edit("locator","");edit("power","0");
        await b.click("wtp-usb-progress-save");assert.equal(calls.at(-1).status,200,JSON.stringify(calls.at(-1)));
        assert.deepEqual(calls.at(-1).body.outcomes,{callsign:"saved",locator:"cleared",power_dbm:"saved"});
        assert.equal(calls.at(-1).body.station_complete,false);assert.equal(calls.at(-1).body.station.power_dbm,0);
        const after=await dump();assert.deepEqual(after.config,initial.config);assert.equal(after.generation,initial.generation);
        await b.click("wtp-usb-progress-load");edit("locator","BAD");edit("power","");
        await b.click("wtp-usb-progress-save");assert.equal(calls.at(-1).status,200,JSON.stringify(calls.at(-1)));
        assert.equal(calls.at(-1).body.outcomes.locator,"invalid");assert.equal(calls.at(-1).body.outcomes.power_dbm,"cleared");
        assert.equal(b.element("wtp-member-locator").value,"BAD");assert.match(b.element("wtp-member-locator-progress").textContent,/Invalid/);
        await b.click("wtp-usb-progress-load");edit("locator","EM18");edit("power","0");
        await b.click("wtp-usb-progress-save");assert.equal(calls.at(-1).status,200,JSON.stringify(calls.at(-1)));
        await b.click("wtp-usb-progress-load");assert.equal(calls.at(-1).body.station_complete,true);
        const saved=calls.at(-1);
        if(consumer) {
            assert(b.element("wtp-usb-progress-apply").disabled);
            const refused=await command({method:"POST",path:"/api/v1/member/progress/apply",revision:saved.etag,
                body:JSON.stringify({schema:"transmitter-progress/1",target:saved.body.target,application:{draft_revision:saved.body.draft_revision,domain:"station"}})});
            assert.equal(refused.status,409,JSON.stringify(refused));assert.equal(refused.body.error.code,"consumer_setup_identity_required");
            assert.deepEqual((await dump()).config,initial.config);
        } else {
            assert(!b.element("wtp-usb-progress-apply").disabled);
            await b.click("wtp-usb-progress-apply");assert.equal(calls.at(-1).status,200,JSON.stringify(calls.at(-1)));
            assert.deepEqual(calls.at(-1).body.config.station,{callsign:"K1ABC",locator:"EM18",power_dbm:0});
            assert.match(b.element("wtp-usb-progress-state").textContent,/station progress applied/);
            const applied=await dump();assert.equal(applied.config.station.callsign,"K1ABC");
            assert.deepEqual(applied.config.wifi,initial.config.wifi);assert.deepEqual(applied.config.pins,initial.config.pins);
            assert.deepEqual(applied.config.schedules,initial.config.schedules);
        }
        console.log(`${carrier}: genuine UI partial/null/zero/invalid field persistence, unchanged applied configuration and explicit application/profile authority passed`);
    } finally {await stop(client);await stop(server);}
}
(async()=>{for(const carrier of ["engineering_mtls","consumer_station","consumer_ap"])await scenario(carrier);})().catch(error=>{console.error(error);process.exitCode=1;});

// SPDX-License-Identifier: MIT
// Rendered UI evidence only. Transport/ownership evidence comes from paired C++ tests.
"use strict";
const assert = require("node:assert/strict"), path = require("node:path"), fs = require("node:fs");
const {waitForPhp, createBrowserProfile, freePort, waitFor, launchBrowser, finishFixture, spawnFixtureChild, waitForSpawn} = require("./browser_harness.js");
async function main() {
    const ui = path.resolve(__dirname, ".."), port = await freePort(), base = `http://127.0.0.1:${port}/index.php?page=operation`;
    const output = path.resolve(ui, "../src/build/fleet-operations-ui");fs.mkdirSync(output, {recursive: true});
    const profile = createBrowserProfile();
    let php, chrome, client, page, failure;
    try {
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${port}`, "-t", "data", "tests/browser_fixture_router.php"], {cwd: ui});
        await waitForSpawn(php);await waitForPhp(php, base, "Operations PHP fixture");
        ({browser: chrome, client, page} = await launchBrowser({executable: process.env.CHROME_BIN || (process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "chromium"), args: ["--headless", "--no-sandbox", "--disable-gpu", "--no-first-run", "--disable-background-networking", "about:blank"], profile, timeoutMs: 60000}));
        await client.send("Page.navigate", {url: base}, 60000);
        const run = expression => client.evaluate(expression);
        await waitFor(() => run("document.readyState === 'complete' && !!window.FleetOperation && !!window.FleetClient"), "Operations scripts");
        await run(`(() => {
            window.__posts=[];window.__reads=0;window.__actions={};window.__pending=false;window.__failPost=false;
            const schedule={mode:'tone',frequency_hz:14097100,duration_ms:1000,period_seconds:120,phase_seconds:0};
            const row=(c,name)=>({device_id:c.repeat(32),product:c==='a'?'WsprryPi':'WsprryPico',name,schedule:{...schedule},enabled:false,in_flight:false});
            const observation=(c)=>({assignment_generation:c.repeat(32),control_revision:'0',observation_revision:'1',observation_age_ms:'0',can_resume:true,revoked:false,boot_id:'d'.repeat(32),state:'idle',message:'',transport:c==='a'?'network_plain':'usb',identity:{device_id:c.repeat(32),boot_id:'d'.repeat(32),product:c==='a'?'WsprryPi':'WsprryPico',firmware_version:'test'},trust:'Expected identity match',retained_name:'Output',actions:[],pending_start:null,forecast:null,utc_anchor_ns:'1800000000000000000',utc_anchor_age_ms:'0',wtp:{identity:{device_id:c.repeat(32),boot_id:'d'.repeat(32)},remote:{state:'empty',output_active:false,owner_id:null,job_id:null},job:null,adjustments:[],last_report:null,last_recovery:null}});
            window.__fleet={scope:'wsprrypi-wtp-fleet/1',maximum_outputs:32,controller_incarnation:'f'.repeat(32),publication_revision:'1',now_ms:'100000',replacement_pending:false,storage_error:'',assignments:[row('a','Long shared station name beside the laboratory amplifier'),row('b','Long shared station name beside the laboratory amplifier')],outputs:{['a'.repeat(32)]:observation('a'),['b'.repeat(32)]:observation('b')}};
            window.fetch=async(url,options={})=>{
                const path=String(url);const reply=(value,status=200)=>({ok:status>=200&&status<300,status,headers:{get:()=> '"fleet-ui"'},json:async()=>structuredClone(value)});
                if(window.__fleetUnavailable && path.includes('/host/fleet'))return reply({error:{code:'fleet_unavailable',message:'Invalid fleet assignment document'}},503);
                if(path.includes('/host/fleet/actions'))return reply({controller_incarnation:__fleet.controller_incarnation,publication_revision:__fleet.publication_revision,actions:Object.fromEntries(new URL(path,location.href).searchParams.get('ids').split(',').map(id=>[id,__actions[id]||{action_id:id,state:'unknown_or_expired'}]))});
                if(path.endsWith('/host/fleet') && options.method==='POST') {
                    const command=JSON.parse(options.body);__posts.push(command);
                    if(__failPost)throw new Error('Lost response');
                    const output=__fleet.outputs[command.device_id],row=__fleet.assignments.find(r=>r.device_id===command.device_id);
                    output.control_revision=String(BigInt(output.control_revision)+1n);__fleet.publication_revision=String(BigInt(__fleet.publication_revision)+1n);
                    const action={...command,control_revision:output.control_revision,state:__pending?'pending':'confirmed',work_pending:__pending,reason:__pending?'Waiting for owned work':'Schedule state committed'};
                    __actions[command.action_id]=action;output.actions=[action];
                    if(!__pending)row.enabled=command.operation==='enable';
                    return reply({controller_incarnation:__fleet.controller_incarnation,publication_revision:__fleet.publication_revision,action},__pending?202:200);
                }
                if(path.endsWith('/host/fleet/operations') || path.endsWith('/host/fleet')) {__reads++;__fleet.now_ms=String(BigInt(__fleet.now_ms)+1n);return reply(__fleet);}
                return reply({});
            };
            window.__refresh=()=>FleetClient.refresh();
        })()`);
        await run("__refresh()");
        assert.equal(await run("document.getElementById('operationDevice').value"), "", "full load defaults to controller");
        assert.equal(await run("document.getElementById('operationDevice').options.length"), 3);
        await run("document.getElementById('operationDevice').value='a'.repeat(32)+':'+ 'a'.repeat(32);document.getElementById('operationDevice').dispatchEvent(new Event('change'))");
        assert.equal(await run("FleetOperation.remote()"), true);
        assert.equal(await run("document.getElementById('operationLocalView').inert"), true);
        await run("patchTransmitControl();stopTransmission();patchRebootBehaviorControl()");
        assert.equal(await run("__posts.length"), 0, "hidden local handlers cannot retarget controller");
        assert.equal(await run("FleetOperation.model(__fleet.outputs['a'.repeat(32)],__fleet.assignments[0],14999n,true).state"), "Idle");
        assert.equal(await run("FleetOperation.model(__fleet.outputs['a'.repeat(32)],__fleet.assignments[0],15000n,true).state"), "Device status stale");
        await run("__fleet.outputs['a'.repeat(32)].wtp.remote.boot_id='d'.repeat(32);__refresh()");
        assert.match(await run("document.getElementById('operationFleetOutput').textContent"), /inactive \(software report\)/);
        async function capture(scene) {
            for (const theme of ["light", "dark"]) for (const viewport of [{name: "desktop", width: 1440, height: 1050}, {name: "mobile", width: 390, height: 844}]) {
                await client.send("Emulation.setDeviceMetricsOverride", {...viewport, deviceScaleFactor: 1, mobile: viewport.name === "mobile"});
                await run(`document.documentElement.setAttribute('data-bs-theme','${theme}')`);
                await client.settleRendering();
                assert.equal(await run("document.documentElement.scrollWidth<=window.innerWidth"), true, "no horizontal page overflow");
                const buttons = await run(`Array.from(document.querySelectorAll('#operationFleetView .btn:disabled')).map(button=>{const s=getComputedStyle(button);return {color:s.color,bg:s.backgroundColor,opacity:s.opacity};})`);
                for (const style of buttons) {
                    const rgb = value => value.match(/[\d.]+/g).map(Number), fg = rgb(style.color), bg = rgb(style.bg), alpha = fg[3] ?? 1;
                    const luminance = values => values.slice(0,3).map(v => {v /= 255;return v <= .04045 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4;}).reduce((sum,v,i) => sum + v * [.2126,.7152,.0722][i], 0);
                    const f = luminance(fg.map((v,i) => v * alpha + bg[i] * (1 - alpha))), b = luminance(bg);
                    assert.equal(style.opacity, "1");
                    assert.ok((Math.max(f,b) + .05) / (Math.min(f,b) + .05) >= 4.5, "disabled action labels stay readable in " + theme);
                }
                const shot = await client.send("Page.captureScreenshot", {format: "png", captureBeyondViewport: scene !== "confirmation"});
                fs.writeFileSync(path.join(output, `${scene}-${theme}-${viewport.name}.png`), Buffer.from(shot.data, "base64"));
            }
        }
        await run(`__fleet.outputs['a'.repeat(32)].actions=[{action_id:'1'.repeat(32),operation:'enable',device_id:'a'.repeat(32),controller_incarnation:'f'.repeat(32),assignment_generation:'b'.repeat(32),control_revision:'1',reviewed_boot_id:'d'.repeat(32),state:'confirmed',work_pending:false}];__refresh().catch(()=>{})`);
        assert.equal(await run("FleetClient.snapshot().valid"), false, "cross-context completion evidence is refused before rendering or action admission");
        assert.equal(await run("document.getElementById('operationFleetEnable').disabled"), true);
        await run("__fleet.outputs['a'.repeat(32)].actions=[];__fleet.publication_revision=String(BigInt(__fleet.publication_revision)+1n);__refresh()");
        // Cancellation has no POST and returns focus to the captured device control.
        await run("document.getElementById('operationFleetPause').click()");
        await waitFor(() => run("document.activeElement===document.getElementById('operationFleetCancel')"), "Pause confirmation");
        await capture("confirmation");
        await run("document.getElementById('operationFleetCancel').click()");
        await waitFor(() => run("!document.getElementById('operationDevice').disabled"), "cancel releases selection");
        assert.equal(await run("__posts.length"), 0);
        // Pending Enable retains a distinct, usable Pause path while saved disabled.
        await run("__pending=true;document.getElementById('operationFleetEnable').click()");
        await waitFor(() => run("__posts.length===1 && !document.getElementById('operationDevice').disabled"), "pending Enable admission");
        assert.equal(await run("document.getElementById('operationFleetPause').disabled"), false);
        assert.equal(await run("__posts[0].reviewed_control_revision"), "0");
        assert.match(await run("__posts[0].action_id"), /^[0-9a-f]{32}$/);
        assert.match(await run("document.getElementById('operationFleetFeedback').textContent"), /Enabling/);
        // Keep render state stable for the desktop/mobile visual batch.
        await capture("operations");
        for (const scene of ["stale", "foreign", "unavailable"]) {
            await run(`(()=>{const s=__fleet.outputs['a'.repeat(32)];s.observation_age_ms='${scene === "stale" ? "15000" : "0"}';s.state='${scene === "stale" ? "idle" : scene}';__fleet.publication_revision=String(BigInt(__fleet.publication_revision)+1n);return __refresh();})()`);
            await capture(scene);
            assert.equal(await run("document.getElementById('operationFleetEnable').disabled"), true);
            assert.equal(await run("document.getElementById('operationFleetPause').disabled"), false, "staleness cannot disable scoped Pause");
        }
        await run(`(()=>{const o=__fleet.outputs['a'.repeat(32)];o.state='recovery_required';o.can_resume=false;__fleet.assignments[0].in_flight=true;
            const action=__actions[__posts[0].action_id];action.state='outcome_unconfirmed';action.work_pending=false;action.reason='Owned cleanup remains unknown';
            __fleet.publication_revision=String(BigInt(__fleet.publication_revision)+1n);return __refresh();})()`);
        await run("document.getElementById('operationFleetReview').click()");
        assert.equal(await run("FleetClient.snapshot().actions.length"), 0, "quiescent uncertain results can be retired to free recovery capacity");
        assert.equal(await run("document.getElementById('operationFleetState').textContent"), "Recovery required");
        assert.equal(await run("document.getElementById('operationFleetEnable').disabled"), true, "review cannot clear the server's recovery barrier");
        assert.equal(await run("__posts.length"), 1, "review is browser-local and submits no recovery or start");
        assert.match(await run("document.getElementById('operationFleetFeedback').textContent"), /earlier action unconfirmed/);
        await capture("review-recovery");
        await run("__fleet.outputs['a'.repeat(32)].observation_age_ms='0'");
        // Current running evidence retires a pending countdown and wins the headline.
        await run(`const o=__fleet.outputs['a'.repeat(32)];o.state='running';o.pending_start=null;o.wtp.job_id='e'.repeat(32);o.wtp.job={device_id:'a'.repeat(32),boot_id:'d'.repeat(32),job_id:'e'.repeat(32),authoritative:true,state:'running'};o.wtp.remote={state:'running',output_active:false,owner_id:'c'.repeat(32),job_id:'e'.repeat(32)};__refresh()`);
        assert.equal(await run("document.getElementById('operationFleetState').textContent"), "Running", "intentional output-off interval retains Running");
        assert.equal(await run("document.getElementById('operationFleetStart').textContent"), "No committed pending start");
        assert.match(await run("document.getElementById('operationFleetRealized').textContent"), /accepted the requested RF targets/);
        await run("__fleetUnavailable=true;__refresh().catch(()=>{})");
        assert.equal(await run("FleetClient.snapshot().valid"), false, "failed controller initialization cannot leave a valid old snapshot");
        assert.equal(await run("document.getElementById('operationFleetEnable').disabled"), true);
        assert.match(await run("document.getElementById('operationFleetFeedback').textContent"), /Invalid fleet assignment document/);
        for (const control of ["Pause", "Recover", "Edit"])
            assert.equal(await run(`document.getElementById('operationFleet${control}').disabled`), true, "controller failure disables " + control);
        await capture("controller-unavailable");
        await run("__fleetUnavailable=false;__refresh()");
        // Same ID with a different assignment generation never retargets selection.
        await run("__fleet.outputs['a'.repeat(32)].assignment_generation='c'.repeat(32);__fleet.outputs['a'.repeat(32)].actions=[];__refresh()");
        assert.equal(await run("document.getElementById('operationFleetState').textContent"), "Removed output");
        assert.equal(await run("document.getElementById('operationFleetEnable').disabled"), true);
        console.log("Fleet Operations: scoped selection, local-handler isolation, freshness boundary, cancellation, pending Enable/Pause, uncertain-result review with preserved recovery, running evidence, explicit controller initialization failure, replacement safety and twenty-eight visual captures passed.");
    } catch (error) {failure = error;}
    await finishFixture({client, browser: chrome, php, profile, failure});
}
main().catch(error => {console.error(error);process.exitCode=1;});

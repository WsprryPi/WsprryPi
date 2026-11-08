"use strict";

const { waitForPhp, createBrowserProfile, freePort, waitFor, launchBrowser, finishFixture, spawnFixtureChild, waitForSpawn } = require("./browser_harness.js");
// SPDX-License-Identifier: MIT
// Hardware-free rendered WTP network workflow with mocked HTTP responses.

const assert = require("node:assert/strict");
const http = require("node:http");
const path = require("node:path");

const UI_ROOT = path.resolve(__dirname, "..");


async function main() {
    let page;
    const fs = require('node:fs');
    const phpPort = await freePort();
    const base = `http://127.0.0.1:${phpPort}/index.php`;
    const profileDir = createBrowserProfile();
    const output = path.resolve(UI_ROOT, '../src/build/wtp-pi/ui');
    fs.mkdirSync(output, {recursive: true});
    let php, chrome, client, failure;
    try {
        php = spawnFixtureChild('php', ['-S', `127.0.0.1:${phpPort}`, '-t', 'data', 'tests/browser_fixture_router.php'], {cwd: UI_ROOT});
        await waitForSpawn(php);
        await waitForPhp(php, base + '?page=config', 'PHP fixture');
        ({browser: chrome, client, page} = await launchBrowser({executable: process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'chromium'), args: ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-background-networking', "about:blank"], profile: profileDir, timeoutMs: 60000}));
        // Navigate the owned target explicitly; startup can expose a blank page.
        await client.send("Page.navigate", {url: base + '?page=config'}, 60000);
        const evaluate = expression => client.evaluate(expression);
        await waitFor(async () => await evaluate('document.readyState === "complete" && !!window.FleetSchedules && !!window.LocalWtp'), 'new UI scripts');
        assert.equal(await evaluate('document.getElementById("fleet-schedule-new").disabled'), true);
        const mock = `(() => {
            Object.defineProperty(navigator,"onLine",{configurable:true,get:()=>true}); // loopback-only container has no default route
            window.__requests = [];
            window.__host = {Operation:{Transmit:false}, 'WTP Server':{Enabled:true,Port:31417,Interface:'wlan0'}};
            window.__endpoint = {schema:'wsprrypi-wtp-endpoint/1',device_id:'f'.repeat(32),enabled:true,listener_running:true,dns_sd_published:true,address:'192.168.1.68',port:31417,local_requested:false,local_effective:false,remote_owner:'b'.repeat(32),remote_job_id:'c'.repeat(32),remote_state:'running',remote_output_active:true,output_unknown:false,revocation_unavailable:false};
            window.__settings = {Transport:'network_plain',Hostname:'wspr4.local','TCP Port':31417,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000,'Allow Frequency Adjustment':true,Endpoint:'','USB Serial':'','USB Vendor ID':0,'USB Product ID':0,'TLS Server Identity':'','TLS CA File':'','TLS Client Certificate':'','TLS Client Key':''};
            const schedule={mode:'tone',frequency_hz:14097100,duration_ms:3000,period_seconds:120,phase_seconds:0};
            window.__fleet = {scope:'wsprrypi-wtp-fleet/1',version:1,maximum_outputs:32,now_ms:'12000',assignments:[
                {device_id:'d'.repeat(32),name:'Bench Pi',schedule,enabled:true,in_flight:true,settings:window.__settings},
                {device_id:'e'.repeat(32),name:'Garden Pico',schedule:{...schedule,mode:'qrss',message:'TEST',dot_ms:3000},enabled:false,in_flight:false,settings:window.__settings}],
                removals:[],outputs:{['d'.repeat(32)]:{state:'scheduled',message:'',wtp:{status_observed_ms:'11000'}},['e'.repeat(32)]:{state:'paused',message:'',wtp:{status_observed_ms:'10000'}}}};
            window.__fleetRevision=1;
            window.fetch=async (url, options={}) => {
                window.__requests.push({url:String(url),options});
                const path=String(url), body=options.body?JSON.parse(options.body):{};
                const reply=(value,status=200,etag='"host-r1"')=>({ok:status>=200&&status<300,status,headers:{get:()=>etag},json:async()=>value});
                if(window.__badJsonSuffix && path.endsWith(window.__badJsonSuffix)) {
                    window.__badReplyCount=(window.__badReplyCount||0)+1;
                    return {ok:window.__badJsonStatus===200,status:window.__badJsonStatus,headers:{get:()=>null},json:async()=>{
                        if(window.__badJsonNull)return null;
                        throw new SyntaxError("Unexpected token '<', <!DOCTYPE is not valid JSON");
                    }};
                }
                if(path.endsWith('/host/config')) {if(options.method==='PUT')Object.assign(window.__host,body);return reply({config:window.__host});}
                if(path.endsWith('/host/wtp-endpoint/enable')) {
                    if(!body.confirmed && window.__endpoint.remote_owner)return reply({error:{code:'local_takeover_confirmation_required'},status:window.__endpoint},409);
                    window.__host.Operation.Transmit=true;window.__endpoint.local_requested=true;
                    window.__endpoint.finishing_remote_job=body.choice==='finish_current';
                    window.__endpoint.local_effective=body.choice!=='finish_current';
                    if(body.choice!=='finish_current'){window.__endpoint.remote_owner='';window.__endpoint.remote_job_id='';window.__endpoint.remote_output_active=false;}
                    return reply(window.__endpoint);
                }
                if(path.endsWith('/host/wtp-endpoint/recover')){window.__endpoint.output_unknown=false;window.__endpoint.remote_owner='';return reply(window.__endpoint);}
                if(path.endsWith('/host/wtp-endpoint'))return reply(window.__endpoint);
                if(path.endsWith('/host/fleet')){
                    if(options.method==='GET' && window.__fleetLoadFailure)throw new Error('Controller disconnected');
                    if(options.method==='POST'){
                        if(window.__fleetConflict || options.headers['If-Match']!=='"fleet-'+window.__fleetRevision+'"')return reply({error:{code:'revision_conflict'}},412);
                        if(body.operation==='assign')window.__fleet.assignments.push({device_id:body.settings['Device ID'],name:body.name,schedule:body.schedule,settings:body.settings,enabled:body.enabled,in_flight:false});
                        if(body.operation==='schedule')window.__fleet.assignments.find(row=>row.device_id===body.device_id).schedule=body.schedule;
                        if(body.operation==='pause')Object.assign(window.__fleet.assignments.find(row=>row.device_id===body.device_id),{enabled:false,in_flight:false});
                        if(body.operation==='remove')window.__fleet.assignments=window.__fleet.assignments.filter(row=>row.device_id!==body.device_id);
                        ++window.__fleetRevision;
                    }
                    return reply(window.__fleet,200,'"fleet-'+window.__fleetRevision+'"');
                }
                if(path.endsWith('/host/devices')){if(window.__catalogLoadFailure)throw new Error('Catalog unavailable');return reply({scope:'wsprrypi-wtp-catalog/1',version:1,active_id:'',profiles:[{id:'1'.repeat(32),name:'New Pi',method:'manual',discovery_id:'',legacy_unverified:false,settings:window.__settings},{id:'2'.repeat(32),name:'Selected Pico',method:'manual',discovery_id:'',legacy_unverified:false,settings:{...window.__settings,Transport:'usb','Device ID':'b'.repeat(32),Endpoint:'/dev/serial/by-id/fixture-pico-if02','USB Serial':'fixture-pico','USB Vendor ID':51966,'USB Product ID':16402}}]},200,'"catalog-r1"');}
                if(path.endsWith('/host/discovery'))return reply({available:true,candidates:[]});
                if(path.endsWith('/status'))return reply({host:{selected:false,firmware_maintenance:false,ready:false,phase:'idle',session_phase:'disconnected'}});
                return reply({});
            };
            window.confirm=()=>{throw new Error('Fleet must not use native confirmation');};
            backendCurrentlyConnected=true;websocketCurrentlyConnected=true;
            clearWebSocketReconnectTimer();syncConnectionAlert();clearBackendStatus('runtime');
            const style=document.createElement('style');style.textContent='*{scroll-behavior:auto!important}';document.head.append(style);
        })()`;
        await evaluate(mock);
        await waitFor(async () => await evaluate('!window.PicoFirmwareSafety.locked'), 'host firmware maintenance status');
        await evaluate(`configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearConfigLoadFailureState();document.getElementById('fleet-tab').click();`);
        await waitFor(async () => await evaluate('document.getElementById("fleet-output-list").textContent.includes("Garden Pico")'), 'independent output rows');
        assert.equal(await evaluate('WtpUi.selected()'), false);
        await evaluate(`window.__fleetLoadFailure=true;document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-feedback").textContent.includes("Fleet status unavailable:")'),'disconnected Fleet feedback');
        await evaluate(`window.__fleetLoadFailure=false;document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-feedback").textContent.includes("Fleet status unavailable:")'),'recovered Fleet feedback');
        // HTML error pages, malformed success replies, and null documents are
        // failures with operator guidance, never raw parser text or success.
        for (const [status, nullBody] of [[503,false],[200,false],[200,true]]) {
            await evaluate(`window.__badJsonSuffix='/host/fleet';window.__badJsonStatus=${status};window.__badJsonNull=${nullBody};window.__badReplyCount=0;document.getElementById('fleet-schedule-feedback').textContent='';document.getElementById('fleet-schedule-refresh').click();`);
            await waitFor(async()=>await evaluate(`__badReplyCount>0 && document.getElementById('fleet-schedule-feedback').textContent.includes('${status===503?'HTTP 503':'unreadable reply'}')`),'safe malformed Fleet reply');
            assert.doesNotMatch(await evaluate(`document.getElementById('fleet-schedule-feedback').textContent`),/Unexpected token|DOCTYPE|Cannot read/);
            assert.match(await evaluate(`document.getElementById('fleet-output-list').textContent`),/Garden Pico/,'Last observed rows survive failed reads');
        }
        await evaluate(`window.__badJsonSuffix='';document.getElementById('fleet-schedule-refresh').click()`);
        await waitFor(async()=>await evaluate(`document.getElementById('fleet-schedule-feedback').textContent===''`),'malformed Fleet read recovery');
        await evaluate(`window.__badJsonSuffix='/status';window.__badJsonStatus=503;window.__badJsonNull=false;`);
        await waitFor(async()=>await evaluate(`document.getElementById('wtp-feedback').textContent.includes('HTTP 503')`),'safe malformed Pico status');
        assert.doesNotMatch(await evaluate(`document.getElementById('wtp-feedback').textContent`),/Unexpected token|DOCTYPE/);
        assert.equal(await evaluate('WtpUi.selected()'),false);
        await evaluate(`window.__badJsonSuffix='/host/devices';window.__badJsonStatus=200;document.getElementById('fleet-refresh').click()`);
        await waitFor(async()=>await evaluate(`document.getElementById('fleet-feedback').textContent.includes('unreadable reply')`),'safe malformed catalog');
        await evaluate(`window.__badJsonSuffix='';document.getElementById('fleet-refresh').click()`);
        await waitFor(async()=>await evaluate(`document.getElementById('fleet-feedback').textContent==='Device list refreshed.'`),'catalog recovery feedback');
        await waitFor(async()=>await evaluate(`document.getElementById('wtp-feedback').textContent==='Status connection restored.'`),'Pico status recovery');
        // Browsing and independent drafts must never dirty the host autosave.
        await evaluate(`document.getElementById('fleet-refresh').click()`);
        await waitFor(async()=>await evaluate(`document.getElementById('fleet-device').options.length===2`),'catalog loaded');
        assert.equal(await evaluate(`document.getElementById('configPageTitle').textContent`),'Fleet');
        assert.match(await evaluate(`document.getElementById('configSaveStatusHint').textContent`),/own save buttons/);
        await evaluate(`configAutosaveSuspended=false;configAutosaveDirty=false;document.getElementById('fleet-device').value='known:'+'2'.repeat(32);document.getElementById('fleet-device').dispatchEvent(new Event('change',{bubbles:true}));`);
        assert.equal(await evaluate('configAutosaveDirty'),false,'Selecting a profile must not queue host autosave');
        await evaluate(`document.getElementById('fleet-edit').click();document.getElementById('fleet-name').value='Unsaved name';document.getElementById('fleet-name').dispatchEvent(new Event('input',{bubbles:true}));`);
        assert.equal(await evaluate('configAutosaveDirty'),false,'Editing a profile draft must not queue host autosave');
        await evaluate(`document.getElementById('fleet-editor-cancel').click();configAutosaveSuspended=true;`);
        assert.equal(await evaluate('document.activeElement.id'),'fleet-edit','Cancel restores Edit focus');
        await evaluate(`document.getElementById('fleet-add').click();document.getElementById('fleet-editor-cancel').click()`);
        assert.equal(await evaluate('document.activeElement.id'),'fleet-add','Cancel restores Add focus');
        await evaluate(`document.getElementById('fleet-edit').click();document.getElementById('fleet-device').selectedIndex=-1;document.getElementById('fleet-device').dispatchEvent(new Event('change'));document.getElementById('fleet-editor-cancel').click()`);
        assert.equal(await evaluate('document.activeElement.id'),'fleet-device','Hidden opener falls back to device selector');
        await evaluate(`document.getElementById('fleet-device').value='known:'+'2'.repeat(32);document.getElementById('fleet-device').dispatchEvent(new Event('change'))`);
        await evaluate('document.getElementById("fleet-schedule-new").click()');
        await waitFor(async () => await evaluate('!document.getElementById("fleet-schedule-editor").hidden'), 'assignment editor');
        assert.equal(await evaluate('document.getElementById("fleet-schedule-device").value'),'2'.repeat(32),'Assignment retains selected saved output');
        assert.equal(await evaluate(`document.getElementById('fleet-schedule-management-field').hidden`),true,'USB Pico hides Pi HTTP port');
        await evaluate(`document.getElementById('fleet-schedule-device').value='1'.repeat(32);document.getElementById('fleet-schedule-device').dispatchEvent(new Event('change'));document.getElementById('fleet-schedule-management').value='31416';`);
        assert.equal(await evaluate(`document.getElementById('fleet-schedule-management-field').hidden`),false,'Network Pi retains HTTP port');
        await evaluate(`document.getElementById('fleet-schedule-device').value='2'.repeat(32);document.getElementById('fleet-schedule-device').dispatchEvent(new Event('change'));`);
        assert.equal(await evaluate(`document.getElementById('fleet-schedule-management').value`),'31416','Hidden port retains its draft value');
        await evaluate(`document.getElementById('fleet-schedule-device').value='1'.repeat(32);document.getElementById('fleet-schedule-device').dispatchEvent(new Event('change'));`);
        assert.equal(await evaluate(`document.getElementById('fleet-schedule-management').value`),'31416','Returning to Pi preserves custom port');
        await evaluate(`document.getElementById('fleet-schedule-management').value='31415';document.getElementById('fleet-schedule-device').value='2'.repeat(32);document.getElementById('fleet-schedule-device').dispatchEvent(new Event('change'));`);
        await evaluate(`configAutosaveSuspended=false;configAutosaveDirty=false;document.getElementById('fleet-schedule-frequency').dispatchEvent(new Event('input',{bubbles:true}));document.getElementById('fleet-listener-port').dispatchEvent(new Event('input',{bubbles:true}));`);
        assert.equal(await evaluate('configAutosaveDirty'),false,'Schedule and listener drafts are independent');
        await evaluate(`document.getElementById('wtp_endpoint').dispatchEvent(new Event('input',{bubbles:true}));`);
        assert.equal(await evaluate('configAutosaveDirty'),true,'Host connection fields retain autosave');
        await evaluate(`clearTimeout(configAutosaveTimer);configAutosaveTimer=null;configAutosaveDirty=false;configAutosaveSuspended=true`);
        for (const [mode, label, help] of [
            ['wspr','WSPR RF center (Hz)','not USB dial or tone 0'],
            ['qrss','QRSS carrier (Hz)','carrier frequency'],
            ['fskcw','FSKCW space (Hz)','mark = space + positive shift'],
            ['dfcw','DFCW dot (Hz)','dash = dot + positive shift'],
            ['tone','Carrier frequency (Hz)','steady RF carrier']]) {
            const saved=await evaluate('JSON.stringify(__fleet.assignments)');
            await evaluate(`document.getElementById('fleet-schedule-frequency').value='14097123';document.getElementById('fleet-schedule-mode').value='${mode}';document.getElementById('fleet-schedule-mode').dispatchEvent(new Event('change'));`);
            assert.equal(await evaluate('document.getElementById("fleet-schedule-frequency-label").textContent'), label);
            assert.ok((await evaluate('document.getElementById("fleet-schedule-frequency-help").textContent')).includes(help));
            assert.equal(await evaluate('document.getElementById("fleet-schedule-frequency").value'), '14097123');
            assert.equal(await evaluate('JSON.stringify(__fleet.assignments)'), saved);
            assert.equal(await evaluate('document.getElementById("fleet-schedule-frequency").getAttribute("aria-describedby")'), 'fleet-schedule-frequency-help');
        }
        await evaluate(`document.getElementById('fleet-schedule-duration').value='4';document.getElementById('fleet-schedule-plain').checked=true;window.__fleetConflict=true;document.getElementById('fleet-schedule-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("fleet-schedule-editor-feedback").textContent.includes("draft is preserved")'), 'revision conflict');
        assert.equal(await evaluate('document.getElementById("fleet-schedule-duration").value'), '4');
        await evaluate(`window.__fleetConflict=false;document.getElementById('fleet-schedule-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("fleet-schedule-editor").hidden'), 'assignment saved');
        assert.equal(await evaluate('__host.Operation.Transmit'), false);
        assert.equal(await evaluate('__fleet.assignments.length'), 3);
        assert.equal(await evaluate(`JSON.parse(__requests.filter(r=>r.url.endsWith('/host/fleet')&&r.options.method==='POST').at(-1).options.body).management_port`),31415,'USB assignment retains current API field');
        // A successful status refresh while a draft is open must not rebase its
        // If-Match revision. Cover both editing and creating an assignment.
        for (const editing of [true,false]) {
            await evaluate(editing
                ? `Array.from(document.querySelectorAll('#fleet-output-list section'))[1].querySelectorAll('button')[3].click()`
                : `document.getElementById('fleet-schedule-new').click()`);
            await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-editor").hidden && !document.getElementById("fleet-schedule-save").disabled'),'draft ready');
            const draftRevision=await evaluate('__fleetRevision');
            await evaluate(`document.getElementById('fleet-schedule-mode').value='tone';document.getElementById('fleet-schedule-mode').dispatchEvent(new Event('change'));document.getElementById('fleet-schedule-duration').value='6';document.getElementById('fleet-schedule-plain').checked=true;++window.__fleetRevision;document.getElementById('fleet-schedule-refresh').click();`);
            await waitFor(async()=>await evaluate('__requests.some(r=>r.url.endsWith("/host/fleet")&&r.options.method==="GET"&&window.__fleetRevision>'+draftRevision+')'),'new status revision');
            await waitFor(()=>evaluate('document.getElementById("fleet-output-list").getAttribute("aria-busy")==="false"'),'status refresh acknowledged');
            const before=await evaluate('JSON.stringify(__fleet.assignments)');
            await evaluate(`document.getElementById('fleet-schedule-save').click()`);
            await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-editor-feedback").textContent.includes("draft is preserved")'),'refreshed draft conflict');
            assert.equal(await evaluate('JSON.stringify(__fleet.assignments)'),before);
            assert.equal(await evaluate('document.getElementById("fleet-schedule-duration").value'),'6');
            assert.equal(await evaluate('__requests.filter(r=>r.url.endsWith("/host/fleet")&&r.options.method==="POST").at(-1).options.headers["If-Match"]'),'"fleet-'+draftRevision+'"');
            await evaluate(`document.getElementById('fleet-schedule-cancel').click()`);
        }
        // Opening another editor may fail before its fields are populated.
        // The still-visible draft must retain its original target authority.
        await evaluate(`Array.from(document.querySelectorAll('#fleet-output-list section'))[1].querySelectorAll('button')[3].click()`);
        await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-editor").hidden && !document.getElementById("fleet-schedule-save").disabled'),'original target draft');
        await evaluate(`window.__catalogLoadFailure=true;document.getElementById('fleet-schedule-new').click()`);
        await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-feedback").textContent.includes("Catalog unavailable") && !document.getElementById("fleet-schedule-save").disabled'),'failed editor switch');
        assert.equal(await evaluate('document.getElementById("fleet-schedule-device").value'),'assigned');
        assert.equal(await evaluate('document.getElementById("fleet-schedule-device").disabled'),true);
        await evaluate(`window.__catalogLoadFailure=false;document.getElementById('fleet-schedule-save').click()`);
        await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-editor").hidden'),'original draft saved');
        assert.equal(await evaluate('JSON.parse(__requests.filter(r=>r.url.endsWith("/host/fleet")&&r.options.method==="POST").at(-1).options.body).device_id'),'e'.repeat(32));
        const viewports=[{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}];
        async function capture(section, element) {
            for(const viewport of viewports){
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                // Full-page CDP screenshots otherwise paint fixed chrome in
                // the middle of the document. Anchor that chrome to document
                // edges only for capture; no product styles are changed.
                await evaluate(`(() => {const s=document.createElement('style');s.id='capture-chrome';s.textContent='#mainNavbar{position:absolute!important;top:0!important}footer.fixed-bottom{position:absolute!important;bottom:0!important}html{scroll-behavior:auto!important}body{position:relative!important}';document.head.append(s);window.scrollTo({top:0,behavior:'instant'});document.getAnimations().forEach(a=>a.finish());})()`);
                await client.settleRendering();
                await evaluate(`document.getAnimations().forEach(a=>a.finish());`);
                assert.equal(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'),true,section+' overflow');
                const height=await evaluate('document.documentElement.scrollHeight');
                const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,
                    clip:{x:0,y:0,width:viewport.width,height,scale:1}});
                fs.writeFileSync(path.join(output,viewport.name+'-'+section+'.png'),Buffer.from(shot.data,'base64'));
                const clip=await evaluate(`(()=>{const b=document.getElementById('${element}').getBoundingClientRect();return {x:Math.max(0,b.x),y:b.y+scrollY,width:Math.min(b.width,innerWidth),height:b.height,scale:1};})()`);
                const detail=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip});
                fs.writeFileSync(path.join(output,viewport.name+'-'+section+'-detail.png'),Buffer.from(detail.data,'base64'));
                await evaluate(`document.getElementById('capture-chrome').remove()`);
            }
        }
        const fleetPosts = async()=>await evaluate('__requests.filter(r=>r.url.endsWith("/host/fleet")&&r.options.method==="POST").length');
        const clickBench = action=>evaluate(`Array.from(document.querySelectorAll('#fleet-output-list section'))[0].querySelectorAll('button')[${action}].click()`);
        const dialogOpen = async()=>await evaluate('document.getElementById("fleet-action-confirmation").classList.contains("show") && document.activeElement.id==="fleet-action-cancel"');
        const dialogClosed = async()=>await evaluate('!document.getElementById("fleet-action-confirmation").classList.contains("show") && !document.getElementById("fleet-schedule-refresh").disabled');
        const originalPosts=await fleetPosts();
        await clickBench(1);
        await waitFor(dialogOpen,'Pause confirmation and safe default focus');
        assert.equal(await evaluate('document.getElementById("fleet-action-target").textContent'),'Bench Pi');
        for(const viewport of viewports){
            await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth + 1'),true,'confirmation overflow');
            const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
            fs.writeFileSync(path.join(output,viewport.name+'-fleet-confirmation.png'),Buffer.from(shot.data,'base64'));
        }
        await evaluate('document.getElementById("fleet-action-cancel").click()');
        await waitFor(dialogClosed,'canceled Pause');
        assert.equal(await fleetPosts(),originalPosts);
        assert.equal(await evaluate('document.activeElement.textContent'),'Pause and stop');
        await clickBench(4);
        await waitFor(dialogOpen,'Remove confirmation');
        await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
        await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
        await waitFor(dialogClosed,'Escape cancels Remove');
        assert.equal(await fleetPosts(),originalPosts);
        await clickBench(1);
        await waitFor(dialogOpen,'confirmation at reviewed revision');
        await evaluate('++__fleetRevision;document.getElementById("fleet-action-confirm").click()');
        await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-feedback").textContent.includes("Schedules changed")'),'stale confirmation rejected');
        assert.equal(await evaluate('__fleet.assignments[0].enabled'),true);
        await waitFor(dialogClosed,'stale action closed');
        await waitFor(()=>evaluate('!document.querySelector(".modal-backdrop") && document.getElementById("fleet-output-list").getAttribute("aria-busy")==="false"'),'stale confirmation cleanup complete');
        await clickBench(1);
        await waitFor(dialogOpen,'fresh Pause confirmation');
        await evaluate('document.getElementById("fleet-action-confirm").click();document.getElementById("fleet-action-confirm").click()');
        await waitFor(async()=>await evaluate('!__fleet.assignments[0].enabled'),'confirmed Pause');
        assert.equal(await fleetPosts(),originalPosts+2,'one stale request and one confirmed request');
        await waitFor(dialogClosed,'Pause completed');
        await clickBench(4);
        await waitFor(dialogOpen,'confirmed Remove dialog');
        await evaluate('document.getElementById("fleet-action-confirm").click()');
        await waitFor(async()=>await evaluate('!__fleet.assignments.some(r=>r.name==="Bench Pi")'),'confirmed Remove');
        assert.equal(await fleetPosts(),originalPosts+3);
        await waitFor(dialogClosed,'Remove completed');
        assert.equal(await evaluate('document.activeElement.id'),'fleet-schedule-refresh');
        await capture('outputs','fleet-schedules');
        await evaluate(`document.getElementById('fleet-schedule-new').click()`);
        await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-editor").hidden'),'editor reopened');
        await capture('assignment','fleet-schedule-editor');
        for (const mode of ['wspr','qrss','fskcw','dfcw']) {
            await evaluate(`document.getElementById('fleet-schedule-mode').value='${mode}';document.getElementById('fleet-schedule-mode').dispatchEvent(new Event('change'));`);
            await capture('assignment-'+mode,'fleet-schedule-editor');
        }

        await evaluate(`document.getElementById('fleet-schedule-cancel').click();document.getElementById('fleet-listener').open=true;`);
        await waitFor(async()=>await evaluate('document.getElementById("fleet-listener-feedback").textContent.includes("Listening")'),'listener status');
        await capture('listener','fleet-listener');
        await evaluate(`document.getElementById('fleet-device').selectedIndex=-1;document.getElementById('fleet-device').dispatchEvent(new Event('change',{bubbles:true}));document.getElementById('fleet-schedule-new').click();`);
        await waitFor(async()=>await evaluate(`!document.getElementById('fleet-schedule-editor').hidden && !document.getElementById('fleet-schedule-save').disabled`),'unselected assignment');
        assert.equal(await evaluate(`document.getElementById('fleet-schedule-device').value`),'','No catalog selection must not silently choose the first device');
        const beforeUnselected=await fleetPosts();
        await evaluate(`document.getElementById('fleet-schedule-save').click()`);
        assert.equal(await fleetPosts(),beforeUnselected,'Unselected output cannot be saved');
        await evaluate(`document.getElementById('fleet-schedule-cancel').click();document.getElementById('fleet-listener').open=false;document.getElementById('wtp-management').open=true;WtpManagement.invalidate();`);
        assert.match(await evaluate(`document.getElementById('gpio-drive-status').textContent`),/has not been read/);
        assert.doesNotMatch(await evaluate(`document.getElementById('gpio-drive-status').textContent`),/changed|preserved/);
        assert.doesNotMatch(await evaluate(`document.getElementById('filter-selector-controls-status').textContent`),/Entries preserved/);
        assert.equal(await evaluate(`document.getElementById('wtp-management-availability').compareDocumentPosition(document.getElementById('wtp-usb-settings')) & Node.DOCUMENT_POSITION_FOLLOWING`),4,'Prerequisites precede forms');
        assert.equal(await evaluate(`document.getElementById('wtp-gps-controls').compareDocumentPosition(document.getElementById('wtp-member-hardware-save')) & Node.DOCUMENT_POSITION_FOLLOWING`),4,'Hardware save follows GPS');
        assert.equal(await evaluate(`document.getElementById('wtp-si5351-controls').compareDocumentPosition(document.getElementById('wtp-member-hardware-save')) & Node.DOCUMENT_POSITION_FOLLOWING`),4,'Hardware save follows Si5351');
        assert.equal(await evaluate(`document.getElementById('wtp-complete-station').open || document.getElementById('wtp-combined-settings').open`),false,'Alternative forms are disclosed explicitly');
        await evaluate(`document.querySelector('#wtp-complete-station summary').focus()`);
        await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});
        await client.send('Input.dispatchKeyEvent',{type:'char',text:'\r',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});
        await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});
        assert.equal(await evaluate(`document.getElementById('wtp-complete-station').open`),true,'Keyboard opens complete station alternative');
        await evaluate(`document.getElementById('wtp-complete-station').open=false;document.getElementById('radio-tab').click()`);
        await waitFor(async()=>await evaluate(`document.getElementById('configPageTitle').textContent==='Signal setup'`),'restored host guidance');
        assert.match(await evaluate(`document.getElementById('configSaveStatusHint').textContent`),/Changes save automatically/);
        await evaluate(`document.getElementById('fleet-tab').click()`);
        await waitFor(async()=>await evaluate(`document.getElementById('configPageTitle').textContent==='Fleet'`),'restored Fleet guidance');
        // The real disabled/first-read state is a useful operator path of its own.
        // Capture at all requested sizes and themes, with actual fixed chrome.
        for (const theme of ['light','dark']) {
            await evaluate(`applyThemePreference('${theme}')`);
            for (const viewport of viewports) {
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                await evaluate(`document.getElementById('wtp-hardware-settings').open=false;document.getElementById('wtp-complete-station').open=false;document.getElementById('wtp-combined-settings').open=false;window.scrollTo({top:0,behavior:'instant'});document.getAnimations().forEach(a=>a.finish());`);
                await client.settleRendering();
                const screenshot=async name=>{
                    const shot=await client.send('Page.captureScreenshot',{format:'png'});
                    fs.writeFileSync(path.join(output,viewport.name+'-'+theme+'-'+name+'.png'),Buffer.from(shot.data,'base64'));
                };
                assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'),true,'Fleet '+theme+' overflow');
                await screenshot('fleet');
                await evaluate(`document.getElementById('fleet-device').value='known:'+'2'.repeat(32);document.getElementById('fleet-device').dispatchEvent(new Event('change'));document.getElementById('fleet-schedule-new').click();`);
                await waitFor(async()=>await evaluate(`!document.getElementById('fleet-schedule-editor').hidden && !document.getElementById('fleet-schedule-save').disabled`),'USB draft visual state');
                await evaluate(`document.getElementById('fleet-schedule-editor').scrollIntoView({block:'start'});window.scrollBy(0,-180);`);
                assert.equal(await evaluate(`document.getElementById('fleet-schedule-management').getBoundingClientRect().height`),0,'USB port removed from layout');
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,'USB draft overflow');
                await screenshot('usb-assignment');
                const draft=await evaluate(`JSON.stringify([...document.querySelectorAll('#fleet-schedule-editor input,#fleet-schedule-editor select')].map(e=>[e.id,e.value,e.checked]))`);
                await evaluate(`window.__badJsonSuffix='/host/fleet';window.__badJsonStatus=503;window.__badJsonNull=false;document.getElementById('fleet-schedule-refresh').click();`);
                await waitFor(async()=>await evaluate(`document.getElementById('fleet-schedule-feedback').textContent.includes('HTTP 503')`),'outage visual state');
                assert.equal(await evaluate(`JSON.stringify([...document.querySelectorAll('#fleet-schedule-editor input,#fleet-schedule-editor select')].map(e=>[e.id,e.value,e.checked]))`),draft,'Outage preserves open draft');
                await evaluate(`document.getElementById('fleet-schedule-feedback').scrollIntoView({block:'start'});window.scrollBy(0,-180);`);
                await screenshot('connection-error');
                await evaluate(`window.__badJsonSuffix='';document.getElementById('fleet-schedule-refresh').click();`);
                await waitFor(async()=>await evaluate(`document.getElementById('fleet-schedule-feedback').textContent===''`),'visual outage recovered');
                await evaluate(`document.getElementById('fleet-schedule-cancel').click();document.getElementById('fleet-edit').click();document.getElementById('fleet-editor-cancel').click();document.getElementById('fleet-edit').scrollIntoView({block:'center'});`);
                assert.equal(await evaluate('document.activeElement.id'),'fleet-edit');
                await screenshot('profile-cancel-focus');
                for (const [name,id] of [['management','wtp-management'],['station','wtp-usb-progress-heading'],['complete-station','wtp-complete-station'],['gps','wtp-gps-controls'],['hardware-actions','wtp-member-validate'],['capability-guidance','wtp-platform-limits'],['alternatives','wtp-combined-settings']]) {
                    await evaluate(`document.getElementById('wtp-hardware-settings').open=true;if(document.getElementById('${id}').tagName==='DETAILS')document.getElementById('${id}').open=true;document.getElementById('${id}').scrollIntoView({block:'start'});window.scrollBy(0,-180);document.getAnimations().forEach(a=>a.finish());`);
                    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'),true,name+' '+theme+' overflow');
                    await screenshot(name);
                }
                const contrast=await evaluate(`(()=>{const s=getComputedStyle(document.getElementById('wtp-selection-hint'));return {color:s.color,bg:getComputedStyle(document.body).backgroundColor};})()`);
                const rgb=text=>text.match(/[\d.]+/g).map(Number);
                const fg=rgb(contrast.color),bg=rgb(contrast.bg),alpha=fg[3]??1;
                const luminance=values=>values.slice(0,3).map(v=>{v/=255;return v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4;}).reduce((sum,v,i)=>sum+v*[0.2126,0.7152,0.0722][i],0);
                const f=luminance(fg.map((v,i)=>v*alpha+bg[i]*(1-alpha))),b=luminance(bg);
                const ratio=(Math.max(f,b)+0.05)/(Math.min(f,b)+0.05);
                assert(ratio>=4.5,theme+' help-text contrast');
                console.log(viewport.name+' '+theme+' help-text contrast: '+ratio.toFixed(2)+':1');
            }
        }

        // Capacity is advertised, never inferred from the historical UI bound.
        await evaluate(`window.__originalFleet=structuredClone(__fleet);__fleet.assignments=Array.from({length:31},(_,i)=>({...__originalFleet.assignments[0],device_id:i.toString(16).padStart(32,'0'),name:'Remote '+(i+1)+' '+('Long device name ').repeat(3),enabled:false,in_flight:false}));__fleet.outputs={};document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('document.querySelectorAll("#fleet-output-list section").length===31 && !document.getElementById("fleet-schedule-new").disabled'),'31 allows final assignment');
        await evaluate(`document.getElementById('fleet-schedule-new').click();`);
        await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-editor").hidden && !document.getElementById("fleet-schedule-save").disabled'),'32nd assignment editor');
        await evaluate(`document.getElementById('fleet-schedule-device').value='1'.repeat(32);document.getElementById('fleet-schedule-device').dispatchEvent(new Event('change'));document.getElementById('fleet-schedule-mode').value='tone';document.getElementById('fleet-schedule-mode').dispatchEvent(new Event('change'));document.getElementById('fleet-schedule-duration').value='3';document.getElementById('fleet-schedule-plain').checked=true;document.getElementById('fleet-schedule-save').click();`);
        await waitFor(async()=>await evaluate('document.querySelectorAll("#fleet-output-list section").length===32 && document.getElementById("fleet-schedule-new").disabled'),'32 prevents 33rd assignment');
        assert.match(await evaluate('document.getElementById("fleet-schedule-capacity").textContent'),/32 of 32/);
        for(const theme of ['light','dark'])for(const viewport of viewports){
            await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            await evaluate(`document.documentElement.setAttribute('data-bs-theme','${theme}');__fleet.outputs[__fleet.assignments[30].device_id]={state:'recovery_required',message:'Disconnected target: reconcile the previous job before resuming. '+('Long recovery detail ').repeat(6)};document.getElementById('fleet-schedule-refresh').click();`);
            await waitFor(async()=>await evaluate('document.getElementById("fleet-output-list").textContent.includes("Long recovery detail")'),'failure row');
            await evaluate(`document.getElementById('fleet-schedules').scrollIntoView({block:'start'});window.scrollBy(0,-160);document.getAnimations().forEach(a=>a.finish());`);
            await client.settleRendering();
            assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth + 1'),true,'32-row layout stays within viewport');
            const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
            fs.writeFileSync(path.join(output,viewport.name+'-fleet32-'+theme+'.png'),Buffer.from(shot.data,'base64'));
            await evaluate(`document.querySelector('#fleet-output-list section:last-child').scrollIntoView({block:'center'});`);
            const last=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
            fs.writeFileSync(path.join(output,viewport.name+'-fleet32-last-'+theme+'.png'),Buffer.from(last.data,'base64'));
        }
        await evaluate(`__fleet.assignments.pop();__fleet.outputs['f'.repeat(32)]={revoked:true,state:'recovery_required',message:'Cleanup unresolved'};document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-capacity").textContent.includes("1 unresolved") && document.getElementById("fleet-schedule-new").disabled'),'retained context consumes capacity');
        await evaluate(`delete __fleet.outputs['f'.repeat(32)];__fleet.assignments.push({...__fleet.assignments[0],device_id:'f'.repeat(32),name:'Reassigned output'});document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('Array.from(document.querySelectorAll("#fleet-output-list section")).some(row=>row.textContent.includes("Reassigned output") && Array.from(row.querySelectorAll("button")).every(button=>!button.hidden))'),'reassignment restores all actions on reused identity row');
        await evaluate(`__fleet.assignments.pop();document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-new").disabled'),'reconciliation reclaims capacity');
        for(const invalid of [null,0,-1,1.5,'32',true,Number.MAX_SAFE_INTEGER+1]){
            await evaluate(`__fleet.maximum_outputs=${JSON.stringify(invalid)};document.getElementById('fleet-schedule-refresh').click();`);
            await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-new").disabled && document.getElementById("fleet-schedule-feedback").textContent.includes("capacity")'),'invalid capacity disables admission');
            assert.equal(await evaluate('Array.from(document.querySelectorAll("#fleet-output-list button")).every(button=>button.disabled)'),true,'unreadable state visibly disables retained row actions');
        }
        await evaluate(`delete __fleet.maximum_outputs;document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("fleet-schedule-new").disabled'),'missing capacity disables admission');
        for(const viewport of viewports){
            await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            await evaluate(`applyThemePreference('light');document.getElementById('fleet-schedule-feedback').scrollIntoView({block:'start'});window.scrollBy(0,-180);document.getAnimations().forEach(a=>a.finish());`);
            await client.settleRendering();
            const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
            fs.writeFileSync(path.join(output,viewport.name+'-fleet-unreadable.png'),Buffer.from(shot.data,'base64'));
        }
        await evaluate(`__fleet.maximum_outputs=40;document.getElementById('fleet-schedule-refresh').click();`);
        await waitFor(async()=>await evaluate('!document.getElementById("fleet-schedule-new").disabled && document.getElementById("fleet-schedule-capacity").textContent.includes("of 40")'),'UI derives capacity from server');
        assert.equal(await evaluate('Array.from(document.querySelectorAll("#fleet-output-list button")).some(button=>!button.disabled)'),true,'valid refresh restores row actions');
        // The keyboard interaction grants native user activation. This fixture
        // has no host config readback, so establish its synthetic baseline before
        // leaving Setup; otherwise the real unsaved-edit guard correctly blocks.
        await evaluate(`configAutosaveDirty=false;configAutosaveInFlight=false;configAutosavePendingAfterFlight=false;lastSavedConfigPayload=currentConfigPayloadSnapshot();`);
        await client.send('Page.navigate',{url:base+'?page=operation'});
        await waitFor(async()=>await evaluate('document.readyState==="complete" && !!window.LocalWtp && typeof requestTransmitEnabledChange==="function"'),'Operation view');
        await evaluate(mock);
        await waitFor(async () => await evaluate('!window.PicoFirmwareSafety.locked'), 'host firmware maintenance status');
        await evaluate(`currentRuntimeTransmitBackend='si5351';currentTransmitUnavailableMessage=()=>'';window.__declined=false;void requestTransmitEnabledChange(true,false).fail((_,status)=>{window.__declined=status==='declined';});`);
        await waitFor(async()=>await evaluate('!document.getElementById("local-wtp-confirmation").hidden'),'local confirmation');
        await capture('local-takeover','local-wtp-control');
        await evaluate('document.getElementById("local-wtp-decline").click()');
        await waitFor(async()=>await evaluate('__declined'),'declined takeover');
        await waitFor(async()=>await evaluate('!document.activeElement.closest("#local-wtp-confirmation") && document.activeElement!==document.body'),'focus restored after cancel');
        assert.equal(await evaluate('__host.Operation.Transmit'),false);
        await evaluate('void requestTransmitEnabledChange(true,false)');
        await waitFor(async()=>await evaluate('!document.getElementById("local-wtp-confirmation").hidden'),'second confirmation');
        await evaluate('document.getElementById("local-wtp-finish").click()');
        await waitFor(async()=>await evaluate('__endpoint.finishing_remote_job'),'finish current');
        assert.equal(await evaluate('__endpoint.local_effective'),false);
        assert.equal(await evaluate('__requests.filter(r=>r.url.endsWith("/host/wtp-endpoint/enable")&&JSON.parse(r.options.body).confirmed).at(-1).options.body.includes("observed_owner")'),true);
        await evaluate(`__endpoint.finishing_remote_job=false;__endpoint.local_requested=false;__host.Operation.Transmit=false;void requestTransmitEnabledChange(true,false);`);
        await waitFor(async()=>await evaluate('!document.getElementById("local-wtp-confirmation").hidden'),'end-now confirmation');
        await evaluate('document.getElementById("local-wtp-end-now").click()');
        await waitFor(async()=>await evaluate('__endpoint.local_effective && !__endpoint.remote_owner'),'end-now local control');
        console.log('Fleet in-page confirmation, cancellation, Escape, feature hiding, stale revisions, duplicate prevention, Pi takeover choices and independent assignments passed.');
        console.log('Mocked desktop/mobile visual evidence: '+output);
    } catch (error) { failure = error; }
    finally {
        await finishFixture({failure: failure, client, browser: chrome, php, profile: profileDir});
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});

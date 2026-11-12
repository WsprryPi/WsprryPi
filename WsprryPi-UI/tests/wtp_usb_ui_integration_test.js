"use strict";

const { waitForPhp, createBrowserProfile, freePort, waitFor, launchBrowser, finishFixture, spawnFixtureChild, waitForSpawn } = require("./browser_harness.js");
// SPDX-License-Identifier: MIT
// Hardware-free rendered USB profile, settings and logging workflow with mocked HTTP responses.

const assert = require("node:assert/strict");
const http = require("node:http");
const path = require("node:path");
const os = require("node:os");

const UI_ROOT = path.resolve(__dirname, "..");


async function main() {
    let page;
    const fs=require("node:fs");
    const phpPort=await freePort(), debugPort=await freePort();
    const url=`http://127.0.0.1:${phpPort}/index.php?page=config`;
    const output=path.resolve(process.env.WSPRRYPI_USB_UI_EVIDENCE_DIR || path.join(UI_ROOT,"../src/build/wtp-network/ui-usb-progress"));fs.mkdirSync(output,{recursive:true});
    const profileDir=createBrowserProfile();
    let php,chrome,client,failure;
    try {
        php=spawnFixtureChild(process.env.PHP_BIN || "php",["-S",`127.0.0.1:${phpPort}`,"-t","data","tests/browser_fixture_router.php"],{cwd:UI_ROOT});
        await waitForSpawn(php);await waitForPhp(php, url, "PHP fixture");
        ({browser: chrome, client, page} = await launchBrowser({executable: process.env.CHROME_BIN || (process.platform==="darwin"?"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome":"chromium"), args: ["--headless","--no-sandbox","--disable-gpu","--no-first-run","--no-default-browser-check","--disable-background-networking","about:blank"], profile: profileDir, timeoutMs: 60000, expectedDialogs: [
            "Review the current saved values beside your preserved draft. Matching values do not prove an earlier save committed. Send one new explicit save with this fresh revision?",
            "Review current saved and active observations beside your preserved draft. Send one new explicit save using the fresh revision?"
        ]}));await client.send("Page.enable");
        // Navigate the owned target explicitly; startup can expose a blank page.
        await client.send("Page.navigate", {url}, 60000);
        const evaluate=async expression=>{const result=await client.send("Runtime.evaluate",{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);return result.result.value;};
        await waitFor(async()=>await evaluate('document.readyState==="complete" && !!window.WtpUi && !!window.WtpUsbSettings'),"USB scripts");
        await evaluate(`(() => {
            configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();
            backendCurrentlyConnected=true;websocketCurrentlyConnected=true;syncConnectionAlert();clearBackendStatus('runtime');clearConfigLoadFailureState();
            const target={scope:'member',device_id:'a'.repeat(32),boot_id:'b'.repeat(32)};
            window.__snapshot={selected:true,transport:'usb',ready:true,management_available:true,host_utc_valid:true,phase:'idle',session_phase:'ready',now_ms:'10000',status_observed_ms:'9000',remote:{output_active:false},owns:false,
                identity:{device_id:target.device_id,boot_id:target.boot_id},common_name:'workbench-pico.local',common_name_confirmed:true,name_pending_restart:false,
                usb:{management:{connected:true,time:{accepted:true,selected:true,uncertainty_ns:'2400000',observed_ms:'9000'}},hostname:{saved:'workbench-pico.local'}}};
            window.__mutations=[];
            window.__catalog={scope:'wsprrypi-wtp-catalog/1',profiles:[{id:'c'.repeat(32),name:'Saved precision Pico',method:'manual',settings:{Transport:'usb',Endpoint:'/dev/serial/by-id/pico-if02','USB Serial':'PICO123','USB Vendor ID':51966,'USB Product ID':16402,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000}}],active_id:'c'.repeat(32)};
            window.fetch=async(url,options={})=>{
                if(options.method && options.method!=='GET') {
                    window.__mutations.push({url:String(url),body:JSON.parse(options.body||'{}')});
                    if(String(url).endsWith('/host/devices') && options.method==='POST') {
                        const body=JSON.parse(options.body);
                        window.__catalog.profiles[0].settings=body.settings;
                        window.__catalog.profiles[0].name=body.name;
                    }
                }
                const path=String(url);if(path.endsWith('/member/logging') && window.__loggingConflict)return {ok:false,status:412,headers:{get:()=>''},json:async()=>({error:{code:'revision_conflict'}})};const header={get:()=> '\\"usb-r1\\"'};
                let value=(path.includes('/api/wtp') || path.endsWith('/api/control/v1/status'))?window.__snapshot:
                    path.endsWith('/member/management')?{carrier:'usb_physical',connected:true,host:'',selection_id:'f'.repeat(32),session:null}:
                    path.endsWith('/member/hostname')?{schema:'hostname',target,saved:'workbench-pico.local',active:'workbench-pico.local',default:'wsprrypico-aaaaaaaa.local',writable:true,pending_restart:false}:
                    path.endsWith('/member/progress')?{schema:'progress',target,draft_revision:'1',config_revision:'0',profile_revision:'0',fields:{callsign:'AA0NT'},last_receipt:null,last_outcomes:{},applied_station:null,station:{callsign:'AA0NT',locator:null,power_dbm:null},station_complete:false,complete:false,apply_domains:{all:false,drive:false,network:false,radio:false,station:false},wspr_plan:null,schedule_enabled:false,matches_applied:false,schedule_suspended:true}:
                    path.endsWith('/member/logging')?{schema:'logging',target,saved:{enabled:true,maximum_severity:6},active:{enabled:true,maximum_severity:6,streaming:false,dropped:'0'}}:
                    path.endsWith('/member/wifi')?{schema:'wifi',target,wifi:null,pending_restart:false,storage_healthy:true}:
                    path.endsWith('/host/devices')?window.__catalog:
                    path.endsWith('/host/discovery')?{available:false,candidates:[],reason:'USB workflow'}:{};
                return {ok:true,status:200,headers:header,json:async()=>value?.schema?{contract:'Wsprry-Control/1',...value}:value};
            };
            WtpUi.populate({Transport:'usb',Endpoint:'/dev/serial/by-id/pico-if02','USB Serial':'PICO123','USB Vendor ID':51966,'USB Product ID':16402,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000,'Allow Frequency Adjustment':false});
            WtpUi.select(true);bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();

            document.getElementById('wtp-management').open=true;
        })()`);
        await waitFor(async()=>await evaluate("document.getElementById('wtp-status-heading').textContent==='workbench-pico.local'"),"common name");
        await evaluate("document.getElementById('wtp-usb-load').click()");
        await waitFor(async()=>await evaluate("document.getElementById('wtp-usb-feedback').textContent.includes('No saved Wi-Fi')"),"USB no-network state");
        await evaluate("document.getElementById('wtp-usb-progress-load').click()");
        await waitFor(async()=>await evaluate("document.getElementById('wtp-usb-progress-state').textContent.includes('station incomplete')"),"partial saved progress");
        await evaluate("document.getElementById('wtp-usb-logging-read').click()");
        await waitFor(async()=>await evaluate("!document.getElementById('wtp-usb-logging-save').disabled"),"logging readback through shipped controls");
        const findings=[];
        for(const [name,width,height] of [["desktop",1440,1100],["mobile",390,844]]) {
            await client.send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:name==="mobile"});
            for(const theme of ["light","dark"]) {
                await evaluate(`applyThemePreference('${theme}');document.getElementById('wtp-usb-settings').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-110)`);
                await client.settleRendering();
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,`${name}/${theme} horizontal overflow`);
                await evaluate("document.getElementById('wtp-usb-logging-controls').open=true;document.getElementById('wtp-usb-logging-enabled').focus()");
                assert.equal(await evaluate("document.activeElement.id"),'wtp-usb-logging-enabled',await evaluate("JSON.stringify((()=>{const a=[];for(let e=document.getElementById('wtp-usb-logging-enabled');e;e=e.parentElement)a.push({id:e.id,cls:e.className,hidden:e.hidden,disabled:e.disabled,display:getComputedStyle(e).display});return a;})())"));
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,'full-page logging controls fit');
                assert.equal(await evaluate('document.getElementById("fleet-tab").textContent.trim()'),"Fleet","shipped Fleet navigation label");
                assert.equal(await evaluate('document.getElementById("fleet-pane").classList.contains("active")'),true);
                assert.equal(await evaluate('document.getElementById("wtp-usb-wifi-save").disabled'),false);
                const state=await evaluate(`(() => {const input=document.getElementById('wtp-usb-ssid'),s=getComputedStyle(input),r=input.getBoundingClientRect();input.focus();return {font:s.fontSize,height:r.height,width:r.width,focus:document.activeElement===input};})()`);
                assert(state.focus&&state.height>0&&state.width>150,JSON.stringify(state));findings.push({name,theme,...state});
                await evaluate("document.getElementById('wtp-usb-progress-heading').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-120)");
                const shot=await client.send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,`${name}-${theme}.png`),Buffer.from(shot.data,"base64"));
            }
        }
        await evaluate("window.__loggingConflict=true;document.getElementById('wtp-usb-logging-read').click()");
        await waitFor(async()=>await evaluate("document.getElementById('wtp-usb-logging-status').textContent.includes('changed elsewhere')"),"authoritative logging conflict feedback");
        assert.equal(await evaluate("document.getElementById('wtp-usb-logging-save').disabled"),true);
        for(const [name,width,height] of [["desktop",1440,1100],["mobile",390,844]]) {
            await client.send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:name==="mobile"});
            for(const theme of ["light","dark"]) {
                await evaluate(`(async()=>{applyThemePreference('${theme}');document.activeElement?.blur();await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));document.getElementById('wtp-usb-logging-status').scrollIntoView({block:'center',behavior:'instant'});await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));})()`);
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,'logging refusal fits full page');
                assert.equal(await evaluate("(()=>{const r=document.getElementById('wtp-usb-logging-status').getBoundingClientRect();return r.bottom>110 && r.top<innerHeight-40;})()"),true,'logging refusal is visible in the captured viewport');
                assert.equal(await evaluate("document.getElementById('wtp-usb-logging-status').closest('fieldset:disabled')"),null,'refusal stays outside disabled controls');
                const shot=await client.send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,`logging-conflict-${name}-${theme}.png`),Buffer.from(shot.data,"base64"));
            }
        }
        // Stored transport fields survive unrelated edits. UTC uncertainty is
        // reported for operator judgment; it is no longer an editable veto.
        await waitFor(async()=>await evaluate("document.getElementById('fleet-edit').disabled===false"),"saved profile list");
        await evaluate("document.getElementById('fleet-add').click()");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"100000000");
        await evaluate("document.getElementById('fleet-editor-cancel').click();document.getElementById('fleet-edit').click()");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"1000000");
        for(const saved of [1000000,37000000,250000000]) {
            await evaluate(`WtpUi.populate({...WtpUi.read(),'Start Uncertainty ns':${saved}})`);
            assert.equal(await evaluate("WtpUi.read()['Start Uncertainty ns']"),saved);
        }
        await evaluate("WtpUi.populate({...WtpUi.read(),'Start Uncertainty ns':1000000});document.getElementById('fleet-name').value='Bench Pico renamed';document.getElementById('fleet-name').dispatchEvent(new Event('input',{bubbles:true}))");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"1000000");
        assert.equal(await evaluate("window.__mutations.length"),0,"profile drafts must not save, connect or arm");
        assert.equal(await evaluate("buildConfigPayload().WTP['Start Uncertainty ns']"),1000000);
        for(const [name,width,height] of [["desktop",1440,1100],["mobile",390,844]]) {
            await client.send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:name==="mobile"});
            for(const theme of ["light","dark"]) {
                await evaluate(`applyThemePreference('${theme}')`);
                await evaluate(`bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();document.getElementById('fleet-name').scrollIntoView({block:'center',behavior:'instant'});document.getElementById('fleet-name').focus()`);
                for(const surface of ["fleet","wtp"]) {
                    const field=surface==="fleet"?"fleet-uncertainty":"wtp_uncertainty";
                    await client.settleRendering();
                    const state=await evaluate(`(() => {const input=document.getElementById('${field}'),r=input.getBoundingClientRect(),name=document.getElementById('fleet-name');return {hidden:!!input.closest('[hidden]'),focus:document.activeElement===name,width:r.width,height:r.height,value:input.value,nameWidth:name.getBoundingClientRect().width,overflow:document.documentElement.scrollWidth>innerWidth};})()`);
                    assert(state.hidden&&state.focus&&state.width===0&&state.height===0&&state.nameWidth>150&&!state.overflow,JSON.stringify(state));
                    findings.push({name,theme,surface,...state});
                    const shot=await client.send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,`policy-${surface}-${name}-${theme}.png`),Buffer.from(shot.data,"base64"));
                }
            }
        }
        await evaluate("bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show()");
        await evaluate("window.confirm=()=>true;document.getElementById('fleet-save').click()");
        await waitFor(async()=>await evaluate("document.getElementById('fleet-editor').hidden"),"explicit profile save");
        const mutations=await evaluate("window.__mutations");
        assert.equal(mutations.length,1);
        assert(mutations[0].url.endsWith('/host/devices'));
        assert.equal(mutations[0].body.settings['Start Uncertainty ns'],1000000);
        assert.equal(mutations[0].body.operation,'edit');
        assert.equal(mutations[0].body.name,'Bench Pico renamed');
        assert.equal(await evaluate("window.__catalog.profiles[0].settings['USB Serial']"),'PICO123');
        await evaluate("document.getElementById('fleet-edit').click()");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"1000000");
        assert.equal(await evaluate("document.getElementById('fleet-name').value"),"Bench Pico renamed");
        // Actual shared editor and Pi wrapper produce the current protected
        // resource request; the paired suite separately exercises its real carrier.
        await evaluate(`(() => {
            const previous=window.fetch;window.__graySettings=WsprryGrayLine.defaults();window.__grayRevision=1;window.__grayWrites=[];
            window.fetch=async(url,options={})=>{
                if(!String(url).endsWith('/member/gray-line'))return previous(url,options);
                if(options.method==='PUT') {
                    const body=JSON.parse(options.body);window.__grayWrites.push({body,revision:options.headers['If-Match']});
                    // Production C++ serializes object keys in sorted order.
                    const sorted=value=>Array.isArray(value)?value.map(sorted):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(key=>[key,sorted(value[key])])):value;
                    window.__graySettings=sorted(body.settings);window.__grayRevision++;
                }
                return {ok:true,status:200,headers:{get:()=> '"gray-'+window.__grayRevision+'"'},json:async()=>({contract:'Wsprry-Control/1',schema:'gray-line',
                    target:{scope:'member',device_id:'a'.repeat(32),boot_id:'b'.repeat(32)},saved:window.__graySettings,storage_healthy:true,runtime:{selection:'ordinary',prolonged_window:false}})};
            };
            WtpManagement.setAvailability(true,true);document.getElementById('wtp-management').open=true;
            document.querySelector('#wtp-gray-line-controls [data-action="read"]').click();
        })()`);
        await waitFor(async()=>await evaluate(`!document.querySelector('#wtp-gray-line-controls [data-gray="morning.enabled"]').matches(':disabled')`),'gray-line readback');
        await evaluate(`(()=>{const input=document.querySelector('#wtp-gray-line-controls [data-gray="morning.enabled"]');input.checked=true;input.dispatchEvent(new Event('input',{bubbles:true}));document.querySelector('#wtp-gray-line-controls [data-action="save"]').click();})()`);
        await waitFor(async()=>await evaluate(`window.__grayWrites.length===1 && document.querySelector('#wtp-gray-line-controls [role="status"]').textContent.includes('saved for future')`),'gray-line save');
        const grayWrite=await evaluate('window.__grayWrites[0]');
        assert.equal(grayWrite.body.contract,'Wsprry-Control/1');assert.equal(grayWrite.body.schema,'gray-line');
        assert.deepEqual(grayWrite.body.target,{scope:'member',device_id:'a'.repeat(32),boot_id:'b'.repeat(32)});
        assert.equal(grayWrite.body.settings.morning.enabled,true);assert.equal(grayWrite.revision,'"gray-1"');
        for(const [name,width,height] of [['desktop',1440,1100],['mobile',390,844]]) {
            await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:name==='mobile'});
            for(const theme of ['light','dark']) {
                await evaluate(`applyThemePreference('${theme}');document.getElementById('wtp-management').open=true;document.getElementById('wtp-gray-line-controls').scrollIntoView({block:'start',behavior:'instant'})`);
                await client.settleRendering();
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true);
                const clip=await evaluate(`(()=>{const r=document.getElementById('wtp-gray-line-controls').getBoundingClientRect();return {x:0,y:r.top+scrollY,width:innerWidth,height:Math.ceil(r.height),scale:1}})()`);
                assert(clip.height>200);
                const shot=await client.send('Page.captureScreenshot',{format:'png',clip,captureBeyondViewport:true});
                fs.writeFileSync(path.join(output,`gray-line-${name}-${theme}.png`),Buffer.from(shot.data,'base64'));
            }
        }
        console.log('Shipped gray-line editor: target/revision/contract producer, confirmed save, desktop/mobile and both themes passed');
        fs.writeFileSync(path.join(output,"checks.json"),JSON.stringify(findings,null,2)+"\n");
        console.log("Rendered Fleet navigation, configured common name and USB no-network/logging editors at desktop/mobile in light/dark; uncertainty veto controls stay hidden, stored values survive explicit profile rename/save/reload, visible focus and layout passed");
        console.log(output);
    } catch(error) {failure=error;throw error;} finally {
        await finishFixture({failure: failure, client, browser: chrome, php, profile: profileDir});
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});

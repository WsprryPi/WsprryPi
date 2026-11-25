// SPDX-License-Identifier: MIT
"use strict";
// Rendered form evidence only. Production refusal-before-write is exercised by
// pico_controller_http_pair_test.js against the actual controller and peers.
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const {waitForPhp,createBrowserProfile,freePort,waitFor,launchBrowser,finishFixture,spawnFixtureChild,waitForSpawn}=require("./browser_harness.js");
async function main(){
    const ui=path.resolve(__dirname,".."),output=path.resolve(process.env.WSPRRYPI_HOST_PATH_EVIDENCE_DIR||path.join(ui,"../src/build/host-path-ui"));
    fs.mkdirSync(output,{recursive:true});const profile=createBrowserProfile();let php,browser,client,failure;
    try{
        const port=await freePort(),url=`http://127.0.0.1:${port}/index.php?page=config`;
        php=spawnFixtureChild(process.env.PHP_BIN||"php",["-S",`127.0.0.1:${port}`,"-t","data","tests/browser_fixture_router.php"],{cwd:ui});
        await waitForSpawn(php);await waitForPhp(php,url,"native path form fixture");
        ({browser,client}=await launchBrowser({executable:process.env.CHROME_BIN||(process.platform==="darwin"?"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome":"chromium"),args:["--headless","--no-sandbox","--disable-gpu","--no-first-run","--no-default-browser-check","--disable-background-networking","about:blank"],profile,timeoutMs:60000}));
        await client.send("Page.enable");await client.send("Page.navigate",{url},60000);
        const evaluate=async expression=>{const r=await client.send("Runtime.evaluate",{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result.value;};
        await waitFor(()=>evaluate('document.readyState==="complete"&&!!window.WtpUi'),"native path controls loaded");
        await evaluate(`(()=>{
            const originalFetch=window.fetch;
            configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();
            backendCurrentlyConnected=true;websocketCurrentlyConnected=true;syncConnectionAlert();clearBackendStatus('runtime');clearConfigLoadFailureState();
            window.__writes=[];window.__confirms=[];window.__refuse=false;
            window.__catalog={scope:'wsprrypi-wtp-catalog/1',profiles:[],active_id:'',active_unmatched:false};
            window.confirm=message=>{window.__confirms.push(message);return true;};
            window.fetch=async(url,options={})=>{
                const resource=String(url),write=options.method&&options.method!=='GET';
                if(resource.endsWith('/version'))return originalFetch(url,options);
                if(write){window.__writes.push({url:resource,body:JSON.parse(options.body)});if(window.__refuse)return {ok:false,status:409,headers:{get:()=>''},json:async()=>({error:{code:'catalog_request_rejected',message:'The selected host refused this file reference.'}})};}
                return {ok:true,status:200,headers:{get:()=> '"profiles-1"'},json:async()=>resource.endsWith('/host/devices')?window.__catalog:{available:false,candidates:[],reason:'Software fixture',usb:{available:true,candidates:[]}}};
            };
            bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();
            bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();
            window.dispatchEvent(new Event('pageshow'));
        })()`);
        await waitFor(()=>evaluate("!document.getElementById('fleet-device').disabled"),"empty device catalog loaded");
        const set=async values=>evaluate(`(()=>{for(const [id,value]of Object.entries(${JSON.stringify(values)})){const e=document.getElementById(id);e.value=value;e.dispatchEvent(new Event('change',{bubbles:true}));}})()`);
        await evaluate("document.getElementById('fleet-add').click()");
        const usb=String.raw`\\?\usb#vid_cafe&pid_4012&mi_04#opaque-interface`;
        await set({'fleet-name':'Windows path fixture','fleet-binding':'usb','fleet-path':usb,'fleet-serial':'0123456789ABCDEF','fleet-vendor':'51966','fleet-product':'16402','fleet-identity':'a'.repeat(32)});
        await evaluate("document.getElementById('fleet-save').click()");
        await waitFor(()=>evaluate('window.__writes.length===1'),"explicit Windows USB profile save");
        assert.equal(await evaluate('window.__writes[0].body.settings.Endpoint'),usb);
        assert.equal(await evaluate('window.__confirms.length'),1);
        await evaluate("document.getElementById('fleet-add').click()");
        const key=String.raw`C:\ProgramData\WsprryPi\private Ω\client.key`;
        await set({'fleet-name':'Windows TLS fixture','fleet-binding':'network','fleet-host':'pico.local','fleet-port':'18443','fleet-identity':'a'.repeat(32),'fleet-ca':String.raw`C:\private\ca.crt`,'fleet-cert':'D:/private/Ω/client.crt','fleet-key':key});
        await evaluate("window.__refuse=true;document.getElementById('fleet-save').click()");
        await waitFor(()=>evaluate("document.getElementById('fleet-editor-feedback').textContent.includes('Your draft is preserved')"),"refused native path retains draft");
        assert.equal(await evaluate('window.__writes.length'),2);
        assert.equal(await evaluate("document.getElementById('fleet-key').value"),key);
        assert.equal(await evaluate("document.getElementById('fleet-editor').hidden"),false);
        await set({'fleet-key':'C:relative.key'});await evaluate("document.getElementById('fleet-save').click()");
        assert.equal(await evaluate('window.__writes.length'),2,"invalid native alias sends no mutation");
        assert.match(await evaluate("document.getElementById('fleet-editor-feedback').textContent"),/absolute file path/);
        assert.equal(await evaluate("document.getElementById('fleet-key').value"),'C:relative.key');
        for(const [name,width,height]of [['desktop',1440,1100],['mobile',390,844]]){
            await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:name==='mobile'});
            for(const theme of ['light','dark']){
                await evaluate(`applyThemePreference('${theme}');document.getElementById('fleet-key').focus();document.getElementById('fleet-editor-feedback').scrollIntoView({block:'center',behavior:'instant'})`);
                await client.settleRendering();
                await waitFor(()=>evaluate("(()=>{let e=document.getElementById('fleet-editor');for(;e;e=e.parentElement){const s=getComputedStyle(e);if(s.display==='none'||s.visibility!=='visible'||Number(s.opacity)<0.99)return false;}return document.getAnimations().every(a=>a.playState!=='running'||!Number.isFinite(a.effect?.getComputedTiming().endTime));})()"),"visible settled editor");
                assert(await evaluate('document.documentElement.scrollWidth<=innerWidth'));
                assert.equal(await evaluate("document.activeElement.id"),'fleet-key');
                await evaluate("document.getElementById('fleet-editor-feedback').scrollIntoView({block:'center',behavior:'instant'});window.dispatchEvent(new Event('scroll'))");
                await client.settleRendering();
                const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(output,`native-path-${name}-${theme}.png`),Buffer.from(shot.data,'base64'));
            }
        }
        await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
        await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
        assert.notEqual(await evaluate('document.activeElement.id'),'fleet-key');
        assert(await evaluate("document.getElementById('fleet-editor-feedback').getAttribute('aria-live')==='polite'"));
        console.log('PASS native path forms: exact USB save, UTF-8 TLS draft, server refusal, invalid alias without writes, keyboard, desktop/mobile, and both themes');
        console.log(output);
    }catch(error){failure=error;throw error;}finally{await finishFixture({failure,client,browser,php,profile});}
}
main().catch(error=>{console.error(error);process.exitCode=1;});

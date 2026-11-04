"use strict";

const { waitForPhp, createBrowserProfile, freePort, waitFor, launchBrowser, finishFixture, spawnFixtureChild, waitForSpawn } = require("./browser_harness.js");

// Actual vendor assets, application handlers, and loopback HTTP only. The PHP
// fixture refuses control writes, journal execution, and external fallback.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const UI_ROOT = path.resolve(__dirname, "..");

async function main() {
    let page;
    const port = await freePort();
    const origin = `http://127.0.0.1:${port}`;
    const profile = createBrowserProfile();
    const evidence = process.env.DEPENDENCY_EVIDENCE_DIR;
    if (evidence) fs.mkdirSync(evidence, { recursive: true });
    let php, browser, client, failure;
    const exceptions = [], failedAssets = [], mutations = [], migrationMessages = [];
    try {
        const migrateSource = process.env.JQUERY_MIGRATE_PATH
            ? fs.readFileSync(process.env.JQUERY_MIGRATE_PATH, "utf8") : "";
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${port}`, "-t", "data", "tests/browser_fixture_router.php"], { cwd: UI_ROOT });
        await waitForSpawn(php);
        await waitForPhp(php, `${origin}/index.php`, "PHP dependency fixture");
        ({browser: browser, client, page} = await launchBrowser({executable: process.env.CHROME_BIN || (process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "chromium"), args: ["--headless", "--no-sandbox", "--disable-gpu", "--disable-background-networking", "about:blank"], profile: profile}));
        const eventErrors = [];
        client.on("event", event => {
            const p = event.params;
            if (event.method === "Runtime.exceptionThrown") exceptions.push(p.exceptionDetails.exception?.description || p.exceptionDetails.text);
            if (event.method === "Runtime.consoleAPICalled") {
                const text = p.args.map(arg => arg.value || arg.description || "").join(" ");
                if (text.includes("JQMIGRATE:") && !text.includes("Migrate is installed")) migrationMessages.push(text);
            }
            if (event.method === "Network.responseReceived" && p.response.url.includes("/vendor/") && p.response.status >= 400) failedAssets.push(p.response.url);
            if (event.method === "Network.requestWillBeSent" && !["GET", "HEAD"].includes(p.request.method)) mutations.push(p.request.url);
            if (event.method === "Fetch.requestPaused") {
                // Optional migration pass appends the official development
                // helper to jQuery in this browser response only, never on disk.
                const body = fs.readFileSync(path.join(UI_ROOT, "data/vendor/js/jquery-4.0.0.min.js"), "utf8") + "\n" + migrateSource;
                client.send("Fetch.fulfillRequest", { requestId: p.requestId, responseCode: 200,
                    responseHeaders: [{ name: "Content-Type", value: "application/javascript" }], body: Buffer.from(body).toString("base64") }).catch(error => eventErrors.push(error));
            }
        });
        await client.send("Page.enable"); await client.send("Runtime.enable"); await client.send("Network.enable");
        if (process.env.JQUERY_MIGRATE_PATH) await client.send("Fetch.enable", { patterns: [{ urlPattern: "*/vendor/js/jquery-4.0.0.min.js*", requestStage: "Request" }] });
        await client.send("Page.addScriptToEvaluateOnNewDocument", { source: `
            window.WebSocket = class { static OPEN=1; static CONNECTING=0; readyState=0; addEventListener(){} close(){} send(){throw Error('No live transport');} };
            window.EventSource = class { static OPEN=1; static CLOSED=2; readyState=1; listeners={};
                constructor(url){this.url=url;window.__dependencyStreams ||= [];window.__dependencyStreams.push(this);}
                addEventListener(type,fn){(this.listeners[type] ||= []).push(fn);} close(){this.readyState=2;}
                emit(data){const e={data:JSON.stringify(data)};this.onmessage?.(e);for(const fn of this.listeners.message || [])fn(e);}
            };
            if (!sessionStorage.getItem('dependency-seeded')) {
                localStorage.setItem('theme','dark');localStorage.setItem('dependency-user-preference','keep');
                sessionStorage.setItem('dependency-seeded','keep');document.cookie='dependency_session=keep; SameSite=Strict; path=/';
            }
            localStorage.setItem('wsprrypi.updateCheckDisabled','true');
        ` });
        const evaluate = expression => client.evaluate(expression);
        const navigate = async url => {
            await client.send("Page.navigate", { url: origin + url });
            await waitFor(() => evaluate("document.readyState==='complete' && typeof jQuery==='function' && typeof bootstrap==='object'"), "vendor scripts");
            assert.equal(await evaluate("$.fn.jquery"), "4.0.0");
            assert.equal(await evaluate("bootstrap.Tooltip.VERSION"), "5.3.8");
            if (migrateSource) assert.match(await evaluate("$.migrateVersion || ''"), /^4\./, "Migrate must actually be loaded");
            else assert.equal(await evaluate("typeof $.migrateVersion"), "undefined", "normal assets must not include Migrate");
        };
        await navigate("/index.php?page=config");
        await waitFor(() => evaluate("window.PicoFirmwareSafety?.locked===false && document.getElementById('configTabs')?.dataset.persistTabStateInitialized==='true'"), "idle synthetic host and initialized tabs");
        assert.equal(await evaluate("document.documentElement.dataset.bsTheme"), "dark");
        assert.equal(await evaluate(`(async () => {
            const success = await $.ajax({url:'/__dependency_fixture__/ok',dataType:'json'});
            if (success.fixture !== true) throw Error('JSON response lost');
            for (const [url,status,timeout] of [['/__dependency_fixture__/invalid','parsererror',0],['/__dependency_fixture__/slow','timeout',25],['/__dependency_fixture__/missing','error',0]]) {
                await new Promise((resolve,reject) => $.ajax({url,dataType:'json',timeout}).done(()=>reject(Error('Unexpected success'))).fail((xhr,text)=>text===status?resolve():reject(Error(text))));
            }
            await new Promise((resolve,reject) => {const request=$.ajax({url:'/__dependency_fixture__/slow'}).fail((xhr,status)=>status==='abort'?resolve():reject(Error(status)));request.abort();});
            return true;
        })()`), true);
        assert.equal(await evaluate(`(async () => {
            suspendConfigAutosave(true);
            $('#callsign').val('BAD CALL');$('#gridsquare').val('XX00');$('#qrss_frequency').val('14097123');
            validatePage();
            if ($('#callsign').val()!=='BAD CALL' || $('#qrss_frequency').val()!=='14097123') throw Error('Draft replaced');
            const tab = document.querySelector('[data-persist-tab-state] [data-bs-toggle="tab"]:not(.active)') || document.querySelector('[data-bs-toggle="tab"]:not(.active)');
            if (!tab) throw Error('No tab fixture');
            let shown=0;$(tab).on('shown.bs.tab.dependency',()=>shown++);
            await new Promise(resolve=>{
                if(tab.classList.contains('active'))return resolve();
                tab.addEventListener('shown.bs.tab',resolve,{once:true});
                bootstrap.Tab.getOrCreateInstance(tab).show();
            });
            if(shown!==1)throw Error('Bootstrap tab event missing or duplicated');
            const target=tab.getAttribute('data-bs-target');
            if(localStorage.getItem(getPersistedTabStorageKey(document.getElementById('configTabs')))!==target)throw Error('Tab preference lost');
            window.__dependencyTab=target;
            const tip=document.createElement('button');tip.title='Dependency tooltip';document.body.append(tip);
            $(tip).tooltip({animation:false});$(tip).tooltip('show');
            if(!document.querySelector('.tooltip'))throw Error('Tooltip bridge failed');
            $(tip).tooltip('dispose');tip.remove();
            const modal=document.createElement('div');modal.className='modal';modal.innerHTML='<div class="modal-dialog"><div class="modal-content"><button>Close</button></div></div>';document.body.append(modal);
            const instance=new bootstrap.Modal(modal);instance.show();instance.hide();instance.dispose();modal.remove();
            if ($('#callsign').val()!=='BAD CALL')throw Error('Interaction discarded invalid draft');
            return true;
        })()`), true);
        const selectedTab = await evaluate("window.__dependencyTab");
        await client.send("Page.reload");
        await waitFor(() => evaluate("typeof window.__dependencyTab==='undefined' && document.readyState==='complete' && document.getElementById('configTabs')?.dataset.persistTabStateInitialized==='true' && window.PicoFirmwareSafety?.locked===false"), "explicit configuration reload and tab restoration");
        assert.equal(await evaluate("document.querySelector('#configTabs .nav-link.active').getAttribute('data-bs-target')"), selectedTab);
        for (const view of ["operation", "config", "logs", "spots", "maintenance"]) {
            await navigate(`/index.php?page=${view}${view === "spots" ? "&demo=1" : ""}`);
            await evaluate("document.fonts.ready");
            const fonts = await evaluate(`(async () => {
                const families=['500 16px "Barlow Semi Condensed"','600 16px "Barlow Semi Condensed"','700 16px "Barlow Semi Condensed"','400 16px "Source Sans 3"','900 16px "Font Awesome 7 Free"','400 16px "bootstrap-icons"'];
                for(const font of families) if(!(await document.fonts.load(font)).length)throw Error('Font missing: '+font);
                for(const icon of document.querySelectorAll('.fa-solid,.bi')){const content=getComputedStyle(icon,'::before').content;if(!content || content==='none' || content==='normal')throw Error('Icon content missing');}
                return true;
            })()`);
            assert.equal(fonts, true);
            assert.deepEqual(await evaluate("$.migrateWarnings || []"), []);
        }
        // Standalone viewer: synthetic SSE only; verify tab/cursor behavior and
        // local assets independently of the shared application bootstrap.
        await navigate("/view_diag_logs.php");
        await waitFor(() => evaluate("window.__dependencyStreams?.length===1"), "diagnostic SSE subscription");
        assert.equal(await evaluate(`(() => {
            const stream=window.__dependencyStreams[0];stream.onopen?.();
            stream.emit({type:'journal',__REALTIME_TIMESTAMP:'1792173600000000',__CURSOR:'dependency-cursor',PRIORITY:'6',SYSLOG_IDENTIFIER:'fixture',MESSAGE:'Dependency fixture log record'});
            return document.body.textContent.includes('Dependency fixture log record');
        })()`), true);
        await evaluate(`$('#btn-clear').trigger('click');`);
        assert.equal(await evaluate("document.body.textContent.includes('Dependency fixture log record')"), false);
        assert.equal(await evaluate("window.__dependencyStreams.length"), 1, "Clear must not reconnect SSE");
        assert.equal(await evaluate("localStorage.getItem('log_stream_last_cursor')"), "dependency-cursor");
        await evaluate(`new Promise(resolve=>{const tab=document.getElementById('info-tab');if(tab.classList.contains('active'))return resolve();tab.addEventListener('shown.bs.tab',resolve,{once:true});bootstrap.Tab.getOrCreateInstance(tab).show()})`);
        assert.equal(await evaluate("new URL(location.href).searchParams.get('tab')"), "info");
        // Reconnect is explicit and must carry the existing cursor, with one
        // replacement stream rather than duplicate subscriptions.
        await evaluate(`$('#btn-reconnect').trigger('click')`);
        await waitFor(() => evaluate("window.__dependencyStreams.length===2"), "explicit diagnostic reconnect");
        assert.equal(await evaluate("window.__dependencyStreams[0].readyState"), 2);
        assert.match(await evaluate("window.__dependencyStreams[1].url"), /dependency-cursor/);
        await evaluate(`window.__dependencyStreams[1].onopen?.();window.__dependencyStreams[1].emit({type:'journal',__REALTIME_TIMESTAMP:'1792173600000000',__CURSOR:'dependency-cursor-2',PRIORITY:'6',SYSLOG_IDENTIFIER:'fixture',MESSAGE:'Visible record: '+ 'technical-value-'.repeat(20)});`);
        for (const theme of ["light", "dark"]) for (const size of [{ name: "desktop", width: 1440, height: 1000 }, { name: "mobile", width: 390, height: 844 }]) {
            await client.send("Emulation.setDeviceMetricsOverride", { ...size, deviceScaleFactor: 1, mobile: size.name === "mobile" });
            await evaluate(`document.documentElement.dataset.bsTheme='${theme}'`);
            await client.settleRendering();
            assert.ok(await evaluate("document.documentElement.scrollWidth<=innerWidth+1"), `diagnostic ${theme} ${size.name} overflow`);
            if (evidence) {
                const shot = await client.send("Page.captureScreenshot", { format: "png" });
                fs.writeFileSync(path.join(evidence, `diagnostic-${theme}-${size.name}.png`), Buffer.from(shot.data, "base64"));
            }
        }
        await navigate("/index.php?page=config");
        assert.equal(await evaluate("localStorage.getItem('theme')"), "dark");
        assert.equal(await evaluate("localStorage.getItem('dependency-user-preference')"), "keep");
        assert.equal(await evaluate("sessionStorage.getItem('dependency-seeded')"), "keep");
        assert.match(await evaluate("document.cookie"), /dependency_session=keep/);
        assert.deepEqual(await evaluate("$.migrateWarnings || []"), []);
        assert.deepEqual(exceptions, []); assert.deepEqual(failedAssets, []);
        assert.deepEqual(mutations, [], "dependency workflows must not write settings");
        assert.deepEqual(migrationMessages, []); assert.deepEqual(eventErrors, []);
        console.log(`Dependency browser: real AJAX, Bootstrap bridge/events, invalid drafts, fonts/icons, standalone SSE, storage/cookie preservation passed${process.env.JQUERY_MIGRATE_PATH ? " with Migrate" : " without Migrate"}`);
    } catch (error) { failure = error; }
    finally {
        await finishFixture({failure: failure, client, browser: browser, php, profile: profile});
    }
}

main().catch(error => { console.error(error); process.exitCode = 1; });

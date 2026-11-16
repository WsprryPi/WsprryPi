// SPDX-License-Identifier: MIT
// Actual production main, templates, HTTP session, parser, and managed store.
'use strict';
const assert = require('node:assert/strict'), fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const http = require('node:http'), net = require('node:net');
const crypto = require('node:crypto');
const contract = require('../data/control-contract.js');
const {waitForPhp,createBrowserProfile,freePort,waitFor,launchBrowser,finishFixture,spawnFixtureChild,waitForSpawn,closeServer,getJson,stopChild} = require('./browser_harness.js');
async function portNumber(start) {
    for(let port = start; port < start + 100; ++port) {
        const server = net.createServer();
        if(await new Promise(resolve => {server.once('error',() => resolve(false));server.listen(port,'127.0.0.1',() => server.close(() => resolve(true)));})) return port;
    }
    throw new Error('No available fixture listener port');
}
async function main() {
    const binary = process.env.WSPRRYPI_MANAGED_MAIN_FIXTURE;
    assert(binary && path.isAbsolute(binary),'Explicit compiled private-state production-main fixture required');
    const ui = path.resolve(__dirname,'..'), directory = fs.mkdtempSync(path.join(os.tmpdir(),'configuration-recovery-ui-'));
    const backend = await portNumber(39400), socket = await portNumber(backend + 1), phpPort = await freePort();
    const ini = path.join(directory,'native.ini'), ledger = path.join(directory,'revocation-v1');
    let source = fs.readFileSync(path.join(ui,'../config/wsprrypi.ini'),'utf8')
        .replace(/^Call Sign = .*$/m,'Call Sign = AA0NT').replace(/^Grid Square = .*$/m,'Grid Square = EM18')
        .replace(/^Transmit Backend = .*$/m,'Transmit Backend = wtp').replace(/^Transport = .*$/m,'Transport = network_plain')
        .replace(/^Hostname =.*$/m,'Hostname = 127.0.0.1').replace(/^TCP Port = .*$/m,'TCP Port = 31417')
        .replace(/^Device ID =.*$/m,'Device ID = ' + 'b'.repeat(32)).replace(/^Enabled = true$/m,'Enabled = false')
        .replace(/^Web Port = .*$/m,'Web Port = ' + backend).replace(/^Socket Port = .*$/m,'Socket Port = ' + socket);
    const safetyOff = process.env.WSPRRYPI_RECOVERY_SAFETY_OFF === '1';
    if(safetyOff) source = source.replace(/^Privileged Network Safety = .*$/m,'Privileged Network Safety = insecure-disabled');
    fs.writeFileSync(ini + '.stock',source,{mode:0o600});
    source = source.replace(/^Fade Shape = .*$/m,'Fade Shape = invalid');fs.writeFileSync(ini,source,{mode:0o600});
    const original = fs.readFileSync(ini);
    async function hostReady() {
        const response = await fetch(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`,{signal:AbortSignal.timeout(2000)});
        const value = await response.json();
        if([409,429].includes(response.status) && contract.validControllerDocument(value,'target-error') &&
            ['capacity','rate_limited'].includes(value.error.code)) return false;
        assert.equal(response.status,200);assert(contract.validControllerDocument(value,'host-readiness'));
        return value.state === 'ready';
    }
    let application, php, proxy, chrome, client, profile, failure, dropNextRecoveryReply = false;
    const mutations = [], sockets = new Set();
    const launch = async () => {
        application = spawnFixtureChild(binary,[ledger,'--backend','simulated','-i',ini,'--web-port',String(backend),
            '--socket-port',String(socket),'--socket-loopback-only','--socket-loopback-family','ipv4']);
        await waitForSpawn(application);
        await waitFor(async () => (await getJson(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`)).state.endsWith('_recovery'),'production recovery readiness',15000);
    };
    try {
        await launch();
        php = spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});
        await waitForSpawn(php);await waitForPhp(php,`http://127.0.0.1:${phpPort}/index.php?page=config`,'recovery PHP templates');
        proxy = http.createServer((request,response) => {
            const url = new URL(request.url,'http://fixture'), production = ['/config','/version'].includes(url.pathname) || url.pathname.startsWith('/api/control/v1/');
            const mutation = production && !['GET','HEAD'].includes(request.method) ? {method:request.method,path:url.pathname} : null;
            if(mutation) mutations.push(mutation);
            if(mutation) {
                const digest = crypto.createHash('sha256');request.on('data',chunk => digest.update(chunk));
                request.on('end',() => {mutation.bodyDigest = digest.digest('hex');});
            }
            const outbound = http.request({hostname:'127.0.0.1',port:production ? backend : phpPort,method:request.method,path:request.url,headers:request.headers},upstream => {
                if(mutation) {
                    mutation.status = upstream.statusCode;let text = '';
                    upstream.on('data',chunk => {if(text.length < 32768) text += chunk.toString();});
                    upstream.on('end',() => {try {const value = JSON.parse(text);mutation.schema = value.schema;mutation.error = value.error?.code;} catch(_) {}});
                }
                if(dropNextRecoveryReply && url.pathname.endsWith('/host/configuration-recovery') && request.method === 'PUT' && upstream.statusCode === 200) {
                    upstream.resume();upstream.once('end',() => response.destroy());return;
                }
                response.writeHead(upstream.statusCode,upstream.headers);upstream.pipe(response);
            });
            outbound.on('error',() => {response.writeHead(503);response.end();});request.pipe(outbound);
        });
        proxy.on('upgrade',(request,downstream,head) => {
            if(request.url !== '/socket') {downstream.destroy();return;}
            const upstream = net.connect(socket,'127.0.0.1');sockets.add(upstream);sockets.add(downstream);
            upstream.on('connect',() => {
                upstream.write(`${request.method} / HTTP/${request.httpVersion}\r\n` + request.rawHeaders.reduce((text,value,index,all) => text + (index % 2 === 0 ? `${value}: ${all[index+1]}\r\n` : ''),'') + '\r\n');
                if(head.length) upstream.write(head);downstream.pipe(upstream).pipe(downstream);
            });
            upstream.on('error',() => downstream.destroy());downstream.on('error',() => upstream.destroy());
            upstream.on('close',() => {sockets.delete(upstream);downstream.destroy();});downstream.on('close',() => {sockets.delete(downstream);upstream.destroy();});
        });
        await new Promise(resolve => proxy.listen(0,'127.0.0.1',resolve));const origin = `http://127.0.0.1:${proxy.address().port}`;
        profile = createBrowserProfile();
        ({browser:chrome,client} = await launchBrowser({executable:process.env.CHROME_BIN ||
            (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'chromium'),
            args:['--headless','--no-sandbox','--disable-gpu','--no-first-run','--disable-background-networking','about:blank'],profile,timeoutMs:60000}));
        const run = expression => client.evaluate(expression);
        client.on('Runtime.exceptionThrown',event => console.error('Browser exception:',JSON.stringify(event.exceptionDetails)));
        await client.send('Page.navigate',{url:origin + '/index.php?page=config'});
        await waitFor(() => run("window.ConfigurationRecovery?.active && !document.getElementById('configurationRecoveryPanel').hidden"),'visible recovery editor',15000);
        assert.equal(await run("document.getElementById('wsprform').hidden && document.getElementById('wsprform').inert"),true);
        assert.equal(await run("document.getElementById('configurationRecoveryFields').textContent.includes('Saved value: invalid')"),true);
        assert.equal(await run("/Certificate|TLS Client Key|TLS CA File|TLS Server Identity/.test(document.getElementById('configurationRecoveryFields').textContent)"),false);
        assert.equal(mutations.length,0,'Page load and recovery projection must not persist or start a management session');
        assert.deepEqual(fs.readFileSync(ini),original);assert(!fs.existsSync(ledger + '.simulated-trace.json'),'No backend from rejected defaults');
        const output = process.env.WSPRRYPI_RECOVERY_SCREENSHOTS || path.join(directory,'screenshots');fs.mkdirSync(output,{recursive:true});
        async function capture(scene) {
            for(const theme of ['light','dark']) for(const viewport of [{name:'desktop',width:1440,height:1050},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name === 'mobile'});
                await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');window.scrollTo(0,0)`);await client.settleRendering();
                assert.equal(await run('document.documentElement.scrollWidth <= window.innerWidth'),true,'Recovery editor fits the viewport');
                const shot = await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
                fs.writeFileSync(path.join(output,`${scene}-${theme}-${viewport.name}.png`),Buffer.from(shot.data,'base64'));
            }
        }
        await capture('editable');
        // An owning semantic rejection must retain both the original source
        // and a new browser draft. A typed shape-valid negative port is not used:
        // set the slice to zero, which the owning validator rejects independently
        // of the transmit gate, while retaining an unrelated station draft.
        await run(`(() => {
            const fade=document.querySelector('[data-recovery-section="CW"][data-recovery-key="Fade Shape"]');
            fade.value=Array.from(fade.options).find(option=>option.textContent==='none').value;fade.dispatchEvent(new Event('change',{bubbles:true}));
            const call=document.querySelector('[data-recovery-section="WSPR"][data-recovery-key="Call Sign"]');
            call.value='UNSAVED DRAFT';call.dispatchEvent(new Event('input',{bubbles:true}));
            const slice=document.querySelector('[data-recovery-section="CW"][data-recovery-key="Fade Slice Ms"]');
            slice.value='0';slice.dispatchEvent(new Event('input',{bubbles:true}));document.getElementById('configurationRecoverySave').click();
        })()`);
        await waitFor(() => run("document.getElementById('configurationRecoveryFeedback').textContent.includes('still invalid') && !document.getElementById('configurationRecoverySave').disabled"),'owning validation refusal',15000);
        assert.deepEqual(fs.readFileSync(ini),original);assert.equal(await run("document.querySelector('[data-recovery-section=\"WSPR\"][data-recovery-key=\"Call Sign\"]').value"),'UNSAVED DRAFT');
        dropNextRecoveryReply = true;
        await run(`(() => {const call=document.querySelector('[data-recovery-section="WSPR"][data-recovery-key="Call Sign"]');call.value='AA0NT';call.dispatchEvent(new Event('input',{bubbles:true}));const slice=document.querySelector('[data-recovery-section="CW"][data-recovery-key="Fade Slice Ms"]');slice.value='20';slice.dispatchEvent(new Event('input',{bubbles:true}));document.getElementById('configurationRecoverySave').click();const later=document.querySelector('[data-recovery-section="CW"][data-recovery-key="Message"]');later.value='LATER UNSAVED DRAFT';later.dispatchEvent(new Event('input',{bubbles:true}));})()`);
        await waitFor(() => run("window.ConfigurationRecovery?.active && !document.getElementById('configurationRecoveryRetry').hidden && !document.getElementById('configurationRecoveryRetry').disabled"),'retained unknown save',15000);
        await waitFor(hostReady,'native readiness despite lost browser reply',15000);
        assert.equal(await run('window.ConfigurationRecovery.active'),true,'Native readiness must not erase an uncertain accepted operation');
        assert.equal(await run("document.querySelector('[data-recovery-section=\"CW\"][data-recovery-key=\"Message\"]').value"),'LATER UNSAVED DRAFT');
        if(safetyOff) {
            await waitFor(async () => (await fetch(`http://127.0.0.1:${backend}/api/control/v1/targets`)).status === 403,'ordinary management remains unavailable under saved Off',10000);
            assert(fs.readFileSync(ini,'utf8').includes('insecure-disabled'),'The saved Off preference is preserved');
        }
        const beforeRetry = fs.readFileSync(ini + '.managed-settings.json');dropNextRecoveryReply = false;
        await run("document.getElementById('configurationRecoveryRetry').click()");
        await waitFor(() => run("!document.getElementById('configurationRecoveryReturn').hidden && !document.getElementById('configurationRecoveryReturn').disabled"),'confirmed receipt retains later draft',15000);
        const saved = await getJson(`http://127.0.0.1:${backend}/config`);assert.notEqual(saved.CW.Message,'LATER UNSAVED DRAFT');
        const writes = mutations.filter(item => item.path.endsWith('/host/configuration-recovery') && item.method === 'PUT');
        assert(writes.length >= 3);assert(writes.slice(1).every(item => item.bodyDigest === writes[1].bodyDigest),'Lost-reply recovery retries the identical accepted request');
        assert.deepEqual(fs.readFileSync(ini + '.managed-settings.json'),beforeRetry,'Exact replay cannot create another configuration generation');
        await run("document.getElementById('configurationRecoveryReturn').click()");
        await waitFor(() => run("!window.ConfigurationRecovery?.active && !document.getElementById('wsprform').hidden && document.getElementById('callsign').value === 'AA0NT'"),'automatic normal Setup after verified save',20000);
        assert.equal((await getJson(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`)).state,'ready');
        const trace = JSON.parse(fs.readFileSync(ledger + '.simulated-trace.json','utf8'));assert(trace.events.every(event => !event.rf_on));
        await stopChild(application);application = null;
        fs.writeFileSync(ini + '.managed-settings.json','{corrupt previously initialized history',{mode:0o600});const corrupt = fs.readFileSync(ini + '.managed-settings.json');
        await launch();await client.send('Page.navigate',{url:origin + '/index.php?page=config'});
        await waitFor(() => run("window.ConfigurationRecovery?.active && document.getElementById('configurationRecoverySave').disabled"),'read-only saved-history hold',15000);
        assert.equal(await run("document.getElementById('configurationRecoveryReset').disabled"),true);
        assert.equal(await run("document.getElementById('configurationRecoveryFields').textContent.includes('No verified saved settings')"),true);
        await capture('read-only');assert.deepEqual(fs.readFileSync(ini + '.managed-settings.json'),corrupt);
        console.log(`PASS: production protected recovery UI, no autosave/default publication, visible invalid saved input, retained rejected draft, explicit save and automatic startup, private-field omission, read-only corrupt history, desktop/mobile light/dark captures: ${output}`);
    } catch(error) {
        failure = error;
        console.error('Recovery mutation outcomes:',JSON.stringify(mutations));
        if(client)try {console.error('Recovery page state:',JSON.stringify(await client.evaluate(`({
            editor:typeof window.ConfigurationRecovery,active:window.ConfigurationRecovery?.active,
            contract:typeof window.WsprryControl,client:typeof window.ConfigurationRecoveryClient,
            panel:document.getElementById('configurationRecoveryPanel')?.hidden,
            notice:document.getElementById('configurationRecoveryNotice')?.hidden,
            feedback:document.getElementById('configurationRecoveryFeedback')?.textContent,
            scriptFiles:Array.from(document.scripts).map(item=>new URL(item.src||location.href).pathname)
        })`)));} catch(_) {}
    }
    finally {
        for(const socket of sockets) socket.destroy();
        await Promise.allSettled([finishFixture({browser:chrome,client,php,profile,page:{}}),closeServer(proxy),stopChild(application)]);
        if(!failure) fs.rmSync(directory,{recursive:true,force:true});
    }
    if(failure) throw failure;
}
main().catch(error => {console.error(error);process.exitCode = 1;});

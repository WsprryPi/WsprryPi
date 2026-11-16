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
    const initializing = process.env.WSPRRYPI_INITIALIZATION_UI === '1', migrating = process.env.WSPRRYPI_MIGRATION_UI === '1';
    const retiring=process.env.WSPRRYPI_CONTROLLER_RETIREMENT_UI==='1';
    const page = initializing || migrating ? 'maintenance' : 'config';
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
    if(!initializing && !migrating && !retiring) source = source.replace(/^Fade Shape = .*$/m,'Fade Shape = invalid');
    if(migrating||retiring) source += '\n; existing local recurrence fixture\n';
    fs.writeFileSync(ini,source,{mode:0o600});
    const retirementTarget='c'.repeat(32);
    if(retiring){
        const port=await portNumber(socket+1);
        const settings={Transport:'network_plain',Hostname:'127.0.0.1','TCP Port':port,'Device ID':retirementTarget,
            'TLS Server Identity':'','TLS CA File':'','TLS Client Certificate':'','TLS Client Key':'',Endpoint:'','USB Serial':'',
            'USB Vendor ID':0,'USB Product ID':0,'Allow Frequency Adjustment':false,'Start Uncertainty ns':100000000};
        fs.writeFileSync(ini+'.wtp-assignments.json',JSON.stringify({version:1,assignments:[{device_id:retirementTarget,name:'Offline fixture transmitter',settings,
            product:'WsprryPi',revocation_generation:'0',management_port:port,enabled:false,in_flight:false,last_start_ns:'0',
            schedule:{mode:'tone',frequency_hz:14097100,duration_ms:1000,period_seconds:60,phase_seconds:0}}],removals:[]}),{mode:0o600});
    }
    const original = fs.readFileSync(ini);
    async function hostReady() {
        const response = await fetch(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`,{signal:AbortSignal.timeout(2000)});
        const value = await response.json();
        if([409,429].includes(response.status) && contract.validControllerDocument(value,'target-error') &&
            ['capacity','rate_limited'].includes(value.error.code)) return false;
        assert.equal(response.status,200);assert(contract.validControllerDocument(value,'host-readiness'));
        return value.state === 'ready';
    }
    let application, php, proxy, chrome, client, profile, failure, dropNextRecoveryReply = false, dropMigrationReply = false, holdMigrationLookup = false, dropRetirementReply=false,holdRetirementRead=false;
    const mutations = [], sockets = new Set();
    const launch = async () => {
        application = spawnFixtureChild(binary,[ledger,'--backend','simulated','-i',ini,'--web-port',String(backend),
            '--socket-port',String(socket),'--socket-loopback-only','--socket-loopback-family','ipv4']);
        await waitForSpawn(application);
        await waitFor(async () => {
            try {return migrating||retiring ? await hostReady() : (await getJson(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`)).state.endsWith('_recovery');}
            catch (_) {return false;}
        },'production recovery readiness',15000);
    };
    try {
        await launch();
        php = spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:ui});
        await waitForSpawn(php);await waitForPhp(php,`http://127.0.0.1:${phpPort}/index.php?page=config`,'recovery PHP templates');
        proxy = http.createServer((request,response) => {
            const url = new URL(request.url,'http://fixture'), production = ['/config','/version'].includes(url.pathname) || url.pathname.startsWith('/api/control/v1/');
            if(holdRetirementRead&&url.pathname.endsWith('/host/retirements')&&request.method==='GET'){response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:contract.contract,schema:'target-error',error:{code:'transport_unavailable',retryable:false}}));return;}
            if(holdMigrationLookup && url.pathname.includes('/operations/')) {response.writeHead(503,{'Content-Type':'application/json'});response.end(JSON.stringify({contract:contract.contract,schema:'target-error',error:{code:'transport_unavailable',retryable:false}}));return;}
            const mutation = production && !['GET','HEAD'].includes(request.method) ? {method:request.method,path:url.pathname} : null;
            if(mutation) mutations.push(mutation);
            if(mutation) {
                const digest = crypto.createHash('sha256');request.on('data',chunk => digest.update(chunk));
                request.on('end',() => {mutation.bodyDigest = digest.digest('hex');});
            }
            const outbound = http.request({hostname:'127.0.0.1',port:production ? backend : phpPort,method:request.method,path:request.url,headers:request.headers},upstream => {
                if(!mutation&&url.pathname.includes('/targets/')&&upstream.statusCode>=400){let diagnostic='';upstream.on('data',chunk=>{if(diagnostic.length<2048)diagnostic+=chunk;});upstream.on('end',()=>console.error('Target read refusal:',url.pathname,upstream.statusCode,diagnostic));}
                if(mutation) {
                    mutation.status = upstream.statusCode;let text = '';
                    upstream.on('data',chunk => {if(text.length < 32768) text += chunk.toString();});
                    upstream.on('end',() => {try {const value = JSON.parse(text);mutation.schema = value.schema;mutation.error = value.error?.code;} catch(_) {}});
                }
                if(dropRetirementReply&&url.pathname.endsWith('/host/retirements')&&request.method==='POST'&&[200,202].includes(upstream.statusCode)){upstream.resume();upstream.once('end',()=>response.destroy());return;}
                if(dropMigrationReply && holdMigrationLookup && /\/migration(?:\/rollback)?$/.test(url.pathname) && request.method === 'POST' && upstream.statusCode === 200) {upstream.resume();upstream.once('end',()=>response.destroy());return;}
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
        await client.send('Runtime.enable');
        client.on('event',event=>{if(event.method==='Runtime.consoleAPICalled')console.error('Browser console:',JSON.stringify(event.params.args));if(event.method==='Runtime.exceptionThrown')console.error('Browser exception:',JSON.stringify(event.params.exceptionDetails));});
        await client.send('Page.navigate',{url:origin + '/index.php?page=' + page});
        if(!migrating&&!retiring) {
        await waitFor(() => run("window.ConfigurationRecovery?.active && !document.getElementById('configurationRecoveryPanel').hidden"),'visible recovery editor',15000);
        if(!initializing) assert.equal(await run("document.getElementById('wsprform').hidden && document.getElementById('wsprform').inert"),true);
        else assert.equal(await run("document.getElementById('configurationRecoveryTitle').textContent"),'Set up this transmitter');
        if(!initializing) assert.equal(await run("document.getElementById('configurationRecoveryFields').textContent.includes('Saved value: invalid')"),true);
        assert.equal(await run("/Certificate|TLS Client Key|TLS CA File|TLS Server Identity/.test(document.getElementById('configurationRecoveryFields').textContent)"),false);
        assert.equal(mutations.length,0,'Page load and recovery projection must not persist or start a management session');
        assert.deepEqual(fs.readFileSync(ini),original);assert(!fs.existsSync(ledger + '.simulated-trace.json'),'No backend from rejected defaults');
        } else if(migrating) {await waitFor(()=>run("!!document.getElementById('recurrenceMigrationReview') && !document.getElementById('recurrenceMigration').hidden && !document.getElementById('recurrenceMigrationReview').disabled"),'verified local migration state',15000);assert.equal(mutations.length,0);}
        const output = process.env.WSPRRYPI_RECOVERY_SCREENSHOTS || path.join(directory,'screenshots');fs.mkdirSync(output,{recursive:true});
        async function capture(scene) {
            for(const theme of ['light','dark']) for(const viewport of [{name:'desktop',width:1440,height:1050},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name === 'mobile'});
                await run(`document.documentElement.setAttribute('data-bs-theme','${theme}')`);await client.settleRendering();
                await run(retiring?"window.scrollTo(0,document.getElementById('controllerRetirement').getBoundingClientRect().top+window.scrollY-(document.querySelector('.navbar')?.getBoundingClientRect().height||110)-24)":'window.scrollTo(0,0)');await client.settleRendering();
                assert.equal(await run('document.documentElement.scrollWidth <= window.innerWidth'),true,'Recovery editor fits the viewport');
                const shot = await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
                fs.writeFileSync(path.join(output,`${scene}-${theme}-${viewport.name}.png`),Buffer.from(shot.data,'base64'));
            }
        }
        async function confirmByKeyboard(consent,confirm) {
            await run(`document.getElementById('${consent}').focus()`);
            assert.equal(await run('document.activeElement.id'),consent);
            await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:' ',code:'Space',windowsVirtualKeyCode:32});
            await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:' ',code:'Space',windowsVirtualKeyCode:32});
            assert.equal(await run(`document.getElementById('${consent}').checked && !document.getElementById('${confirm}').disabled`),true);
            await run(`document.getElementById('${confirm}').focus()`);
            assert.equal(await run('document.activeElement.id'),confirm);
            await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:' ',code:'Space',windowsVirtualKeyCode:32});
            await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:' ',code:'Space',windowsVirtualKeyCode:32});
        }
        if(retiring){
            await waitFor(()=>run("!!window.FleetClient && !!window.FleetSchedules && !!window.ControllerRetirement"),'retirement editor scripts',15000);
            await run("document.getElementById('fleet-tab').click()");
            await waitFor(()=>run("document.getElementById('fleet-tab').classList.contains('active')"),'visible Fleet tab',10000);
            await waitFor(()=>run("Array.from(document.querySelectorAll('#fleet-output-list button')).some(button=>button.textContent==='Retire schedule'&&!button.disabled)"),'existing assignment row',15000);
            await run("Array.from(document.querySelectorAll('#fleet-output-list button')).find(button=>button.textContent==='Retire schedule').click()");
            await waitFor(()=>run("!document.getElementById('controllerRetirement').hidden && !document.getElementById('controllerRetirementReview').disabled"),'verified retirement review',15000);
            assert.equal(await run("document.getElementById('controllerRetirement').getBoundingClientRect().height>0"),true,'Retirement review must be visually reachable through Fleet');
            assert.equal(await run("document.getElementById('fleet-schedule-new').disabled"),true);
            await capture('retirement-legacy');
            await run("document.getElementById('controllerRetirementReview').click()");
            assert.equal(await run("document.getElementById('controllerRetirementConfirm').disabled"),true);
            await capture('retirement-review');
            const localBeforeRetirement=fs.readFileSync(ini);
            dropRetirementReply=true;holdRetirementRead=true;
            await confirmByKeyboard('controllerRetirementConsent','controllerRetirementConfirm');
            await waitFor(()=>run("!document.getElementById('controllerRetirementRefresh').disabled && document.getElementById('controllerRetirementFeedback').textContent.includes('unconfirmed')"),'retained retirement intent',15000);
            const readiness=await getJson(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`);
            const file=path.join(ledger+'.controller-retirements-'+readiness.device_id,'authority.json');
            const accepted=fs.readFileSync(file),book=JSON.parse(accepted);
            assert.equal(book.assignments[0].state,'retiring');assert(book.assignments[0].intent);assert.equal(book.assignments[0].completion,null);
            await capture('retirement-unknown');dropRetirementReply=false;holdRetirementRead=false;
            await run("document.getElementById('controllerRetirementRefresh').click()");
            await waitFor(()=>run("document.getElementById('controllerRetirementFeedback').textContent.includes('work remains pending')"),'verified pending retirement',15000);
            assert.deepEqual(fs.readFileSync(file),accepted,'Readback cannot create another retirement generation');
            assert.equal(await run("document.getElementById('fleet-schedule-save').disabled"),true);
            const writes=mutations.filter(item=>item.path.endsWith('/host/retirements')&&item.method==='POST');
            assert(writes.length>=1);assert(writes.every(item=>item.bodyDigest===writes[0].bodyDigest));
            assert.deepEqual(fs.readFileSync(ini),localBeforeRetirement,'Retirement cannot rewrite local transmitter settings');
            const trace=JSON.parse(fs.readFileSync(ledger+'.simulated-trace.json'));assert(trace.events.every(event=>!event.rf_on));
            await capture('retirement-pending');
            console.log('PASS: actual protected existing-assignment retirement review, retained unknown request, exact readback without another generation, offline pending, save fence, desktop/mobile light/dark, and no RF: '+output);return;
        }
        if(migrating) {
            await capture('legacy');
            const before=JSON.parse(fs.readFileSync(ini+'.managed-settings.json'));
            dropMigrationReply=true;holdMigrationLookup=true;
            await run("document.getElementById('recurrenceMigrationReview').click()");
            assert.equal(await run("document.getElementById('recurrenceMigrationConfirm').disabled"),true);
            await capture('review');
            await confirmByKeyboard('recurrenceMigrationConsent','recurrenceMigrationConfirm');
            await waitFor(()=>run("!document.getElementById('recurrenceMigrationRetry').hidden && !document.getElementById('recurrenceMigrationRetry').disabled"),'retained unknown migration',15000);
            await waitFor(()=>JSON.parse(fs.readFileSync(ini+'.managed-settings.json')).scheduling.enabled===false,'actual durable disabled staging',15000);
            await capture('unconfirmed');
            const staged=fs.readFileSync(ini+'.managed-settings.json');
            holdMigrationLookup=false;await run("document.getElementById('recurrenceMigrationRetry').click()");
            await waitFor(()=>run("document.getElementById('recurrenceMigrationRetry').hidden && !document.getElementById('recurrenceMigrationRollback').hidden && !document.getElementById('recurrenceMigrationRollback').disabled"),'verified staging readback',15000);
            assert.deepEqual(fs.readFileSync(ini+'.managed-settings.json'),staged,'Receipt recovery cannot write another generation');
            assert.equal(BigInt(JSON.parse(staged).configuration_generation),BigInt(before.configuration_generation)+1n);
            await capture('staged');
            await run("document.getElementById('recurrenceMigrationRollback').click()");
            assert.equal(await run("document.getElementById('recurrenceMigrationConfirm').disabled"),true);
            assert.match(await run("document.getElementById('recurrenceMigrationConsentLabel').textContent"),/may transmit/);
            await capture('rollback-review');
            await confirmByKeyboard('recurrenceMigrationConsent','recurrenceMigrationConfirm');
            await waitFor(()=>run("document.getElementById('recurrenceMigrationState').textContent.includes('previous schedule is permitted') && document.getElementById('recurrenceMigrationRetry').hidden"),'verified explicit rollback',15000);
            const rolled=JSON.parse(fs.readFileSync(ini+'.managed-settings.json'));
            assert.equal(rolled.scheduling.enabled,true);assert.equal(rolled.configuration.Operation.Transmit,false);
            const actions=mutations.filter(item=>/\/migration(?:\/rollback)?$/.test(item.path));
            assert(actions.length>=2);
            const trace=JSON.parse(fs.readFileSync(ledger+'.simulated-trace.json'));assert(trace.events.every(event=>!event.rf_on));
            await capture('rollback');
            console.log('PASS: actual Maintenance migration, explicit review, lost reply, exact receipt/readback without duplicate generation, explicit rollback, desktop/mobile light/dark, and no RF: '+output);
            return;
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
        await waitFor(() => run(initializing ? "!window.ConfigurationRecovery?.active && !document.getElementById('repairConfigButton').disabled" : "!window.ConfigurationRecovery?.active && !document.getElementById('wsprform').hidden && document.getElementById('callsign').value === 'AA0NT'"),'ordinary page after verified save',20000);
        if(initializing) {const record=JSON.parse(fs.readFileSync(path.join(ledger+'.recurrence-'+(await getJson(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`)).device_id,'authority.json')));assert.equal(record.state,'staged');assert.equal(JSON.parse(fs.readFileSync(ini+'.managed-settings.json')).scheduling.enabled,false);}
        assert.equal((await getJson(`http://127.0.0.1:${backend}/api/control/v1/host/readiness`)).state,'ready');
        const trace = JSON.parse(fs.readFileSync(ledger + '.simulated-trace.json','utf8'));assert(trace.events.every(event => !event.rf_on));
        await stopChild(application);application = null;
        fs.writeFileSync(ini + '.managed-settings.json','{corrupt previously initialized history',{mode:0o600});const corrupt = fs.readFileSync(ini + '.managed-settings.json');
        await launch();await client.send('Page.navigate',{url:origin + '/index.php?page=' + page});
        await waitFor(() => run("window.ConfigurationRecovery?.active && document.getElementById('configurationRecoverySave').disabled"),'read-only saved-history hold',15000);
        assert.equal(await run("document.getElementById('configurationRecoveryReset').disabled"),true);
        assert.equal(await run("document.getElementById('configurationRecoveryFields').textContent.includes('No verified saved settings')"),true);
        await capture('read-only');assert.deepEqual(fs.readFileSync(ini + '.managed-settings.json'),corrupt);
        console.log(`PASS: production protected recovery UI, no autosave/default publication, visible invalid saved input, retained rejected draft, explicit save and automatic startup, private-field omission, read-only corrupt history, desktop/mobile light/dark captures: ${output}`);
    } catch(error) {
        failure = error;
        if(client)try {console.error('Migration diagnosis:',await client.evaluate(`(async()=>{const c=ConfigurationRecoveryClient.create({base:'/api/control/v1'});try{const r=await c.readiness();const out={r};for(const k of ['context','settings','migration'])try{out[k]=await c.targetRequest(r.device_id,k);if(k==='context')out.capabilities=out[k].data.capabilities.map(v=>({resource:v.resource,valid:WsprryControl.supportedPiResource(v)}));}catch(e){out[k]={error:e.code,message:e.message};}return out;}catch(e){return {error:e.code,message:e.message};}})()`));}catch(_){}
        console.error('Recovery mutation outcomes:',JSON.stringify(mutations));
        if(client)try {console.error('Recovery page state:',JSON.stringify(await client.evaluate(`({
            editor:typeof window.ConfigurationRecovery,active:window.ConfigurationRecovery?.active,
            contract:typeof window.WsprryControl,client:typeof window.ConfigurationRecoveryClient,
            panel:document.getElementById('configurationRecoveryPanel')?.hidden,
            notice:document.getElementById('configurationRecoveryNotice')?.hidden,
            feedback:document.getElementById('configurationRecoveryFeedback')?.textContent,
            migration:{hidden:document.getElementById('recurrenceMigration')?.hidden,feedback:document.getElementById('recurrenceMigrationFeedback')?.textContent,state:document.getElementById('recurrenceMigrationState')?.textContent},
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

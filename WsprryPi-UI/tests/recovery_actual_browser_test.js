// SPDX-License-Identifier: MIT
// Actual isolated backend/web/maintenance owners; no RF or installed services.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { freePort, spawnFixtureChild, waitForPhp, createBrowserProfile, launchBrowser, waitFor, finishFixture, closeServer } = require('./browser_harness.js');
(async () => {
    const backendPort = Number(process.argv[2]), directory = path.resolve(process.argv[3]);
    const controllerOnly = process.argv[4] === 'controller';
    const basePath = '/wsprrypi';
    assert(Number.isInteger(backendPort) && backendPort >= 1024 && backendPort <= 49151);
    const ui = path.resolve(__dirname, '..'), captures = '/private/tmp/wsprrypi-sqlite-maintenance-ui' + (controllerOnly ? '/controller' : '');
    fs.mkdirSync(captures, { recursive: true });
    const phpPort = await freePort();
    let php, proxy, browser, client, profile, failure, dropConfirm = true, failReadback = false;
    const mutations = [];
    try {
        php = spawnFixtureChild('php', ['-S', `127.0.0.1:${phpPort}`, '-t', 'data', 'tests/browser_fixture_router.php'], { cwd: ui, env: { ...process.env, WSPRRYPI_UI_FIXTURE_PREFIX: basePath }, stdio: ['ignore', 'pipe', 'pipe'] });
        await waitForPhp(php, `http://127.0.0.1:${phpPort}/index.php?page=maintenance`, 'Maintenance templates');
        proxy = http.createServer((request, response) => {
            const route = request.url.startsWith(basePath + '/') ? request.url.slice(basePath.length) : request.url;
            if (failReadback && request.method === 'GET' && route === '/api/recovery/jobs') {
                response.writeHead(503, { 'Content-Type': 'application/json' });response.end(JSON.stringify({ error: 'backend_unavailable' }));return;
            }
            const api = route.startsWith('/api/') || route === '/version' || route === '/config';
            if (api && request.method !== 'GET') mutations.push(route);
            const outgoing = http.request({ hostname: '127.0.0.1', port: api ? backendPort : phpPort,
                path: api ? route : request.url, method: request.method, headers: request.headers }, incoming => {
                if (dropConfirm && request.url.endsWith('/confirm') && incoming.statusCode === 200) {
                    dropConfirm = false; incoming.resume(); incoming.once('end', () => {
                        response.writeHead(503, { 'Content-Type': 'application/json' });
                        response.end(JSON.stringify({ error: 'outcome_unknown' }));
                    }); return;
                }
                response.writeHead(incoming.statusCode, incoming.headers); incoming.pipe(response);
            });
            outgoing.on('error', () => { if (!response.headersSent) response.writeHead(503); response.end(); });
            request.pipe(outgoing);
        });
        await new Promise(resolve => proxy.listen(0, '127.0.0.1', resolve));
        const origin = `http://127.0.0.1:${proxy.address().port}`;
        profile = createBrowserProfile();
        ({ browser, client } = await launchBrowser({ executable: process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'chromium'),
            args: ['--headless', '--no-sandbox', '--disable-gpu', '--disable-background-networking', 'about:blank'], profile, timeoutMs: 60000 }));
        const run = code => client.evaluate(code);
        const settled = () => waitFor(() => run("document.getElementById('backupRestore')?.getAttribute('aria-busy')==='false'"), 'Maintenance action settled', 30000);
        const capture = async state => {
            for (const theme of ['light', 'dark']) for (const [name, width, height] of [['desktop', 1365, 1000], ['mobile', 390, 844]]) {
                await client.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: name === 'mobile' });
                await run('document.fonts.ready');
                await client.settleRendering();
                await run(`document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});window.scrollTo({top:0,behavior:'instant'});new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
                assert(await run('document.documentElement.scrollWidth<=innerWidth'), 'No viewport overflow');
                const bounds = await run("(()=>{const r=document.getElementById('backupRestore').getBoundingClientRect();return {x:Math.max(0,r.x+scrollX),y:r.y+scrollY,width:r.width,height:r.height,scale:1}})()");
                assert(await run("(()=>{const p=document.getElementById('backupRestore').getBoundingClientRect();return [...document.getElementById('backupRestore').children].every(e=>{const r=e.getBoundingClientRect();return r.left>=p.left&&r.right<=p.right+1})})()"), 'Maintenance children fit their panel');
                const image = await client.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true, clip: bounds });
                fs.writeFileSync(path.join(captures, `${state}-${theme}-${name}.png`), Buffer.from(image.data, 'base64'));
            }
        };
        await client.send('Page.navigate', { url: origin + basePath + '/index.php?page=maintenance' });
        await waitFor(() => run("!document.getElementById('backupCreate')?.disabled"), 'Actual maintenance admission', 25000);
        assert.equal(await run('WSPRRYPI_PATHS.recoveryPath'), basePath + '/api/recovery', 'Actual PHP alias preserves the installed application prefix');
        await capture('ready');
        const phrase = 'synthetic browser recovery passphrase';
        await run(`document.getElementById('backupPassphrase').value=${JSON.stringify(phrase)};document.getElementById('backupConfirmation').value='different';document.getElementById('backupCreate').click()`);
        assert(await run("document.getElementById('backupFeedback').textContent.includes('matching')"));
        assert.equal(mutations.filter(item => item === '/api/recovery/jobs').length, 0);
        for (let attempt = 0; attempt < 5; ++attempt) {
            await run(`document.getElementById('backupPassphrase').value=${JSON.stringify(phrase)};document.getElementById('backupConfirmation').value=${JSON.stringify(phrase)};document.getElementById('backupCreate').click()`);
            await settled();
            await waitFor(() => run("!document.getElementById('backupDownload').hidden || document.getElementById('recoveryStatus').textContent.includes('busy')"), 'Actual encrypted backup', 30000);
            if (await run("!document.getElementById('backupDownload').hidden")) break;
            assert(attempt < 4, 'Backup capture remained busy');
        }
        assert.equal(await run("document.getElementById('backupPassphrase').value+document.getElementById('backupConfirmation').value"), '');
        await client.send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: directory });
        await run("document.getElementById('backupDownload').click()");
        let downloaded;
        await waitFor(() => { downloaded = fs.readdirSync(directory).find(name => /^wsprrypi-backup-[a-f0-9]{32}\.age$/.test(name)); return !!downloaded; }, 'Encrypted browser download', 15000);
        const dom = await client.send('DOM.getDocument');
        const input = await client.send('DOM.querySelector', { nodeId: dom.root.nodeId, selector: '#restoreFile' });
        await client.send('DOM.setFileInputFiles', { nodeId: input.nodeId, files: [path.join(directory, downloaded)] });
        await run("document.getElementById('restoreUpload').click()"); await settled();
        await waitFor(() => run("!document.getElementById('restoreValidateForm').hidden"), 'Uploaded archive ready for separate validation', 20000);
        assert(await run("document.getElementById('restorePreview').hidden"), 'Upload alone does not confirm or preview');
        const beforeValidation = mutations.filter(item => item.endsWith('/validate')).length;
        await run("document.getElementById('restorePassphrase').value='x'.repeat(1025);document.getElementById('restoreValidate').click()");
        assert(await run("document.getElementById('recoveryActionFeedback').textContent.includes('1,024') && document.activeElement.id==='restorePassphrase'"));
        assert.equal(mutations.filter(item => item.endsWith('/validate')).length, beforeValidation);
        await run("document.getElementById('restorePassphrase').value='wrong phrase';document.getElementById('restoreValidate').click()"); await settled();
        await waitFor(() => run("document.getElementById('recoveryStatus').textContent.includes('passphrase')"), 'Wrong phrase recovery', 15000);
        await run(`document.getElementById('restorePassphrase').value=${JSON.stringify(phrase)};document.getElementById('restoreValidate').click()`); await settled();
        await waitFor(() => run("!document.getElementById('restorePreview').hidden"), 'Actual owner preview', 25000);
        assert.equal(await run('document.activeElement.id'), 'restorePreviewTitle', 'Explicit validation hands focus to its preview');
        assert(await run("document.getElementById('restoreConfirm').disabled"), 'Exact review consent required');
        failReadback = true;
        await run("document.getElementById('recoveryRefresh').click()");await settled();
        assert(await run("document.getElementById('recoveryStatus').textContent.includes('Last observed:') && document.getElementById('restoreValidate').disabled && document.getElementById('restoreConfirm').disabled"), 'Failed readback remains explicit and staged actions stay disabled');
        failReadback = false;
        await run("document.getElementById('recoveryRefresh').click()");await settled();
        await capture('preview');
        await run("document.getElementById('restoreConsent').focus()");
        await client.send('Input.dispatchKeyEvent', { type: 'keyDown', key: ' ', code: 'Space', windowsVirtualKeyCode: 32 });
        await client.send('Input.dispatchKeyEvent', { type: 'keyUp', key: ' ', code: 'Space', windowsVirtualKeyCode: 32 });
        assert(await run("!document.getElementById('restoreConfirm').disabled"));
        await client.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
        await client.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
        assert.equal(await run('document.activeElement.id'), 'restoreConfirm');
        await client.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13, text: '\r', unmodifiedText: '\r' });
        await client.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
        await waitFor(() => run("document.getElementById('recoveryStatus').textContent.includes('Restore verified')"), 'Restore completes after lost confirmation reply', 60000);
        assert.equal(mutations.filter(item => item.endsWith('/confirm')).length, 1, 'Lost confirmation is not replayed');
        await client.send('Page.reload');
        await waitFor(() => run("document.getElementById('recoveryStatus')?.textContent.includes('Restore verified')"), 'Reload reads durable result', 20000);
        await waitFor(() => run("!document.getElementById('destinationRead').disabled"), 'Independent destination review remains reachable', 15000);
        assert.equal(mutations.filter(item => item.endsWith('/confirm')).length, 1);
        await run("document.getElementById('destinationRead').click()"); await settled();
        assert(await run("!document.getElementById('destinationReview').hidden"));
        await capture('restored-review');
        if (controllerOnly) {
            assert(await run("document.getElementById('destinationLocalRow').hidden && document.getElementById('destinationLocal').disabled"), 'Controller role has no local-output resumption control');
            await run("document.getElementById('destinationRemote').click();document.getElementById('destinationRelease').click()");await settled();
            assert(await run("document.getElementById('destinationFeedback').textContent.includes('unavailable in controller-only mode') && document.getElementById('destinationFeedback').textContent.includes('existing pairing and recovery checks apply')"));
        } else {
            await run("document.getElementById('destinationLocal').click();document.getElementById('destinationRelease').click()"); await settled();
            assert(await run("document.getElementById('destinationFeedback').textContent.includes('stopped; use Start') && document.getElementById('destinationFeedback').textContent.includes('Remote authority: held')"));
        }
        assert.equal(await run('document.activeElement.id'), 'destinationFeedback', 'Hold release returns focus to its outcome');
        assert.equal(await run(`JSON.stringify(localStorage).includes(${JSON.stringify(phrase)})||JSON.stringify(sessionStorage).includes(${JSON.stringify(phrase)})`), false, 'Passphrase is never persisted');
        console.log('PASS actual Maintenance create/download/upload/wrong-passphrase/preview/keyboard-confirm/lost-reply/reload/destination-review; desktop/mobile light/dark captures: ' + captures);
    } catch (error) {
        failure = error;
        if (client) try { console.error(await client.evaluate("({status:document.getElementById('recoveryStatus')?.textContent,feedback:document.getElementById('recoveryActionFeedback')?.textContent,backup:document.getElementById('backupFeedback')?.textContent,upload:document.getElementById('restoreUploadFeedback')?.textContent,destination:document.getElementById('destinationFeedback')?.textContent})")); } catch (_) {}
    } finally {
        await finishFixture({ failure, client, browser, php, profile, cleanups: [async () => { if (proxy) await closeServer(proxy); }] });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });

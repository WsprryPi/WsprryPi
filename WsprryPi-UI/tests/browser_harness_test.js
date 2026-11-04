"use strict";

const assert = require("node:assert/strict");
const { test: runTest } = require("node:test");
const test = (name, body) => runTest(name, { timeout: 30000 }, body);
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { once } = require("node:events");
const { WebSocketServer } = require("ws");
const { CdpClient, bounded, waitFor, getJson, getStatus, launchBrowser, finishFixture, closeServer,
    spawnIsolatedProcess, waitForSpawn, stopIsolatedProcess, spawnFixtureChild } = require("./browser_harness.js");

async function peer(handler, body, options = {}) {
    const server = new WebSocketServer({ port: 0, host: "127.0.0.1" });
    await once(server, "listening");
    server.on("connection", socket => socket.on("message", raw => handler(socket, JSON.parse(raw))));
    let client;
    const connect = async () => { client = new CdpClient(`ws://127.0.0.1:${server.address().port}`, options); await client.open(); };
    try {
        if (options.createInStage) await bounded("completed startup", 3000, connect);
        else await connect();
        await body(client, server);
    }
    finally { client?.close(); for (const socket of server.clients) socket.terminate(); await new Promise(resolve => server.close(resolve)); }
}

test("real CDP socket: correlation, fragmented replies, evaluation and protocol refusal", async () => {
    await peer((socket, message) => {
        if (message.method === "bad-eval") socket.send(JSON.stringify({ id: message.id, result: { exceptionDetails: { text: "fixture exception" } } }));
        else if (message.method === "bad-protocol") socket.send(JSON.stringify({ id: message.id, error: { message: "fixture protocol refusal" } }));
        else {
            const wire = JSON.stringify({ id: message.id, result: { result: { type: "number", value: message.params.value } } });
            socket.send(wire.slice(0, 8), { fin: false }); socket.send(wire.slice(8), { fin: true });
        }
    }, async client => {
        const replies = await Promise.all([client.send("echo", { value: 1 }), client.send("echo", { value: 2 })]);
        assert.deepEqual(replies.map(reply => reply.result.value), [1, 2]);
        await assert.rejects(client.send("bad-eval"), /fixture exception/);
        await assert.rejects(client.send("bad-protocol"), /fixture protocol refusal/);
        assert.equal(client.pending.size, 0);
    });
});

for (const mode of ["disconnect", "malformed", "dialog", "dispose", "missing-ack"]) {
    test(`real CDP exchange: ${mode} rejects pending and leaves no request behind`, async () => {
        let observed;
        const received = new Promise(resolve => { observed = resolve; });
        await peer((socket, message) => {
            observed();
            if (mode === "disconnect") socket.close();
            if (mode === "malformed") socket.send('{');
            if (mode === "dialog") socket.send(JSON.stringify({ method: "Page.javascriptDialogOpening", params: { type: "confirm", message: "unexpected destructive action" } }));
        }, async client => {
            const pending = client.send("held operation", {}, 150);
            const outcome = assert.rejects(pending, mode === "missing-ack" ? error => error.code === "HARNESS_TIMEOUT" && error.stage === "held operation" : /disconnect|framing|dialog|disposed/);
            await received; // Establish omitted acknowledgment before timing out, never sleep to guess it.
            if (mode === "dispose") client.close();
            await outcome;
            assert.equal(client.pending.size, 0);
        });
    });
}

test("expected dialog policy is per consumer and explicitly supports declining", async () => {
    let handled;
    await peer((socket, message) => {
        if (message.method === "trigger") socket.send(JSON.stringify({ method: "Page.javascriptDialogOpening", params: { type: "confirm", message: "expected review" } }));
        if (message.method === "Page.handleJavaScriptDialog") {
            handled = message.params.accept;
            socket.send(JSON.stringify({ id: message.id, result: {} }));
            socket.send(JSON.stringify({ id: 1, result: {} }));
        }
    }, async client => {
        client.expectedDialogs.add("expected review"); client.acceptDialogs = false;
        await client.send("trigger");
        assert.equal(handled, false); assert.deepEqual(client.dialogs, ["expected review"]);
    }, { createInStage: true });
});

test("HTTP: status/JSON/size refusal, stalled body, and a bounded outer readiness stage", async () => {
    let observed;
    let held = new Promise(resolve => { observed = resolve; });
    const server = http.createServer((req, res) => {
        if (req.url === "/hold") { res.writeHead(200); res.write("{"); observed(); return; }
        if (req.url === "/bad") { res.end("{"); return; }
        if (req.url === "/large") { res.end("x".repeat(100)); return; }
        res.writeHead(503); res.end('{}');
    });
    await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
    const url = `http://127.0.0.1:${server.address().port}`;
    try {
        assert.equal(await getStatus(url), 503);
        await assert.rejects(getJson(url), /status 503/);
        await assert.rejects(getJson(url + "/bad"), SyntaxError);
        await assert.rejects(getJson(url + "/large", { maxBytes: 10 }), /capture bound/);
        const request = assert.rejects(getJson(url + "/hold", { timeoutMs: 150 }), error => error.code === "HARNESS_TIMEOUT");
        await held; await request;
        held = new Promise(resolve => { observed = resolve; });
        const ready = assert.rejects(waitFor(() => getJson(url + "/hold"), "scripts initialized", 150), error => error.code === "HARNESS_TIMEOUT" && /scripts initialized|HTTP JSON/.test(error.message));
        await held; await ready;
    } finally { await closeServer(server); }
});

test("arbitrary held predicate and rejected script readiness cannot escape a stage bound", async () => {
    await assert.rejects(waitFor(() => new Promise(() => {}), "missing initialization acknowledgment", 100), error => error.code === "HARNESS_TIMEOUT" && error.stage === "missing initialization acknowledgment");
    const semantic = new Error("fixture broken behavior");
    await assert.rejects(waitFor(() => { throw semantic; }, "fixture state"), error => error === semantic);
});

test("real child exits while CDP reply is pending", async () => {
    const child = spawnIsolatedProcess(process.execPath, ["-e", "process.on('SIGTERM',()=>process.exit(17));setInterval(()=>{},1000)"]);
    await waitForSpawn(child);
    let observed;
    const received = new Promise(resolve => { observed = resolve; });
    try {
        await peer(() => { observed(); }, async client => {
            const result = assert.rejects(client.send("held until child exit"), /child exit/);
            await received; child.kill("SIGKILL"); await result;
        }, { child });
    } finally { await stopIsolatedProcess(child); }
});

test("missing executable, early exit, unsafe profile refusal and concurrent cleanup failure", async () => {
    const missingProfile = require("./browser_harness.js").createBrowserProfile();
    await assert.rejects(launchBrowser({ executable: "/nonexistent/wsprrypi-test-browser", profile: missingProfile }), error => error.cause?.code === "ENOENT");
    assert.equal(fs.existsSync(missingProfile), false, "never-spawned child cannot leave a profile behind");
    await assert.rejects(launchBrowser({ executable: process.execPath, args: ["--not-a-real-node-flag"] }), /Browser workflow or cleanup/);
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), "wsprrypi-preserve-profile-"));
    const marker = path.join(profile, "unique-data"); fs.writeFileSync(marker, "keep");
    try {
        await assert.rejects(launchBrowser({ profile }), /Browser workflow or cleanup/);
        assert.equal(fs.readFileSync(marker, "utf8"), "keep");
        const primary = new Error("primary assertion"), cleanup = new Error("cleanup fault");
        let later = false;
        await assert.rejects(finishFixture({ failure: primary, cleanups: [() => { throw cleanup; }, () => { later = true; }] }), error => error.cause === primary && error.errors.includes(primary) && error.errors.includes(cleanup));
        assert.equal(later, true);
    } finally { fs.rmSync(profile, { recursive: true }); }
});

test("real launch discovery gates: empty/partial file, endpoint without page, then scripts initialized", async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "wsprrypi-discovery-gates-"));
    const profile = path.join(root, "profile"), fake = path.join(root, "browser");
    const source = `#!${process.execPath}
const fs=require('node:fs'),http=require('node:http'),path=require('node:path');
const {WebSocketServer}=require(${JSON.stringify(require.resolve("ws"))});
const profile=process.argv.find(a=>a.startsWith('--user-data-dir=')).split('=').slice(1).join('=');
const id='11111111-1111-1111-1111-111111111111', exists=n=>fs.existsSync(path.join(profile,n));
const server=http.createServer((req,res)=>{
 res.setHeader('Content-Type','application/json');
 if(req.url==='/json/version')res.end(JSON.stringify({Browser:'Controlled fixture/1',webSocketDebuggerUrl:'ws://127.0.0.1:'+server.address().port+'/devtools/browser/'+id}));
 else {if(!exists('page'))fs.writeFileSync(path.join(profile,'no-page-observed'),'');res.end(JSON.stringify(exists('page')?[{id:'owned',type:'page',url:'about:blank',webSocketDebuggerUrl:'ws://127.0.0.1:'+server.address().port+'/devtools/page/owned'}]:[]));}
});
const sockets=new WebSocketServer({server});sockets.on('connection',socket=>socket.on('message',raw=>{
 const m=JSON.parse(raw);let result={};
 if(m.method==='Runtime.evaluate'){fs.writeFileSync(path.join(profile,'scripts-probed'),'');result={result:{type:'boolean',value:exists('scripts')}};}
 socket.send(JSON.stringify({id:m.id,result}));
}));
server.listen(0,'127.0.0.1',()=>{
 fs.writeFileSync(path.join(profile,'DevToolsActivePort'),'');fs.writeFileSync(path.join(profile,'spawned'),'');
 fs.watch(profile,()=>{if(exists('partial')&&fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8')!==String(server.address().port)+'\\n')fs.writeFileSync(path.join(profile,'DevToolsActivePort'),String(server.address().port)+'\\n');if(exists('endpoint')){const content=String(server.address().port)+'\\n/devtools/browser/'+id;if(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8')!==content)fs.writeFileSync(path.join(profile,'DevToolsActivePort'),content);}});
});
`;
    fs.writeFileSync(fake, source, { mode: 0o700 });
    let browser, client, failure, outcome;
    const release = name => fs.writeFileSync(path.join(profile, name), "");
    try {
        let settled = false;
        const launched = launchBrowser({ executable: fake, profile, timeoutMs: 5000 }).then(result => { settled = true; return result; });
        outcome = launched.then(value => ({ value }), error => ({ error }));
        await waitFor(() => fs.existsSync(path.join(profile, "spawned")), "fixture child spawned");
        assert.equal(settled, false, "empty discovery is not ready");
        release("partial");
        await waitFor(() => fs.readFileSync(path.join(profile, "DevToolsActivePort"), "utf8").includes("\n"), "partial discovery observed");
        assert.equal(settled, false, "port alone is not a verified endpoint");
        fs.unlinkSync(path.join(profile, "partial")); release("endpoint");
        await waitFor(() => fs.existsSync(path.join(profile, "no-page-observed")), "endpoint ready without page");
        assert.equal(settled, false, "HTTP-ready endpoint is not usable-page readiness");
        release("page"); const result = await outcome; if (result.error) throw result.error; ({ browser, client } = result.value);
        const initialized = waitFor(() => client.evaluate("fixture scripts ready"), "scripts initialized", 2000);
        await waitFor(() => fs.existsSync(path.join(profile, "scripts-probed")), "script initialization probed");
        release("scripts"); assert.equal(await initialized, true);
        const noPageProfile = path.join(root, "no-page-profile");
        const noPage = launchBrowser({ executable: fake, profile: noPageProfile, timeoutMs: 3000 }).then(value => ({value}), error => ({error}));
        await waitFor(() => fs.existsSync(path.join(noPageProfile, "spawned")), "no-page child spawned");
        fs.writeFileSync(path.join(noPageProfile, "endpoint"), "");
        await waitFor(() => fs.existsSync(path.join(noPageProfile, "no-page-observed")), "known absent usable target");
        const refused = await noPage;
        assert.equal(refused.error?.cause?.code, "HARNESS_TIMEOUT");
        assert.match(refused.error.cause.message, /usable page/);
        assert.equal(fs.existsSync(noPageProfile), false, "no-target launch reaps child before removing profile");
    } catch (error) { failure = error; }
    finally { const result = await outcome; if (result?.value) ({browser,client}=result.value); await finishFixture({ failure: failure || result?.error, browser, client, profile, cleanups: [] }); fs.rmSync(root, { recursive: true, force: true }); }
});

test("detailed browser exceptions retain the failed assertion and stack", async () => {
    await peer((socket, message) => socket.send(JSON.stringify({ id: message.id, result: {
        exceptionDetails: { text: "Uncaught", exception: { description: "Error: unique failed fixture assertion\n at scenario:42" } }
    } })), async client => {
        await assert.rejects(client.evaluate("fixture assertion"), /unique failed fixture assertion[\s\S]*scenario:42/);
    });
});

test("PHP occupied port refuses unrelated successful HTTP responses", async () => {
    const server = http.createServer((req, res) => res.end("unrelated successful server"));
    await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
    const port = server.address().port;
    const child = spawnIsolatedProcess(process.execPath, ["-e", `require('node:http').createServer().listen(${port},'127.0.0.1')`]);
    try {
        await waitForSpawn(child);
        await assert.rejects(require("./browser_harness.js").waitForPhp(child, `http://127.0.0.1:${port}`, "owned PHP fixture", 3000), /PHP fixture startup/);
        assert.equal(await getStatus(`http://127.0.0.1:${port}`), 200, "unrelated server still lives");
    } finally { await stopIsolatedProcess(child); await closeServer(server); }
});

test("HTTP inherits the readiness stage budget instead of an arbitrary short probe limit", async () => {
    let observed, release;
    const received = new Promise(resolve => { observed = resolve; });
    const server = http.createServer((req, res) => { release = () => res.end('{}'); observed(); });
    await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
    let timer;
    try {
        const outcome = waitFor(() => getJson(`http://127.0.0.1:${server.address().port}`), "slow valid readiness", 5000);
        await received;
        // Deliberate latency stimulus after the request arrived, not assumed readiness.
        timer = setTimeout(release, 1100);
        assert.deepEqual(await outcome, {});
    } finally { clearTimeout(timer); await closeServer(server); }
});

test("failed launch plus denied group cleanup retains ownership through the caller finalizer", async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "wsprrypi-failed-launch-"));
    const profile = path.join(root, "profile"), fake = path.join(root, "browser");
    fs.writeFileSync(fake, `#!${process.execPath}
const fs=require('node:fs'),http=require('node:http'),path=require('node:path');
const p=process.argv.find(a=>a.startsWith('--user-data-dir=')).split('=').slice(1).join('=');
fs.writeFileSync(path.join(p,'pid'),String(process.pid));
const server=http.createServer((req,res)=>{fs.writeFileSync(path.join(p,'request-observed'),'');const done=()=>{if(fs.existsSync(path.join(p,'release'))){res.end('{');clearInterval(timer);}};const timer=setInterval(done,10);});
server.listen(0,'127.0.0.1',()=>fs.writeFileSync(path.join(p,'DevToolsActivePort'),String(server.address().port)+'\\n/devtools/browser/11111111-1111-1111-1111-111111111111'));
`, { mode: 0o700 });
    const launch = launchBrowser({ executable: fake, profile, timeoutMs: 5000 }).then(value => ({ value }), error => ({ error }));
    const kill = process.kill;
    let owned, group;
    try {
        await waitFor(() => fs.existsSync(path.join(profile, "request-observed")), "controlled launch request arrived");
        group = Number(fs.readFileSync(path.join(profile, "pid"), "utf8"));
        process.kill = (pid, signal) => {
            if (pid === -group && signal !== 0) { const error = new Error("controlled group permission failure"); error.code = "EPERM"; throw error; }
            return kill(pid, signal);
        };
        fs.writeFileSync(path.join(profile, "release"), "");
        const result = await launch; assert(result.error); owned = result.error.resources;
        assert(fs.existsSync(profile), "failed launch must preserve a live writer's profile");
        let secondary = false;
        await assert.rejects(finishFixture({ failure: result.error, profile, cleanups: [() => { secondary = true; }] }), error => error.errors.some(item => /group permission/.test(item.message)));
        assert.equal(secondary, true);
        assert(fs.existsSync(profile), "caller without a browser assignment must not delete the live profile");
    } finally {
        process.kill = kill;
        const result = await launch; owned ||= result.error?.resources || result.value;
        await finishFixture({ browser: owned?.browser, client: owned?.client, profile });
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test("disposing an enclosing stage cancels its pending CDP operation", async () => {
    let observed;
    const received = new Promise(resolve => { observed = resolve; });
    await peer(() => observed(), async client => {
        let rejection;
        await bounded("owning operation", 3000, async () => {
            rejection = assert.rejects(client.send("held after owner finishes"), /stage disposed/);
            await received;
        });
        await rejection;
        assert.equal(client.pending.size, 0);
    });
});

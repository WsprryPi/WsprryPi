"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnFixtureChild, waitForSpawn, childDiagnostic, spawnIsolatedProcess, stopIsolatedProcess, stopChild } = require("./browser_fixture_process.js");
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

async function main() {
    if (process.platform === "win32") {
        console.log("POSIX fixture process-group test skipped on Windows");
        return;
    }
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "wsprrypi-browser-process-"));
    const profile = path.join(root, "profile"), ticks = path.join(profile, "ticks");
    const writer = `const fs=require('node:fs');let n=0;process.on('SIGTERM',()=>{});setInterval(()=>{fs.mkdirSync(${JSON.stringify(profile)},{recursive:true});fs.writeFileSync(${JSON.stringify(ticks)},String(++n));},10);`;
    const launcher = `const {spawn}=require('node:child_process');const c=spawn(process.execPath,['-e',${JSON.stringify(writer)}],{stdio:'ignore'});c.unref();`;
    let sentinel, leader, crash, failure, stopped = false;
    try {
        sentinel = spawnFixtureChild(process.execPath, ["-e", "setInterval(()=>{},1000)"]);
        leader = spawnIsolatedProcess(process.execPath, ["-e", launcher]);
        await Promise.all([waitForSpawn(sentinel), waitForSpawn(leader)]);
        const deadline = Date.now() + 3000;
        while ((!fs.existsSync(ticks) || leader.exitCode === null) && Date.now() < deadline) await pause(20);
        assert(fs.existsSync(ticks), "owned descendant started writing");
        assert.equal(leader.exitCode, 0, "leader exits while its stubborn descendant remains");
        await stopIsolatedProcess(leader); stopped = true;
        const before = fs.readFileSync(ticks, "utf8"); await pause(100);
        assert.equal(fs.readFileSync(ticks, "utf8"), before, "owned descendant stopped writing");
        assert.equal(sentinel.exitCode, null); assert.equal(sentinel.signalCode, null);
        assert.doesNotThrow(() => process.kill(sentinel.pid, 0), "unrelated sentinel survives");
        fs.rmSync(profile, { recursive: true }); await pause(100);
        assert(!fs.existsSync(profile), "profile cannot be recreated after shutdown");
        const missing = spawnIsolatedProcess(path.join(root, "missing-executable"), []);
        let spawnError;
        try { await waitForSpawn(missing); } catch (error) { spawnError = error; }
        assert.equal(spawnError?.code, "ENOENT", "original spawn failure is captured");
        const kill = process.kill; let signals = 0;
        process.kill = () => { ++signals; throw new Error("Missing child must not signal a group"); };
        try { await assert.rejects(stopIsolatedProcess(missing), error => error === spawnError); }
        finally { process.kill = kill; }
        assert.equal(signals, 0); assert.equal(sentinel.exitCode, null);
        crash = spawnFixtureChild(process.execPath, ["-e", "console.error('expected fixture startup diagnostic');process.exit(42)"]);
        await waitForSpawn(crash);
        const crashDeadline = Date.now() + 3000;
        while ((crash.exitCode === null || !childDiagnostic(crash).includes('expected fixture startup diagnostic')) && Date.now() < crashDeadline) await pause(20);
        assert.equal(crash.exitCode, 42); assert.match(childDiagnostic(crash), /expected fixture startup diagnostic/);
    } catch (error) { failure = error; }
    const errors = failure ? [failure] : [];
    for (const cleanup of [() => stopped ? undefined : stopIsolatedProcess(leader), () => stopChild(sentinel), () => stopChild(crash), () => fs.rmSync(root, { recursive: true, force: true })]) {
        try { await cleanup(); } catch (error) { errors.push(error); }
    }
    if (errors.length) throw new AggregateError(errors, "Fixture process cleanup regression failed");
    assert.notEqual(sentinel.signalCode, null, "other child is retired after the captured spawn failure");
    console.log("Owned process group: exited leader, stubborn writer, sentinel isolation, missing executable and cleanup passed");
}
main().catch(error => { console.error(error); process.exitCode = 1; });

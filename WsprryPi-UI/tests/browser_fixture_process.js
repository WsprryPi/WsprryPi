"use strict";

const { spawn, execFileSync } = require("node:child_process");
const { performance } = require("node:perf_hooks");
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const children = new WeakMap();
const exited = child => child.exitCode !== null || child.signalCode !== null || (children.get(child)?.error && !child.pid);

function spawnFixtureChild(command, args, options = {}) {
    const child = spawn(command, args, { stdio: ["ignore", "ignore", "pipe"], ...options });
    const state = { error: null, spawned: false, stderr: "", isolated: options.detached === true }; children.set(child, state);
    child.stderr?.on("data", data => { state.stderr = (state.stderr + data.toString()).slice(-8192); });
    child.on("error", error => { state.error ||= error; });
    child.once("spawn", () => { state.spawned = true; });
    return child;
}

function failedBeforeSpawn(child) {
    const state = child && children.get(child);
    return !!(state?.error && !state.spawned && !child.pid);
}

function childDiagnostic(child) {
    if (!child) return "Fixture child not started";
    const state = children.get(child);
    return `Fixture child ${child.pid ?? "unspawned"}: exit=${child.exitCode}, signal=${child.signalCode}\n${state?.error?.message || ""}\n${state?.stderr || ""}`;
}

function waitForSpawn(child) {
    const state = children.get(child);
    if (state?.error) return Promise.reject(state.error);
    if (state?.spawned) return Promise.resolve();
    return new Promise((resolve, reject) => {
        const finish = error => {
            clearTimeout(timer); child.off("spawn", ready); child.off("error", failed);
            error ? reject(error) : resolve();
        };
        const ready = () => finish(); const failed = error => finish(error);
        const timer = setTimeout(() => finish(new Error("Fixture child spawn deadline")), 2000);
        child.once("spawn", ready); child.once("error", failed);
    });
}

function waitForExit(child, timeoutMs) {
    if (exited(child)) return Promise.resolve(true);
    return new Promise(resolve => {
        const finish = value => { clearTimeout(timer); child.off("exit", onExit); child.off("close", onExit); resolve(value); };
        const onExit = () => finish(true);
        const timer = setTimeout(() => finish(exited(child)), timeoutMs);
        child.once("exit", onExit); child.once("close", onExit);
    });
}

function spawnIsolatedProcess(command, args) {
    // POSIX detached children own a new session/process group. Never signal
    // a name, a guessed descendant, or the test runner's own process group.
    return spawnFixtureChild(command, args, { detached: process.platform !== "win32" });
}

async function stopChild(child) {
    if (!child) return;
    const error = children.get(child)?.error;
    if (error && !child.pid) throw error;
    if (exited(child)) return;
    child.kill("SIGTERM");
    if (await waitForExit(child, 2000)) return;
    child.kill("SIGKILL");
    if (!await waitForExit(child, 2000)) throw new Error(`Fixture child ${child.pid} did not exit`);
}

function liveGroup(pid) {
    // kill(pid, 0) includes orphan zombies. Inspect actual runnable writers instead.
    const listing = execFileSync("ps", ["-axo", "pid=,pgid=,stat="], { encoding: "utf8", timeout: 1000, maxBuffer: 4 * 1024 * 1024 });
    return listing.split("\n").some(line => {
        const [, group, state] = line.trim().split(/\s+/);
        return Number(group) === pid && state && !state.startsWith("Z");
    });
}

async function stopIsolatedProcess(child) {
    if (!child) return;
    const state = children.get(child);
    if (state?.error && !child.pid) throw state.error;
    if (process.platform === "win32") return stopChild(child);
    if (!state?.isolated || !Number.isInteger(child.pid) || child.pid <= 0) throw new Error("Fixture process has no owned group");
    const signalGroup = signal => {
        try { process.kill(-child.pid, signal); }
        catch (error) { if (error.code !== "ESRCH") throw error; }
    };
    const gone = async budgetMs => {
        const deadline = performance.now() + budgetMs;
        while (liveGroup(child.pid)) {
            if (performance.now() >= deadline) return false;
            await pause(20);
        }
        return true;
    };
    // Inspect and signal the owned group even after the leader has exited.
    if (liveGroup(child.pid)) {
        signalGroup("SIGTERM");
        if (!await gone(2000)) {
            signalGroup("SIGKILL");
            if (!await gone(2000)) throw new Error(`[cleanup forced termination] live group ${child.pid}; ${childDiagnostic(child)}`);
        }
    }
    if (!await waitForExit(child, 2000)) throw new Error(`[cleanup leader exit] ${childDiagnostic(child)}`);
}

module.exports = { failedBeforeSpawn, spawnFixtureChild, waitForSpawn, childDiagnostic, spawnIsolatedProcess, stopIsolatedProcess, stopChild };

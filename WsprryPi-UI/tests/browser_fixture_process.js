"use strict";

const { spawn } = require("node:child_process");
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const children = new WeakMap();
const exited = child => child.exitCode !== null || child.signalCode !== null || (children.get(child)?.error && !child.pid);

function spawnFixtureChild(command, args, options = {}) {
    const child = spawn(command, args, { stdio: ["ignore", "ignore", "pipe"], ...options });
    const state = { error: null, spawned: false, stderr: "" }; children.set(child, state);
    child.stderr?.on("data", data => { state.stderr = (state.stderr + data.toString()).slice(-8192); });
    child.on("error", error => { state.error ||= error; });
    child.once("spawn", () => { state.spawned = true; });
    return child;
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

async function stopIsolatedProcess(child) {
    if (!child) return;
    const error = children.get(child)?.error;
    if (error && !child.pid) throw error;
    if (process.platform === "win32") return stopChild(child);
    if (!Number.isInteger(child.pid) || child.pid <= 0) throw new Error("Fixture process has no owned group");
    // Browser.close acknowledges before shutdown finishes. Give the leader a
    // bounded opportunity to finish before terminating residual descendants.
    await waitForExit(child, 1500);
    const signalGroup = signal => {
        try { process.kill(-child.pid, signal); }
        catch (error) { if (error.code !== "ESRCH") throw error; }
    };
    // Signal the owned group even when its leader has already exited.
    signalGroup("SIGTERM");
    await pause(250);
    signalGroup("SIGKILL");
    if (!await waitForExit(child, 2000)) throw new Error(`Fixture leader ${child.pid} did not exit`);
    // Group probes also include orphan zombies in containers. Do not wait on
    // them indefinitely; verify the profile can be removed after writers stop.
    await pause(50);
}

module.exports = { spawnFixtureChild, waitForSpawn, childDiagnostic, spawnIsolatedProcess, stopIsolatedProcess, stopChild };

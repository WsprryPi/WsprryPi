"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

// Runs in the existing isolated PHP/CDP fixture, with no application transport.
async function reviewPageWarnings(client, origin, evidence) {
    fs.mkdirSync(evidence, {recursive: true});
    const evaluate = async expression => {
        const result = await client.send("Runtime.evaluate", {expression, awaitPromise: true, returnByValue: true});
        assert.ok(!result.exceptionDetails, result.exceptionDetails?.exception?.description);
        return result.result.value;
    };
    const measurements = [];
    for (const view of ["operation", "config", "logs", "spots", "maintenance"]) {
        await client.send("Page.navigate", {url: `${origin}/index.php?page=${view}`});
        let ready = false;
        for (let attempt = 0; attempt < 200; attempt++) {
            ready = await evaluate(`location.search === '?page=${view}' && document.readyState === 'complete' && typeof showUiConsistencyDiagnostic === 'function' && window.PicoFirmwareSafety?.locked === false`);
            if (ready) break;
            await new Promise(resolve => setTimeout(resolve, 50));
        }
        assert.ok(ready, `${view}: production scripts and idle firmware status must load`);
        for (const theme of ["light", "dark"]) for (const width of [1440, 1024, 390, 320]) {
            await client.send("Emulation.setDeviceMetricsOverride", {width, height: 1000, deviceScaleFactor: 1, mobile: width < 768});
            await evaluate(`applyThemePreference(${JSON.stringify(theme)});document.documentElement.style.fontSize='';window.scrollTo(0,0)`);
            await evaluate("new Promise(resolve => setTimeout(resolve, 250))");
            for (const state of ["hidden", "firmware", "identity", "both"]) {
                const result = await evaluate(`(() => {
                    const state = ${JSON.stringify(state)};
                    const firmware = document.getElementById('pico-firmware-safety');
                    const identity = document.getElementById('uiConsistencyDiagnostic');
                    PicoFirmwareSafety.observeFirmware({running:false, maintenance:false});
                    if (state === 'firmware' || state === 'both') PicoFirmwareSafety.begin();
                    identity.hidden = true;
                    const url = new URL(location.href);
                    url.searchParams.delete('ui_refresh');
                    if (state === 'identity' || state === 'both') url.searchParams.set('ui_refresh', 'sha256:'+'a'.repeat(64));
                    history.replaceState(null, '', url);
                    window.WSPRRYPI_INSTALLED_UI_BUILD_ID = 'sha256:'+'b'.repeat(64);
                    const converged = checkUiRefreshConvergence();
                    const card = document.querySelector('.page-card').getBoundingClientRect();
                    const banners = [firmware, identity].map(node => {
                        const box = node.getBoundingClientRect();
                        return {id:node.id, hidden:node.hidden, left:box.left, right:box.right, top:box.top, bottom:box.bottom, height:box.height,
                            overflow:node.scrollWidth>node.clientWidth+1, role:node.getAttribute('role'), live:node.getAttribute('aria-live')};
                    });
                    return {card:{left:card.left,right:card.right,top:card.top}, banners, converged,
                        contentTop:document.querySelector('.page-shell__inner').getBoundingClientRect().top,
                        gap:parseFloat(getComputedStyle(document.querySelector('.page-shell__inner')).rowGap),
                        overflow:document.documentElement.scrollWidth>innerWidth+1,
                        unique:document.querySelectorAll('#pico-firmware-safety').length===1 && document.querySelectorAll('#uiConsistencyDiagnostic').length===1,
                        atomic:identity.getAttribute('aria-atomic'), text:identity.textContent};
                })()`);
                const label = `${view} ${theme} ${width} ${state}`;
                assert.ok(result.unique, `${label}: unique live regions`);
                assert.equal(result.atomic, "true", `${label}: identity announcement remains atomic`);
                assert.equal(result.overflow, false, `${label}: page overflow`);
                assert.equal(result.converged, state !== 'identity' && state !== 'both', `${label}: refresh-convergence trigger`);
                if (state === 'hidden') assert.ok(Math.abs(result.card.top-result.contentTop) <= 1, `${label}: hidden banners leave no shell gap`);
                const visible = result.banners.filter(banner => !banner.hidden);
                assert.equal(visible.length, state === "both" ? 2 : state === "hidden" ? 0 : 1, label);
                for (const banner of result.banners) {
                    assert.equal(banner.role, "status", label);
                    assert.equal(banner.live, "polite", label);
                    if (banner.hidden) assert.equal(banner.height, 0, `${label}: hidden banner space`);
                    else {
                        assert.ok(Math.abs(banner.left-result.card.left) <= 1 && Math.abs(banner.right-result.card.right) <= 1, `${label}: banner/card edge alignment`);
                        assert.equal(banner.overflow, false, `${label}: banner overflow`);
                    }
                }
                for (let i = 0; i < visible.length; i++) {
                    const nextTop = visible[i+1]?.top ?? result.card.top;
                    assert.ok(Math.abs(nextTop-visible[i].bottom-result.gap) <= 1, `${label}: consistent shell gap`);
                }
                if (state === "identity" || state === "both") assert.ok(result.text.includes('a'.repeat(64)) && result.text.includes('b'.repeat(64)), `${label}: complete hashes`);
                measurements.push({view, theme, width, state, ...result});
                if (view === "logs" && state === "both" && (width === 1440 || width === 390)) {
                    const shot = await client.send("Page.captureScreenshot", {format: "png"});
                    fs.writeFileSync(path.join(evidence, `logs-${theme}-${width}.png`), Buffer.from(shot.data, "base64"));
                }
            }
        }
    }
    fs.writeFileSync(path.join(evidence, "measurements.json"), JSON.stringify(measurements, null, 2));
    console.log(`Page warnings: 5 pages x 2 themes x 4 widths x 4 states passed; ${evidence}`);
}

module.exports = {reviewPageWarnings};

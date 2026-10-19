"use strict";

// Bounded whole-console rendering and behavior checks, using the owning Test
// Tone fixture's browser/process cleanup. No application transport is available.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

async function reviewIntegratedUi(client, origin, evidence) {
    fs.mkdirSync(evidence, {recursive: true});
    const evaluate = async expression => {
        let timer;
        const result = await Promise.race([
            client.send("Runtime.evaluate", {expression, awaitPromise:true, returnByValue:true}),
            new Promise((_,reject) => {timer=setTimeout(()=>reject(Error("Integrated review evaluation deadline")),10000);})
        ]).finally(()=>clearTimeout(timer));
        assert.ok(!result.exceptionDetails, result.exceptionDetails?.exception?.description || JSON.stringify(result.exceptionDetails));
        return result.result.value;
    };
    // Bootstrap color transitions last 150ms. Keep the sampling delay finite;
    // animation.finished can stay pending when Chrome pauses an emulated tab.
    const settled = () => evaluate("new Promise(resolve => setTimeout(resolve, 250))");
    const waitFor = async (expression, description) => {
        const deadline = Date.now() + 10000;
        while (Date.now() < deadline) {
            if (await evaluate(expression)) return;
            await new Promise(resolve => setTimeout(resolve, 25));
        }
        throw Error(`Integrated review timed out: ${description}`);
    };
    const capture = async name => {
        const shot = await client.send("Page.captureScreenshot", {format:"png"});
        fs.writeFileSync(path.join(evidence, `${name}.png`), Buffer.from(shot.data, "base64"));
    };
    const measure = () => evaluate(`(() => {
        const pixel = document.createElement('canvas').getContext('2d', {willReadFrequently:true});
        function rgba(color) {pixel.clearRect(0,0,1,1);pixel.fillStyle=color;pixel.fillRect(0,0,1,1);return [...pixel.getImageData(0,0,1,1).data];}
        function background(node) {
            const ancestors=[];for(let n=node;n;n=n.parentElement)ancestors.unshift(n);
            return ancestors.reduce((base,n)=>{const c=rgba(getComputedStyle(n).backgroundColor),a=c[3]/255;return c.slice(0,3).map((v,i)=>v*a+base[i]*(1-a));},[255,255,255]);
        }
        function luminance(c) {return c.map(v=>{v/=255;return v<=.04045?v/12.92:((v+.055)/1.055)**2.4;}).reduce((s,v,i)=>s+v*[.2126,.7152,.0722][i],0);}
        const contrast=(a,b)=>{const x=luminance(a),y=luminance(b);return (Math.max(x,y)+.05)/(Math.min(x,y)+.05);};
        const buttons=[...document.querySelectorAll('.btn,.nav-link')].filter(n=>!n.disabled&&!n.classList.contains('disabled')&&n.getClientRects().length&&n.textContent.trim()&&!n.closest('details:not([open])')&&getComputedStyle(n).visibility!=='hidden').map(n=>({id:n.id||n.textContent.trim(),ratio:contrast((()=>{const c=rgba(getComputedStyle(n).color),b=background(n),a=c[3]/255;return c.slice(0,3).map((v,i)=>v*a+b[i]*(1-a));})(),background(n)),font:getComputedStyle(n).fontSize,color:getComputedStyle(n).color,bg:background(n)}));
        const dangling=[...document.querySelectorAll('[aria-describedby]')].filter(n=>n.getClientRects().length).flatMap(n=>n.getAttribute('aria-describedby').split(/\\s+/).filter(id=>!document.getElementById(id)).map(id=>({control:n.id,id})));
        return {width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth,buttons,dangling};
    })()`);
    await client.send("Page.addScriptToEvaluateOnNewDocument", {source:`
        // Real production log handlers receive synthetic SSE, never journalctl.
        window.EventSource=class {
            static OPEN=1;static CLOSED=2;readyState=1;listeners={};
            constructor(){window.__reviewLogStream=this;}
            addEventListener(type,handler){(this.listeners[type]||=[]).push(handler);}
            close(){this.readyState=2;}
            emit(type,data){const e={type,data:JSON.stringify(data)};this['on'+type]?.(e);for(const f of this.listeners[type]||[])f(e);}
        };
    `});
    await client.send("DOM.enable");await client.send("CSS.enable");
    const measurements=[];
    for (const view of ["operation","config","logs","spots","maintenance"]) {
        await client.send("Page.navigate", {url:`${origin}/index.php?page=${view}${view==='spots'?'&demo=1':''}`});
        await waitFor("document.readyState==='complete' && typeof applyThemePreference==='function' && window.PicoFirmwareSafety?.locked===false", `${view} load and idle host firmware status`);
        await client.send("Page.bringToFront");
        if (view === "logs") {
            await evaluate(`__reviewLogStream.onopen();__reviewLogStream.emit('message',{type:'journal',__REALTIME_TIMESTAMP:'1792173600000000',PRIORITY:'6',SYSLOG_IDENTIFIER:'wsprrypi-long-service-identifier',MESSAGE:'First visible record: '+ 'long-technical-value-'.repeat(14)});`);
            await waitFor("!!document.querySelector('.logs-line')", "production log record");
        }
        if (view === "maintenance") {
            await evaluate("setUpdateCheckDisabled(false);updateWsprryPiVersion();");
            await waitFor("document.getElementById('updateCheckStatus').dataset.state==='failed'", "unavailable version settles panel");
            assert.match(await evaluate("document.getElementById('updateCheckPanelTitle').textContent"), /Unable to check/);
            // Exercise the malformed-success branch separately from HTTP failure.
            await evaluate(`window.__oldGetJson=getJsonWithEndpointFallback;getJsonWithEndpointFallback=()=>$.Deferred().resolve({}).promise();updateWsprryPiVersion();getJsonWithEndpointFallback=window.__oldGetJson;`);
            assert.equal(await evaluate("document.getElementById('updateCheckStatus').dataset.state"), "failed");
            await evaluate("setUpdateCheckDisabled(true);updateWsprryPiVersion();");
            await waitFor("document.getElementById('updateCheckStatus').dataset.state==='disabled'", "disabled preference retained");
        }
        for (const theme of ["light","dark"]) for (const size of [{name:"desktop",width:1440,height:1000},{name:"mobile",width:390,height:844}]) {
            await client.send("Emulation.setDeviceMetricsOverride", {width:size.width,height:size.height,deviceScaleFactor:1,mobile:size.name==='mobile'});
            await evaluate(`document.documentElement.style.fontSize='';applyThemePreference('${theme}');window.scrollTo(0,0);`);
            await settled();
            const result=await measure();measurements.push({view,theme,size:size.name,...result});fs.writeFileSync(path.join(evidence,'measurements.json'),JSON.stringify(measurements,null,2));
            assert.ok(result.scrollWidth<=result.width+1, `${view} ${theme} ${size.name} overflow: ${JSON.stringify(result)}`);
            assert.deepEqual(result.dangling,[],`${view}: descriptions resolve`);
            assert.deepEqual(result.buttons.filter(b=>b.ratio<4.5),[],`${view} ${theme} button contrast: ${JSON.stringify(result.buttons)}`);
            if (view === "logs") {
                const layout=await evaluate(`(()=>{const controls=document.querySelector('.logs-control-row').getBoundingClientRect(),line=document.querySelector('.logs-line').getBoundingClientRect();return {controlsBottom:controls.bottom,firstLine:line.top};})()`);
                assert.ok(layout.firstLine>=layout.controlsBottom,JSON.stringify(layout));
            }
            if (view === "operation" && size.name==='mobile') {
                assert.ok(await evaluate("document.querySelector('.operation-hero__controls').getBoundingClientRect().top < document.querySelector('.operation-hero__reboot').getBoundingClientRect().top"),"mobile runtime controls precede reboot policy");
            }
            await capture(`${view}-${theme}-${size.name}`);
            if (size.name==='desktop') {
                const doc=await client.send('DOM.getDocument',{depth:0});
                for(const selector of ['#operationRetryButton','#repairConfigButton','#test_tone','#updateCheckNowBtn','#updateCheckToggleBtn','#btn-clear','#btn-reconnect']) {
                    const {nodeId}=await client.send('DOM.querySelector',{nodeId:doc.root.nodeId,selector});
                    if(!nodeId)continue;
                    for(const pseudoClasses of [['hover'],['active'],['focus-visible']]) {
                        await client.send('CSS.forcePseudoState',{nodeId,forcedPseudoClasses:pseudoClasses});await settled();
                        const states=await measure();
                        assert.deepEqual(states.buttons.filter(b=>b.ratio<4.5),[],`${view} ${theme} ${selector} ${pseudoClasses} contrast: ${JSON.stringify(states.buttons)}`);
                    }
                    await client.send('CSS.forcePseudoState',{nodeId,forcedPseudoClasses:[]});
                }
            }
            if(size.name==='mobile') {
                await evaluate("document.querySelector('.footer-meta').open=true;window.scrollTo(0,document.body.scrollHeight)");await settled();
                const about=await evaluate("(()=>{const r=document.querySelector('.footer-meta__panel').getBoundingClientRect();return {width:r.width,left:r.left,right:r.right,scroll:document.documentElement.scrollWidth};})()");
                assert.ok(about.width>200&&about.left>=0&&about.right<=size.width+1&&about.scroll<=size.width+1,JSON.stringify(about));
                if(theme==='dark')await capture(`${view}-about-dark-mobile`);
                await evaluate("document.querySelector('.footer-meta').open=false;window.scrollTo(0,0)");
            }
            await evaluate("document.documentElement.style.fontSize='200%';syncFixedChromeOffsets();window.scrollTo(0,0)");await settled();
            assert.ok(await evaluate("document.documentElement.scrollWidth<=innerWidth+1"),`${view} ${theme} ${size.name} 200% text overflow`);
            const shellBounds=await evaluate(`(()=>{
                const nodes=[...document.querySelectorAll('.navbar-signal-status__value,.time-label,.time-value,#mainNav .nav-link')].filter(n=>n.getClientRects().length);
                return nodes.map(n=>{const box=n.getBoundingClientRect(),range=document.createRange();range.selectNodeContents(n);const text=range.getBoundingClientRect();const parent=n.closest('.navbar-signal-status,.card-header,#mainNavbar').getBoundingClientRect();return {id:n.id||n.className,left:text.left,right:text.right,parentLeft:parent.left,parentRight:parent.right,viewport:innerWidth,boxLeft:box.left,boxRight:box.right};});
            })()`);
            assert.deepEqual(shellBounds.filter(b=>b.left<b.parentLeft-1||b.right>b.parentRight+1||b.right>b.viewport+1),[],`${view} ${theme} ${size.name} 200% clipped shell text: ${JSON.stringify(shellBounds)}`);
            if(view==='maintenance' && size.name==='mobile') {
                await evaluate("document.querySelector('.navbar-toggler').focus()");
                for(const type of ['keyDown','keyUp']) await client.send('Input.dispatchKeyEvent',{type,key:' ',code:'Space',windowsVirtualKeyCode:32});
                await waitFor("document.getElementById('mainNav').classList.contains('show') && !document.getElementById('mainNav').classList.contains('collapsing')",'enlarged menu opens');
                await evaluate("document.getElementById('themeToggle').scrollIntoView({block:'center',behavior:'instant'});document.getElementById('themeToggle').focus()");await settled();
                const menu=await evaluate("(()=>{const n=document.getElementById('themeToggle'),r=n.getBoundingClientRect();return {top:r.top,bottom:r.bottom,height:innerHeight,focused:document.activeElement===n};})()");
                assert.ok(menu.top>=0&&menu.bottom<=menu.height&&menu.focused,`200% mobile navigation must reach theme control: ${JSON.stringify(menu)}`);
                await capture(`maintenance-menu-${theme}-mobile-200-percent`);
                await evaluate("document.querySelector('.navbar-toggler').scrollIntoView({block:'center',behavior:'instant'});document.querySelector('.navbar-toggler').focus()");
                for(const type of ['keyDown','keyUp']) await client.send('Input.dispatchKeyEvent',{type,key:' ',code:'Space',windowsVirtualKeyCode:32});
                await waitFor("!document.getElementById('mainNav').classList.contains('show') && !document.getElementById('mainNav').classList.contains('collapsing')",'enlarged menu closes');
                assert.ok(await evaluate("document.activeElement===document.querySelector('.navbar-toggler') && getComputedStyle(document.activeElement).outlineStyle==='solid'"),'menu closes with visible keyboard focus');
                await evaluate("window.scrollTo(0,0)");
            }
            if(view==='logs') {
                const bounds=await evaluate(`(()=>{const p=document.getElementById('logs-overlay-controls').getBoundingClientRect();return [...document.querySelectorAll('#logs-overlay-controls .btn')].map(n=>{const r=n.getBoundingClientRect();return {id:n.id,left:r.left,right:r.right,containerLeft:p.left,containerRight:p.right,scroll:n.scrollWidth,client:n.clientWidth};});})()`);
                assert.deepEqual(bounds.filter(b=>b.left<b.containerLeft||b.right>b.containerRight+1||b.scroll>b.client+1),[],JSON.stringify(bounds));
                const rects=await evaluate("[...document.querySelectorAll('#logs-overlay-controls .btn')].map(n=>{const r=n.getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom};})");
                assert.ok(rects[0].right<=rects[1].left||rects[0].bottom<=rects[1].top,'log controls must not overlap');
            }
            if (size.name==='mobile' && theme==='dark') await capture(`${view}-dark-mobile-200-percent`);
            if(view==='logs' && size.name==='mobile' && theme==='dark') {
                await evaluate("document.querySelector('.logs-control-row').scrollIntoView({block:'center',behavior:'instant'})");await settled();
                await capture('logs-dark-mobile-200-controls');
            }
            await evaluate("document.documentElement.style.fontSize=''");
        }
        if (view === "config") {
            for (const mode of ["WSPR","QRSS","FSKCW","DFCW"]) {
                await evaluate(`configAutosaveSuspended=true;applyConfigModeSelection('${mode}');`);await settled();
                assert.equal(await evaluate("selectedConfigMode()"),mode);
                await capture(`config-${mode}-dark-mobile`);
            }
        }
    }
    // Open and dismiss Test Tone entirely by keyboard; no Start is invoked.
    await evaluate("document.getElementById('test_tone').focus()");
    for(const type of ['keyDown','keyUp']) await client.send('Input.dispatchKeyEvent',{type,key:' ',code:'Space',windowsVirtualKeyCode:32});
    await waitFor("document.getElementById('testToneModal').classList.contains('show')",'keyboard opens Test Tone');
    await waitFor("document.getElementById('testToneModal').contains(document.activeElement)",'modal receives keyboard focus');
    for(const type of ['keyDown','keyUp']) await client.send('Input.dispatchKeyEvent',{type,key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
    await waitFor("!document.getElementById('testToneModal').classList.contains('show') && document.activeElement.id==='test_tone'",'Escape restores trigger focus');
    assert.equal(await evaluate("getComputedStyle(document.activeElement).outlineStyle"),'solid','keyboard focus outline is visible');
    await capture('maintenance-keyboard-return-dark-mobile');
    fs.writeFileSync(path.join(evidence,"measurements.json"),JSON.stringify(measurements,null,2));
    console.log(`Integrated console: 5 pages, 4 modes, 2 themes x 2 viewports, 200% text, button contrast, descriptions and first-record clearance passed; ${evidence}`);
}
module.exports={reviewIntegratedUi};

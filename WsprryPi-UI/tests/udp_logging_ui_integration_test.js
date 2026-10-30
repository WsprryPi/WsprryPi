// SPDX-License-Identifier: MIT
// Render the shipped shared editor against incumbent Pico/Pi styles. Transport
// and storage are qualified separately by the actual production pair fixtures.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),os=require('node:os');
const WebSocket=require('ws');
const {spawnIsolatedProcess,stopIsolatedProcess,waitForSpawn}=require('./browser_fixture_process.js');
const ui=path.resolve(__dirname,'..'),pico=path.resolve(process.argv[2]),output=path.resolve(process.argv[3]||'/tmp/wsprry-udp-ui');
fs.mkdirSync(output,{recursive:true});
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(fn){for(let i=0;i<200;i++){try{const v=await fn();if(v)return v;}catch{}await wait(50);}throw Error('Browser fixture deadline');}
class Cdp {
 constructor(url){this.ws=new WebSocket(url);this.id=0;this.pending=new Map();this.ws.on('message',raw=>{const m=JSON.parse(raw);if(m.id){const p=this.pending.get(m.id);if(p){this.pending.delete(m.id);m.error?p.reject(Error(m.error.message)):p.resolve(m.result);}}});}
 async open(){if(this.ws.readyState!==1)await new Promise((resolve,reject)=>{this.ws.once('open',resolve);this.ws.once('error',reject);});}
 send(method,params={}){return new Promise((resolve,reject)=>{const id=++this.id;this.pending.set(id,{resolve,reject});this.ws.send(JSON.stringify({id,method,params}));});}
 async eval(expression){const r=await this.send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;}
}
const counters=Object.fromEntries(['generated','filtered','redacted_rejected','queued','overflow','expired','reconfigured_drops','truncated','attempted','locally_submitted','locally_failed'].map(k=>[k,'0']));
const fixtures=`window.fixtureHost=new URL(location).searchParams.get('surface')==='pi';
const host=window.fixtureHost;window.context=1;window.mode='ok';window.writes=0;
const common={enabled:false,destination_ipv4:null,destination_port:5514,maximum_severity:6,event_codes:[host?'PI_LOG':'UDP_TEST',...Array.from({length:15},(_,i)=>String.fromCharCode(65+i).repeat(32))],rate:host?16:4,burst:host?32:8};
const target=host?{scope:'host',collector_id:'a'.repeat(32)}:{scope:'member',device_id:'a'.repeat(32),boot_id:'b'.repeat(32)};
const caps={...(host?{}:{contract:'Wsprry-Control/1'}),schema:host?'udp-logging-capabilities/1':'udp-logging-capabilities',target,resource:host?'/api/control/v1/host/udp-logging':'/api/control/v1/udp-logging',settings_schema:host?'host-udp-logging/1':'udp-logging',wire_schema:'wsprry-udp/1',limits:{payload_bytes:480,queue_records:host?256:16,maximum_sources:host?8:0,maximum_peers:host?32:0,maximum_rate:host?64:8,maximum_burst:host?128:16,address_family:'ipv4'}};
const sender={state:'disabled',receipt:'unknown',queued:0,counters:${JSON.stringify(counters)}};
const sourceCandidates=Array.from({length:8},(_,i)=>({catalog_id:(i+1).toString(16).padStart(32,'0'),device_id:(i+17).toString(16).padStart(32,'0'),name:'Pico '+'n'.repeat(59)}));
window.saved=host?{sender:{...common,interface:null,native_enabled:false,usb_sources:sourceCandidates.map(x=>({profile_id:x.catalog_id,device_id:x.device_id}))},collector:{enabled:false,bind_ipv4:null,interface:null,port:5514,peers:Array.from({length:32},(_,i)=>({slot:'peer_'+String(i).padStart(27,'0'),ipv4:'192.0.2.'+(i+1)}))}}:common;
const active=host?{applied_revision:null,sender,collector:{state:'disabled',bind_ipv4:null,port:null,counters:Object.fromEntries(['received','oversize','disallowed','malformed','duplicate','conflict','limited','overflow','journal_failed','journal_submitted','discontinuity'].map(k=>[k,'0'])),peers:[]}}:{...sender,applied_revision:null};
window.editor=WsprryUdp.mount(document.getElementById('editor'),{host,context:()=>window.context,inputClass:host?'form-control':'',buttonClass:host?'btn btn-outline-secondary':'',checkClass:host?'form-check':'check',usbSources:async()=>({candidates:sourceCandidates}),exchange:async(p,method='GET',body)=>{if(window.mode==='unsupported')throw Error('UDP logging unsupported');if(p.endsWith('/test/capabilities'))return {value:{...(host?{}:{contract:'Wsprry-Control/1'}),schema:host?'udp-logging-test-capabilities/1':'udp-logging-test-capabilities',target,resource:caps.resource+'/test',request_schema:host?'udp-logging-test/1':'udp-logging-test',event_code:host?'PI_LOG':'UDP_TEST',maximum_rate:2}};if(method==='POST'){window.tests=(window.tests||0)+1;return {value:{...(host?{}:{contract:'Wsprry-Control/1'}),schema:host?'udp-logging-test/1':'udp-logging-test',target,accepted:true,receipt:'unknown'}};}if(method==='PUT'){window.writes++;if(window.mode==='lost')throw Error('Lost reply');window.saved=body.settings;}return {value:p.endsWith('capabilities')?caps:{...(host?{}:{contract:'Wsprry-Control/1'}),schema:caps.settings_schema,target,saved:window.saved,active},revision:'revision-1'};}});`;
(async()=>{
 let chrome,cdp;const profile=fs.mkdtempSync(path.join(os.tmpdir(),'wsprry-udp-chrome-'));
 const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost');let file;
  if(url.pathname==='/module.js')file=path.join(ui,'data/udp-logging.js');
  if(url.pathname==='/pico.css')file=path.join(pico,'src/network/web/style.css');
  if(url.pathname==='/site.css')file=path.join(ui,'data/site.css');
  if(url.pathname==='/bootstrap.css')file=path.join(ui,'data/vendor/css/bootswatch-zephyr-5.3.8.min.css');
  if(url.pathname.startsWith('/vendor/')){const asset=path.resolve(ui,'data','.'+url.pathname);if(asset.startsWith(path.join(ui,'data/vendor')+path.sep)&&fs.existsSync(asset)&&fs.statSync(asset).isFile())file=asset;}
  if(file){res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript; charset=utf-8':file.endsWith('.ttf')?'font/ttf':file.endsWith('.woff2')?'font/woff2':'text/css; charset=utf-8');res.end(fs.readFileSync(file));return;}
  const pi=url.searchParams.get('surface')==='pi';res.setHeader('Content-Type','text/html; charset=utf-8');
  res.end(`<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>UDP logging controls fixture</title>${pi?'<link rel="stylesheet" href="/vendor/fonts/google/fonts.css">':''}<link rel="stylesheet" href="${pi?'/bootstrap.css':'/pico.css'}">${pi?'<link rel="stylesheet" href="/site.css">':''}</head><body><main class="${pi?'container py-3':''}"><h1>${pi?'Logs':'Connection and setup Wi-Fi'}</h1><details open><summary>${pi?'Logging on this Pi':'UDP logging'}</summary><div id="editor"></div></details></main><script src="/module.js"></script><script>${fixtures}</script></body></html>`);
 });
 try{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const port=server.address().port;
  chrome=spawnIsolatedProcess(process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),['--headless','--no-sandbox','--disable-gpu','--no-first-run','--disable-background-networking','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank']);await waitForSpawn(chrome);
  const endpoint=await until(()=>{const f=path.join(profile,'DevToolsActivePort');return fs.existsSync(f)&&fs.readFileSync(f,'utf8').split('\n')[0];});
  const tabs=await until(async()=>{const r=await fetch(`http://127.0.0.1:${endpoint}/json/list`);return (await r.json()).find(t=>t.type==='page');});cdp=new Cdp(tabs.webSocketDebuggerUrl);await cdp.open();
  for(const surface of ['pico','pi']){
   await cdp.send('Page.navigate',{url:`http://127.0.0.1:${port}/?surface=${surface}`});await until(()=>cdp.eval(`window.fixtureHost===${surface==='pi'} && typeof window.editor?.read==='function' && !!document.querySelector('fieldset')`));
   assert.equal(await cdp.eval('document.querySelector("fieldset").disabled'),true);
   await cdp.eval('editor.read()');assert.equal(await cdp.eval('document.querySelector("fieldset").disabled'),false);
   assert.equal(await cdp.eval('document.querySelector("[name=event_codes]").value.split(",").length'),16);
   if(surface==='pi'){
    assert.equal(await cdp.eval('document.querySelectorAll("input[data-device]").length'),8);
    assert.equal(await cdp.eval('document.querySelector("textarea").value.split("\\n").length'),32);
   }
   await cdp.eval('document.querySelector("summary").focus()');assert.equal(await cdp.eval('document.activeElement.tagName'),'SUMMARY');
   const key=async(name,code)=>{await cdp.send('Input.dispatchKeyEvent',{type:'keyDown',key:name,code:name,windowsVirtualKeyCode:code,...(name==='Enter'?{text:'\r',unmodifiedText:'\r'}:{})});await cdp.send('Input.dispatchKeyEvent',{type:'keyUp',key:name,code:name,windowsVirtualKeyCode:code});};
   await key('Enter',13);assert.equal(await cdp.eval('document.querySelector("details").open'),false);
   await key('Enter',13);assert.equal(await cdp.eval('document.querySelector("details").open'),true);
   await key('Tab',9);assert.match(await cdp.eval('document.activeElement.textContent'),/Read UDP logging/);
   await key('Tab',9);assert.equal(await cdp.eval('document.activeElement.name'),'enabled');
   assert.match(await cdp.eval('document.querySelector("[role=status]").textContent'),/receipt is unknown/);
   await cdp.eval('document.querySelector("[name=destination_ipv4]").value="127.0.0.1";document.querySelector("[name=enabled]").checked=true;document.querySelector("form").requestSubmit()');
   assert.equal(await cdp.eval('window.writes'),0);
   await cdp.eval('document.querySelector("[name=destination_ipv4]").value="192.0.2.30";if(fixtureHost)document.querySelector("[name=interface]").value="eth0";document.querySelector("form").dispatchEvent(new Event("input",{bubbles:true}));window.mode="lost";document.querySelector("form").requestSubmit()');
   await until(()=>cdp.eval('document.querySelector("[role=status]").textContent.includes("Save unconfirmed")'));
   assert.equal(await cdp.eval('document.querySelector("button[type=submit]").disabled'),true);
   await cdp.eval('window.mode="ok";editor.read()');assert.equal(await cdp.eval('document.querySelector("[name=destination_ipv4]").value'),'192.0.2.30');
   assert.equal(await cdp.eval('document.querySelector("button[type=submit]").disabled'),true);
   assert.match(await cdp.eval('document.querySelector(".udp-logging-controls ul").textContent'),/Destination: not set/);
   await cdp.send('Emulation.setDeviceMetricsOverride',{width:360,height:1000,deviceScaleFactor:1,mobile:true});
   assert.equal(await cdp.eval('document.documentElement.scrollWidth<=innerWidth && !document.querySelector(".udp-logging-controls details").hidden'),true,'Preserved-draft comparison must remain readable on mobile');
   const reviewShot=await cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(path.join(output,`${surface}-review-360.png`),Buffer.from(reviewShot.data,'base64'));
   const writesBeforeReview=await cdp.eval('window.writes');await cdp.eval('document.querySelector("form").requestSubmit()');assert.equal(await cdp.eval('window.writes'),writesBeforeReview);
   await cdp.eval('[...document.querySelectorAll("button")].find(b=>b.textContent==="Confirm review").click()');assert.equal(await cdp.eval('document.querySelector("button[type=submit]").disabled'),false);
   await cdp.eval('editor.invalidate();window.context++;');assert.equal(await cdp.eval('document.querySelector("fieldset").disabled'),true);
   await cdp.eval('editor.read()');
   await cdp.send('Page.navigate',{url:`http://127.0.0.1:${port}/?surface=${surface}`});await until(()=>cdp.eval(`window.fixtureHost===${surface==='pi'} && typeof window.editor?.read==='function' && !!document.querySelector('fieldset')`));
   await cdp.eval('if(fixtureHost){window.saved.sender.enabled=true;window.saved.sender.native_enabled=true;window.saved.sender.destination_ipv4="192.0.2.30";window.saved.sender.interface="eth0";}else {window.saved.enabled=true;window.saved.destination_ipv4="192.0.2.30";}editor.invalidate();editor.read()');
   assert.equal(await cdp.eval('[...document.querySelectorAll("button")].find(b=>b.textContent.includes("Test saved UDP export")).disabled'),false);
   await cdp.eval('[...document.querySelectorAll("button")].find(b=>b.textContent.includes("Test saved UDP export")).click()');
   await until(()=>cdp.eval('window.tests===1 && ![...document.querySelectorAll("button")].find(b=>b.textContent.includes("Read UDP")).disabled'));
   await cdp.eval('document.querySelector("form").dispatchEvent(new Event("input",{bubbles:true}))');
   for(const width of [360,390,1280])for(const theme of ['light','dark']){
    await cdp.send('Emulation.setDeviceMetricsOverride',{width,height:1000,deviceScaleFactor:1,mobile:width<500});
    await cdp.send('Emulation.setEmulatedMedia',{features:[{name:'prefers-color-scheme',value:theme}]});
    await cdp.eval(`document.documentElement.setAttribute('data-bs-theme','${theme}');document.querySelectorAll('details').forEach(x=>x.open=true)`);
    assert.equal(await cdp.eval('document.documentElement.scrollWidth<=innerWidth'),true,`${surface} ${width} ${theme} overflow`);
    assert.equal(await cdp.eval('[...document.querySelectorAll("input")].every(x=>x.labels.length>0)'),true);
    if(surface==='pi')await until(()=>cdp.eval(`(()=>{
     const b=[...document.querySelectorAll('button')].find(x=>x.textContent.includes('Test saved UDP export')),s=getComputedStyle(b);
     const rgb=x=>x.match(/[\\d.]+/g).map(Number),bg=rgb(s.backgroundColor),fg=rgb(s.color);
     const lum=c=>c.slice(0,3).map(x=>x/255).map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4).reduce((a,x,i)=>a+x*[.2126,.7152,.0722][i],0);
     const text=fg.slice(0,3).map((x,i)=>x*(fg[3]??1)+bg[i]*(1-(fg[3]??1))),a=lum(text),z=lum(bg);
     return b.disabled&&(Math.max(a,z)+.05)/(Math.min(a,z)+.05)>=3;
    })()`));
    const screenshot=await cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(path.join(output,`${surface}-${width}-${theme}.png`),Buffer.from(screenshot.data,'base64'));
   }
   await cdp.send('Emulation.setDeviceMetricsOverride',{width:1280,height:1000,deviceScaleFactor:1,mobile:false});
   await cdp.eval('document.documentElement.style.zoom="2"');
   assert.equal(await cdp.eval('document.documentElement.scrollWidth<=document.documentElement.clientWidth'),true,`${surface} 200% CSS zoom reflow`);
   await cdp.eval('document.documentElement.style.zoom="1"');
  }
  console.log('Rendered UDP controls: explicit read, invalid refusal, uncertain save/readback, preserved draft, selection invalidation, labels and 360/390/1280 light/dark layouts passed');
 }finally{cdp?.ws.close();await stopIsolatedProcess(chrome);await new Promise(resolve=>server.close(resolve));fs.rmSync(profile,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});

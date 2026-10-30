// SPDX-License-Identifier: MIT
// Real production Logs DOM/CSS/JS, synthetic log API and EventSource transport.
"use strict";
const assert=require('node:assert/strict'), fs=require('node:fs'), path=require('node:path'), http=require('node:http');
const {spawnIsolatedProcess,stopIsolatedProcess,waitForSpawn}=require('./browser_fixture_process.js');
const WebSocket=require('ws');
const root=path.resolve(__dirname,'../data');
const html=fs.readFileSync(process.env.LOGGING_RENDERED_HTML,'utf8');
const evidence=process.env.LOGGING_EVIDENCE_DIR || '/tmp/udp-logging-logs-browser';
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(check){for(let n=0;n<100;n++){const value=await check();if(value)return value;await wait(50);}throw Error('Browser fixture timed out');}
class CdpClient {
    constructor(url) {
        this.socket = new WebSocket(url);
        this.nextId = 1;
        this.pending = new Map();
        this.socket.on("message", (raw) => {
            const message = JSON.parse(raw);
            if (!message.id || !this.pending.has(message.id)) return;
            const { resolve, reject } = this.pending.get(message.id);
            this.pending.delete(message.id);
            if (message.error) reject(new Error(message.error.message));
            else resolve(message.result);
        });
    }

    async open() {
        if (this.socket.readyState === WebSocket.OPEN) return;
        await new Promise((resolve, reject) => {
            this.socket.once("open", resolve);
            this.socket.once("error", reject);
        });
    }

    send(method, params = {}) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.socket.send(JSON.stringify({ id, method, params }));
        });
    }

    close() {
        this.socket.close();
    }
}


const a='a'.repeat(32), b='b'.repeat(32), host='c'.repeat(32), urls=[];
let failHostRead=false,failHostSave=false,hostWrites=0;
const candidates=[{catalog_id:'1'.repeat(32),device_id:a,name:'Pico A (fixture)'},{catalog_id:'2'.repeat(32),device_id:b,name:'bench-b.local'}];
let policy={schema:'host-usb-logging/1',collector_id:host,sources:[],candidates,journal_available:true,status:{[a]:{state:'streaming',rejected:'0'},[b]:{state:'disconnected',rejected:'2'}}};
const rows=[{source:'local',origin_kind:'pi',MESSAGE:'Pi service ready (synthetic fixture)',__CURSOR:'pi-1'},
 {source:`usb:${a}`,origin_kind:'pico',origin_id:a,origin_name:'pico-a.local',origin_boot:'4'.repeat(32),origin_sequence:'9007199254740993',origin_utc_ns:null,MESSAGE:'Pico A USB logging active (synthetic fixture)',__CURSOR:'a-1'},
 {source:`usb:${b}`,origin_kind:'pico',origin_id:b,origin_name:'pico-b.local',origin_boot:'5'.repeat(32),origin_sequence:'2',origin_utc_ns:'1793098800000000000',MESSAGE:'Pico B <script> stays plain text (synthetic fixture)',__CURSOR:'b-1'}];
const counts=Object.fromEntries(['generated','filtered','redacted_rejected','queued','overflow','expired','reconfigured_drops','truncated','attempted','locally_submitted','locally_failed'].map(k=>[k,'0']));
const cap={schema:'udp-logging-capabilities/1',target:{scope:'host',collector_id:host},resource:'/api/v1/host/udp-logging',settings_schema:'host-udp-logging/1',wire_schema:'wsprry-udp/1',limits:{payload_bytes:480,queue_records:256,maximum_sources:8,maximum_peers:32,maximum_rate:64,maximum_burst:128,address_family:'ipv4'}};
const hostPolicy={schema:'host-udp-logging/1',target:cap.target,saved:{sender:{enabled:false,destination_ipv4:null,destination_port:5514,maximum_severity:6,event_codes:[],rate:16,burst:32,interface:null,native_enabled:false,usb_sources:[]},collector:{enabled:false,bind_ipv4:null,interface:null,port:5514,peers:[{slot:'member',ipv4:'192.0.2.10'}]}},active:{applied_revision:null,sender:{state:'disabled',receipt:'unknown',queued:0,counters:counts},collector:{state:'disabled',bind_ipv4:null,port:null,peers:[],counters:Object.fromEntries(['received','oversize','disallowed','malformed','duplicate','conflict','limited','overflow','journal_failed','journal_submitted','discontinuity'].map(k=>[k,'0']))}}};
for(const ingress of ['direct','relay'])rows.push({source:`udp:member:pico:${a}:${ingress}`,ingress:'udp',peer_slot:'member',peer_ipv4:'192.0.2.10',origin_kind:'pico',origin_id:a,origin_name:'Repeated <name>',origin_boot:'4'.repeat(32),origin_sequence:'1',forwarders:ingress==='relay'?'[{"id":"'+host+'","receipt_us":"1","ingress":"usb"}]':'[]',MESSAGE:'UDP logging test event',__CURSOR:'udp-'+ingress});
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://fixture');
 if(url.pathname==='/api/v1/host/udp-logging'&&req.method==='PUT'){
  hostWrites++;res.writeHead(failHostSave?503:500,{'Content-Type':'application/json'});res.end('{}');return;
 }
 if(url.pathname==='/api/v1/host/udp-logging'&&failHostRead){res.writeHead(503,{'Content-Type':'application/json'});res.end('{}');return;}
 if(url.pathname==='/api/v1/host/udp-logging/installation'){
  res.setHeader('Content-Type','application/json');res.end(JSON.stringify({schema:'host-udp-installation/1',target:cap.target,worker_state:'installed',native_journal_state:'available',policy_state:'valid',service_state:'inactive'}));return;
 }
 if(url.pathname==='/api/v1/udp-logging/capabilities'||url.pathname==='/api/v1/host/udp-logging'){
  res.setHeader('Content-Type','application/json');res.setHeader('ETag','"udp1"');
  setTimeout(()=>res.end(JSON.stringify(url.pathname.endsWith('/capabilities')?cap:hostPolicy)),200);return;
 }

 if(url.pathname==='/api/v1/host/logging'){
  if(req.method==='PUT'){let body='';req.on('data',data=>body+=data);req.on('end',()=>{const next=JSON.parse(body);policy={...policy,sources:next.sources};res.setHeader('ETag','"logging2"');res.end(JSON.stringify(policy));});return;}
  res.setHeader('Content-Type','application/json');res.setHeader('ETag','"logging1"');
  setTimeout(()=>res.end(JSON.stringify(policy)),150);return;
 }
 if(url.pathname==='/log_stream.php'){
  urls.push(req.url);res.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache'});
  const selected=url.searchParams.get('source')||'all';
  const cursor=url.searchParams.get('cursor'), after=rows.findIndex(row=>row.__CURSOR===cursor);
  const severity=Number(url.searchParams.get('priority_max')||7);
  for(const row of rows.filter((row,index)=>(after<0||index>after)&&(selected==='all'||row.source===selected)&&Number(row.PRIORITY||6)<=severity)){
   const data={type:'journal',playback:false,__REALTIME_TIMESTAMP:'1793098800123456',PRIORITY:'6',_SYSTEMD_UNIT:'wsprrypi.service',...row};
   res.write(`id: ${row.__CURSOR}\nevent: journal\ndata: ${JSON.stringify(data)}\n\n`);
  }
  res.write('event: internal\ndata: {"type":"internal","MESSAGE":"[HEARTBEAT]","PRIORITY":"7","__CURSOR":null}\n\n');
  const timer=setInterval(()=>res.write(': heartbeat\n\n'),1000);req.on('close',()=>clearInterval(timer));return;
 }
 if(url.pathname==='/index.php'||url.pathname==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
 if(url.pathname==='/api/v1/status'){res.setHeader('Content-Type','application/json');res.end('{"host":{"selected":false,"firmware_maintenance":false}}');return;}
 const file=path.resolve(root,'.'+url.pathname);
 if(file.startsWith(root+path.sep)&&fs.existsSync(file)&&fs.statSync(file).isFile()&&!file.endsWith('.php')){
  const ext=path.extname(file);res.setHeader('Content-Type',({'.css':'text/css','.js':'application/javascript','.woff2':'font/woff2','.png':'image/png','.svg':'image/svg+xml'})[ext]||'application/octet-stream');res.end(fs.readFileSync(file));return;
 }
 res.writeHead(503,{'Content-Type':'application/json'});res.end('{"error":{"code":"fixture_only"}}');
});
async function main(){
 fs.mkdirSync(evidence,{recursive:true});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const debugServer=http.createServer();await new Promise(resolve=>debugServer.listen(0,'127.0.0.1',resolve));const debugPort=debugServer.address().port;await new Promise(resolve=>debugServer.close(resolve));
 let chrome,client;
 try{
  chrome=spawnIsolatedProcess(process.env.CHROME_BIN||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',['--headless','--no-sandbox','--disable-gpu',`--remote-debugging-port=${debugPort}`,`--user-data-dir=/tmp/udp-logging-logs-chrome-${process.pid}`,'about:blank']);await waitForSpawn(chrome);
  const tab=await until(async()=>{try{return(await(await fetch(`http://127.0.0.1:${debugPort}/json`)).json()).find(row=>row.type==='page');}catch{return null;}});
  client=new CdpClient(tab.webSocketDebuggerUrl);await client.open();await client.send('Page.enable');
  const evaluate=async expression=>{const value=await client.send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(value.exceptionDetails)throw Error(value.exceptionDetails.text+': '+value.exceptionDetails.exception?.description);return value.result.value;};
  await client.send('Page.addScriptToEvaluateOnNewDocument',{source:`window.WebSocket=class{static OPEN=1;static CONNECTING=0;readyState=0;addEventListener(){}close(){}};localStorage.setItem('wsprrypi.updateCheckDisabled','true');window.WSPRRYPI_ENABLE_LOG_TEST_HOOKS=true;`});
  await client.send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/index.php?page=logs`});
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===5 && document.querySelectorAll('#logs-source option[value^="udp:"]').length===2`));
  const editor='#host-udp-logging-controls';
  await until(()=>evaluate(`!document.querySelector('${editor} fieldset').disabled`));
  assert.equal(await evaluate(`document.querySelector('${editor} [name=destination_port]').value`),'5514','Settings must populate on page load before opening the dialog');
  assert.equal(await evaluate(`document.querySelector('${editor} > button').hidden`),true,'Successful automatic loading removes the read action');
  assert.deepEqual(await evaluate(`[...document.querySelectorAll('#logs-overlay-controls button')].map(b=>b.textContent.trim())`),['Clear','Refresh stream','Pause view','Log options','Local logging']);
  const openModal=async id=>{
   await evaluate(`document.querySelector('[data-bs-target="#${id}"]').focus();document.querySelector('[data-bs-target="#${id}"]').click()`);
   await until(()=>evaluate(`document.getElementById('${id}').classList.contains('show') && document.getElementById('${id}').contains(document.activeElement)`));
  };
  const closeModal=async id=>{
   await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
   await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
   await until(()=>evaluate(`!document.querySelector('.modal.show') && document.activeElement.dataset.bsTarget==='#${id}' && !document.querySelector('.modal-backdrop')`));
  };
  await openModal('host-logging-modal');
  await evaluate(`document.querySelector('${editor} [name=destination_port]').value='5515';document.querySelector('${editor} form').dispatchEvent(new Event('input',{bubbles:true}))`);
  await closeModal('host-logging-modal');await openModal('host-logging-modal');
  assert.equal(await evaluate(`document.querySelector('${editor} [name=destination_port]').value`),'5515','Closing the modal must preserve drafts');
  failHostSave=true;
  await evaluate(`document.querySelector('${editor} form').requestSubmit()`);
  await until(()=>evaluate(`document.querySelector('${editor} > p:last-child').textContent.includes('Save unconfirmed')`));
  assert.equal(await evaluate(`document.querySelector('${editor} > button').hidden`),false,'An uncertain save needs an explicit recovery action');
  await evaluate(`document.querySelector('${editor} > button').click()`);
  await until(()=>evaluate(`!document.querySelector('${editor} fieldset').disabled`));
  assert.equal(await evaluate(`document.querySelector('${editor} [name=destination_port]').value`),'5515');
  assert.equal(await evaluate(`document.querySelector('${editor} button[type=submit]').disabled`),true,'Preserved draft still requires review after uncertain save');
  await closeModal('host-logging-modal');
  failHostRead=true;
  await client.send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/index.php?page=logs`});
  await until(()=>evaluate(`document.querySelector('${editor} > button')?.hidden===false`));
  assert.equal(await evaluate(`document.querySelector('${editor} fieldset').disabled`),true,'Failed automatic reads must keep editing disabled');
  failHostRead=false;
  await openModal('host-logging-modal');
  await evaluate(`document.querySelector('${editor} > button').click()`);
  await until(()=>evaluate(`!document.querySelector('${editor} fieldset').disabled`));
  assert.equal(hostWrites,1,'Loading, opening, closing and recovery must never write settings');
  await closeModal('host-logging-modal');
  for(const width of [390,1280])for(const theme of ['light','dark']){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:960,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='${theme}'`);await wait(200);
   assert(await evaluate(`document.documentElement.scrollWidth<=innerWidth`),'Toolbar must reflow without page overflow');
   assert(await evaluate(`['height','fontSize','fontWeight','borderRadius','paddingTop','paddingLeft'].every(k=>getComputedStyle(document.getElementById('btn-clear'))[k]===getComputedStyle(document.getElementById('sse-status-badge'))[k])`),'Stream status must share button geometry and typography');
   let shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(evidence,`toolbar-${width}-${theme}.png`),Buffer.from(shot.data,'base64'));
   for(const id of ['logs-options-modal','host-logging-modal']){
    await openModal(id);
    assert(await evaluate(`document.documentElement.scrollWidth<=innerWidth && document.querySelector('#${id} .modal-body').scrollWidth<=document.querySelector('#${id} .modal-body').clientWidth`),'Modal controls must wrap without horizontal scrolling');
    await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
    await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
    assert(await evaluate(`document.getElementById('${id}').contains(document.activeElement)`),'Keyboard focus must stay in the dialog');
    shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(evidence,`${id}-${width}-${theme}.png`),Buffer.from(shot.data,'base64'));
    await closeModal(id);
   }
  }
  for (const width of [360,390,801,1280]) {
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:960,deviceScaleFactor:1,mobile:width<600});
   assert(await evaluate(`[...document.querySelectorAll('.logs-network')].every(r=>r.scrollWidth<=r.clientWidth+1&&r.querySelector('.logs-msg').clientWidth>0)`),'Network provenance must wrap without obscuring severity or message');
  }
  const options=await evaluate(`[...document.querySelectorAll('#logs-source option[value^="udp:"]')].map(x=>({value:x.value,label:x.textContent}))`);
  assert(options.every(o=>o.label.includes(a)&&o.label.includes('member')&&/direct|relay/.test(o.label)));
  assert(await evaluate(`!document.querySelector('#all script') && document.getElementById('all').textContent.includes('<name>')`));
  await evaluate(`document.getElementById('logs-source').value='udp:member:pico:${a}:relay';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===1`));
  assert(await evaluate(`document.getElementById('all').textContent.includes('relayed')`));
  assert(await evaluate(`document.getElementById('logs-source-status').textContent.includes('${a}') && document.getElementById('logs-source-status').textContent.includes('relay')`));
  assert(urls.some(u=>u.includes('stream_version=2')&&decodeURIComponent(u).includes('source=udp:member:pico:'+a+':relay')));
  await evaluate(`document.getElementById('logs-source').value='all';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===5`));
  const maximum=[];hostPolicy.saved.collector.peers=[];
  for(let peer=0;peer<32;peer++){
   const slot='peer_'+String(peer).padStart(27,'0'),ip=`192.0.2.${peer+1}`;
   hostPolicy.saved.collector.peers.push({slot,ipv4:ip});
   for(let origin=1;origin<=65;origin++)for(const ingress of ['direct','relay']){
    const id=origin.toString(16).padStart(32,'0');
    maximum.push({source:`udp:${slot}:pico:${id}:${ingress}`,ingress:'udp',peer_slot:slot,peer_ipv4:ip,origin_kind:'pico',origin_id:id,origin_name:'h'.repeat(57)+'.local',origin_boot:'4'.repeat(32),origin_sequence:'1',forwarders:ingress==='relay'?'[{"id":"'+host+'","receipt_us":"1","ingress":"usb"}]':'[]',MESSAGE:'Maximum source list fixture',__CURSOR:`max-${peer}-${origin}-${ingress}`});
   }
  }
  rows.splice(0,rows.length,...maximum);
  await client.send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/index.php?page=logs`});
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===4160 && document.querySelectorAll('#logs-source option[value^="udp:"]').length===4096`));
  assert(await evaluate(`document.getElementById('logs-source-status').textContent.includes('More network origins')`));
  for(const width of [360,390,1280])for(const theme of ['light','dark']){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height:960,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.setAttribute('data-bs-theme','${theme}')`);
   assert(await evaluate(`document.documentElement.scrollWidth<=innerWidth && [...document.querySelectorAll('.logs-network')].every(r=>r.scrollWidth<=r.clientWidth+1)`),'Maximum source history must remain bounded and wrap');
  }
  await client.send('Emulation.setDeviceMetricsOverride',{width:1280,height:960,deviceScaleFactor:1,mobile:false});
  await evaluate(`document.documentElement.style.zoom='2'`);
  assert(await evaluate(`document.documentElement.scrollWidth<=document.documentElement.clientWidth`),'200% CSS zoom reflow');
  await evaluate(`document.documentElement.style.zoom='1'`);
  console.log('UDP Logs browser: toolbar order, modal focus/reflow, automatic settings, failed-read recovery, preserved drafts, uncertain-save review, replay-before-policy race, stable ID labels, direct/relay source filters and safe text rendering passed');
 }finally{client?.close();if(chrome)await stopIsolatedProcess(chrome);server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);process.exitCode=1;});

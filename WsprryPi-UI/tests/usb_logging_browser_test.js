// SPDX-License-Identifier: MIT
// Real production Logs DOM/CSS/JS, synthetic log API and EventSource transport.
"use strict";
const assert=require('node:assert/strict'), fs=require('node:fs'), path=require('node:path'), http=require('node:http');
const {spawnIsolatedProcess,stopIsolatedProcess,waitForSpawn}=require('./browser_fixture_process.js');
const WebSocket=require('ws');
const {createProductionStream}=require('./logging_stream_fixture.js');
let productionStream, loggingDelay=150, loggingRequests=0, loggingReplies=0, discoveryHeld=true;const heldDiscovery=[];
const root=path.resolve(__dirname,'../data');
const html=fs.readFileSync(process.env.LOGGING_RENDERED_HTML,'utf8');
const evidence=process.env.LOGGING_EVIDENCE_DIR || '/tmp/usb-logging-browser';
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
const candidates=[{catalog_id:'1'.repeat(32),device_id:a,name:'Pico A (fixture)'},{catalog_id:'2'.repeat(32),device_id:b,name:'bench-b.local'}];
let policy={schema:'host-usb-logging/1',collector_id:host,sources:[],candidates,journal_available:true,status:{[a]:{state:'streaming',rejected:'0'},[b]:{state:'disconnected',rejected:'2'}}};
const rows=[{source:'local',origin_kind:'pi',MESSAGE:'Pi service ready (synthetic fixture)',__CURSOR:'pi-1'},
 {source:`usb:${a}`,origin_kind:'pico',origin_id:a,origin_name:'pico-a.local',origin_boot:'4'.repeat(32),origin_sequence:'9007199254740993',origin_utc_ns:null,MESSAGE:'Pico A USB logging active (synthetic fixture)',__CURSOR:'a-1'},
 {source:`usb:${b}`,origin_kind:'pico',origin_id:b,origin_name:'pico-b.local',origin_boot:'5'.repeat(32),origin_sequence:'2',origin_utc_ns:'1793098800000000000',MESSAGE:'Pico B <script> stays plain text (synthetic fixture)',__CURSOR:'b-1'}];
const counts=Object.fromEntries(['generated','filtered','redacted_rejected','queued','overflow','expired','reconfigured_drops','truncated','attempted','locally_submitted','locally_failed'].map(k=>[k,'0']));
const cap={schema:'udp-logging-capabilities/1',target:{scope:'host',collector_id:host},resource:'/api/control/v1/host/udp-logging',settings_schema:'host-udp-logging/1',wire_schema:'wsprry-udp/1',limits:{payload_bytes:480,queue_records:256,maximum_sources:8,maximum_peers:32,maximum_rate:64,maximum_burst:128,address_family:'ipv4'}};
const hostPolicy={schema:'host-udp-logging/1',target:cap.target,saved:{sender:{enabled:false,destination_ipv4:null,destination_port:5514,maximum_severity:6,event_codes:[],rate:16,burst:32,interface:null,native_enabled:false,usb_sources:[]},collector:{enabled:false,bind_ipv4:null,interface:null,port:5514,peers:[]}},active:{applied_revision:null,sender:{state:'disabled',receipt:'unknown',queued:0,counters:counts},collector:{state:'disabled',bind_ipv4:null,port:null,peers:[],counters:Object.fromEntries(['received','oversize','disallowed','malformed','duplicate','conflict','limited','overflow','journal_failed','journal_submitted','discontinuity'].map(k=>[k,'0']))}}};
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://fixture');
 if(url.pathname==='/api/control/v1/udp-logging/capabilities'||url.pathname==='/api/control/v1/host/udp-logging'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(url.pathname.endsWith('/capabilities')?cap:hostPolicy));return;}
 if(url.pathname==='/api/control/v1/host/logging'){
  if(req.method==='PUT'){let body='';req.on('data',data=>body+=data);req.on('end',()=>{const next=JSON.parse(body);policy={...policy,sources:next.sources};res.setHeader('ETag','"logging2"');res.end(JSON.stringify(policy));});return;}
  res.setHeader('Content-Type','application/json');res.setHeader('ETag','"logging1"');
  const snapshot=JSON.stringify(policy), delay=loggingDelay;loggingRequests++;
  const reply=()=>setTimeout(()=>{res.end(snapshot);loggingReplies++;},delay);if(discoveryHeld)heldDiscovery.push(reply);else reply();return;
 }
 if(url.pathname==='/log_stream.php'){
  urls.push(req.url);if(productionStream){productionStream.forward(req,res,rows);return;}res.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache'});
  res.write('event: playback_start\ndata: {}\n\n');
  const selected=url.searchParams.get('source')||'all';
  const cursor=url.searchParams.get('cursor'), after=rows.findIndex(row=>row.__CURSOR===cursor);
  const severity=Number(url.searchParams.get('priority_max')||7);
  for(const row of rows.filter((row,index)=>(after<0||index>after)&&(selected==='all'||row.source===selected)&&Number(row.PRIORITY||6)<=severity)){
   const data={type:'journal',playback:false,__REALTIME_TIMESTAMP:'1793098800123456',PRIORITY:'6',_SYSTEMD_UNIT:'wsprrypi.service',...row};
   res.write(`id: ${row.__CURSOR}\nevent: journal\ndata: ${JSON.stringify(data)}\n\n`);
  }
  res.write('event: playback_end\ndata: {}\n\n');
  res.write('event: internal\ndata: {"type":"internal","MESSAGE":"[HEARTBEAT]","PRIORITY":"7","__CURSOR":null}\n\n');
  const timer=setInterval(()=>res.write(': heartbeat\n\n'),1000);req.on('close',()=>clearInterval(timer));return;
 }
 if(url.pathname==='/index.php'||url.pathname==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
 if(url.pathname==='/api/control/v1/status'){res.setHeader('Content-Type','application/json');res.end('{"host":{"selected":false,"firmware_maintenance":false}}');return;}
 const file=path.resolve(root,'.'+url.pathname);
 if(file.startsWith(root+path.sep)&&fs.existsSync(file)&&fs.statSync(file).isFile()&&!file.endsWith('.php')){
  const ext=path.extname(file);res.setHeader('Content-Type',({'.css':'text/css','.js':'application/javascript','.woff2':'font/woff2','.png':'image/png','.svg':'image/svg+xml'})[ext]||'application/octet-stream');res.end(fs.readFileSync(url.pathname==='/view_logs.js'&&process.env.LOGGING_VIEWER_SCRIPT?process.env.LOGGING_VIEWER_SCRIPT:file));return;
 }
 res.writeHead(503,{'Content-Type':'application/json'});res.end('{"error":{"code":"fixture_only"}}');
});
async function main(){
 fs.mkdirSync(evidence,{recursive:true});
 if(process.env.LOGGING_REAL_STREAM==='1')productionStream=await createProductionStream(root);
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const debugServer=http.createServer();await new Promise(resolve=>debugServer.listen(0,'127.0.0.1',resolve));const debugPort=debugServer.address().port;await new Promise(resolve=>debugServer.close(resolve));
 let chrome,client,evaluate;
 try{
  chrome=spawnIsolatedProcess(process.env.CHROME_BIN||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',['--headless','--no-sandbox','--disable-gpu',`--remote-debugging-port=${debugPort}`,`--user-data-dir=/tmp/usb-logging-chrome-${process.pid}`,'about:blank']);await waitForSpawn(chrome);
  const tab=await until(async()=>{try{return(await(await fetch(`http://127.0.0.1:${debugPort}/json`)).json()).find(row=>row.type==='page');}catch{return null;}});
  client=new CdpClient(tab.webSocketDebuggerUrl);await client.open();await client.send('Page.enable');
  evaluate=async expression=>{const value=await client.send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(value.exceptionDetails)throw Error(value.exceptionDetails.text+': '+value.exceptionDetails.exception?.description);return value.result.value;};
  await client.send('Page.addScriptToEvaluateOnNewDocument',{source:`window.WebSocket=class{static OPEN=1;static CONNECTING=0;readyState=0;addEventListener(){}close(){}};localStorage.setItem('wsprrypi.updateCheckDisabled','true');window.WSPRRYPI_ENABLE_LOG_TEST_HOOKS=true;window.logSourceFetches=0;const NativeFetch=window.fetch;window.fetch=(url,...args)=>{if(String(url).endsWith('/host/logging'))window.logSourceFetches++;return NativeFetch(url,...args)};const NativeInterval=window.setInterval;window.setInterval=(fn,delay,...args)=>{if(delay===10000)window.logSourcePoll=fn;return NativeInterval(fn,delay,...args)};window.logTestStreams=[];const NativeEventSource=window.EventSource;window.EventSource=class extends NativeEventSource{constructor(url){super(url);window.logTestStreams.push(this);this.addEventListener('playback_end',()=>{this.testReplayed=true;});}};`});
  await client.send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/index.php?page=logs`});
  await until(()=>evaluate(`window.logTestStreams.at(-1)?.testReplayed===true && document.querySelectorAll('#all .logs-line').length===3`));
  assert.equal(await evaluate(`Object.keys(localStorage).filter(key=>key.startsWith('log_stream_cursor_')).length`),0,'Unverified collector cannot persist a cursor');
  discoveryHeld=false;heldDiscovery.splice(0).forEach(reply=>reply());
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===3 && document.getElementById('logs-source').options.length===4`));
  await until(()=>evaluate(`document.querySelector('#all .logs-unit[data-pico-id="${a}"]')?.textContent==='Pico A (fixture) [aaaaaaaa] '`));
  assert.equal(await evaluate(`document.querySelector('#logs-source option[value="usb:${a}"]').textContent`),`Pico A (fixture) · ${a}`,'Incoming hostname must not replace the Fleet alias');
  assert.equal(await evaluate(`document.querySelector('#logs-source option[value="usb:${b}"]').textContent`),`bench-b · ${b}`,'Only display text omits terminal .local');
  assert.equal(await evaluate(`document.querySelector('#all .logs-unit[data-pico-id="${b}"]').textContent`),'bench-b [bbbbbbbb] ');
  assert(await evaluate(`document.querySelector('#all .logs-msg').textContent.includes('Pi service ready')`));
  assert(await evaluate(`document.getElementById('logs-source-status').textContent==='' && document.getElementById('logs-source-status').getBoundingClientRect().height===0`),'Routine source caption must reclaim its vertical space');
  assert(await evaluate(`!document.querySelector('#all script') && document.getElementById('all').textContent.includes('<script>')`));
  for(const [width,height,theme,name] of [[1440,960,'light','desktop-light'],[390,844,'light','mobile-light'],[1440,960,'dark','desktop-dark'],[390,844,'dark','mobile-dark']]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='${theme}'`);await wait(150);
   assert(await evaluate(`document.documentElement.scrollWidth<=${width}`),name+' horizontal overflow');
   assert(await evaluate(`Array.from(document.querySelectorAll('.logs-unit[data-pico-id]')).every(row=>row.scrollWidth<=row.clientWidth)`),name+' Pico name and bracketed ID must remain visible');
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(evidence,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  const select=async source=>{await evaluate(`document.getElementById('logs-source').value=${JSON.stringify(source)};document.getElementById('logs-source').dispatchEvent(new Event('change'));`);await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===1`));};
  await select('local');assert(await evaluate(`!document.getElementById('all').textContent.includes('Pico A')`));
  await evaluate(`document.getElementById('logs-source').value='all';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===3`));
  assert(!new URL(urls.at(-1),'http://fixture').searchParams.has('cursor'),'Returning to a cleared view reloads history');
  await select('usb:'+a);assert(await evaluate(`document.getElementById('all').textContent.includes('received')`));
  const keyA=await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey()`);assert.equal(await evaluate(`localStorage.getItem(${JSON.stringify(keyA)})`),'a-1');
  await select('usb:'+b);const keyB=await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey()`);assert.notEqual(keyA,keyB);assert.equal(await evaluate(`localStorage.getItem(${JSON.stringify(keyB)})`),'b-1');
  await select('usb:'+a);assert(!new URL(urls.at(-1),'http://fixture').searchParams.has('cursor'),'Returning to A loads its history');
  const before=urls.length;await evaluate(`document.getElementById('logs-pause').click()`);await wait(100);assert.equal(urls.length,before);
  assert.equal(await evaluate(`document.getElementById('btn-reconnect').textContent`),'Resume view');
  await evaluate(`window.dispatchEvent(new Event('online'))`);await wait(100);assert.equal(urls.length,before,'Online recovery must preserve explicit pause');
  candidates[0].name='Renamed Pico A (fixture)';
  await evaluate(`document.getElementById('logs-refresh-sources').click()`);
  await until(()=>evaluate(`document.querySelector('#all .logs-unit[data-pico-id="${a}"]')?.textContent==='Renamed Pico A (fixture) [aaaaaaaa] '`));
  assert.equal(urls.length,before,'Name refresh must preserve pause and stream cursor');
  assert.equal(await evaluate(`document.querySelector('#logs-source option[value="usb:${a}"]').textContent`),`Renamed Pico A (fixture) · ${a}`);
  await evaluate(`document.getElementById('btn-reconnect').click()`);await until(()=>urls.length>before);
  await until(()=>evaluate(`document.querySelector('#all .logs-unit[data-pico-id="${a}"]')?.textContent==='Renamed Pico A (fixture) [aaaaaaaa] '`));
  assert.equal(await evaluate(`document.querySelector('#logs-source option[value="usb:${a}"]').textContent`),`Renamed Pico A (fixture) · ${a}`,'Replayed events must not overwrite the saved name');
  const beforeSaveRead=loggingRequests;
  await evaluate(`document.getElementById('logs-collect').click()`);await until(()=>Promise.resolve(policy.sources.length===1));assert.equal(policy.sources[0].device_id,a);
  await until(()=>Promise.resolve(loggingRequests>beforeSaveRead));
  await evaluate(`window.dispatchEvent(new Event('wsprry-udp-policy-changed'))`);
  await until(()=>evaluate(`document.getElementById('logs-collect').checked&&!document.getElementById('logs-collect').disabled`));
  policy={...policy,candidates:candidates.filter(row=>row.device_id!==a)};
  await evaluate(`document.getElementById('logs-refresh-sources').click()`);await until(()=>evaluate(`!document.getElementById('logs-collect').disabled`));
  assert.equal(await evaluate(`document.getElementById('logs-collect').disabled`),false,'Removed Fleet profile retains disable control');
  await evaluate(`document.getElementById('logs-collect').click()`);await until(()=>Promise.resolve(policy.sources.length===0));
  await evaluate(`document.querySelector('[data-bs-target="#logs-options-modal"]').click()`);
  await until(()=>evaluate(`document.getElementById('logs-options-modal').classList.contains('show') && document.getElementById('logs-options-modal').contains(document.activeElement)`));
  assert(await evaluate(`document.getElementById('logs-options-modal').textContent.includes('Console remains available')`));
  policy={...policy,status:{...policy.status,[b]:{state:'dedicated_logging_unsupported'}}};
  await evaluate(`document.getElementById('logs-source').value='usb:'+ '${'b'.repeat(32)}';document.getElementById('logs-source').dispatchEvent(new Event('change'));document.getElementById('logs-refresh-sources').click()`);
  await until(()=>evaluate(`document.getElementById('logs-source-status').textContent.includes('Update Pico firmware')`));
  assert(await evaluate(`document.getElementById('logs-options-status').textContent===document.getElementById('logs-source-status').textContent`),'Collection feedback must stay visible beside the dialog controls');
  for(const [width,height,theme,name] of [[1440,960,'light','options-desktop-light'],[390,844,'light','options-mobile-light'],[1440,960,'dark','options-desktop-dark'],[390,844,'dark','options-mobile-dark']]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='${theme}';window.scrollTo(0,0)`);await wait(150);
   assert(await evaluate(`document.documentElement.scrollWidth<=${width}`));
   const metrics=await client.send('Page.getLayoutMetrics');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:metrics.cssContentSize.height,scale:1}});fs.writeFileSync(path.join(evidence,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  await evaluate(`document.querySelector('#logs-options-modal [data-bs-dismiss="modal"]').click()`);
  await until(()=>evaluate(`!document.querySelector('.modal.show') && !document.querySelector('.modal-backdrop')`));
  policy={...policy,attached:[{device_id:'d'.repeat(32),name:'Attached Pico (fixture)'}]};
  await evaluate(`document.getElementById('logs-refresh-sources').click()`);
  await until(()=>evaluate(`Array.from(document.getElementById('logs-source').options).some(row=>row.value==='usb:'+ 'd'.repeat(32))`));
  assert(await evaluate(`Array.from(document.getElementById('logs-source').options).some(row=>row.value==='usb:'+ 'd'.repeat(32))`));
  await evaluate(`document.getElementById('logs-source').value='usb:'+ 'd'.repeat(32);document.getElementById('logs-source').dispatchEvent(new Event('change'))`);await wait(150);
  assert(await evaluate(`document.getElementById('logs-collect').disabled && document.getElementById('logs-source-status').textContent.includes('Fleet')`));
  assert.equal(policy.sources.length,0,'Attached inventory must never auto-enroll collection');
  for(const [width,height,theme,name] of [[1440,960,'light','attached-desktop-light'],[390,844,'light','attached-mobile-light'],[1440,960,'dark','attached-desktop-dark'],[390,844,'dark','attached-mobile-dark']]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='${theme}';window.scrollTo(0,0)`);await wait(150);
   assert(await evaluate(`document.documentElement.scrollWidth<=${width}`));
   const metrics=await client.send('Page.getLayoutMetrics');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:metrics.cssContentSize.height,scale:1}});fs.writeFileSync(path.join(evidence,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  rows.push({source:`usb:${'e'.repeat(32)}`,origin_kind:'pico',origin_id:'e'.repeat(32),origin_name:'unknown.LOCAL',MESSAGE:'Unregistered hostname fixture',__CURSOR:'e-1'});
  rows.push({source:`usb:${'f'.repeat(32)}`,origin_kind:'pico',origin_id:'f'.repeat(32),origin_name:'lab.local.example',MESSAGE:'Nonterminal suffix fixture',__CURSOR:'f-1'});
  await evaluate(`document.getElementById('logs-source').value='all';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===5`));
  assert.equal(await evaluate(`document.querySelector('#all .logs-unit[data-pico-id="${'e'.repeat(32)}"]').textContent`),'unknown [eeeeeeee] ');
  assert.equal(await evaluate(`document.querySelector('#logs-source option[value="usb:${'e'.repeat(32)}"]').textContent`),`unknown · ${'e'.repeat(32)}`);
  assert.equal(await evaluate(`document.querySelector('#all .logs-unit[data-pico-id="${'f'.repeat(32)}"]').textContent`),'lab.local.example [ffffffff] ');
  const exported=await evaluate(`(async()=>{const original=URL.createObjectURL;URL.createObjectURL=blob=>{window.exportedLogs=blob;return original.call(URL,blob)};document.getElementById('logs-export').click();return await window.exportedLogs.text()})()`);
  const exportedRows=exported.trim().split('\n').map(JSON.parse);
  assert.equal(exportedRows.find(row=>row.origin_id===a).origin_name,'pico-a.local','Exports retain reported names, not Fleet aliases');
  assert.equal(exportedRows.find(row=>row.origin_id==='e'.repeat(32)).origin_name,'unknown.LOCAL','Display formatting must not mutate records');
  assert.equal(candidates[1].name,'bench-b.local','Display formatting must not modify Fleet names');
  // A slow earlier discovery response must not restore a retired identity.
  const previousReplies=loggingReplies, previousRequests=loggingRequests;
  loggingDelay=600;
  await evaluate(`document.getElementById('logs-refresh-sources').click()`);
  await until(()=>Promise.resolve(loggingRequests>previousRequests));
  assert(await evaluate(`(()=>{const before=window.logSourceFetches;window.logSourcePoll();return window.logSourceFetches===before})()`),'Periodic polling must not starve an in-flight discovery');
  policy={...policy,collector_id:'d'.repeat(32)};cap.target.collector_id=policy.collector_id;loggingDelay=0;
  await evaluate(`document.getElementById('logs-refresh-sources').click()`);
  await until(()=>evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey().includes('${'d'.repeat(32)}')`));
  await until(()=>Promise.resolve(loggingReplies>=previousReplies+2));
  assert(await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey().includes('${'d'.repeat(32)}')`),'Late discovery must not restore the old collector');
  await require('./logging_resume_cases.js')({evaluate,until,urls,rows,a,b});
  if(productionStream){
   hostPolicy.saved.collector.peers=[{slot:'member',ipv4:'192.0.2.10'}];
   for(const ingress of ['direct','relay'])rows.push({source:`udp:member:pico:${a}:${ingress}`,ingress:'udp',peer_slot:'member',peer_ipv4:'192.0.2.10',origin_kind:'pico',origin_id:a,origin_name:'remote.local',origin_boot:'4'.repeat(32),origin_sequence:'40',forwarders:ingress==='relay'?JSON.stringify([{id:host,receipt_us:'1',ingress:'usb'}]):'[]',MESSAGE:'UTC holdover',PRIORITY:'7',__CURSOR:'remote-'+ingress});
   await evaluate(`document.getElementById('logs-severity').value='7';document.getElementById('logs-severity').dispatchEvent(new Event('change'));document.getElementById('logs-refresh-sources').click()`);
   await until(()=>evaluate(`document.querySelectorAll('#logs-source option[value^="udp:"]').length===2`));
   for(const ingress of ['direct','relay','direct']){
    await evaluate(`document.getElementById('logs-source').value='udp:member:pico:${a}:${ingress}';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
    await until(()=>evaluate(`window.logTestStreams.at(-1)?.testReplayed===true`));
    assert.equal(await evaluate(`document.querySelectorAll('#all .logs-line').length`),1);
   }
   const probe=async(source,cursor)=>{
    const response=await fetch(`http://127.0.0.1:${server.address().port}/log_stream.php?`+new URLSearchParams({stream_version:'2',source,cursor,backlog:'200'}),{signal:AbortSignal.timeout(10000)});
    const reader=response.body.getReader(), decoder=new TextDecoder(), events=[];let text='';
    try{while(true){const chunk=await reader.read();if(chunk.done)break;text+=decoder.decode(chunk.value,{stream:true});let end;while((end=text.indexOf('\n\n'))>=0){const frame=text.slice(0,end);text=text.slice(end+2);for(const line of frame.split('\n'))if(line.startsWith('data: ')){const event=JSON.parse(line.slice(6));events.push(event);if(event.MESSAGE==='journalctl follow loop entering')return events;}}if(text.length>65536)throw Error('Unbounded stream fixture');}throw Error('Replay boundary missing');}finally{await reader.cancel();}
   };
   const tail=await probe('usb:'+a,'clock-2');assert(!tail.some(e=>e.type==='journal'),'Valid tail must not replay');
   const recovered=await probe('usb:'+a,'not-a-cursor');assert(recovered.some(e=>String(e.MESSAGE).includes('Falling back')));const history=recovered.filter(e=>e.type==='journal');assert.equal(history.length,4);assert(history.every(e=>e.source==='usb:'+a));
  }
  await evaluate(`document.getElementById('logs-severity').value='7';document.getElementById('logs-source').value='usb:${a}';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
  await until(()=>evaluate(`window.logTestStreams.at(-1)?.testReplayed===true`));
  for(const [width,height,name] of [[1280,960,'clock-desktop'],[390,844,'clock-mobile']]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='light';window.scrollTo(0,0)`);
   assert(await evaluate(`document.documentElement.scrollWidth<=innerWidth`));
   const metrics=await client.send('Page.getLayoutMetrics');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:metrics.cssContentSize.height,scale:1}});fs.writeFileSync(path.join(evidence,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  console.log('USB Logs browser: stable Fleet aliases, live rename, hostname display/export separation, history reload, cursor-only continuation, Clear, paused changes, stale callbacks/discovery, severity, source isolation and rendered viewports passed');
 }catch(error){if(evaluate)console.error(await evaluate(`({view:document.getElementById("all")?.textContent,internal:document.getElementById("internal")?.textContent,options:document.getElementById("logs-source")?.options.length})`));throw error;}finally{client?.close();if(chrome)await stopIsolatedProcess(chrome);await new Promise(resolve=>server.close(resolve));if(productionStream)await productionStream.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});

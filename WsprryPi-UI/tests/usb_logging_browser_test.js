// SPDX-License-Identifier: MIT
// Real production Logs DOM/CSS/JS, synthetic log API and EventSource transport.
"use strict";
const assert=require('node:assert/strict'), fs=require('node:fs'), path=require('node:path'), http=require('node:http');
const {spawnIsolatedProcess,stopIsolatedProcess,waitForSpawn}=require('./browser_fixture_process.js');
const WebSocket=require('ws');
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
const candidates=[{catalog_id:'1'.repeat(32),device_id:a,name:'Pico A (fixture)'},{catalog_id:'2'.repeat(32),device_id:b,name:'Pico B (fixture)'}];
let policy={schema:'host-usb-logging/1',collector_id:host,sources:[],candidates,journal_available:true,status:{[a]:{state:'streaming',rejected:'0'},[b]:{state:'disconnected',rejected:'2'}}};
const rows=[{source:'local',origin_kind:'pi',MESSAGE:'Pi service ready (synthetic fixture)',__CURSOR:'pi-1'},
 {source:`pico:${a}`,origin_kind:'pico',origin_id:a,origin_name:'pico-a.local',origin_boot:'4'.repeat(32),origin_sequence:'9007199254740993',origin_utc_ns:null,MESSAGE:'Pico A USB logging active (synthetic fixture)',__CURSOR:'a-1'},
 {source:`pico:${b}`,origin_kind:'pico',origin_id:b,origin_name:'pico-b.local',origin_boot:'5'.repeat(32),origin_sequence:'2',origin_utc_ns:'1793098800000000000',MESSAGE:'Pico B <script> stays plain text (synthetic fixture)',__CURSOR:'b-1'}];
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://fixture');
 if(url.pathname==='/api/v1/host/logging'){
  if(req.method==='PUT'){let body='';req.on('data',data=>body+=data);req.on('end',()=>{const next=JSON.parse(body);policy={...policy,sources:next.sources};res.setHeader('ETag','"logging2"');res.end(JSON.stringify(policy));});return;}
  res.setHeader('Content-Type','application/json');res.setHeader('ETag','"logging1"');res.end(JSON.stringify(policy));return;
 }
 if(url.pathname==='/log_stream.php'){
  urls.push(req.url);res.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache'});
  const selected=url.searchParams.get('source')||'all';
  for(const row of rows.filter(row=>selected==='all'||row.source===selected)){
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
  chrome=spawnIsolatedProcess(process.env.CHROME_BIN||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',['--headless','--no-sandbox','--disable-gpu',`--remote-debugging-port=${debugPort}`,`--user-data-dir=/tmp/usb-logging-chrome-${process.pid}`,'about:blank']);await waitForSpawn(chrome);
  const tab=await until(async()=>{try{return(await(await fetch(`http://127.0.0.1:${debugPort}/json`)).json()).find(row=>row.type==='page');}catch{return null;}});
  client=new CdpClient(tab.webSocketDebuggerUrl);await client.open();await client.send('Page.enable');
  const evaluate=async expression=>{const value=await client.send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(value.exceptionDetails)throw Error(value.exceptionDetails.text+': '+value.exceptionDetails.exception?.description);return value.result.value;};
  await client.send('Page.addScriptToEvaluateOnNewDocument',{source:`window.WebSocket=class{static OPEN=1;static CONNECTING=0;readyState=0;addEventListener(){}close(){}};localStorage.setItem('wsprrypi.updateCheckDisabled','true');window.WSPRRYPI_ENABLE_LOG_TEST_HOOKS=true;`});
  await client.send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/index.php?page=logs`});
  await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===3 && document.getElementById('logs-source').options.length===4`));
  assert(await evaluate(`document.querySelector('#all .logs-msg').textContent.includes('Pi service ready')`));
  assert(await evaluate(`!document.querySelector('#all script') && document.getElementById('all').textContent.includes('<script>')`));
  for(const [width,height,theme,name] of [[1440,960,'light','desktop-light'],[390,844,'light','mobile-light'],[1440,960,'dark','desktop-dark'],[390,844,'dark','mobile-dark']]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='${theme}'`);await wait(150);
   assert(await evaluate(`document.documentElement.scrollWidth<=${width}`),name+' horizontal overflow');
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(evidence,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  const select=async source=>{await evaluate(`document.getElementById('logs-source').value=${JSON.stringify(source)};document.getElementById('logs-source').dispatchEvent(new Event('change'));`);await until(()=>evaluate(`document.querySelectorAll('#all .logs-line').length===1`));};
  await select('local');assert(await evaluate(`!document.getElementById('all').textContent.includes('Pico A')`));
  await select('pico:'+a);assert(await evaluate(`document.getElementById('all').textContent.includes('received')`));
  const keyA=await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey()`);assert.equal(await evaluate(`localStorage.getItem(${JSON.stringify(keyA)})`),'a-1');
  await select('pico:'+b);const keyB=await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey()`);assert.notEqual(keyA,keyB);assert.equal(await evaluate(`localStorage.getItem(${JSON.stringify(keyB)})`),'b-1');
  await select('pico:'+a);assert(urls.at(-1).includes('cursor=a-1'));assert(!urls.at(-1).includes('cursor=b-1'));
  const before=urls.length;await evaluate(`document.getElementById('logs-pause').click()`);await wait(100);assert.equal(urls.length,before);
  assert.equal(await evaluate(`document.getElementById('btn-reconnect').textContent`),'Resume view');
  await evaluate(`window.dispatchEvent(new Event('online'))`);await wait(100);assert.equal(urls.length,before,'Online recovery must preserve explicit pause');
  await evaluate(`document.getElementById('btn-reconnect').click()`);await until(()=>urls.length>before);
  await evaluate(`document.getElementById('logs-collect').click()`);await until(()=>Promise.resolve(policy.sources.length===1));assert.equal(policy.sources[0].device_id,a);
  policy={...policy,candidates:candidates.filter(row=>row.device_id!==a)};
  await evaluate(`document.getElementById('logs-refresh-sources').click()`);await wait(150);
  assert.equal(await evaluate(`document.getElementById('logs-collect').disabled`),false,'Removed Fleet profile retains disable control');
  await evaluate(`document.getElementById('logs-collect').click()`);await until(()=>Promise.resolve(policy.sources.length===0));
  await evaluate(`document.querySelector('.logs-options').open=true;document.getElementById('logs-source').focus()`);
  for(const [width,height,theme,name] of [[1440,960,'light','options-desktop-light'],[390,844,'light','options-mobile-light'],[1440,960,'dark','options-desktop-dark'],[390,844,'dark','options-mobile-dark']]){
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});
   await evaluate(`document.documentElement.dataset.bsTheme='${theme}';window.scrollTo(0,0)`);await wait(150);
   assert(await evaluate(`document.documentElement.scrollWidth<=${width}`));
   const metrics=await client.send('Page.getLayoutMetrics');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:metrics.cssContentSize.height,scale:1}});fs.writeFileSync(path.join(evidence,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  console.log('USB Logs browser: source isolation, cursor scope, pause/resume, collection save, injection resistance and four rendered viewports passed');
 }finally{client?.close();if(chrome)await stopIsolatedProcess(chrome);await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);process.exitCode=1;});

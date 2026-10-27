// SPDX-License-Identifier: MIT
// Visual fixtures use the production logging form markup and theme assets.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http'),WebSocket=require('ws');
const {spawnIsolatedProcess,stopIsolatedProcess,waitForSpawn}=require('./browser_fixture_process.js');
const root=path.resolve(__dirname,'../data'),pico=process.env.PICO_WEB_ROOT;
const evidence=process.env.LOGGING_CONTROLS_EVIDENCE_DIR||'/tmp/usb-logging-controls';
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(check){for(let n=0;n<100;n++){try{const value=await check();if(value)return value;}catch{}await wait(50);}throw Error('Fixture timed out');}
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



const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://fixture');
 if(url.pathname==='/fleet'){res.setHeader('Content-Type','text/html');res.end(fs.readFileSync(process.env.LOGGING_FLEET_HTML));return;}
 if(url.pathname==='/pico'){res.setHeader('Content-Type','text/html');res.end(fs.readFileSync(path.join(pico,'index.html'),'utf8').replace(/<script[\s\S]*?<\/script>/g,'').replace('href="/style.css"','href="/pico-style.css"'));return;}
 const file=url.pathname==='/pico-style.css'?path.join(pico,'style.css'):path.resolve(root,'.'+url.pathname);
 if((file.startsWith(root+path.sep)||file===path.join(pico,'style.css'))&&fs.existsSync(file)&&!file.endsWith('.php')){res.setHeader('Content-Type',file.endsWith('.css')?'text/css':file.endsWith('.js')?'application/javascript':'application/octet-stream');res.end(fs.readFileSync(file));return;}
 res.writeHead(404);res.end();
});
async function main(){
 fs.mkdirSync(evidence,{recursive:true});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const probe=http.createServer();await new Promise(resolve=>probe.listen(0,'127.0.0.1',resolve));const debug=probe.address().port;await new Promise(resolve=>probe.close(resolve));let chrome,client;
 try{
  chrome=spawnIsolatedProcess('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',['--headless','--no-sandbox','--disable-gpu',`--remote-debugging-port=${debug}`,`--user-data-dir=/tmp/usb-logging-controls-${process.pid}`,'about:blank']);await waitForSpawn(chrome);
  const tab=await until(async()=>{return(await(await fetch(`http://127.0.0.1:${debug}/json`)).json()).find(row=>row.type==='page');});client=new CdpClient(tab.webSocketDebuggerUrl);await client.open();await client.send('Page.enable');
  const evaluate=async expression=>{const r=await client.send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result.value;};
  for(const view of ['pico','fleet']){
   await client.send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/${view}`});
   await until(()=>evaluate(`document.readyState==='complete' && !!document.getElementById('${view==='pico'?'logging-section':'wtp-usb-settings'}')`));
   await evaluate(`(()=>{const section=document.getElementById('${view==='pico'?'logging-section':'wtp-usb-settings'}');section.hidden=false;const main=document.createElement('main');main.className='container py-4';const heading=document.createElement('h1');heading.textContent='${view==='pico'?'Pico USB logging':'Fleet USB settings'}';const note=document.createElement('p');note.textContent='Synthetic saved settings for visual validation.';main.append(heading,note,section);document.body.replaceChildren(main);for(const detail of section.querySelectorAll('details'))detail.open=true;if(section.tagName==='DETAILS')section.open=true;for(const input of section.querySelectorAll('input,select,button,fieldset'))input.disabled=false;const status=document.getElementById('${view==='pico'?'logging-status':'wtp-usb-logging-status'}');document.getElementById('${view==='pico'?'logging-enabled':'wtp-usb-logging-enabled'}').checked=true;document.getElementById('${view==='pico'?'logging-severity':'wtp-usb-logging-severity'}').value='6';status.textContent='Saved: enabled. Active: enabled; stream started; 0 dropped events. Synthetic fixture.';})()`);
   const themes=view==='fleet'?['light','dark']:['light'];
   for(const theme of themes)for(const [width,height,size]of[[1440,960,'desktop'],[390,844,'mobile']]){
    await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});await evaluate(`document.documentElement.dataset.bsTheme='${theme}';window.scrollTo(0,0)`);await wait(100);
    assert(await evaluate(`document.documentElement.scrollWidth<=${width}`),view+' '+size+' overflow');
    const metrics=await client.send('Page.getLayoutMetrics');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:metrics.cssContentSize.height,scale:1}});fs.writeFileSync(path.join(evidence,`${view}-${size}-${theme}.png`),Buffer.from(shot.data,'base64'));
   }
   await evaluate(`(()=>{const status=document.getElementById('${view==='pico'?'logging-status':'wtp-usb-logging-status'}');status.textContent='${view==='pico'?'Logging state is unconfirmed. Unlock controls and read logging settings before editing.':'Logging settings changed elsewhere. Read logging settings and review your draft before another save.'}';document.getElementById('${view==='pico'?'logging-settings':'wtp-usb-logging-enabled'}').disabled=true;if('${view}'==='pico')document.getElementById('logging-read').disabled=true;else {document.getElementById('wtp-usb-logging-save').disabled=true;document.getElementById('wtp-usb-logging-severity').disabled=true;}})()`);
   for(const theme of themes)for(const [width,height,size]of[[1440,960,'desktop'],[390,844,'mobile']]){
    await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<600});await evaluate(`document.documentElement.dataset.bsTheme='${theme}';window.scrollTo(0,0)`);await wait(100);
    const metrics=await client.send('Page.getLayoutMetrics');const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:metrics.cssContentSize.height,scale:1}});fs.writeFileSync(path.join(evidence,`${view}-${view==='pico'?'locked':'error'}-${size}-${theme}.png`),Buffer.from(shot.data,'base64'));
   }
  }
  console.log('Production Pico/Fleet logging controls rendered at desktop/mobile sizes and shipped themes; no horizontal overflow');
 }finally{client?.close();if(chrome)await stopIsolatedProcess(chrome);await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);process.exitCode=1;});

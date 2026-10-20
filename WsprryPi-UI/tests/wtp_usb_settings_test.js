// SPDX-License-Identifier: MIT
"use strict";
const assert=require("node:assert/strict"), fs=require("node:fs"), vm=require("node:vm"), path=require("node:path");
const source=fs.readFileSync(path.join(__dirname,"../data/wtp-usb-settings.js"),"utf8");
const target={scope:"member",device_id:"a".repeat(32),boot_id:"b".repeat(32)};
function browser(exchange) {
    const fields=new Map(), requests=[];
    const element=id=>{if(!fields.has(id))fields.set(id,{value:"",disabled:false,hidden:false,listeners:{},textContent:"",setAttribute(){},addEventListener(name,fn){this.listeners[name]=fn;}});return fields.get(id);};
    const root={document:{readyState:"complete",getElementById:element},WtpUi:{read:()=>({Transport:"usb"})},AbortSignal,crypto:require("node:crypto").webcrypto,confirm:()=>true,
        fetch:async(url,options)=>{requests.push({url,...options});return exchange(url,options);}};
    root.window=root;vm.runInNewContext(source,root);root.WtpUsbSettings.setAvailability(true);
    return {root,requests,element,click:async name=>element("wtp-usb-"+name).listeners.click()};
}
const reply=(body,status=200,etag='"r1"')=>({ok:status>=200&&status<300,status,headers:{get:()=>etag},json:async()=>body});
async function unit() {
    let saved=null, name="pico-one.local", revision=1, fail=false;
    const b=browser(async(url,options)=>{
        if(url.endsWith("progress"))return reply({schema:"transmitter-progress/1",target,draft_revision:"1",station:{callsign:"AA0NT",locator:null,power_dbm:null},station_complete:false,complete:false,apply_domains:{station:false},schedule_suspended:true,matches_applied:false});
        if(url.endsWith("progress/update"))return reply({outcomes:{callsign:"saved",locator:"cleared",power_dbm:"invalid"}});
        if(options.method==="GET")return reply(url.endsWith("hostname")?{schema:"transmitter-hostname/1",target,saved:name,active:"pico-one.local",default:"pico-default.local",writable:true,pending_restart:name!=="pico-one.local"}:{schema:"transmitter-wifi/1",target,wifi:saved,pending_restart:false},200,`"r${revision}"`);
        if(fail)throw new Error("simulated disconnect");
        const body=JSON.parse(options.body);revision++;
        if(url.endsWith("wifi"))saved=body.wifi?{...body.wifi,password:null}:null;
        if(url.endsWith("hostname"))name=body.hostname||"pico-default.local";
        return reply({},200,`"r${revision}"`);
    });
    await b.click("load");assert.equal(b.element("wtp-usb-hostname").value,name);assert(b.element("wtp-usb-wifi-remove").disabled);
    b.element("wtp-usb-ssid").value="new-network";b.element("wtp-usb-password").value="synthetic-password";b.element("wtp-usb-time-server").value="192.0.2.1";
    await b.click("wifi-save");assert.equal(b.requests.at(-1).method,"PUT");assert.equal(JSON.parse(b.requests.at(-1).body).wifi.password,"synthetic-password");assert.equal(b.element("wtp-usb-password").value,"");
    assert(b.element("wtp-usb-wifi-save").disabled);await b.click("load");
    b.element("wtp-usb-time-server").value="192.0.2.2";await b.click("wifi-save");assert.equal(JSON.parse(b.requests.at(-1).body).wifi.password,null);
    await b.click("load");await b.click("wifi-remove");assert.equal(b.requests.at(-1).method,"DELETE");assert.equal(JSON.parse(b.requests.at(-1).body).wifi,null);
    await b.click("load");assert.match(b.element("wtp-usb-feedback").textContent,/No saved Wi-Fi/);
    b.element("wtp-usb-hostname").value="renamed.local";fail=true;const count=b.requests.length;await b.click("name-save");assert.equal(b.requests.length,count+1);assert.equal(b.element("wtp-usb-hostname").value,"renamed.local");
    fail=false;await b.click("load");b.root.confirm=()=>false;const reviewed=b.requests.length;await b.click("name-save");assert.equal(b.requests.length,reviewed);
    b.root.confirm=()=>true;await b.click("name-save");assert.equal(JSON.parse(b.requests.at(-1).body).hostname,"renamed.local");
    console.log("USB settings producer: name, Wi-Fi create/replace/remove, password preservation, uncertain outcome and explicit review passed");
}
module.exports={browser,reply,target};
if(require.main===module)unit().catch(error=>{console.error(error);process.exitCode=1;});

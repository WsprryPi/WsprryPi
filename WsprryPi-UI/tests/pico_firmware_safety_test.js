"use strict";
const assert=require("node:assert/strict"), fs=require("node:fs"), vm=require("node:vm"), path=require("node:path");
async function main() {
    const node=(id,disabled=false,safe=false)=>({id,disabled,safe,hidden:true,textContent:"",closest(selector){return selector==="nav"?null:this;},matches(){return this.safe;}});
    const controls=[node("rebootButton"),node("shutdownButton"),node("wtp_use"),node("fleet-use"),node("test_tone"),node("transmit"),node("pre-disabled",true),node("wtp-firmware-read"),node("wtp-firmware-recover"),node("fleet-tab",false,true)];
    const banner=node("pico-firmware-safety"),events=new Map(); let observer,scheduled,maintenance=false,failed=false;
    const root={AbortSignal,document:{readyState:"complete",body:{},getElementById:()=>banner,querySelectorAll:()=>controls,addEventListener:(name,handler)=>events.set(name,handler)},
        MutationObserver:class{constructor(callback){observer=callback;}observe(){}},addEventListener(){},clearTimeout(){},setTimeout(callback){scheduled=callback;return 1;},
        fetch:async()=>{if(failed)throw Error("offline");return {ok:true,json:async()=>({host:{firmware_maintenance:maintenance}})};}};
    root.window=root; vm.runInNewContext(fs.readFileSync(path.join(__dirname,"../data/pico-firmware-safety.js"),"utf8"),root);
    await new Promise(setImmediate);
    root.PicoFirmwareSafety.begin();
    for(const control of controls.slice(0,7)) assert(control.disabled,control.id);
    assert(!controls[7].disabled && !controls[8].disabled && !controls[9].disabled);
    assert(!banner.hidden);assert.match(banner.textContent,/reboot and shutdown are disabled/);
    controls[0].disabled=false; observer();assert(controls[0].disabled,"other renderers cannot unlock reboot");
    let prevented=false;events.get("click")({target:controls[0],preventDefault(){prevented=true;},stopImmediatePropagation(){}});assert(prevented);
    await scheduled();assert(root.PicoFirmwareSafety.locked,"local request stays held until firmware status settles it");
    root.PicoFirmwareSafety.observeFirmware({running:false,maintenance:true});failed=true;await scheduled();assert(root.PicoFirmwareSafety.locked,"lost status retains maintenance");
    root.PicoFirmwareSafety.observeFirmware({running:false,maintenance:false});assert(root.PicoFirmwareSafety.locked);
    failed=false;await scheduled();assert(!root.PicoFirmwareSafety.locked);
    assert(!controls[0].disabled && controls[6].disabled,"restore prior disabled state");
    assert(banner.hidden);
    maintenance=true;await scheduled();assert(root.PicoFirmwareSafety.locked,"another client or a reloaded page sees the hold");
    root.PicoFirmwareSafety.observeFirmware({running:false,maintenance:false});assert(root.PicoFirmwareSafety.locked,"one member cannot clear a host-wide hold");
    maintenance=false;await scheduled();assert(!root.PicoFirmwareSafety.locked);
    console.log("Firmware safety: mutation controls, capture guard, renderer races, uncertain replies, cross-client holds and restoration passed");
}
main().catch(error=>{console.error(error);process.exitCode=1;});

"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const source = fs.readFileSync(path.join(__dirname,"../data/index.js"),"utf8");
const site = fs.readFileSync(path.join(__dirname,"../data/site.js"),"utf8");
function load(context,text,name) {
 const start=text.indexOf(`function ${name}(`), end=text.indexOf("\nfunction ",start+1);
 assert(start>=0); vm.runInContext(text.slice(start,end<0?undefined:end),context);
}
const field={options:[],value:"",replaceChildren(...items){this.options=items;},add(v){this.options.push(v);},setCustomValidity(v){this.validation=v;}};
const hint={}, label={}, address={value:"0x60"};
const identity="usb:04d8:00dd:path:6d61636f732d6c6f636174696f6e2d3132333430303030";
const context={window:{WSPRRYPI_PLATFORM:{si5351Transport:"mcp2221a-hid",si5351AdapterDiscoveryError:"",si5351Adapters:[]}},
 document:{getElementById:id=>id==="si5351_i2c_bus"?field:id==="si5351_i2c_address"?address:hint,querySelector:()=>label},
 Option:function(text,value){this.text=text;this.value=value;this.disabled=false;},
 selectedTransmitBackend:()=>"si5351",setFieldValidationState(){},populateSi5351Addresses(){},si5351AddressDiscoverySequence:0};
vm.createContext(context);
for(const name of ["nativeSi5351Transport","nativeSi5351Adapters","populateNativeSi5351Adapters","availableI2cBuses","selectedAdapterIdentity","selectedI2cBusValue","populateI2cBuses"])load(context,source,name);
const adapter={Identity:identity,Name:"MCP2221A",Available:true};
context.window.WSPRRYPI_PLATFORM.si5351Adapters=[adapter];
context.populateI2cBuses(17,"");
assert.equal(field.value,"");assert(field.validation,"discovery does not select the first adapter");
context.populateI2cBuses(17,identity);
assert.equal(context.selectedI2cBusValue(),17);assert.equal(field.numericBus,17);
assert.equal(context.selectedAdapterIdentity(),identity);assert.equal(field.validation,"");
assert(!hint.textContent.includes("/dev/i2c"));assert.match(hint.textContent,/Mac and USB port/);
context.window.WSPRRYPI_PLATFORM.si5351Adapters=[adapter,adapter];context.populateI2cBuses(17,identity);
assert(Number.isNaN(context.selectedI2cBusValue()));assert(field.validation);
context.window.WSPRRYPI_PLATFORM.si5351Adapters=[adapter,{...adapter,Available:"true"}];
assert.equal(context.nativeSi5351Adapters().length,0,"one malformed duplicate invalidates the entire inventory");
context.window.WSPRRYPI_PLATFORM.si5351Adapters=[];context.populateI2cBuses(17,identity);
assert.equal(field.value,identity,"unavailable saved identity remains visible");assert(field.disabled);
context.window.WSPRRYPI_PLATFORM.si5351Transport="unknown";
assert.equal(context.availableI2cBuses().length,0,"unknown transport never becomes Linux");

const buttons={rebootButton:{setAttribute(){}},shutdownButton:{setAttribute(){}}}, status={};
let confirmation, modal=0, sent=0, connected=true, timeout, requestId;
const power={window:{PicoFirmwareSafety:{locked:false}},document:{getElementById:id=>buttons[id]||status},
 pendingSystemAction:null,clearTimeout(){},setTimeout(fn){timeout=fn;return 1;},
 showSystemModal(){modal++;},showConfirmationDialog(value){confirmation=value;},nextTestToneRequestId:()=>String(sent),sendCommand(value){sent++;requestId=value.client_request_id;return connected;}};
vm.createContext(power);
const start=site.indexOf("let machinePowerCapabilities ="),end=site.indexOf("// Show the system modal for reboot",start);
vm.runInContext(site.slice(start,end),power);load(power,site,"openConfirmModal");
power.updateMachinePowerCapabilities({reboot:false,power_off:false});
assert(buttons.rebootButton.disabled&&buttons.shutdownButton.disabled);
power.openConfirmModal("reboot",{});assert.equal(confirmation,undefined);assert.equal(sent,0);
power.updateMachinePowerCapabilities({reboot:true,power_off:true});
assert(!buttons.rebootButton.disabled);power.window.PicoFirmwareSafety.locked=true;power.syncMachinePowerControls();assert(buttons.rebootButton.disabled);
power.window.PicoFirmwareSafety.locked=false;
power.updateMachinePowerCapabilities({reboot:true,power_off:"true"});assert(buttons.rebootButton.disabled);
power.updateMachinePowerCapabilities({reboot:true,power_off:true});power.openConfirmModal("reboot",{});
power.updateMachinePowerCapabilities({reboot:false,power_off:false});confirmation.onConfirm();assert.equal(sent,0,"capability retirement vetoes an open confirmation");
power.updateMachinePowerCapabilities({reboot:true,power_off:true});power.openConfirmModal("reboot",{});connected=false;confirmation.onConfirm();assert.equal(modal,0);assert.match(status.textContent,/No machine power request/);
connected=true;power.openConfirmModal("reboot",{});confirmation.onConfirm();assert.equal(modal,0,"sending a request is not an initiated reboot");
power.handleMachinePowerResponse({command:"reboot",client_request_id:requestId,status:"error",error:"unsupported",message:"Unsupported; application remains running."});assert.equal(modal,0);assert.match(status.textContent,/Unsupported/);
power.openConfirmModal("reboot",{});confirmation.onConfirm();timeout();assert.match(status.textContent,/unconfirmed/);assert.equal(modal,0);
power.openConfirmModal("shutdown",{});confirmation.onConfirm();power.handleMachinePowerResponse({command:"shutdown",client_request_id:"retired-request",status:"ok",requested:true});assert.equal(modal,0);
power.handleMachinePowerResponse({command:"shutdown",client_request_id:requestId,status:"ok",requested:true});assert.equal(modal,1);
console.log("Native host UI: explicit identities, ambiguity, malformed metadata, retained Linux setting, conservative power capabilities, maintenance, stale confirmation, rejection, timeout, and accepted-action feedback passed");

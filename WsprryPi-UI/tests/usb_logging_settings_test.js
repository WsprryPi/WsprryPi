// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict');
const {browser,reply,target}=require('./wtp_usb_settings_test.js');
async function main(){
 let enabled=false,severity=6,revision=1,fail=false,pending=null;
 const resource=()=>({schema:'logging',target,saved:{enabled,maximum_severity:severity},active:{enabled,maximum_severity:severity,streaming:false,dropped:'0'}});
 const test=browser(async(url,options)=>{
  assert(url.endsWith('/logging'));
  if(pending)return new Promise(resolve=>pending.resolve=resolve);
  if(options.method==='PUT'){
   const body=JSON.parse(options.body);assert.equal(body.schema,'logging');assert.deepEqual(body.target,target);
   enabled=body.logging.enabled;severity=body.logging.maximum_severity;revision++;
   if(fail)throw Error('simulated lost save reply');
  }
  return reply(resource(),200,`"r${revision}"`);
 });
 await test.click('logging-read');
 assert.equal(test.element('wtp-usb-logging-save').disabled,false);
 test.element('wtp-usb-logging-enabled').checked=true;test.element('wtp-usb-logging-severity').value='4';
 await test.click('logging-save');assert.equal(enabled,true);assert.equal(severity,4);
 assert.equal(test.requests.at(-1).method,'GET','successful save requires authoritative readback');
 fail=true;test.element('wtp-usb-logging-enabled').checked=false;await test.click('logging-save');
 assert(test.element('wtp-usb-logging-save').disabled);assert.match(test.element('wtp-usb-logging-status').textContent,/unconfirmed/i);
 const attempts=test.requests.length;await test.click('logging-save');assert.equal(test.requests.length,attempts,'uncertain save cannot replay');
 fail=false;await test.click('logging-read');assert.equal(test.element('wtp-usb-logging-enabled').checked,false);
 pending={};const reading=test.click('logging-read');await Promise.resolve();test.root.WtpUsbSettings.setAvailability(false);
 pending.resolve(reply(resource(),200,'"old"'));await reading;
 assert(test.element('wtp-usb-logging-save').disabled,'retired connection cannot populate an actionable editor');
 const refused=browser(async()=>reply({error:{code:'revision_conflict'}},412));
 await refused.click('logging-read');assert.match(refused.element('wtp-usb-logging-status').textContent,/Read logging settings/);assert.doesNotMatch(refused.element('wtp-usb-logging-status').textContent,/Read name and Wi-Fi/);
 console.log('Pico logging UI: target/revision save, readback, lost reply and connection retirement passed');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

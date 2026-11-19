// SPDX-License-Identifier: MIT
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync(require.resolve('../data/index.js'),'utf8');
const begin=source.indexOf('function restorePersistedConfigDraft()');
const end=source.indexOf('\nfunction ',begin+1);assert(begin>=0&&end>begin);
for(const backend of ['wtp','WTP']){
 const saved=JSON.stringify({version:1,payload:{Operation:{'Transmit Backend':backend},WTP:{Hostname:'PreserveD.LoCaL.','TCP Port':31499},CW:{Message:'KEEP THIS DRAFT'}}});
 const values=new Map([['draft',saved]]),messages=[];
 const context={suppressNextConfigDraftRestore:false,CONFIG_DRAFT_STORAGE_KEY:'draft',lastSavedConfigPayload:'{}',
  window:{sessionStorage:{getItem:key=>values.get(key)}},
  removePersistedConfigDraft(){throw Error('Must preserve the legacy draft');},
  suspendConfigAutosave(){throw Error('Must refuse before populating native controls');},
  showBackendStatus:(...args)=>messages.push(args)};
 vm.createContext(context);vm.runInContext(source.slice(begin,end),context);
 assert.equal(vm.runInContext('restorePersistedConfigDraft()',context),false);
 assert.equal(values.get('draft'),saved);assert.match(messages[0][0],/Migration required/);
}
console.log('PASS saved WTP browser draft remains unchanged and cannot populate a native backend');

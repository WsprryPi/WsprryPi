// SPDX-License-Identifier: MIT
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs'), vm = require('node:vm'), path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../data/wtp-management.js'), 'utf8');
const config = {version:1,enabled:false,station:{callsign:'AA0NT',locator:'EM18',power_dbm:20},
  wifi:{ssid:'test-network',password:null,ntp_ipv4:'192.0.2.1'},schedules:[{period_s:120,phase_s:0}],
  allow_unqualified_frequency:true,allow_non_amateur_frequency:true};
const target = {scope:'member',device_id:'a'.repeat(32),boot_id:'1'.repeat(32)};
function browser(exchange, producer = source) {
  const fields = new Map(), requests = [];
  const element = id => {
    if (!fields.has(id)) fields.set(id, {value:'',checked:false,disabled:false,hidden:false,textContent:'',listeners:{},
      setAttribute(){},addEventListener(name,callback){this.listeners[name]=callback;}});
    return fields.get(id);
  };
  const root = {document:{readyState:'complete',getElementById:element,querySelectorAll:()=>[]},
    WtpUi:{developmentControlsVisible:true,read:()=>({Transport:'network'})}, AbortController,setTimeout,clearTimeout,
    fetch:async (url, options) => {
      const request = {url,...options,body:options.body ? JSON.parse(options.body) : null}; requests.push(request);
      return exchange(request);
    }};
  root.window=root; vm.runInNewContext(producer,root);
  root.WtpManagement.setAvailability(true);
  return {root,fields,requests,element,click:id=>element(id).listeners.click()};
}
const response = (body, status=200, etag='"r1"') => ({ok:status===200,status,headers:{get:()=>etag},json:async()=>body});
const context = (c=config) => ({schema:'wsprrypi-pico-settings/1',target,config:structuredClone(c),update_strategy:'merge',reboot_required:false});
module.exports = {browser, response, context, config, target};

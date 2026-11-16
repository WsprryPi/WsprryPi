// SPDX-License-Identifier: MIT
"use strict";
const assert = require("node:assert/strict");
const {status} = require("../data/controller-mode.js");
const valid = {role:"controller-only",controller_ready:true,device_id:"a".repeat(32),
    local_output:null,local_transmission_available:false};
assert.deepEqual(status(valid), valid);
for (const input of [null, [], {}, {...valid,device_id:123}, {...valid,device_id:"A".repeat(32)},
    {...valid,controller_ready:1}, {...valid,local_output:"simulated"},
    {...valid,local_transmission_available:true}, {...valid,role:"transmitter"}]) assert.equal(status(input), null);
assert.equal(status({...valid,controller_ready:false}).controller_ready, false);
console.log("PASS: controller status requires explicit role, identity, readiness, and no local output");

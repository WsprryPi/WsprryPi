// SPDX-License-Identifier: MIT
// Station drafts must survive invalid siblings, uncertainty and late responses.
"use strict";
const assert = require("node:assert/strict");
const {browser,response,target} = require("./wtp_management_fixture.js");
const base = () => ({schema:"transmitter-progress/1",target:structuredClone(target),draft_revision:"0",config_revision:"0",profile_revision:"0",
    fields:{},last_receipt:null,last_outcomes:{},applied_station:null,station:{callsign:null,locator:null,power_dbm:null},
    station_complete:false,complete:false,apply_domains:{station:false,network:false,radio:false,all:false},wspr_plan:null,
    schedule_enabled:false,schedule_suspended:true,matches_applied:false});
const turn = () => new Promise(resolve => setImmediate(resolve));
async function tests() {
    let state=base(), sent, delay, release, malformed=false;
    const b=browser(async r=>{
        if (delay) await new Promise(resolve=>{release=resolve;});
        if (r.method === "POST") {
            sent=r.body;
            const outcomes={callsign:"saved",locator:"invalid",power_dbm:sent.fields.power_dbm===null?"cleared":"saved"};
            state={...state,draft_revision:"1",station:{callsign:sent.fields.callsign,locator:null,power_dbm:sent.fields.power_dbm},outcomes,request_id:sent.request_id};
        }
        return response(malformed ? {...state,schema:"transmitter-progress/99"} : structuredClone(state));
    });
    await b.click("wtp-usb-progress-load");
    assert(!b.element("wtp-member-callsign").disabled,"blank station can save progress without complete resources");
    assert.equal(b.element("wtp-member-power").value,"");
    assert(b.element("wtp-usb-progress-apply").disabled);
    b.element("wtp-member-callsign").value="AA0NT"; b.element("wtp-member-callsign").listeners.input();
    b.element("wtp-member-locator").value="invalid"; b.element("wtp-member-power").value="0";
    await b.click("wtp-usb-progress-save");
    assert.equal(sent.fields.power_dbm,0);assert.equal(sent.fields.locator,"invalid");assert.match(sent.request_id,/^[0-9a-f]{32}$/);
    assert.match(b.element("wtp-member-locator-progress").textContent,/Invalid/);
    assert.equal(b.element("wtp-member-locator").value,"invalid");
    assert(b.element("wtp-usb-progress-save").disabled,"readback is required after every change");
    delete state.outcomes;delete state.request_id;
    await b.click("wtp-usb-progress-load");
    assert.equal(b.element("wtp-member-locator").value,"invalid","readback preserves invalid draft");
    b.element("wtp-member-power").value="";
    delay=true;
    const saving=b.click("wtp-usb-progress-save");await turn();
    b.element("wtp-member-callsign").value="K1ABC";b.element("wtp-member-callsign").listeners.input();
    b.element("wtp-member-callsign").value="AA0NT";b.element("wtp-member-callsign").listeners.input();
    release();await saving;delay=false;
    assert.equal(sent.fields.power_dbm,null,"unset numeric field must never become zero");
    assert.equal(b.element("wtp-member-callsign").value,"AA0NT");
    assert.match(b.element("wtp-member-callsign-progress").textContent,/newer edit has not been saved/);
    delete state.outcomes;delete state.request_id;
    await b.click("wtp-usb-progress-load");
    malformed=true;const before=b.requests.length;await b.click("wtp-usb-progress-save");
    assert.equal(b.requests.length,before+1,"malformed success is never replayed");
    assert(b.element("wtp-usb-progress-save").disabled);assert.match(b.element("wtp-usb-progress-state").textContent,/incomplete/);
    malformed=false;delete state.outcomes;delete state.request_id;
    state.target={...target,device_id:"b".repeat(32)};await b.click("wtp-usb-progress-load");
    assert(b.element("wtp-usb-progress-save").disabled,"previous member drafts cannot be saved to another member");
    assert.match(b.element("wtp-usb-progress-state").textContent,/different Pico/);
    await b.click("wtp-usb-progress-discard");assert(!b.element("wtp-usb-progress-save").disabled);
    state={...base(),target:state.target,draft_revision:"2",station:{callsign:"AA0NT",locator:"EM18",power_dbm:0},station_complete:true,
        apply_domains:{station:true,network:false,radio:false,all:false}};
    b.root.managementState={carrier:"consumer_station",host:"192.0.2.1",connected:true,selection_id:"f".repeat(32),session:null};
    await b.click("wtp-management-state");
    await b.click("wtp-usb-progress-load");
    assert(b.element("wtp-usb-progress-apply").disabled,"profile station application belongs to transactional Pico setup");
    assert(!b.element("wtp-usb-progress-save").disabled,"independent draft saves remain available");
    delay=true;const pending=b.click("wtp-usb-progress-load");await turn();b.root.WtpManagement.invalidate();release();await pending;
    assert(b.element("wtp-usb-progress-save").disabled,"a late read cannot restore an invalidated context");
    console.log("Station progress: incomplete/null/zero, invalid siblings, late edits, malformed success, explicit review, cross-member drafts and profile authority passed");
}
if(require.main===module)tests().catch(error=>{console.error(error);process.exitCode=1;});
module.exports={base};

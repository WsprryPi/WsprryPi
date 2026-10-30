// SPDX-License-Identifier: MIT
// Shared production Pi/Pico network and USB cases; no physical pad is accessed.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),{createHash}=require('node:crypto');
const {browser,response}=require('./wtp_management_fixture.js');
function legacyProducer() {
    const base=path.join(__dirname,'fixtures/pi-41188326-wtp-management');
    const metadata=JSON.parse(fs.readFileSync(base+'.json','utf8')), bytes=fs.readFileSync(base+'.js');
    assert.equal(metadata.repository,'WsprryPi/WsprryPi');
    assert.equal(metadata.revision,'411883266bfe4bcbf845b3b8f1caabd74851c23e');
    assert.equal(metadata.path,'WsprryPi-UI/data/wtp-management.js');
    assert.equal(metadata.sha256,'f541c2806195535f80eab600cfdde67feb528fe85133ef6be1eb8751dff84035');
    assert.equal(createHash('sha256').update(bytes).digest('hex'),metadata.sha256,'frozen producer bytes changed');
    return bytes.toString('utf8');
}
async function legacySave(call, producer, edits) {
    const calls=[];
    const b=browser(async request=>{
        const result=await call({method:request.method||'GET',path:request.url,
            body:request.body?JSON.stringify(request.body):'',headers:request.headers||{}});
        calls.push({request,result});return response(result.body,result.status,result.etag);
    },producer);
    b.root.WtpUi.developmentControlsVisible=true;
    const management=await call({method:'GET',path:'/api/v1/member/management'});
    assert.equal(management.status,200,JSON.stringify(management));b.root.managementState=management.body;
    await b.click('wtp-remote-load');
    assert.equal(calls.at(-1).result.status,200,JSON.stringify(calls.at(-1)));
    for(const [field,value] of Object.entries(edits))b.element('wtp-remote-'+field).value=String(value);
    await b.click('wtp-remote-save');
    const writes=calls.filter(({request})=>request.method && request.method!=='GET');
    assert.equal(writes.length,1,'one actual frozen-producer Save required: '+JSON.stringify({calls,feedback:b.element('wtp-management-feedback').textContent}));
    assert(!Object.hasOwn(writes[0].request.body.config,'gpio_drive_ma'),'historical producer must omit new drive field');
    return writes[0].result;
}
async function driveCases(call,{profileOwnedStation=false}={}) {
    const get=resource=>call({method:'GET',path:'/api/v1/member/'+resource});
    const first=await get('gpio-drive');assert.equal(first.status,200,JSON.stringify(first));
    const target=first.body.target, producer=legacyProducer();
    const initial=(await get('configuration')).body.config, original=initial.station;
    let previous=original;
    for(const [index,drive] of [2,4,8,12].entries()) {
        const base=profileOwnedStation?original:previous;
        const station=index===3&&!profileOwnedStation?original:{callsign:base.callsign==='K1ABC'?'K2ABC':'K1ABC',locator:base.locator==='EM18'?'FN42':'EM18',power_dbm:base.power_dbm===20?23:20};
        previous=station;
        const read=await get('gpio-drive');
        const written=await call({method:'PUT',path:'/api/v1/member/gpio-drive',revision:read.etag,
            body:JSON.stringify({schema:'transmitter-gpio-drive/1',target,gpio_drive_ma:drive})});
        assert.equal(written.status,200,JSON.stringify(written));assert.equal(written.body.saved,drive);
        assert.equal(written.body.pad_readback,null,'host fixtures must not claim physical pad readback');
        const beforeRefusal=profileOwnedStation?await call({control:'dump'}):null;
        const saved=await legacySave(call,producer,{callsign:station.callsign,locator:station.locator,power:station.power_dbm});
        if(profileOwnedStation) {
            assert.equal(saved.status,409,JSON.stringify(saved));
            assert.equal(saved.body.error.code,'consumer_setup_identity_required');
            assert.equal(saved.wtp_io,0,'refused station save emits no job traffic');
            const afterRefusal=await call({control:'dump'});
            for(const key of ['bytes_sha256','generation','cursor'])assert.equal(afterRefusal[key],beforeRefusal[key],'refused station save changed '+key);
            assert.deepEqual((await get('configuration')).body.config.station,original);
            const unrelated=await legacySave(call,producer,{expiry:index===3?initial.expires_utc_s:2000000000+drive*120});
            assert.equal(unrelated.status,200,JSON.stringify(unrelated));
        } else {
            assert.equal(saved.status,200,JSON.stringify(saved));
            assert.deepEqual((await get('configuration')).body.config.station,station);
        }
        const after=await get('gpio-drive');assert.equal(after.status,200,JSON.stringify(after));
        assert.equal(after.body.saved,drive,'historical save must retain selected drive');
        const invalid=await call({method:'PUT',path:'/api/v1/member/gpio-drive',revision:after.etag,
            body:JSON.stringify({schema:'transmitter-gpio-drive/1',target,gpio_drive_ma:6})});
        assert.equal(invalid.status,400,JSON.stringify(invalid));
        assert.deepEqual((await get('gpio-drive')).body,after.body,'invalid strength must not mutate saved state');
    }
    console.log('PASS all GPIO drive strengths, frozen producer preservation, profile ownership, invalid refusal and nonphysical readback through production pair');
}
module.exports={driveCases};

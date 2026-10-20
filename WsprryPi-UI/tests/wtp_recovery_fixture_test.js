// SPDX-License-Identifier: MIT
'use strict';
const assert = require('node:assert/strict'), fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const {loadResourceFixtures, planNames} = require('./wtp_recovery_resource_fixtures.js');
const build = fs.mkdtempSync(path.join(os.tmpdir(), 'wtp-recovery-fixtures-'));
const write = (name, schema) => fs.writeFileSync(path.join(build, name), JSON.stringify({application:{schema}}));
try {
    for (const name of ['loaded-engineering_mtls', 'pending-consumer_station', 'fault-engineering_mtls'])
        write(`resources-${name}.json`, 'transmitter-application/1');
    assert.equal(loadResourceFixtures(build).plans.length, 0, 'historical firmware needs no v2 fixtures');
    write('resources-loaded-engineering_mtls.json', 'transmitter-application/99');
    assert.throws(() => loadResourceFixtures(build), /Unexpected loaded application schema/);
    write('resources-loaded-engineering_mtls.json', 'transmitter-application/2');
    assert.throws(() => loadResourceFixtures(build), /ENOENT/, 'current firmware cannot silently skip new-format coverage');
    for (const name of planNames) write(`resources-wspr-${name}.json`, 'transmitter-application/2');
    assert.equal(loadResourceFixtures(build).plans.length, planNames.length);
    fs.unlinkSync(path.join(build, 'resources-wspr-type3.json'));
    assert.throws(() => loadResourceFixtures(build), /resources-wspr-type3/, 'missing current Type 3 fixture fails closed');
    write('resources-wspr-type3.json', 'transmitter-application/1');
    assert.throws(() => loadResourceFixtures(build), /Wrong versioned fixture/);
} finally { fs.rmSync(build, {recursive:true, force:true}); }
console.log('Recovery fixtures: old profile compatible, current complete set required, missing/wrong version fails closed');

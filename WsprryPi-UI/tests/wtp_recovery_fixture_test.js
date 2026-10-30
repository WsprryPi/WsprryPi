// SPDX-License-Identifier: MIT
'use strict';
const assert = require('node:assert/strict'), fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const {loadResourceFixtures, planNames} = require('./wtp_recovery_resource_fixtures.js');
const build = fs.mkdtempSync(path.join(os.tmpdir(), 'wtp-recovery-fixtures-'));
const write = (name, schema) => fs.writeFileSync(path.join(build, name), JSON.stringify({application:{schema}}));
try {
    for (const name of ['loaded-engineering_mtls', 'pending-consumer_station', 'fault-engineering_mtls'])
        write(`resources-${name}.json`, 'application');
    write('resources-loaded-engineering_mtls.json', 'application9');
    assert.throws(() => loadResourceFixtures(build), /Unexpected loaded application schema/);
    write('resources-loaded-engineering_mtls.json', 'application');
    assert.throws(() => loadResourceFixtures(build), /ENOENT/, 'current firmware cannot silently skip new-format coverage');
    for (const name of planNames) write(`resources-wspr-${name}.json`, 'application');
    assert.equal(loadResourceFixtures(build).plans.length, planNames.length);
    fs.unlinkSync(path.join(build, 'resources-wspr-type3.json'));
    assert.throws(() => loadResourceFixtures(build), /resources-wspr-type3/, 'missing current Type 3 fixture fails closed');
    write('resources-wspr-type3.json', 'transmitter-application/4');
    assert.throws(() => loadResourceFixtures(build), /Wrong control fixture/);
} finally { fs.rmSync(build, {recursive:true, force:true}); }
console.log('Recovery fixtures: one current complete set required; missing/obsolete types fail closed');

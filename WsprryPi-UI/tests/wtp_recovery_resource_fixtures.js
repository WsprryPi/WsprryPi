// SPDX-License-Identifier: MIT
'use strict';
const fs = require('node:fs'), path = require('node:path');
const planNames = ['type1', 'ordinary-six', 'type2', 'type3', 'pair', 'w1a', 'w1a-type3', 'ambiguity'];
function loadResourceFixtures(build) {
    const read = name => JSON.parse(fs.readFileSync(path.join(build, name), 'utf8'));
    const fixtures = {
        loaded: read('resources-loaded-engineering_mtls.json'),
        consumer: read('resources-pending-consumer_station.json'),
        fault: read('resources-fault-engineering_mtls.json'),
        plans: []
    };
    const schema = fixtures.loaded.application.schema;
    if (!['transmitter-application/1', 'transmitter-application/2'].includes(schema)) throw new Error('Unexpected loaded application schema');
    if (schema === 'transmitter-application/2') {
        fixtures.plans = planNames.map(name => {
            const saved = read(`resources-wspr-${name}.json`);
            if (saved.application.schema !== 'transmitter-application/2') throw new Error(`Wrong versioned fixture: ${name}`);
            return [name, saved];
        });
    }
    return fixtures;
}
module.exports = {loadResourceFixtures, planNames};

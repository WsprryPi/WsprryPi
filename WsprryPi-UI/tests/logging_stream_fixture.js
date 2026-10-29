// SPDX-License-Identifier: MIT
// The real PHP adapter owns replay, scope checks, normalization and child
// cleanup. Only its journalctl input is deterministic; no system journal is read.
"use strict";
const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http');
const {spawnFixtureChild, waitForSpawn, stopIsolatedProcess} = require('./browser_fixture_process.js');

async function createProductionStream(root) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'logging-stream-'));
    const input = path.join(directory, 'journal.json');
    fs.writeFileSync(input, '[]');
    fs.copyFileSync(path.join(__dirname, 'fixtures/logging_journalctl.py'), path.join(directory, 'journalctl'));
    fs.chmodSync(path.join(directory, 'journalctl'), 0o700);
    const probe = http.createServer();
    await new Promise(resolve => probe.listen(0, '127.0.0.1', resolve));
    const port = probe.address().port;
    await new Promise(resolve => probe.close(resolve));
    const php = spawnFixtureChild(process.env.PHP_BIN || 'php', ['-S', `127.0.0.1:${port}`, '-t', root], {
        detached:process.platform !== 'win32',
        env:{...process.env, PHP_CLI_SERVER_WORKERS:'16', PATH:directory + path.delimiter + process.env.PATH, LOGGING_JOURNAL_FIXTURE:input}
    });
    await waitForSpawn(php);
    try {
        let ready = false;
        for (let i = 0; i < 100 && !ready; i++) {
            try { ready = (await fetch(`http://127.0.0.1:${port}/log_origin.php`)).ok; } catch (_) {}
            if (!ready) await new Promise(resolve => setTimeout(resolve, 20));
        }
        if (!ready) throw Error('PHP fixture did not start');
        return {
            forward(req, res, rows) {
                const journal = rows.map(row => ({
                    __CURSOR:row.__CURSOR, __REALTIME_TIMESTAMP:'1793098800123456', PRIORITY:String(row.PRIORITY || 6),
                    MESSAGE:row.MESSAGE, _SYSTEMD_UNIT:row.ingress === 'udp' ? 'wsprrypi-log.service' : 'wsprrypi.service',
                    _HOSTNAME:'fixture', WSPRRY_ORIGIN_KIND:row.origin_kind,
                    WSPRRY_ORIGIN_ID:row.origin_id, WSPRRY_ORIGIN_NAME:row.origin_name,
                    WSPRRY_ORIGIN_BOOT:row.origin_boot, WSPRRY_ORIGIN_SEQUENCE:row.origin_sequence,
                    WSPRRY_ORIGIN_UTC_NS:row.origin_utc_ns, WSPRRY_INGRESS:row.ingress || (row.origin_kind === 'pico' ? 'usb' : 'native'),
                    WSPRRY_PEER_SLOT:row.peer_slot, WSPRRY_PEER_IPV4:row.peer_ipv4, WSPRRY_FORWARDERS:row.forwarders
                }));
                const temporary = input + '.new';
                fs.writeFileSync(temporary, JSON.stringify(journal)); fs.renameSync(temporary, input);
                const upstream = http.get(`http://127.0.0.1:${port}${req.url}&heartbeat=5`, response => {
                    res.writeHead(response.statusCode, response.headers); response.pipe(res);
                });
                upstream.on('error', error => res.destroy(error));
                res.on('close', () => upstream.destroy());
            },
            async close() { await stopIsolatedProcess(php); fs.rmSync(directory, {recursive:true, force:true}); }
        };
    } catch (error) {
        await stopIsolatedProcess(php); fs.rmSync(directory, {recursive:true, force:true}); throw error;
    }
}
module.exports = {createProductionStream};

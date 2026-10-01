// Read-only local-transmitter status and lifecycle notifications during the batch.
const fs = require('node:fs');
const WebSocket = require(process.argv[4]);
const stream = fs.createWriteStream(process.argv[2], {flags: 'wx'});
const deadline = Number(process.argv[3]) * 1000;
const socket = new WebSocket('ws://192.168.1.54:31416/',
    {origin: 'http://192.168.1.54:31416'});
let connected = false;
let polling;
function record(event, value) {
    stream.write(JSON.stringify({utc_ns: String(BigInt(Date.now()) * 1000000n), event, value}) + '\n');
}
socket.on('open', () => {
    connected = true;
    record('connected', {});
    polling = setInterval(() => {
        if (socket.readyState === WebSocket.OPEN)
            socket.send(JSON.stringify({command: 'get_tx_state'}));
    }, 1000);
});
socket.on('message', raw => {
    try { record('message', JSON.parse(raw.toString())); }
    catch { record('invalid_message', raw.toString()); }
});
socket.on('error', error => record('error', error.message));
socket.on('close', (code, reason) => {
    clearInterval(polling);
    clearTimeout(deadlineTimer);
    record('closed', {code, reason: reason.toString()});
    stream.end();
    if (!connected || Date.now() < deadline - 3000) process.exitCode = 1;
});
const deadlineTimer = setTimeout(() => socket.close(), Math.max(1, deadline - Date.now()));

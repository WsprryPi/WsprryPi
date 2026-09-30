"""Inspect an installed WTP endpoint; optionally claim/release without a job."""
import argparse
import json
import socket
import struct
import time
import urllib.request
import uuid
from pathlib import Path


def crc32c(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff


def endpoint(host):
    request = urllib.request.Request(
        f'http://{host}:31415/api/v1/host/wtp-endpoint',
        headers={'X-WsprryPico-Request': '1', 'Origin': f'http://{host}:31415'})
    with urllib.request.urlopen(request, timeout=15) as response:
        return json.load(response)


class Wire:
    def __init__(self, host):
        self.sock = socket.create_connection((host, 31417), 5)
        self.sock.settimeout(8)
        self.session = uuid.uuid4().hex
        self.owner = uuid.uuid4().hex
        self.messages = []

    def read(self, count):
        data = b''
        while len(data) < count:
            part = self.sock.recv(count - len(data))
            if not part:
                raise RuntimeError('WTP peer closed the connection')
            data += part
        return data

    def request(self, op, body=None):
        request_id = uuid.uuid4().hex
        message = {'type': 'request', 'protocol': 'WTP/1',
                   'session_id': self.session, 'request_id': request_id,
                   'op': op, 'body': body or {}}
        payload = json.dumps(message, separators=(',', ':')).encode()
        self.sock.sendall(b'WTPF\x01\x01\0\0' + struct.pack('!II', len(payload), crc32c(payload)) + payload)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            header = self.read(16)
            assert header[:8] == b'WTPF\x01\x01\0\0', header
            count, crc = struct.unpack('!II', header[8:])
            assert 0 < count <= 65536, count
            raw = self.read(count)
            assert crc32c(raw) == crc
            reply = json.loads(raw)
            self.messages.append(reply)
            if reply.get('type') == 'response' and reply.get('request_id') == request_id:
                assert reply.get('ok'), reply
                return reply
        raise RuntimeError('WTP reply deadline exceeded')


parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
parser.add_argument('--phase', required=True)
parser.add_argument('--claim', action='store_true')
parser.add_argument('--output', required=True)
args = parser.parse_args()
record = {'phase': args.phase, 'host': args.host, 'passed': False,
          'captured_utc_ns': str(time.time_ns()), 'job_requests': 0}
wire = None
claimed = False
try:
    record['before'] = endpoint(args.host)
    assert record['before']['listener_running'] and record['before']['dns_sd_published']
    assert not record['before']['local_requested'] and not record['before']['output_unknown']
    assert not record['before']['remote_owner'] and not record['before']['remote_output_active']
    wire = Wire(args.host)
    record['hello'] = wire.request('HELLO', {'versions': ['WTP/1'], 'client_name': 'Clean install acceptance', 'client_version': '1'})
    for op in ['CAPS', 'STATUS', 'GET_CLOCK']:
        record[op.lower()] = wire.request(op)
    assert record['caps']['body']['engine'] == 'si5351', record['caps']
    if args.claim:
        record['claim'] = wire.request('CLAIM', {'owner_id': wire.owner, 'lease_ms': 60000})
        claimed = True
        record['owned'] = endpoint(args.host)
        assert record['owned']['remote_owner'] == wire.owner, record['owned']
        record['release'] = wire.request('RELEASE')
        claimed = False
    record['after'] = endpoint(args.host)
    assert not record['after']['remote_owner'] and not record['after']['remote_job_id']
    assert not record['after']['remote_output_active'] and not record['after']['output_unknown']
    record['passed'] = True
finally:
    if wire is not None:
        if claimed:
            try:
                record['cleanup_release'] = wire.request('RELEASE')
            except Exception as error:
                record['cleanup_error'] = repr(error)
        record['wire_messages'] = wire.messages
        wire.sock.close()
    Path(args.output).write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({'phase': args.phase, 'passed': record['passed'], 'job_requests': 0}))

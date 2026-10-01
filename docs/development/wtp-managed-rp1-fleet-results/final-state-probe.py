"""Read HELLO/STATUS on the six bench endpoints; never claim or submit a job."""
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


if __name__ == '__main__':
    import sys
    targets = [('wspr1', '192.168.1.44'), ('wspr2', '192.168.1.123'),
               ('wspr4', '192.168.1.120'), ('wspr5', '192.168.1.54'),
               ('Pico A', 'wsprrypico-0a60df.local'),
               ('Pico B', 'wsprrypico-0a9d89.local')]
    results = []
    for name, host in targets:
        wire = Wire(host)
        try:
            hello = wire.request('HELLO', {'versions': ['WTP/1'],
                'client_name': 'fleet-final-inspection', 'client_version': '1'})['body']
            status = wire.request('STATUS')['body']
            assert status['state'] == 'empty' and not status['output_active']
            assert not status['owner_id'] and not status['job_id']
            results.append({'name': name, 'hello': hello, 'status': status})
        finally:
            wire.sock.close()
    Path(sys.argv[1]).write_text(json.dumps(results, indent=2) + '\n')
    print('Six endpoints independently report empty, unowned and output inactive')

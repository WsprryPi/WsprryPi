#!/usr/bin/env python3
"""Bounded WTP/1 Pi validation. Mutating job cases require --execute.

Use --allow-rf only after operator RF authorization. This records protocol and
HTTP observations; SDR visibility must be evaluated separately.
"""
import argparse
import json
import socket
import struct
import time
import urllib.error
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


class Wtp:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), 5)
        self.sock.settimeout(8)
        self.session = uuid.uuid4().hex
        self.owner = uuid.uuid4().hex
        self.records = []
        self.hello = self.request('HELLO', {'versions': ['WTP/1'], 'client_name': 'Pi validation', 'client_version': '1'})

    def read(self, count):
        result = b''
        while len(result) < count:
            part = self.sock.recv(count - len(result))
            if not part:
                raise RuntimeError('WTP peer closed the connection')
            result += part
        return result

    def request(self, op, body=None, okay=True, request_id=None):
        rid = request_id or uuid.uuid4().hex
        value = {'type': 'request', 'protocol': 'WTP/1', 'session_id': self.session,
                 'request_id': rid, 'op': op, 'body': body or {}}
        payload = json.dumps(value, separators=(',', ':')).encode()
        self.sock.sendall(b'WTPF\x01\x01\0\0' + struct.pack('!II', len(payload), crc32c(payload)) + payload)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            header = self.read(16)
            assert header[:8] == b'WTPF\x01\x01\0\0', header
            count, crc = struct.unpack('!II', header[8:])
            assert 0 < count <= 65536, count
            raw = self.read(count)
            assert crc32c(raw) == crc
            response = json.loads(raw)
            self.records.append({'at': time.time(), 'message': response})
            if response.get('type') == 'response' and response.get('request_id') == rid:
                if okay:
                    assert response.get('ok'), response
                return response
        raise RuntimeError('WTP reply deadline exceeded')

    def close(self):
        self.sock.close()


def http(host, port, method, path, body=None, revision=None):
    if method == 'PUT' and path == 'config' and not revision:
        status, current_headers, current = http(host, port, 'GET', path)
        assert status == 200, current
        revision = next(v for k, v in current_headers.items() if k.lower() == 'etag')
    headers = {'X-WsprryPico-Request': '1', 'Content-Type': 'application/json',
               'Origin': f'http://{host}:{port}'}
    if revision:
        headers['If-Match'] = revision
    request = urllib.request.Request(f'http://{host}:{port}/api/v1/host/{path}',
                                     json.dumps(body).encode() if body is not None else None,
                                     headers, method=method)
    try:
        response = urllib.request.urlopen(request, timeout=20)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, dict(response.headers), json.load(response)


def run(args):
    endpoint = lambda: http(args.host, args.http_port, 'GET', 'wtp-endpoint')[2]
    result = {'host': args.host, 'port': args.port, 'case': args.case,
              'started_at': time.time(), 'before': endpoint()}
    wire = Wtp(args.host, args.port)
    try:
        result['hello'] = wire.hello
        result['caps'] = wire.request('CAPS')
        result['status'] = wire.request('STATUS')
        result['clock'] = wire.request('GET_CLOCK')
        if args.case == 'inspect':
            return result
        assert args.execute, 'Job cases require --execute'
        assert args.allow_rf or 'simulated' in json.dumps(result['caps']), 'Physical endpoint requires --allow-rf'
        status, _, _ = http(args.host, args.http_port, 'PUT', 'config', {'Operation': {'Transmit': False}})
        assert status == 200, status
        wire.request('CLAIM', {'owner_id': wire.owner, 'lease_ms': 60000})
        job = uuid.uuid4().hex
        result['chosen'] = {'frequency_hz': args.frequency, 'duration_s': args.duration, 'job_id': job}
        duration = round(args.duration * 1_000_000_000)
        # RF-event fixtures exercise backend execution, not message encoding.
        # The WSPR fixture contains all four tones; it is not a callsign frame.
        pattern = {'tone': [0], 'qrss': [0, None, 0, None, 0],
                   'fskcw': [0, 1, 0, 1, 0], 'dfcw': [0, None, 1, None, 0],
                   'wspr': [i % 4 for i in range(162)]}[args.mode]
        events = []
        for i, tone in enumerate(pattern):
            offset = duration * i // len(pattern)
            event = {'offset_ns': str(offset),
                     'duration_ns': str(duration * (i + 1) // len(pattern) - offset),
                     'rf_on': tone is not None}
            if tone is not None:
                step = 1_464_843_750 if args.mode == 'wspr' else 4_000_000_000
                event['frequency_nhz'] = str(args.frequency * 1_000_000_000 + tone * step)
            events.append(event)
        result['chosen']['mode'] = args.mode
        result['chosen']['event_count'] = len(events)
        loaded = wire.request('LOAD', {'job_id': job, 'profile': 'rf-events/1', 'mode': args.mode,
                         'total_duration_ns': str(duration), 'allow_frequency_adjustment': True,
                         'events': events})
        result['load'] = loaded
        start = (int(time.time()) + 5) * 1_000_000_000
        result['arm'] = wire.request('ARM', {'job_id': job, 'start_utc_ns': str(start),
                                            'max_start_uncertainty_ns': '500000000'})
        result['chosen']['start_utc_ns'] = str(start)
        while time.time_ns() < start + 300_000_000:
            time.sleep(.05)
        state = endpoint()
        result['running'] = state
        assert state['remote_state'] == 'running' and state['remote_output_active'], state
        if args.case in ('end-now', 'finish', 'http-enable'):
            status, headers, _ = http(args.host, args.http_port, 'GET', 'config')
            assert status == 200
            revision = next(v for k, v in headers.items() if k.lower() == 'etag')
            if args.case == 'http-enable':
                status, _, reply = http(args.host, args.http_port, 'PUT', 'config',
                                       {'Operation': {'Transmit': True}}, revision)
            else:
                command = {'choice': 'finish_current' if args.case == 'finish' else 'end_now',
                           'confirmed': False, 'observed_owner': '', 'observed_job': ''}
                status, _, reply = http(args.host, args.http_port, 'POST', 'wtp-endpoint/enable', command, revision)
                assert status == 409 and reply['error']['code'] == 'local_takeover_confirmation_required', reply
                assert endpoint()['remote_owner'] == state['remote_owner'], 'unconfirmed action changed ownership'
                command.update(confirmed=True, observed_owner=state['remote_owner'], observed_job=state['remote_job_id'])
                status, _, reply = http(args.host, args.http_port, 'POST', 'wtp-endpoint/enable', command, revision)
            result['takeover'] = {'status': status, 'body': reply}
            assert status == 200, reply
            immediate = endpoint()
            result['immediate'] = immediate
            if args.case == 'finish':
                assert immediate['finishing_remote_job'] and not immediate['local_effective'], immediate
            else:
                assert not immediate['remote_owner'] and not immediate['remote_output_active'], immediate
        deadline = time.monotonic() + args.duration + 8
        renew_at = time.monotonic() + 15
        while time.monotonic() < deadline:
            if time.monotonic() >= renew_at:
                wire.request('RENEW', {'owner_id': wire.owner, 'lease_ms': 60000})
                renew_at = time.monotonic() + 15
            state = endpoint()
            if not state['remote_output_active'] and state['remote_state'] not in ('armed', 'running'):
                break
            time.sleep(.1)
        result['after'] = state
        assert not state['remote_output_active'] and not state['output_unknown'], state
        if args.case == 'job':
            assert state['remote_state'] == 'complete', state
        if args.case == 'finish':
            assert state['local_effective'] and not state['remote_owner'], state
        return result
    except Exception as error:
        result['failure'] = str(error)
        raise
    finally:
        if args.execute:
            # Always leave the test assignment released and local schedule off.
            try:
                if result.get('chosen', {}).get('job_id'):
                    wire.request('ABORT', {'job_id': result['chosen']['job_id']}, okay=False)
                wire.request('RELEASE', okay=False)
            except Exception as error:
                result['wire_cleanup_error'] = str(error)
            try:
                status, _, reply = http(args.host, args.http_port, 'PUT', 'config', {'Operation': {'Transmit': False}})
                result['cleanup'] = {'http_status': status, 'endpoint': endpoint()}
            except Exception as error:
                result['http_cleanup_error'] = str(error)
        result['wire'] = wire.records
        wire.close()
        Path(args.output).write_text(json.dumps(result, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=31417)
    parser.add_argument('--http-port', type=int, default=31415)
    parser.add_argument('--case', choices=['inspect', 'job', 'end-now', 'finish', 'http-enable'], default='inspect')
    parser.add_argument('--frequency', type=int, default=14097100)
    parser.add_argument('--duration', type=float, default=3,
                        help='Seconds, from 1 through 120; standard WSPR timing is 110.592')
    parser.add_argument('--mode', choices=['tone','wspr','qrss','fskcw','dfcw'], default='tone')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--allow-rf', action='store_true')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 1 <= args.duration <= 120:
        parser.error('--duration must be from 1 through 120 seconds')
    record = run(args)
    Path(args.output).write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'case': args.case, 'passed': True, 'output': args.output}))


if __name__ == '__main__':
    main()

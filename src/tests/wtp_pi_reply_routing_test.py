#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Root-only, hardware-free routing acceptance in three private namespaces."""
import argparse
import json
import os
from pathlib import Path
import re
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid

SERVER = '192.168.83.10'
WIFI = '192.168.83.11'
CLIENT = '192.168.83.20'
PORT = 31417


def command(*args):
    try:
        return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT, timeout=10).strip()
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f'{args}: {error.output}') from error


def crc32c(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff


class Connection:
    def __init__(self, address=SERVER, port=PORT):
        self.sock = socket.create_connection((address, port), timeout=3)
        self.session = uuid.uuid4().hex
        self.hello = self.request('HELLO', {'versions': ['WTP/1'],
                                          'client_name': 'routing-test', 'client_version': '1'})

    def receive(self, count):
        result = bytearray()
        while len(result) < count:
            chunk = self.sock.recv(count-len(result))
            if not chunk:
                raise RuntimeError('WTP connection closed')
            result.extend(chunk)
        return bytes(result)

    def request(self, op, body=None):
        request_id = uuid.uuid4().hex
        data = json.dumps({'type': 'request', 'protocol': 'WTP/1', 'session_id': self.session,
                           'request_id': request_id, 'op': op, 'body': body or {}},
                          separators=(',', ':')).encode()
        self.sock.sendall(b'WTPF\x01\x01\0\0'+struct.pack('!II', len(data), crc32c(data))+data)
        deadline = time.monotonic()+4
        while time.monotonic() < deadline:
            header = self.receive(16)
            assert header[:8] == b'WTPF\x01\x01\0\0', 'invalid WTP header'
            length, checksum = struct.unpack('!II', header[8:])
            assert 0 < length <= 65536, 'unbounded WTP frame'
            payload = self.receive(length)
            assert crc32c(payload) == checksum, 'WTP CRC mismatch'
            response = json.loads(payload)
            if response.get('type') == 'event':
                continue
            assert response['request_id'] == request_id and response['op'] == op and response['ok'], response
            return response['body']
        raise RuntimeError('WTP response deadline')

    def close(self):
        self.sock.close()


def client_mode():
    peer = Connection()
    print(json.dumps({'hello': peer.hello, 'status': peer.request('STATUS')}), flush=True)
    try:
        for line in sys.stdin:
            if line.strip() == 'NEW':
                fresh = Connection()
                try:
                    result = {'hello': fresh.hello, 'status': fresh.request('STATUS')}
                finally:
                    fresh.close()
            else:
                result = {'status': peer.request('STATUS')}
            print(json.dumps(result), flush=True)
    finally:
        peer.close()


def line_from(child, timeout=5):
    with selectors.DefaultSelector() as selector:
        selector.register(child.stdout, selectors.EVENT_READ)
        assert selector.select(timeout), 'fixture startup/response deadline'
        line = child.stdout.readline()
    assert line, f'fixture exited: {child.poll()}'
    return line


def stop(child):
    if child.poll() is None:
        if child.stdin:
            child.stdin.close()
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=3)
    if child.stdout:
        child.stdout.close()


def main(probe):
    assert sys.platform == 'linux' and os.geteuid() == 0, 'requires Linux root for private namespaces'
    suffix = uuid.uuid4().hex[:8]
    server, client, bridge = ['wtpr-'+suffix+'-'+x for x in ('s', 'c', 'b')]
    namespaces = []
    children = []
    evidence = {}
    with tempfile.TemporaryDirectory(prefix='wtp-routing-') as directory:
        def ip(ns, *args):
            return command('ip', '-n', ns, *args)

        def run(ns, *args):
            return command('ip', 'netns', 'exec', ns, *args)

        def routes(first):
            for dev in ('eth0', 'wifi0'):
                ip(server, 'route', 'del', '192.168.83.0/24', 'dev', dev, 'metric', '100')
            for dev in (first, 'wifi0' if first == 'eth0' else 'eth0'):
                source = SERVER if dev == 'eth0' else WIFI
                ip(server, 'route', 'append', '192.168.83.0/24', 'dev', dev, 'src', source, 'metric', '100')
            route = json.loads(ip(server, '-j', 'route', 'get', CLIENT, 'from', SERVER))[0]
            assert route['dev'] == first, route
            return route

        def start_capture(label):
            path = Path(directory)/(label+'.txt')
            out = path.open('w')
            child = subprocess.Popen(['ip', 'netns', 'exec', server, 'tcpdump', '-l', '-n', '-i', 'any',
                                      'tcp', 'port', str(PORT)], stdout=out, stderr=subprocess.PIPE, bufsize=0)
            children.append(child)
            with selectors.DefaultSelector() as selector:
                selector.register(child.stderr, selectors.EVENT_READ)
                deadline = time.monotonic()+3
                while True:
                    assert selector.select(max(0, deadline-time.monotonic())), 'capture startup deadline'
                    if b'listening on' in child.stderr.readline():
                        break
            return child, out, path

        def finish_capture(capture):
            child, out, path = capture
            child.send_signal(signal.SIGINT)
            child.wait(timeout=3)
            out.close()
            child.stderr.close()
            text = path.read_text()
            outgoing = re.findall(r'\s(\S+)\s+Out\s+IP\s'+re.escape(SERVER)+r'\.'+str(PORT)+r'\s*>', text)
            assert outgoing, text
            return {'reply_interfaces': outgoing, 'packets': text}

        try:
            for ns in (server, client, bridge):
                command('ip', 'netns', 'add', ns)
                namespaces.append(ns)
                ip(ns, 'link', 'set', 'lo', 'up')
            ip(bridge, 'link', 'add', 'lan', 'type', 'bridge')
            ip(bridge, 'link', 'set', 'lan', 'up')
            for ns, dev, peer in ((server, 'eth0', 'e'), (server, 'wifi0', 'w'), (client, 'eth0', 'c')):
                ip(ns, 'link', 'add', dev, 'type', 'veth', 'peer', 'name', peer, 'netns', bridge)
                ip(ns, 'link', 'set', dev, 'up')
                ip(bridge, 'link', 'set', peer, 'master', 'lan')
                ip(bridge, 'link', 'set', peer, 'up')
            for dev, address in (('eth0', SERVER), ('wifi0', WIFI)):
                ip(server, 'addr', 'add', address+'/24', 'dev', dev, 'noprefixroute')
                ip(server, 'route', 'append', '192.168.83.0/24', 'dev', dev, 'src', address, 'metric', '100')
            ip(client, 'addr', 'add', CLIENT+'/24', 'dev', 'eth0')
            # Pin destination MACs so ARP flux cannot substitute the ingress path.
            server_mac = json.loads(ip(server, '-j', 'link', 'show', 'eth0'))[0]['address']
            client_mac = json.loads(ip(client, '-j', 'link', 'show', 'eth0'))[0]['address']
            ip(client, 'neigh', 'replace', SERVER, 'lladdr', server_mac, 'nud', 'permanent', 'dev', 'eth0')
            for dev in ('eth0', 'wifi0'):
                ip(server, 'neigh', 'replace', CLIENT, 'lladdr', client_mac, 'nud', 'permanent', 'dev', dev)
            for ns in (server, client):
                for dev in ('all', 'default', 'eth0') + (('wifi0',) if ns == server else ()):
                    run(ns, 'sysctl', '-q', '-w', 'net.ipv4.conf.'+dev+'.rp_filter=0')
            evidence['baseline_route'] = routes('wifi0')
            capture = start_capture('baseline')
            baseline = subprocess.Popen(['ip', 'netns', 'exec', server, probe, SERVER, 'eth0', str(PORT),
                                         '--baseline-address-only'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            children.append(baseline)
            assert line_from(baseline).strip() == 'READY '+str(PORT)
            baseline_client = subprocess.Popen(['ip', 'netns', 'exec', client, sys.executable, __file__, '--client'],
                                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            children.append(baseline_client)
            evidence['baseline_inspection'] = json.loads(line_from(baseline_client))
            stop(baseline_client); stop(baseline)
            evidence['baseline_capture'] = finish_capture(capture)
            assert 'wifi0' in evidence['baseline_capture']['reply_interfaces'], 'baseline did not reproduce misrouting'
            evidence['initial_route'] = routes('eth0')
            capture = start_capture('fixed')
            fixed = subprocess.Popen(['ip', 'netns', 'exec', server, probe, SERVER, 'eth0', str(PORT)],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            children.append(fixed)
            assert line_from(fixed).strip() == 'READY '+str(PORT)
            peer = subprocess.Popen(['ip', 'netns', 'exec', client, sys.executable, __file__, '--client'],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            children.append(peer)
            evidence['initial_inspection'] = json.loads(line_from(peer))
            evidence['reordered_route'] = routes('wifi0')
            for phase in ('STATUS', 'NEW'):
                peer.stdin.write(phase+'\n'); peer.stdin.flush()
                result = json.loads(line_from(peer))
                assert result['status']['state'] == 'empty' and not result['status']['output_active']
                assert result['status']['boot_id'] == evidence['initial_inspection']['status']['boot_id']
                evidence[phase.lower()+'_after_reorder'] = result
            stop(peer); stop(fixed)
            evidence['fixed_capture'] = finish_capture(capture)
            assert set(evidence['fixed_capture']['reply_interfaces']) == {'eth0'}, evidence['fixed_capture']
            print(json.dumps(evidence, indent=2))
        finally:
            for child in reversed(children):
                stop(child)
            for ns in reversed(namespaces):
                command('ip', 'netns', 'del', ns)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe')
    parser.add_argument('--client', action='store_true')
    options = parser.parse_args()
    if options.client:
        client_mode()
    else:
        assert options.probe
        main(str(Path(options.probe).resolve()))

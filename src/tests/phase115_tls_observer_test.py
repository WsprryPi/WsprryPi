#!/usr/bin/env python3
"""Linux-only loopback validation of the private Phase 11.5 OpenSSL observer."""
import argparse
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading


def require(value, message):
    if not value: raise ValueError(message)


def decode(data):
    rows = []; active = {}; next_id = 1; last = 0
    while data:
        require(len(data) >= 64, 'Truncated record header')
        magic, sequence, kind, identity, mono, utc, pid, length = struct.unpack('>8s7Q', data[:64])
        require(magic == b'P115TLS1' and sequence == len(rows) and mono >= last and
                pid > 0 and utc > 0 and length <= 65536 and len(data) >= 64+length,
                'Record identity, sequence, time or size')
        payload = data[64:64+length]; data = data[64+length:]; last = mono
        if not rows: require(kind == identity == length == 0, 'Missing observer start')
        elif kind == 1:
            require(identity == next_id and length == 0, 'Connection identity reused')
            active[identity] = False; next_id += 1
        elif kind == 2:
            require(identity in active and length == 32, 'Certificate record')
            active[identity] = True
        elif kind in (3,4):
            require(active.get(identity) is True and length > 0, 'Unauthenticated or empty I/O record')
        elif kind == 5:
            require(identity in active and length == 0, 'Unknown connection close')
            del active[identity]
        elif kind == 6:
            require(identity == length == 0 and not data and not active, 'Incomplete observer finish')
        else: raise ValueError('Unexpected observer record')
        rows.append(dict(sequence=sequence,kind=kind,connection_id=identity,
                         monotonic_ns=mono,utc_ns=utc,pid=pid,payload=payload))
    require(rows and rows[-1]['kind'] == 6, 'Missing final record')
    require(len({row['pid'] for row in rows}) == 1, 'Mixed process identity')
    return rows


def child(directory):
    server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server.minimum_version = server.maximum_version = ssl.TLSVersion.TLSv1_3
    server.load_cert_chain(directory/'cert.pem', directory/'key.pem')
    server.load_verify_locations(cafile=str(directory/'cert.pem'))
    server.verify_mode = ssl.CERT_REQUIRED
    client = ssl.create_default_context(cafile=str(directory/'cert.pem'))
    client.minimum_version = client.maximum_version = ssl.TLSVersion.TLSv1_3
    client.load_cert_chain(directory/'cert.pem', directory/'key.pem')
    listener = socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(8)
    listener.settimeout(10); address = listener.getsockname()
    errors = []; accepted = []
    message = bytes(range(256))*32
    def handle(raw):
        try:
            with server.wrap_socket(raw,server_side=True) as stream:
                stream.settimeout(10); data = b''
                while len(data) < len(message): data += stream.recv(317)
                require(data == message, 'Server plaintext differs')
                for start in range(0,len(data),509): stream.sendall(data[start:start+509])
        except BaseException as error: errors.append(error)
    def serve():
        try:
            for _ in range(4):
                raw,_ = listener.accept(); thread = threading.Thread(target=handle,args=(raw,))
                thread.start(); accepted.append(thread)
        except BaseException as error: errors.append(error)
    def request():
        try:
            with client.wrap_socket(socket.create_connection(address,timeout=10),
                                    server_hostname='localhost') as stream:
                stream.settimeout(10)
                for start in range(0,len(message),271): stream.sendall(message[start:start+271])
                data = b''
                while len(data) < len(message):
                    part = stream.recv(193);require(part,'Client EOF');data += part
                require(data == message, 'Client plaintext differs')
        except BaseException as error: errors.append(error)
    server_thread = threading.Thread(target=serve);server_thread.start()
    clients = [threading.Thread(target=request) for _ in range(4)]
    for thread in clients: thread.start()
    for thread in clients: thread.join(15);require(not thread.is_alive(),'Client deadline')
    server_thread.join(15);require(not server_thread.is_alive(),'Server deadline')
    for thread in accepted: thread.join(15);require(not thread.is_alive(),'Server stream deadline')
    listener.close();require(not errors, str(errors))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observer',type=Path)
    parser.add_argument('--child',type=Path)
    args=parser.parse_args()
    if args.child: child(args.child);return
    require(sys.platform == 'linux' and args.observer and args.observer.is_file(),
            'Linux observer library required')
    with tempfile.TemporaryDirectory(prefix='phase115-observer-test-') as directory:
        root=Path(directory); path=root/'events.bin'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1',
                        '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
                        '-keyout',str(root/'key.pem'),'-out',str(root/'cert.pem')],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=30)
        subprocess.run([sys.executable,str(Path(__file__).resolve()),'--child',str(root)],
                       env=dict(os.environ,LD_PRELOAD=str(args.observer.resolve()),
                                PHASE115_TLS_LOG=str(path)),check=True,timeout=60)
        data=path.read_bytes(); rows=decode(data)
        connections={row['connection_id'] for row in rows if row['kind']==1}
        require(len(connections)==8,'Missing concurrent streams')
        for identity in connections:
            for kind in (3,4):
                body=b''.join(row['payload'] for row in rows
                              if row['connection_id']==identity and row['kind']==kind)
                require(body==bytes(range(256))*32,'Reconstructed observer bytes differ')
        for mutation in (data[:-1],data[64:],data[:30],data[:-64],
                         data[:8]+struct.pack('>Q',999)+data[16:],
                         data[:56]+struct.pack('>Q',65537)+data[64:]):
            try: decode(mutation)
            except ValueError: pass
            else: raise ValueError('Invalid observer evidence accepted')
        print(f'Observer PASS: {len(rows)} durable records, eight concurrent TLS streams; six corruptions rejected')


if __name__ == '__main__': main()

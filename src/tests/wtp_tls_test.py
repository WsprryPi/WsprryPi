#!/usr/bin/env python3
"""Parent TLS adapter against controlled loopback TLS peers; ephemeral credentials."""
import contextlib
import os
import pathlib
import socket
import ssl
import subprocess
import sys
import threading
import time

build = pathlib.Path(sys.argv[1]).resolve()
credentials = build / 'tls-fixtures'
os.umask(0o077)
credentials.mkdir(parents=True, exist_ok=True)

def openssl(*args):
    subprocess.run(['openssl', *args], cwd=credentials, check=True, capture_output=True)

openssl('req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
        '-keyout', 'ca.key', '-out', 'ca.crt', '-days', '2', '-subj', '/CN=Temporary host tests',
        '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
for name, usage in [('server', 'serverAuth'), ('client', 'clientAuth'), ('other', 'clientAuth'),
                    ('expired', 'serverAuth'), ('future', 'serverAuth'),
                    ('expired-client', 'clientAuth'), ('future-client', 'clientAuth')]:
    openssl('req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
            '-keyout', name + '.key', '-out', name + '.csr', '-subj', '/CN=' + name)
    (credentials / (name + '.ext')).write_text('basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=' + usage + '\nsubjectAltName=IP:127.0.0.1,DNS:localhost,DNS:pico-test.local\n')
    openssl('x509', '-req', '-in', name + '.csr', '-CA', 'ca.crt', '-CAkey', 'ca.key', '-CAcreateserial',
            '-out', name + '.crt', '-days', '1', '-extfile', name + '.ext')
for name, san in [('dns-only', 'DNS:pico-test.local'), ('cn-only', ''), ('wildcard', 'DNS:*.local')]:
    openssl('req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
            '-keyout', name + '.key', '-out', name + '.csr', '-subj', '/CN=pico-test.local')
    (credentials / (name + '.ext')).write_text('basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n' + ('subjectAltName=' + san + '\n' if san else ''))
    openssl('x509', '-req', '-in', name + '.csr', '-CA', 'ca.crt', '-CAkey', 'ca.key', '-CAcreateserial',
            '-out', name + '.crt', '-days', '1', '-extfile', name + '.ext')
for name in ['rogue', 'rogue-client']:
    openssl('req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
            '-keyout', name + '.key', '-out', name + '.crt', '-days', '1', '-subj', '/CN=Untrusted',
            '-addext', 'subjectAltName=IP:127.0.0.1', '-addext', 'extendedKeyUsage=' + ('clientAuth' if name.endswith('client') else 'serverAuth'))
(credentials / 'index.txt').write_text('')
(credentials / 'serial').write_text('1000\n')
(credentials / 'ca.cnf').write_text('[ca]\ndefault_ca=test\n[test]\ndatabase=index.txt\nnew_certs_dir=.\nserial=serial\ncertificate=ca.crt\nprivate_key=ca.key\ndefault_md=sha256\npolicy=subject\n[subject]\ncommonName=supplied\n')
for name in ['expired', 'future', 'expired-client', 'future-client']:
    start, end = ('20200101000000Z', '20210101000000Z') if name.startswith('expired') else ('20900101000000Z', '20910101000000Z')
    openssl('ca', '-batch', '-config', 'ca.cnf', '-in', name + '.csr', '-out', name + '.crt',
            '-startdate', start, '-enddate', end, '-extfile', name + '.ext', '-notext')

class Server:
    def __init__(self, certificate='server', protocol='http/1.1', tls12=False, stall=False, abrupt=False, large=False, backpressure=False):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = self.context.maximum_version = ssl.TLSVersion.TLSv1_2 if tls12 else ssl.TLSVersion.TLSv1_3
        self.context.load_cert_chain(credentials / (certificate + '.crt'), credentials / (certificate + '.key'))
        self.context.load_verify_locations(credentials / 'ca.crt')
        self.context.verify_mode = ssl.CERT_REQUIRED
        self.context.set_alpn_protocols([protocol])
        self.socket = socket.socket()
        if backpressure: self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.listen(1)
        self.socket.settimeout(.1)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.backpressure = backpressure
        self.stall, self.abrupt, self.large = stall, abrupt, large
        self.thread = threading.Thread(target=self.run)
        self.thread.start()
    def run(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with connection:
                connection.settimeout(12)
                if self.stall:
                    self.stop.wait(12)
                    continue
                try:
                    with self.context.wrap_socket(connection, server_side=True) as tls:
                        if self.backpressure:
                            self.stop.wait(25)
                            continue
                        request = b''
                        while not request.endswith(b'\r\n\r\n'):
                            chunk = tls.recv(97)
                            if not chunk: break
                            request += chunk
                        if self.abrupt: continue
                        body = b'x' * (16384 if self.large else 17)
                        message = b'HTTP/1.1 200 OK\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body
                        for pos in range(0, len(message), 31): tls.sendall(message[pos:pos+31])
                except (ssl.SSLError, OSError):
                    pass
    def close(self):
        self.stop.set()
        self.socket.close()
        self.thread.join(timeout=15)
        assert not self.thread.is_alive()

count = 0
def probe(server, success, host='127.0.0.1', identity='127.0.0.1', cert='client', key=None, ca='ca', mode='/'):
    global count
    result = subprocess.run([str(build / 'wtp_tls_probe'), host, identity, str(server.port),
        str(credentials / (ca + '.crt')), str(credentials / (cert + '.crt')), str(credentials / ((key or cert) + '.key')), mode],
        capture_output=True, text=True, timeout=25)
    assert (result.returncode == 0) == success, (host, identity, cert, result.stderr)
    assert 'PRIVATE KEY' not in result.stdout + result.stderr
    count += 1

with contextlib.closing(Server()) as server:
    probe(server, True)
    probe(server, True, identity='')
    probe(server, True, host='localhost', identity='localhost')
    probe(server, True, identity='pico-test.local')
    for identity in ['wrong.local', '127.0.0.2']:
        probe(server, False, identity=identity)
    for cert, key in [('missing', None), ('client', 'other'), ('rogue-client', None), ('expired-client', None), ('future-client', None)]:
        probe(server, False, cert=cert, key=key)
        probe(server, True)
    probe(server, False, ca='rogue')
    (credentials / 'client.key').chmod(0o644)
    probe(server, False)
    (credentials / 'client.key').chmod(0o600)
    probe(server, True)
    probe(server, False, host='invalid.invalid')
with contextlib.closing(Server(certificate='dns-only')) as server:
    probe(server, True, identity='PiCo-TeSt.LoCaL.')
    probe(server, False, identity='127.0.0.1')
    probe(server, False, identity='wrong.local')
for name in ['cn-only', 'wildcard']:
    with contextlib.closing(Server(certificate=name)) as server:
        probe(server, False, identity='pico-test.local')
for options in [dict(certificate='expired'), dict(certificate='future'), dict(certificate='rogue'), dict(protocol='wrong'), dict(tls12=True), dict(abrupt=True), dict(stall=True)]:
    with contextlib.closing(Server(**options)) as server:
        began = time.monotonic()
        probe(server, False)
        assert time.monotonic() - began < 16
with contextlib.closing(Server(large=True)) as server:
    probe(server, True)
with contextlib.closing(Server(backpressure=True)) as server:
    probe(server, True, mode='backpressure')
subprocess.run([str(build / 'wtp_tls_contract_test'), str(credentials)], check=True, timeout=15)
print(f'{count} actual TLS identity/certificate/version/ALPN/partial-I/O/failure cases passed')

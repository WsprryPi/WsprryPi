#!/usr/bin/env python3
"""Run owned clients against the actual Pico server on loopback; bounded restarts."""
import pathlib
import os
import socket
import ssl
import selectors
import subprocess
import sys
import time
from network_process import PipeLines, wait_ready

build = pathlib.Path(sys.argv[1]).resolve()
server = client = None

def stop(process):
    if not process: return
    process.terminate()
    try: process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()

def start(boot='0', device='a', address='127.0.0.1'):
    process = subprocess.Popen([str(build / 'pico_tls_server'), boot, device], stdout=subprocess.PIPE, bufsize=0, env={**os.environ, 'WSPRRY_TEST_LISTEN_ADDRESS': address})
    try:
        wait_ready(process)
    except BaseException:
        stop(process)
        raise
    return process

try:
    server = start()
    # The real Pico TLS server must reject the retired carrier before any
    # authenticated application bytes can reach native execution.
    context=ssl.create_default_context(cafile=str(build/'credentials-v3/client-ca.crt'))
    context.load_cert_chain(str(build/'credentials-v3/client.crt'),str(build/'credentials-v3/client.key'))
    context.minimum_version=context.maximum_version=ssl.TLSVersion.TLSv1_3
    context.set_alpn_protocols(['wtp/1'])
    try:
        with socket.create_connection(('127.0.0.1',18443),timeout=5) as connection:
            with context.wrap_socket(connection,server_hostname='wsprrypico-'+32*'a'+'.local') as retired:
                raise AssertionError('Retired WTP ALPN handshake unexpectedly succeeded')
    except ssl.SSLError:
        pass
    second_loopback = False
    with socket.socket() as probe:
        try:
            probe.bind(('127.0.0.2', 0)); second_loopback = True
        except OSError as error:
            print(f'SKIP actual second-IPv4 TLS rebind: host loopback unavailable ({error}); injected-address contract still runs', flush=True)
    client_env = dict(os.environ)
    client_env.pop('WSPRRY_TEST_SECOND_LOOPBACK', None)
    if second_loopback: client_env['WSPRRY_TEST_SECOND_LOOPBACK'] = '1'
    client = subprocess.Popen([str(build / 'pico_https_management_interop_test'), str(build / 'credentials-v3')],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0, env=client_env)
    lines = PipeLines(client.stdout)
    deadline = time.monotonic() + 180
    with selectors.DefaultSelector() as selector:
        selector.register(client.stdout, selectors.EVENT_READ)
        while not lines.eof:
            if time.monotonic() > deadline: raise RuntimeError('Interop deadline')
            if not selector.select(1): continue
            for line in lines.read_available():
                print(line, end='', flush=True)
                if line.startswith('RESTART '):
                    stop(server)
                    server = start() if 'address1' in line else start(address='127.0.0.2') if 'address2' in line else start('1', 'a')
                    client.stdin.write(b'READY\n'); client.stdin.flush()
    assert client.wait(timeout=max(0, deadline - time.monotonic())) == 0, 'Pico interoperability client failed'
finally:
    stop(client)
    stop(server)

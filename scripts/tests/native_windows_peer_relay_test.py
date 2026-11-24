#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Relay admission and owned-process cleanup using the actual software Pico fixture."""
import argparse
import asyncio
import json
from pathlib import Path
import sys
import tempfile


async def run(build):
    with tempfile.TemporaryDirectory(prefix='issue475-relay-test-') as temporary:
        session = Path(temporary) / 'session'
        script = Path(__file__).resolve().parents[2] / 'src/tests/native_windows/peer_relay.py'
        relay = await asyncio.create_subprocess_exec(sys.executable, '-I', '-B', str(script), '--build', str(build),
                    '--session', str(session), stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        try:
            assert (await asyncio.wait_for(relay.stdout.readline(), 5)).startswith(b'Software peer relay ready:')
            state = json.loads((session / 'relay-state.json').read_text())
            client = json.loads((session / 'peer-session.json').read_text())
            good = {'schema': client['schema'], 'nonce': client['nonce'], 'kind': 'tls', 'args': ['0', 'a', 'settings', '18443']}
            wrong_nonce = ('1' if client['nonce'][0] == '0' else '0') + client['nonce'][1:]
            async def connection():
                return await asyncio.open_connection('127.0.0.1', state['control_port'])
            bad = [b'not-json\n', b'x' * 4097 + b'\n',
                   json.dumps(dict(good, nonce=wrong_nonce)).encode() + b'\n',
                   json.dumps(dict(good, nonce='é' * 32)).encode() + b'\n',
                   json.dumps(dict(good, schema='issue475-software-peer/99')).encode() + b'\n',
                   b'[' * 1200 + b']' * 1200 + b'\n',
                   json.dumps(dict(good, extra=True)).encode() + b'\n',
                   json.dumps(dict(good, kind='shell')).encode() + b'\n',
                   json.dumps(dict(good, args=['/bin/sh'])).encode() + b'\n',
                   ('{"schema":"invalid",' + json.dumps(good)[1:]).encode() + b'\n']
            for header in bad:
                reader, writer = await connection()
                writer.write(header)
                await writer.drain()
                assert await asyncio.wait_for(reader.read(1), 5) == b''
                writer.close()
                await writer.wait_closed()
            reader, writer = await connection()
            writer.write(json.dumps(good).encode() + b'\n')
            await writer.drain()
            assert (await asyncio.wait_for(reader.readline(), 10)).startswith(b'READY 18443')
            writer.write(b'DUMP\n')
            await writer.drain()
            reply = await asyncio.wait_for(reader.readline(), 10)
            assert reply.startswith(b'DUMP ') and type(json.loads(reply[5:])) is dict
            writer.close()
            await writer.wait_closed()
            relay.terminate()
            assert await asyncio.wait_for(relay.wait(), 10) == 0
            stopped = json.loads((session / 'relay-stopped.json').read_text())
            assert stopped == {'owned_children': 0, 'launches': 1}
            assert not await relay.stderr.read(), 'Relay must not log request or protocol contents'
            print('PASS: closed relay admission, malformed/duplicate/unknown refusal before spawn, actual Pico READY/DUMP, and joined owned-peer teardown')
        finally:
            if relay.returncode is None:
                relay.terminate()
                try:
                    await asyncio.wait_for(relay.wait(), 10)
                except asyncio.TimeoutError:
                    relay.kill()
                    await relay.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    arguments = parser.parse_args()
    asyncio.run(run(arguments.build.resolve(strict=True)))

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded raw relay to the explicitly selected, hardware-free Pico TLS fixture."""
import argparse
import asyncio
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import signal

SCHEMA = 'issue475-software-peer/1'
TLS_ARGUMENTS = {('0', 'a', 'settings', '18443'), ('1', 'b', 'settings', '18444', 'second'),
                 ('7', 'a', 'settings', '18443')}


def strict_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate relay key')
        result[key] = value
    return result


def reject_constant(_value):
    raise ValueError('Non-finite relay value')


def private_record(path, value):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, 'w') as output:
        json.dump(value, output, sort_keys=True)
        output.write('\n')


async def serve(build, session, usb_build=None):
    binary = build / 'pico_tls_server'
    if not binary.is_file() or binary.is_symlink() or not os.access(binary, os.X_OK):
        raise ValueError('Select the actual candidate Pico TLS fixture')
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    usb_binary = None if usb_build is None else usb_build / 'usb_management_driver'
    if usb_binary is not None and (not usb_binary.is_file() or usb_binary.is_symlink() or not os.access(usb_binary, os.X_OK)):
        raise ValueError('Select the actual candidate Pico USB fixture')
    usb_digest = None if usb_binary is None else hashlib.sha256(usb_binary.read_bytes()).hexdigest()
    usb_lock = asyncio.Lock()
    session.mkdir(mode=0o700)
    nonce = secrets.token_hex(16)
    private_record(session / 'peer-session.json', {'schema': SCHEMA, 'nonce': nonce, 'port': 18445})
    children, handlers = set(), set()
    ports = {18443: asyncio.Lock(), 18444: asyncio.Lock()}
    launches = 0

    async def stop_child(child):
        if child.returncode is None:
            try:
                child.terminate()
            except ProcessLookupError:
                pass
            try:
                await asyncio.wait_for(child.wait(), 2)
            except asyncio.TimeoutError:
                try:
                    child.kill()
                except ProcessLookupError:
                    pass
                await child.wait()
        children.discard(child)

    async def pump(source, destination):
        total = 0
        while True:
            data = await source.read(65536)
            if not data:
                return
            total += len(data)
            if total > 64 * 1024 * 1024:
                raise ValueError('Relay byte bound exceeded')
            destination.write(data)
            await asyncio.wait_for(destination.drain(), 5)

    async def exchange(reader, writer):
        nonlocal launches
        task = asyncio.current_task()
        if len(handlers) >= 8 or launches >= 64:
            writer.close()
            return
        handlers.add(task)
        child, tasks = None, []
        try:
            async with asyncio.timeout(600):
                line = await asyncio.wait_for(reader.readline(), 5)
                if not line.endswith(b'\n') or len(line) > 4096:
                    raise ValueError('Relay header bound')
                header = json.loads(line, object_pairs_hook=strict_object, parse_constant=reject_constant)
                if (type(header) is not dict or set(header) != {'schema', 'nonce', 'kind', 'args'}
                        or header['schema'] != SCHEMA or type(header['nonce']) is not str or not re.fullmatch('[0-9a-f]{32}', header['nonce'])
                        or not hmac.compare_digest(header['nonce'], nonce) or type(header['kind']) is not str
                        or type(header['args']) is not list or not all(type(item) is str for item in header['args'])
                        ):
                    raise ValueError('Relay admission refused')
                if header['kind'] == 'tls' and tuple(header['args']) in TLS_ARGUMENTS:
                    selected_binary, selected_digest, selected_build = binary, digest, build
                    lock = ports[int(header['args'][3])]
                elif header['kind'] == 'usb' and not header['args'] and usb_binary is not None:
                    selected_binary, selected_digest, selected_build = usb_binary, usb_digest, usb_build
                    lock = usb_lock
                else:
                    raise ValueError('Relay carrier admission refused')
                await asyncio.wait_for(lock.acquire(), 10)
                try:
                    if hashlib.sha256(selected_binary.read_bytes()).hexdigest() != selected_digest:
                        raise ValueError('Candidate image changed')
                    launches += 1
                    descriptor = os.open(session / f'peer-{launches}.stderr', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                    with os.fdopen(descriptor, 'wb') as diagnostic:
                        child = await asyncio.create_subprocess_exec(str(selected_binary), *header['args'], cwd=selected_build,
                            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=diagnostic,
                            env={'PATH': '/usr/bin:/bin', 'HOME': str(session), 'LANG': 'C'}, start_new_session=True)
                        children.add(child)
                        tasks = [asyncio.create_task(pump(reader, child.stdin)),
                                 asyncio.create_task(pump(child.stdout, writer)), asyncio.create_task(child.wait())]
                        finished, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                        if tasks[2] in finished and not tasks[1].done():
                            await asyncio.wait_for(tasks[1], 2)
                finally:
                    for pending in tasks:
                        pending.cancel()
                    if tasks:
                        await asyncio.gather(*tasks, return_exceptions=True)
                    if child is not None:
                        await stop_child(child)
                    lock.release()
        except (ValueError, OSError, RecursionError, asyncio.TimeoutError):
            # Neither headers nor protocol bodies enter diagnostics.
            pass
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass
            handlers.discard(task)

    stopping = asyncio.Event()
    loop = asyncio.get_running_loop()
    for name in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(name, stopping.set)
    server = await asyncio.start_server(exchange, '127.0.0.1', 0, limit=4096, backlog=8)
    port = server.sockets[0].getsockname()[1]
    private_record(session / 'relay-state.json', {'schema': SCHEMA, 'control_port': port,
                   'candidate_binary': str(binary), 'candidate_sha256': digest, 'scope': 'actual software Pico TLS/API/journal fixture'})
    if usb_binary is not None:
        private_record(session / 'usb-candidate.json', {'candidate_binary': str(usb_binary), 'candidate_sha256': usb_digest})
    print(f'Software peer relay ready: 127.0.0.1:{port}; session {session}', flush=True)
    try:
        await stopping.wait()
    finally:
        server.close()
        await server.wait_closed()
        for task in list(handlers):
            task.cancel()
        await asyncio.gather(*list(handlers), return_exceptions=True)
        for child in list(children):
            await stop_child(child)
        private_record(session / 'relay-stopped.json', {'owned_children': len(children), 'launches': launches})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--usb-build', type=Path)
    options = parser.parse_args()
    if not options.build.is_absolute() or not options.session.is_absolute() or options.session.exists():
        parser.error('Select absolute candidate build and new private session paths')
    if options.usb_build is not None and not options.usb_build.is_absolute():
        parser.error('Select an absolute actual USB fixture build path')
    asyncio.run(serve(options.build.resolve(strict=True), options.session,
                     None if options.usb_build is None else options.usb_build.resolve(strict=True)))

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the pinned application-private age adapter; never install it."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

REPOSITORY = Path(__file__).resolve().parents[1]
PROVIDER = REPOSITORY / 'scripts/recovery_age_provider.json'
SOURCE = REPOSITORY / 'src/application_recovery/age_helper'


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def target(compiler):
    triple = subprocess.check_output([*shlex.split(compiler), '-dumpmachine'], text=True, timeout=15).strip().lower()
    system = 'darwin' if 'darwin' in triple else 'linux' if 'linux' in triple else None
    cpu = triple.split('-')[0]
    arch = {'aarch64': 'arm64', 'arm64': 'arm64', 'x86_64': 'amd64', 'amd64': 'amd64'}.get(cpu)
    if cpu.startswith('arm') and arch is None:
        arch = 'arm'
    if system is None or arch is None:
        raise ValueError('Recovery helper requires an explicitly supported compiler target')
    return system, arch


def toolchain(root, provider, supplied):
    if supplied:
        executable = Path(supplied).resolve(strict=True)
    else:
        system = platform.system().lower()
        cpu = {'aarch64': 'arm64', 'arm64': 'arm64', 'x86_64': 'amd64', 'armv6l': 'armv6l', 'armv7l': 'armv6l'}.get(platform.machine().lower())
        selected = provider['toolchains'].get(f'{system}-{cpu}')
        if not selected:
            raise ValueError('No pinned Go toolchain for this build host')
        directory = root / provider['go']
        executable = directory / 'go/bin/go'
        if not executable.is_file():
            archive = root / selected['filename']
            if not archive.is_file() or digest(archive) != selected['sha256']:
                descriptor, temporary = tempfile.mkstemp(prefix='go-download-', dir=root)
                try:
                    with os.fdopen(descriptor, 'wb') as output, urllib.request.urlopen('https://go.dev/dl/' + selected['filename'], timeout=60) as response:
                        total = 0
                        while chunk := response.read(1024 * 1024):
                            total += len(chunk)
                            if total > 128 * 1024 * 1024:
                                raise ValueError('Toolchain download exceeds bound')
                            output.write(chunk)
                    if digest(Path(temporary)) != selected['sha256']:
                        raise ValueError('Go toolchain checksum mismatch')
                    os.replace(temporary, archive)
                finally:
                    Path(temporary).unlink(missing_ok=True)
            staging = Path(tempfile.mkdtemp(prefix='go-extract-', dir=root))
            try:
                with tarfile.open(archive) as source:
                    members = source.getmembers()
                    if len(members) > 30000 or sum(item.size for item in members) > 1024 * 1024 * 1024:
                        raise ValueError('Toolchain archive exceeds bound')
                    for item in members:
                        name = Path(item.name)
                        if name.is_absolute() or '..' in name.parts or name.parts[0] != 'go' or not (item.isfile() or item.isdir()):
                            raise ValueError('Unsafe toolchain member')
                    source.extractall(staging, members=members, filter='data')
                staging.rename(directory)
            finally:
                if staging.exists():
                    shutil.rmtree(staging)
    version = subprocess.check_output([str(executable), 'version'], text=True, timeout=15).split()
    if len(version) != 4 or version[2] != provider['go']:
        raise ValueError('Recovery helper requires the pinned Go version')
    return executable


def embed(binary, destination):
    data = binary.read_bytes()
    if not data or len(data) > 16 * 1024 * 1024:
        raise ValueError('Invalid recovery helper image')
    # String literals keep compiler memory bounded; millions of array initializer
    # AST nodes would make source builds unnecessarily expensive on a Pi 1.
    text = '#pragma once\n#include <cstddef>\ninline constexpr unsigned char recovery_age_image[]=\n'
    text += '\n'.join('"' + ''.join('\\x%02x' % byte for byte in data[offset:offset+128]) + '"' for offset in range(0, len(data), 128))
    text += ';\ninline constexpr std::size_t recovery_age_size=sizeof(recovery_age_image)-1;\n'
    text += 'inline constexpr char recovery_age_digest[]="' + hashlib.sha256(data).hexdigest() + '";\n'
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text() != text:
        temporary = destination.with_name(destination.name + '.building')
        temporary.write_text(text)
        os.replace(temporary, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--go', help='Explicit already-verified pinned Go executable')
    parser.add_argument('--embed', type=Path, help='Generate the executable image header')
    parser.add_argument('--test', action='store_true', help='Run native helper unit tests')
    args = parser.parse_args()
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(root / '.build.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        build(args, root)
    finally:
        os.close(descriptor)


def build(args, root):
    provider = json.loads(PROVIDER.read_text())
    system, arch = target(args.compiler)
    destination = root / f'{system}-{arch}'
    destination.mkdir(exist_ok=True)
    output = destination / 'recovery-age'
    inputs = {path.name: digest(path) for path in sorted(SOURCE.iterdir()) if path.name in ('go.mod', 'go.sum', 'main.go')}
    identity = {'provider': provider, 'inputs': inputs, 'target': [system, arch, '6' if arch == 'arm' else '']}
    record = destination / 'build.json'
    valid = False
    if output.is_file() and record.is_file():
        try:
            saved = json.loads(record.read_text())
            valid = saved.get('identity') == identity and saved.get('sha256') == digest(output)
        except (ValueError, OSError):
            pass
    if valid and not args.test:
        if args.embed:
            embed(output, args.embed)
        print(output)
        return
    go = toolchain(root, provider, args.go)
    environment = dict(os.environ, GOTOOLCHAIN='local', CGO_ENABLED='0', GOOS=system, GOARCH=arch, GOARM='6' if arch == 'arm' else '',
                       GOFLAGS='', GOEXPERIMENT='', GOWORK='off', GOSUMDB='sum.golang.org', GONOSUMDB='', GONOPROXY='',
                       GOPROXY='https://proxy.golang.org', GOPATH=str(root / 'modules'), GOCACHE=str(root / 'cache'))
    if args.test:
        native = {key: value for key, value in environment.items() if key not in ('GOOS', 'GOARCH', 'GOARM')}
        subprocess.run([str(go), 'test', '-mod=readonly', './...'], cwd=SOURCE, env=native, check=True, timeout=300, stdout=__import__('sys').stderr)
    if valid:
        if args.embed:
            embed(output, args.embed)
        print(output)
        return
    temporary = destination / 'recovery-age.building'
    try:
        subprocess.run([str(go), 'build', '-mod=readonly', '-trimpath', '-buildvcs=false', '-ldflags=-s -w', '-o', str(temporary), '.'],
                       cwd=SOURCE, env=environment, check=True, timeout=900)
        os.replace(temporary, output)
        record.write_text(json.dumps({'identity': identity, 'sha256': digest(output)}, sort_keys=True) + '\n')
    finally:
        temporary.unlink(missing_ok=True)
    if args.embed:
        embed(output, args.embed)
    print(output)


if __name__ == '__main__':
    main()

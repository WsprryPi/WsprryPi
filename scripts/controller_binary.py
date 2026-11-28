#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect a selected Ubuntu 26.04 controller executable without running it."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

from precompiled_binary import MAX_BINARY, elf_header


PLATFORM = 'ubuntu-26.04'
PHP_MODULE = 'php8.5'
TARGETS = {
    'amd64': ('amd64', '/lib64/ld-linux-x86-64.so.2'),
    'arm64': ('aarch64', '/lib/ld-linux-aarch64.so.1'),
}


LIBRARY_PACKAGES = {
    'libatomic.so.1': 'libatomic1',
    'libavahi-client.so.3': 'libavahi-client3',
    'libavahi-common.so.3': 'libavahi-common3',
    'libc.so.6': 'libc6',
    'libcrypto.so.3': 'libssl3t64',
    'libgcc_s.so.1': 'libgcc-s1',
    'libm.so.6': 'libc6',
    'libsqlite3.so.0': 'libsqlite3-0',
    'libssl.so.3': 'libssl3t64',
    'libstdc++.so.6': 'libstdc++6',
    'libsystemd.so.0': 'libsystemd0',
}
IDENTITY_SECTION = '.wsprrypi.controller'
IDENTITY = re.compile(
    r'wsprrypi-controller/1 ([0-9a-f]{40}|[0-9a-f]{64}) '
    r'([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?) (true|false) ([0-9a-f]{64})'
)


def readelf(binary, *options):
    descriptor = binary.fileno()
    result = subprocess.run(
        ['/usr/bin/readelf', '--wide', *options, f'/proc/self/fd/{descriptor}'],
        pass_fds=(descriptor,), capture_output=True, text=True,
        encoding='ascii', errors='replace', env={'LC_ALL': 'C'},
        check=True, timeout=20,
    )
    if result.stderr:
        raise ValueError('ELF inspection reported an invalid or missing section')
    return result.stdout


def inspect_elf(binary, architecture):
    cpu, loader = TARGETS[architecture]
    elf_header(f'/proc/self/fd/{binary.fileno()}', cpu)
    headers = readelf(binary, '--program-headers')
    if re.findall(r'\[Requesting program interpreter: ([^\]]+)\]', headers) != [loader]:
        raise ValueError('Unexpected ELF interpreter for the selected controller architecture')

    dynamic = readelf(binary, '--dynamic')
    if re.search(r'\((?:RPATH|RUNPATH|AUDIT|DEPAUDIT|FILTER|AUXILIARY)\)', dynamic):
        raise ValueError('ELF specifies an alternate library search or loading policy')
    needed = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', dynamic)
    # The native linker can also record the selected interpreter as DT_NEEDED.
    library_packages = {**LIBRARY_PACKAGES, Path(loader).name: 'libc6'}
    if 'libc.so.6' not in needed or set(needed) - library_packages.keys():
        raise ValueError('ELF libraries are outside the Ubuntu 26.04 controller profile')

    versions = readelf(binary, '--version-info')
    glibc = re.findall(r'\bName: (GLIBC_[^\s]+)', versions)
    abi = []
    for name in glibc:
        if name == 'GLIBC_ABI_DT_RELR':
            abi.append((2, 36))
            continue
        if not re.fullmatch(r'GLIBC_[0-9]+(?:\.[0-9]+){1,2}', name):
            raise ValueError('ELF requires an unsupported glibc symbol version')
        abi.append(tuple(map(int, name.removeprefix('GLIBC_').split('.'))))
    # A zero patch component does not raise the ABI floor.
    if not abi or max((*value, 0)[:3] for value in abi) > (2, 43, 0):
        raise ValueError('ELF requires a glibc ABI outside the Ubuntu 26.04 baseline')

    section = readelf(binary, f'--string-dump={IDENTITY_SECTION}')
    records = re.findall(r'^\s*\[\s*([0-9a-f]+)\]\s+(.+)$', section, re.MULTILINE)
    identity = IDENTITY.fullmatch(records[0][1]) if len(records) == 1 and records[0][0] == '0' else None
    if not identity:
        raise ValueError('Expected one controller build identity in the ELF')
    revision, version, dirty, inputs = identity.groups()
    return {
        'platform': PLATFORM,
        'architecture': architecture,
        'role': 'controller',
        'source_commit': revision,
        'application_version': version,
        'tracked_source_dirty': dirty == 'true',
        'controller_inputs_sha256': inputs,
        'elf_libraries': sorted(set(needed)),
        'runtime_library_packages': sorted({library_packages[name] for name in needed}),
    }


def verify(path, architecture, expected_sha256):
    if sys.platform != 'linux':
        raise ValueError('Linux is required for offline ELF inspection')
    if not re.fullmatch(r'[0-9a-f]{64}', expected_sha256):
        raise ValueError('Expected a lowercase SHA-256 from the selected candidate')
    fd = os.open(path, os.O_PATH | os.O_NOFOLLOW)
    try:
        before = os.fstat(fd)
        if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1
                or before.st_mode & (stat.S_IWGRP | stat.S_IWOTH)
                or not 0 < before.st_size <= MAX_BINARY):
            raise ValueError('Expected a bounded regular executable file without aliases or shared write access')
        with open(f'/proc/self/fd/{fd}', 'rb') as binary:
            digest = hashlib.sha256()
            remaining = before.st_size
            while remaining:
                block = binary.read(min(remaining, 1024 * 1024))
                if not block:
                    raise ValueError('Executable changed while being read')
                remaining -= len(block)
                digest.update(block)
            if digest.hexdigest() != expected_sha256:
                raise ValueError('Executable checksum mismatch')
            report = inspect_elf(binary, architecture)

        after = os.fstat(fd)
        if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
            after.st_size, after.st_mtime_ns, after.st_ctime_ns
        ):
            raise ValueError('Executable changed during inspection')
        return {**report, 'sha256': expected_sha256}
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--architecture', required=True, choices=TARGETS)
    parser.add_argument('--sha256', required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.binary, args.architecture, args.sha256), sort_keys=True))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Controller executable rejected: {error}\n')


if __name__ == '__main__':
    main()

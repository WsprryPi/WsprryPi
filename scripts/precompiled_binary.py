#!/usr/bin/env python3
"""Check standalone WsprryPi executables against the host OS and ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import urllib.parse
import urllib.request

MAX_BINARY = 64 * 1024 * 1024
MAX_METADATA = 4 * 1024 * 1024


def output(*args):
    return subprocess.check_output(args, text=True, env={**os.environ, 'LC_ALL': 'C'})


def os_release(path='/etc/os-release'):
    return dict(line.split('=', 1) for line in Path(path).read_text().splitlines()
                if '=' in line and not line.startswith('#'))


def host_target():
    release = os_release().get('VERSION_CODENAME', '').strip('"')
    arch = output('dpkg', '--print-architecture').strip()
    if release not in ('bookworm', 'trixie') or arch not in ('armhf', 'arm64'):
        raise ValueError('precompiled installs require Bookworm/Trixie armhf or arm64 userspace')
    return ('armv6' if arch == 'armhf' else 'aarch64'), release


def elf_header(binary, cpu):
    with Path(binary).open('rb') as stream:
        data = stream.read(64)
    bits = 1 if cpu == 'armv6' else 2
    if len(data) < 64 or data[:4] != b'\x7fELF' or data[4:6] != bytes([bits, 1]):
        raise ValueError(f'executable is not a little-endian {cpu} ELF file')
    kind, machine = struct.unpack_from('<HH', data, 16)
    if kind not in (2, 3) or machine != (40 if bits == 1 else 183):
        raise ValueError('executable has the wrong ELF machine or type')
    if bits == 1 and not struct.unpack_from('<I', data, 36)[0] & 0x400:
        raise ValueError('ARM executable does not declare the hard-float ABI')


def inspect(binary, cpu, release):
    elf_header(binary, cpu)
    if cpu == 'armv6':
        attrs = output('readelf', '-A', str(binary))
        for pattern in (r'Tag_CPU_arch: v6$', r'Tag_FP_arch: VFPv2$',
                        r'Tag_ABI_VFP_args: VFP registers$'):
            if not re.search(pattern, attrs, re.M):
                raise ValueError('executable must use ARMv6/VFPv2 hard-float attributes')
    loader = '/lib/ld-linux-armhf.so.3' if cpu == 'armv6' else '/lib/ld-linux-aarch64.so.1'
    if f'Requesting program interpreter: {loader}]' not in output('readelf', '-l', str(binary)):
        raise ValueError('executable has an unexpected dynamic loader')
    dynamic = output('readelf', '-d', str(binary))
    mapping = {'libatomic.so.1': 'libatomic1', 'libstdc++.so.6': 'libstdc++6',
               'libgcc_s.so.1': 'libgcc-s1', 'libc.so.6': 'libc6', 'libm.so.6': 'libc6',
               'libpthread.so.0': 'libc6', 'librt.so.1': 'libc6', 'libdl.so.2': 'libc6',
               'libsystemd.so.0': 'libsystemd0',
               'libavahi-client.so.3': 'libavahi-client3', 'libavahi-common.so.3': 'libavahi-common3',
               'libssl.so.3': 'libssl3' if release == 'bookworm' else 'libssl3t64',
               'libcrypto.so.3': 'libssl3' if release == 'bookworm' else 'libssl3t64'}
    generation = ('1', '2', 'libgpiod2') if release == 'bookworm' else ('2', '3', 'libgpiod3')
    mapping['libgpiodcxx.so.' + generation[0]] = generation[2]
    mapping['libgpiod.so.' + generation[1]] = generation[2]
    needed = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
    if not needed:
        raise ValueError('executable has no required shared libraries')
    unsupported = [name for name in needed if name not in mapping]
    if unsupported:
        raise ValueError(f'unsupported libraries for {release}: {unsupported}')
    return sorted({mapping[name] for name in needed})


def download(url, path, limit):
    with urllib.request.urlopen(url, timeout=60) as response, Path(path).open('xb') as dest:
        total = 0
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            total += len(block)
            if total > limit:
                raise ValueError('release asset exceeds its size limit')
            dest.write(block)


def release_base(repo, tag):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/WsprryPi', repo):
        raise ValueError('invalid release repository')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]*', tag):
        raise ValueError('invalid release tag')
    return f'https://github.com/{repo}/releases/download/{urllib.parse.quote(tag, safe="")}/'


def read_json(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'WsprryPi-Installer/1'})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read(MAX_METADATA + 1)
    if len(data) > MAX_METADATA:
        raise ValueError('release metadata exceeds its size limit')
    return json.loads(data)


def resolve_release(repo, branch):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/WsprryPi', repo):
        raise ValueError('invalid release repository')
    try:
        cpu, release = host_target()
    except ValueError as error:
        if str(error).startswith('precompiled installs require '):
            return None
        raise
    releases = []
    for page in range(1, 11):
        records = read_json(f'https://api.github.com/repos/{repo}/releases?per_page=100&page={page}')
        if not isinstance(records, list) or len(records) > 100 or not all(isinstance(r, dict) for r in records):
            raise ValueError('invalid release listing')
        releases.extend(r for r in records if not r.get('draft') and r.get('published_at'))
        if len(records) < 100:
            break
    else:
        raise ValueError('release listing exceeds its page limit')
    releases.sort(key=lambda r: (r['published_at'], r.get('id', 0)), reverse=True)
    name = f'wsprrypi-{cpu}-{release}'
    for record in releases:
        assets = record.get('assets', [])
        if not isinstance(assets, list) or not all(isinstance(a, dict) for a in assets):
            raise ValueError('invalid release asset listing')
        matches = [a for a in assets if a.get('name') == name and a.get('state') == 'uploaded']
        if not matches:
            continue
        if len(matches) != 1:
            raise ValueError('ambiguous released executable')
        tag = record.get('tag_name', '')
        base = release_base(repo, tag)
        branch_name = record.get('target_commitish')
        manifest = None
        if any(a.get('name') == 'publication-manifest.json' and a.get('state') == 'uploaded' for a in assets):
            manifest = read_json(base + 'publication-manifest.json')
            if not isinstance(manifest, dict) or manifest.get('release_tag') != tag:
                raise ValueError('publication manifest does not identify its release')
            executable = manifest.get('executable_source')
            if not isinstance(executable, dict) or not isinstance(executable.get('branch'), str):
                raise ValueError('publication manifest has no executable branch')
            branch_name = executable['branch']
        if branch_name != branch:
            continue
        asset = matches[0]
        if type(asset.get('size')) is not int or not 0 < asset['size'] <= MAX_BINARY:
            raise ValueError('invalid released executable size')
        checksum = ''
        if asset.get('digest'):
            if not re.fullmatch(r'sha256:[0-9a-f]{64}', asset['digest']):
                raise ValueError('invalid released executable checksum')
            checksum = asset['digest'][7:]
        if manifest is not None:
            inventory = manifest.get('assets')
            if not isinstance(inventory, list) or not all(isinstance(a, dict) for a in inventory):
                raise ValueError('invalid publication manifest asset inventory')
            declared = [a for a in inventory if a.get('name') == name]
            if len(declared) != 1 or declared[0].get('bytes') != asset['size'] or not re.fullmatch(r'[0-9a-f]{64}', str(declared[0].get('sha256', ''))):
                raise ValueError('publication manifest does not identify the released executable')
            if checksum and checksum != declared[0]['sha256']:
                raise ValueError('released executable checksums disagree')
            checksum = declared[0]['sha256']
        return tag, checksum
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    check = sub.add_parser('check')
    check.add_argument('--binary', required=True)
    check.add_argument('--full', action='store_true')
    check.add_argument('--runtime-user')
    check.add_argument('--field', choices=('version', 'runtime_packages'))
    resolve = sub.add_parser('resolve')
    resolve.add_argument('--repo', required=True)
    resolve.add_argument('--branch', required=True)
    fetch = sub.add_parser('fetch')
    fetch.add_argument('--repo', required=True)
    fetch.add_argument('--tag', required=True)
    fetch.add_argument('--directory', required=True)
    fetch.add_argument('--sha256')
    args = parser.parse_args()
    try:
        if args.action == 'resolve':
            selected = resolve_release(args.repo, args.branch)
            print('build' if selected is None else f'release {selected[0]} {selected[1] or "-"}')
        elif args.action == 'fetch':
            cpu, release = host_target()
            name = f'wsprrypi-{cpu}-{release}'
            url = release_base(args.repo, args.tag) + name
            if args.sha256 and not re.fullmatch(r'[0-9a-f]{64}', args.sha256):
                raise ValueError('invalid expected executable checksum')
            directory = Path(args.directory)
            binary = directory / 'wsprrypi'
            download(url, binary, MAX_BINARY)
            if args.sha256 and hashlib.sha256(binary.read_bytes()).hexdigest() != args.sha256:
                raise ValueError('released executable checksum mismatch')
        else:
            cpu, release = host_target()
            elf_header(args.binary, cpu)
            packages = ['libgpiod2', 'libssl3'] if release == 'bookworm' else ['libgpiod3', 'libssl3t64']
            if args.full:
                inspect(args.binary, cpu, release)
            if args.runtime_user:
                if not args.full or args.runtime_user == 'root':
                    raise ValueError('runtime library validation requires a non-root user and full inspection')
                result = subprocess.run(['runuser', '-u', args.runtime_user, '--', 'ldd', '-v', args.binary],
                                        capture_output=True, text=True, timeout=60, env={**os.environ, 'LC_ALL': 'C'})
                if result.returncode or 'not found' in result.stdout + result.stderr:
                    raise ValueError('required libraries or symbol versions are missing: ' + result.stdout + result.stderr)
            if args.field == 'runtime_packages':
                print('\n'.join(packages))
            elif args.field == 'version':
                if not args.runtime_user:
                    raise ValueError('version reporting requires runtime validation')
                result = subprocess.run(['runuser', '-u', args.runtime_user, '--', args.binary, '--version'],
                                        capture_output=True, text=True, timeout=15)
                version = re.search(r'\b(\d+\.\d+\.\d+[A-Za-z0-9._+/-]*)', result.stdout)
                if result.returncode or not version:
                    raise ValueError('executable did not report a version')
                print(version.group(1))
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f'Precompiled executable rejected: {error}\n')


if __name__ == '__main__':
    main()

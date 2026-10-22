#!/usr/bin/env python3
"""Install a pinned release package. This module never compiles or accesses USB."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import uuid

from precompiled_binary import elf_header, host_target

VERSION = '2.3.1'
PREFIX = Path('/usr/local/lib/wsprrypi/picotool')
TARGETS = ('armv6-bookworm', 'armv6-trixie', 'aarch64-bookworm', 'aarch64-trixie')
OPTIONS = {
    'info': ('-a', '--ser'), 'save': ('-r', '-v', '-t', '--ser'),
    'load': ('--ignore-partitions', '-v', '-u', '-x', '--ser'),
    'verify': ('-t', '-o', '--ser'), 'erase': ('-r', '--ser'),
}
MAX_ARCHIVE = 32 * 1024 * 1024
MAX_CONTENT = 64 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(*arguments, **kwargs):
    return subprocess.run([str(item) for item in arguments], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
                          env={**os.environ, 'LC_ALL': 'C'}, **kwargs).stdout


def validate(path):
    report = run(path, 'version')
    version = re.match(r'^picotool v(\d+)\.(\d+)\.(\d+) ', report)
    require(version and int(version[1]) == 2 and
            tuple(map(int, version.groups())) >= (2, 3, 0) and 'without USB support' not in report,
            'picotool 2.3 or newer within 2.x, with USB support, is required')
    for operation, options in OPTIONS.items():
        help_text = run(path, 'help', operation)
        require(all(re.search(r'(?<![\w-])' + re.escape(option) + r'(?![\w-])', help_text)
                    for option in options), 'picotool lacks required ' + operation + ' options')


def existing_tool():
    path = shutil.which('picotool')
    if path:
        try:
            validate(path)
            return True
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
    return False


def policy_record(policy, target):
    require(policy.get('schema') == 'wsprrypi-picotool-release/1' and policy.get('version') == VERSION,
            'Unsupported picotool release policy')
    require(re.fullmatch(r'v[0-9][0-9A-Za-z.+-]*', policy.get('release_tag', '')),
            'Invalid picotool release tag')
    require(set(policy['targets']) == set(TARGETS) and target in TARGETS, 'Unsupported picotool target')
    record = policy['targets'][target]
    require(record['asset'] == 'picotool-' + target + '.tar.gz' and
            type(record['bytes']) is int and 0 < record['bytes'] <= MAX_ARCHIVE and
            re.fullmatch(r'[0-9a-f]{64}', record['sha256']), 'Invalid picotool archive identity')
    require({'picotool', 'enc_bootloader.elf', 'xip_ram_perms.elf', 'rp2350_otp_contents.json'} <=
            set(record['files']), 'Incomplete picotool support-file inventory')
    return record


def download(url, limit):
    request = urllib.request.Request(url, headers={'User-Agent': 'WsprryPi-picotool/1'})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(limit + 1)
    require(len(data) <= limit, 'Oversized picotool download')
    return data


def unpack(data, record, destination):
    require(len(data) == record['bytes'] and sha(data) == record['sha256'],
            'picotool archive size/SHA-256 mismatch')
    inventory = {}
    total = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            require(member.isfile() and not path.is_absolute() and '..' not in path.parts and
                    str(path) == member.name and member.name not in inventory,
                    'Unsafe or duplicate picotool archive entry')
            total += member.size
            require(total <= MAX_CONTENT and len(inventory) < 256, 'Oversized picotool content')
            require(member.name in record['files'], 'Unexpected picotool package file')
            payload = archive.extractfile(member).read()
            require({'bytes': len(payload), 'sha256': sha(payload)} == record['files'][member.name],
                    'picotool package file identity mismatch')
            inventory[member.name] = payload
    require(set(inventory) == set(record['files']), 'Incomplete picotool package')
    for name, payload in inventory.items():
        path = destination/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        path.chmod(0o755 if name == 'picotool' else 0o644)


def validate_elf(path, target):
    cpu, _release = target.split('-')
    elf_header(path, cpu)
    loader = '/lib/ld-linux-armhf.so.3' if cpu == 'armv6' else '/lib/ld-linux-aarch64.so.1'
    require(f'Requesting program interpreter: {loader}]' in run('readelf', '-l', path),
            'picotool has an unexpected dynamic loader')
    if cpu == 'armv6':
        attrs = run('readelf', '-A', path)
        require(all(re.search(pattern, attrs, re.M) for pattern in
                    (r'Tag_CPU_arch: v6$', r'Tag_FP_arch: VFPv2$',
                     r'Tag_ABI_VFP_args: VFP registers$')), 'picotool is not ARMv6/VFPv2 hard-float')
    require('[libusb-1.0.so.0]' in run('readelf', '-d', path), 'picotool does not link libusb')


def protected_directory(path, root):
    require(not any(item.is_symlink() for item in (path, *path.parents)),
            'Symlink in picotool installation destination')
    if not path.exists():
        protected_directory(path.parent, root)
        path.mkdir(mode=0o755)
        path.chmod(0o755)
    info = path.stat()
    require(path.is_dir() and info.st_uid == os.geteuid() and not info.st_mode & 0o022,
            'Unprotected picotool installation directory')
    if path != root:
        protected_directory(path.parent, root)


def install(policy, root=Path('/'), fetch=download):
    if existing_tool():
        return 'reused'
    target = '-'.join(host_target())
    record = policy_record(policy, target)
    url = ('https://github.com/WsprryPi/WsprryPi/releases/download/' + policy['release_tag'] +
           '/' + record['asset'])
    data = fetch(url, record['bytes'])
    root = root.resolve()
    destination = root/PREFIX.relative_to('/')
    command = root/'usr/local/bin/picotool'
    protected_directory(destination.parent, root)
    protected_directory(command.parent, root)
    require(not destination.is_symlink(), 'Symlink at picotool installation destination')
    if destination.exists():
        receipt = destination/'installation.json'
        require(receipt.is_file() and json.loads(receipt.read_text()).get('schema') ==
                'wsprrypi-picotool-installation/1', 'Unrecognized picotool directory retained')
    with tempfile.TemporaryDirectory(prefix='.picotool-', dir=destination.parent) as temporary:
        stage = Path(temporary)/'new'
        stage.mkdir(mode=0o755)
        stage.chmod(0o755)
        unpack(data, record, stage)
        validate_elf(stage/'picotool', target)
        validate(stage/'picotool')
        (stage/'installation.json').write_text(json.dumps({
            'schema': 'wsprrypi-picotool-installation/1', 'target': target,
            'version': VERSION, 'archive_sha256': record['sha256']}, sort_keys=True) + '\n')
        previous = Path(temporary)/'previous'
        old_command = Path(temporary)/'previous-command'
        link = command.with_name('.picotool-' + uuid.uuid4().hex)
        had_directory = destination.exists()
        had_command = command.exists() or command.is_symlink()
        try:
            if had_directory:
                destination.rename(previous)
            stage.rename(destination)
            # Relative target also permits isolated installation-prefix tests.
            link.symlink_to(os.path.relpath(destination/'picotool', command.parent))
            if had_command:
                command.rename(old_command)
            os.replace(link, command)
            for directory in destination.rglob('*'):
                if directory.is_dir():
                    directory.chmod(0o755)
            validate(command)
            if root == Path('/'):
                require(existing_tool(), 'Installed picotool is shadowed by an incompatible command')
        except BaseException:
            link.unlink(missing_ok=True)
            if old_command.exists() or old_command.is_symlink():
                os.replace(old_command, command)
            elif not had_command:
                command.unlink(missing_ok=True)
            if destination.exists() and not stage.exists():
                shutil.rmtree(destination)
            if previous.exists():
                previous.rename(destination)
            raise
    return 'installed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, required=True)
    args = parser.parse_args()
    print('picotool: ' + install(json.loads(args.policy.read_text())))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, tarfile.TarError, subprocess.SubprocessError) as error:
        raise SystemExit('picotool installation failed: ' + str(error))

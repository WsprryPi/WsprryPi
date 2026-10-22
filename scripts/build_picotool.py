#!/usr/bin/env python3
"""Maintainer-only four-target picotool builds; never invoked by installation."""
import argparse
import gzip
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import uuid

from container_build import TARGETS, check_destination
import picotool_install as tool

REPO = Path(__file__).resolve().parents[1]
PICOTOOL_REVISION = '2041936441b48a3cc53ae3da9e805229fe8f4e18'  # 2.3.1
SDK_REVISION = 'a1438dff1d38bd9c65dbd693f0e5db4b9ae91779'  # 2.2.0 headers


def run(*arguments, **kwargs):
    return subprocess.run([str(item) for item in arguments], check=True, text=True, **kwargs)


def checkout(path, repository, revision):
    run('git', 'init', '--quiet', path)
    run('git', '-C', path, 'fetch', '--quiet', '--depth=1',
        'https://github.com/raspberrypi/' + repository + '.git', revision)
    run('git', '-C', path, 'checkout', '--quiet', '--detach', 'FETCH_HEAD')
    actual = run('git', '-C', path, 'rev-parse', 'HEAD', capture_output=True).stdout.strip()
    tool.require(actual == revision, 'Unexpected ' + repository + ' source')


def package(payload, output, target):
    files = {}
    buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode='wb', mtime=0, filename='') as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for path in sorted(payload.rglob('*')):
                tool.require(not path.is_symlink(), 'Unexpected package symlink')
                if path.is_dir():
                    continue
                tool.require(path.is_file() and not path.is_symlink(), 'Unexpected package file')
                name, data = path.relative_to(payload).as_posix(), path.read_bytes()
                files[name] = {'bytes': len(data), 'sha256': tool.sha(data)}
                info = tarfile.TarInfo(name)
                info.size, info.mode = len(data), 0o755 if name == 'picotool' else 0o644
                archive.addfile(info, io.BytesIO(data))
    data = buffer.getvalue()
    name = 'picotool-' + target + '.tar.gz'
    (output/name).write_bytes(data)
    record = {'asset': name, 'bytes': len(data), 'sha256': tool.sha(data), 'files': files}
    (output/'package.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    return record


def inside(target, output):
    tool.require('-'.join(tool.host_target()) == target, 'Wrong build userspace')
    if target.startswith('armv6-'):
        release = dict(line.split('=', 1) for line in Path('/etc/os-release').read_text().splitlines()
                       if '=' in line)
        tool.require(release['ID'].strip('"') == 'raspbian', 'ARMv6 requires Raspbian userspace')
    check_destination(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='picotool-build-') as temporary:
        root = Path(temporary)
        source, sdk, build, staging = (root/name for name in ('picotool', 'pico-sdk', 'build', 'stage'))
        checkout(source, 'picotool', PICOTOOL_REVISION)
        checkout(sdk, 'pico-sdk', SDK_REVISION)
        run('cmake', '-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG', '-DCMAKE_INSTALL_PREFIX=' + str(tool.PREFIX.parent),
            '-DPICOTOOL_FLAT_INSTALL=ON', '-DPICO_SDK_PATH=' + str(sdk),
            '-DPICOTOOL_NO_LIBUSB=OFF', '-DUSE_PRECOMPILED=ON', '-DGIT_SUBMODULE=OFF')
        run('cmake', '--build', build, '--parallel', '1')
        run('cmake', '--install', build, env={**os.environ, 'DESTDIR': str(staging)})
        payload = staging/tool.PREFIX.relative_to('/')
        # Distribute upstream notices alongside the executable and support data.
        for prefix, tree in (('picotool', source), ('pico-sdk', sdk)):
            for path in sorted(tree.rglob('*')):
                if (path.is_file() and '.git' not in path.parts and
                        re.match(r'(?i)^(license|copying|notice)(\.|$)', path.name)):
                    destination = payload/'licenses'/prefix/path.relative_to(tree)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, destination)
        tool.validate_elf(payload/'picotool', target)
        for name, arguments in {
                'elf-header.txt': ('readelf', '-h'), 'elf-attributes.txt': ('readelf', '-A'),
                'elf-dynamic.txt': ('readelf', '-d'), 'elf-versions.txt': ('readelf', '-V')}.items():
            (output/name).write_text(run(*arguments, payload/'picotool', capture_output=True).stdout)
        (output/'build-packages.txt').write_text(run('dpkg-query', '-W', capture_output=True).stdout)
        shutil.copyfile('/etc/os-release', output/'os-release.txt')
        # Runtime validation is performed by the host in a separate unprivileged,
        # read-only, network-disabled container of this exact build image.
        package(payload, output, target)
        installed = tool.PREFIX
        installed.mkdir(parents=True, exist_ok=True)
        shutil.copytree(payload, installed, dirs_exist_ok=True)
        shutil.copyfile(installed/'picotool', output/'picotool')
        (output/'picotool').chmod(0o755)


def build(target, output):
    output = Path(os.path.abspath(output))
    check_destination(output)
    source = (REPO/'containers'/target/'Dockerfile').read_text()
    base = re.search(r'^FROM (\S+) AS build$', source, re.M).group(1)
    container = 'picotool-export-' + uuid.uuid4().hex
    try:
        with tempfile.TemporaryDirectory(prefix='picotool-image-') as temporary:
            iid = Path(temporary)/'image-id'
            run('docker', 'buildx', 'build', '--platform', TARGETS[target], '--file',
                REPO/'containers/picotool/Dockerfile', '--build-arg', 'BASE_IMAGE=' + base,
                '--build-arg', 'PICOTOOL_TARGET=' + target, '--iidfile', iid,
                '--tag', 'wsprrypi-picotool:' + target, '--load', REPO)
            image = iid.read_text().strip()
            tool.require(re.fullmatch(r'sha256:[0-9a-f]{64}', image), 'Missing build image identity')
            run('docker', 'create', '--name', container, '--platform', TARGETS[target],
                image, stdout=subprocess.PIPE)
            output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='.picotool-export-', dir=output.parent) as stage:
                stage = Path(stage)
                run('docker', 'cp', container + ':/artifact/.', stage)
                command = ['docker', 'run', '--rm', '--platform', TARGETS[target], '--network', 'none',
                           '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges', '--user', '65534:65534',
                           '--read-only', '--ulimit', 'core=0']
                for name, arguments in [('libraries', ['--env', 'LD_TRACE_LOADED_OBJECTS=1',
                                                       '--entrypoint', str(tool.PREFIX/'picotool'), image, 'version']),
                                        ('validation', ['--entrypoint', 'python3', image, '-B', '-c',
                                                        'import picotool_install as p; p.validate(p.PREFIX/"picotool"); print("USB command contract passed")'])]:
                    checked = run(*command, *arguments, capture_output=True, timeout=120)
                    (stage/(name + '.txt')).write_text(checked.stdout + checked.stderr)
                needed = re.findall(r'\(NEEDED\).*\[([^\]]+)\]', (stage/'elf-dynamic.txt').read_text())
                libraries = (stage/'libraries.txt').read_text()
                tool.require(needed and 'not found' not in libraries and all(n in libraries for n in needed),
                             'Incomplete shared-library resolution')
                record = json.loads((stage/'package.json').read_text())
                archive = (stage/record['asset']).read_bytes()
                with tempfile.TemporaryDirectory() as verify:
                    tool.unpack(archive, record, Path(verify))
                (stage/'build.json').write_text(json.dumps({
                    'target': target, 'image': image, 'base_image': base,
                    'picotool_revision': PICOTOOL_REVISION, 'sdk_revision': SDK_REVISION,
                    'builder_sha256': tool.sha(Path(__file__).read_bytes()),
                    'dockerfile_sha256': tool.sha((REPO/'containers/picotool/Dockerfile').read_bytes())},
                    indent=2, sort_keys=True) + '\n')
                check_destination(output)
                stage.chmod(0o755)
                os.rename(stage, output)
    finally:
        subprocess.run(['docker', 'rm', '-f', container], check=False, stdout=subprocess.DEVNULL)
    print(target + ' picotool package exported to ' + str(output))


def stage_release(exports, output, tag):
    check_destination(output)
    policy = {'schema': 'wsprrypi-picotool-release/1', 'version': tool.VERSION,
              'release_tag': tag, 'picotool_revision': PICOTOOL_REVISION,
              'sdk_revision': SDK_REVISION, 'targets': {}}
    for target in TARGETS:
        directory = exports/target
        record = json.loads((directory/'package.json').read_text())
        data = (directory/record['asset']).read_bytes()
        tool.require(tool.sha(data) == record['sha256'] and len(data) == record['bytes'],
                     'Changed exported picotool archive')
        policy['targets'][target] = record
    for target in TARGETS:
        tool.policy_record(policy, target)
    output.mkdir(parents=True, exist_ok=True)
    checksums = []
    for target, record in policy['targets'].items():
        shutil.copyfile(exports/target/record['asset'], output/record['asset'])
        checksums.append(record['sha256'] + '  ' + record['asset'])
    (output/'picotool-SHA256SUMS').write_text('\n'.join(checksums) + '\n')
    (output/'picotool-manifest.json').write_text(json.dumps(policy, indent=2, sort_keys=True) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=(*TARGETS, 'stage'))
    parser.add_argument('output', type=Path)
    parser.add_argument('--inside', action='store_true')
    parser.add_argument('--exports', type=Path)
    parser.add_argument('--release-tag')
    args = parser.parse_args()
    if args.inside:
        inside(args.target, args.output)
    elif args.target == 'stage':
        if not (args.exports and args.release_tag):
            parser.error('stage requires --exports and --release-tag')
        stage_release(args.exports, args.output, args.release_tag)
    else:
        build(args.target, args.output)


if __name__ == '__main__':
    main()

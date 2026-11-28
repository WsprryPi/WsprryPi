#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assemble and verify local prepublication controller bundles."""

import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tarfile
import tempfile

import controller_binary
import controller_native_binary as native_binary
from controller_source import fingerprint
from recovery_runtime_install import MODULES


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 'wsprrypi-controller-bundle/1'
MANIFEST = 'controller-bundle.json'
MAX_ARCHIVE = 384 * 1024 * 1024
MAX_MANIFEST = 4 * 1024 * 1024
EXCLUDED_UI = {'fetch_spots.php', 'log_stream.php', 'view_diag_logs.php'}
REQUIRED_UI = {'index.php', 'controller-mode.js', 'site.js', 'ui-manifest.php',
               'ui-version.php', 'version.php', 'ui-manifest.json'}
RECOVERY_FILES = {'THIRD_PARTY_NOTICES.txt', *MODULES}
LICENSE_FILES = {'licenses/WsprryPi.txt', 'licenses/Botan.txt', 'licenses/pairing-provider.json',
                 'licenses/nlohmann-json.txt'}
INSTALL_DEPENDENCIES = {'apache2', f'libapache2-mod-{controller_binary.PHP_MODULE}', 'python3', 'systemd', 'udev', 'binutils'}
SHA256 = re.compile(r'[0-9a-f]{64}')
REVISION = re.compile(r'(?:[0-9a-f]{40}|[0-9a-f]{64})')
TARGETS = {controller_binary.PLATFORM: controller_binary.TARGETS, **native_binary.PROFILES}
WINDOWS_RECOVERY = {
    'native_files.py', 'windows_backend.py', 'windows_service.py', 'windows_owner_pipe.py', 'windows_state_owner.py',
}
APACHE_MODULES = ('authz_core', 'alias', 'dir', 'headers', 'mime', 'proxy', 'proxy_http', 'proxy_wstunnel')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, 'Duplicate manifest field')
        value[key] = item
    return value


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + '\n').encode()


def load_manifest_module(git_root):
    path = git_root / 'WsprryPi-UI' / 'scripts' / 'generate_ui_manifest.py'
    spec = importlib.util.spec_from_file_location('controller_ui_manifest', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def safe_name(name):
    if not isinstance(name, str) or len(name) > 200:
        return False
    return all(re.fullmatch(r'[A-Za-z0-9_@+.-]+', part) and not part.endswith('.')
               and not re.fullmatch(r'(?i)(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\..*)?', part)
               for part in name.split('/'))


def source_name(name):
    return (isinstance(name, str) and 0 < len(name) <= 4096 and not re.search(r'[\\\x00-\x1f]', name)
            and all(part not in ('', '.', '..') for part in name.split('/')))


def ui_member(name):
    path = PurePosixPath(name)
    return (safe_name(name) and not any(part.startswith('.') for part in path.parts)
            and path.parts[0] not in ('cache', 'backups') and path.name not in EXCLUDED_UI)


def safe_source(path, limit):
    """Read a bounded regular member, rejecting aliases and replacements."""
    if sys.platform == 'linux':
        pinned = os.open(path, os.O_PATH | os.O_NOFOLLOW)
        try:
            return read_source(Path(f'/proc/self/fd/{pinned}'), os.fstat(pinned), limit, pinned=True)
        finally:
            os.close(pinned)
    return read_source(path, path.lstat(), limit)


def read_source(path, observed, limit, pinned=False):
    require(regular_source(observed) and 0 < observed.st_size <= limit
            and (sys.platform == 'win32' or not observed.st_mode & 0o022), 'Unsafe candidate input')
    descriptor = os.open(path, os.O_RDONLY | (0 if pinned else getattr(os, 'O_NOFOLLOW', 0))
                         | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_BINARY', 0))
    with os.fdopen(descriptor, 'rb') as stream:
        info = os.fstat(stream.fileno())
        require(regular_source(info) and source_identity(info) == source_identity(observed)
                and (sys.platform != 'win32' or info.st_ino != 0), 'Candidate path was replaced')
        value = stream.read(info.st_size + 1)
        after = os.fstat(stream.fileno())
        require(len(value) == info.st_size and regular_source(after) and source_identity(info) == source_identity(after)
                and info.st_ctime_ns == after.st_ctime_ns, 'Candidate input changed while reading')
        if sys.platform == 'win32':
            # Path stat reports creation time as st_ctime on Windows. The
            # descriptor reports change time, so only compare like observations.
            current = path.lstat()
            require(regular_source(current) and source_identity(current) == source_identity(observed)
                    and current.st_ctime_ns == observed.st_ctime_ns, 'Candidate path was replaced')
    return value


def regular_source(info):
    return stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and not getattr(info, 'st_file_attributes', 0) & 0x400


def source_identity(info):
    common = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
    if sys.platform == 'win32':
        return common
    return (*common, info.st_ctime_ns, info.st_mode, info.st_uid, info.st_gid)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git_bytes(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], timeout=30)


def source_paths(root, *args):
    return [os.fsdecode(raw) for raw in git_bytes(root, *args).split(b'\0') if raw]


def snapshot(root):
    tracked = source_paths(root, 'diff', '--no-renames', '--name-only', '-z', 'HEAD', '--')
    untracked = source_paths(root, 'ls-files', '-z', '--others', '--exclude-standard')
    changes = {}
    for relative in sorted(set(tracked + untracked)):
        require(source_name(relative), 'Unsupported changed source path in build inventory')
        path = root / relative
        changes[relative] = digest(safe_source(path, MAX_ARCHIVE)) if os.path.lexists(path) else None
    return bool(tracked), changes


def runtime_packages(binary):
    return INSTALL_DEPENDENCIES | set(binary['runtime_library_packages'])


def package_versions(packages):
    versions = {}
    for name in sorted(packages):
        record = subprocess.check_output(
            ['/usr/bin/dpkg-query', '-W', '-f=${db:Status-Status}\t${Version}', name],
            text=True, timeout=10).split('\t')
        require(len(record) == 2 and record[0] == 'installed' and record[1], f'Missing Ubuntu package {name}')
        versions[name] = record[1]
    return versions


def native_payload(platform):
    windows = platform == 'windows-26300'
    suffix = '.exe' if windows else ''
    backend, age = f'bin/wsprrypi{suffix}', f'bin/recovery-age{suffix}'
    recovery = RECOVERY_FILES | (WINDOWS_RECOVERY if windows else set())
    extra = {'bin/wsprrypi-recovery-native.dll'} if windows else set()
    return backend, age, {backend, age, *extra, *LICENSE_FILES, *(f'lib/recovery/{name}' for name in recovery)}


def native_dependencies(value, files, platform):
    windows = platform == 'windows-26300'
    expected_runtime = {
        'runtime/apache/bin/httpd.exe' if windows else 'runtime/bin/httpd',
        'runtime/apache/conf/mime.types',
        'runtime/php/php8apache2_4.dll' if windows else 'runtime/php/libphp.so',
        'runtime/python.exe' if windows else 'runtime/bin/python3',
        *(f'runtime/apache/modules/mod_{name}.so' for name in
          APACHE_MODULES + (() if windows else ('mpm_prefork', 'unixd'))),
    }
    if windows:
        expected_runtime.add('runtime/php/php8ts.dll')
    required = {'apache', 'openssl', 'php', 'python', 'sqlite'}
    require(isinstance(value, dict) and required <= value.keys() and len(value) <= 128,
            'Missing native package provenance')
    observed = {}
    for name, entry in value.items():
        require(re.fullmatch(r'[a-z0-9][a-z0-9_.+-]{0,63}', name) and isinstance(entry, dict)
                and set(entry) == {'version', 'source', 'sha256', 'license', 'files'}
                and isinstance(entry['source'], str) and re.fullmatch(r'https://[^\s]{1,512}', entry['source'])
                and isinstance(entry['version'], str) and re.fullmatch(r'[A-Za-z0-9.+:~_-]{1,128}', entry['version'])
                and isinstance(entry['sha256'], str) and SHA256.fullmatch(entry['sha256'])
                and isinstance(entry['license'], str) and entry['license'].startswith('runtime/licenses/')
                and isinstance(entry['files'], dict) and entry['license'] in entry['files'],
                f'Invalid dependency provenance for {name}')
        for path, checksum in entry['files'].items():
            require(safe_name(path) and provider_member(path, platform) and path not in observed
                    and PurePosixPath(path).suffix.lower() not in ('.key', '.pem', '.p12', '.sqlite', '.ini', '.conf')
                    and isinstance(checksum, str) and SHA256.fullmatch(checksum),
                    'Invalid provider file inventory')
            observed[path] = checksum
    require(observed == {name: checksum for name, checksum in files.items() if provider_member(name, platform)}
            and expected_runtime <= observed.keys(), 'Native dependency inventory or digest differs')
    return set(observed)


def provider_member(name, platform):
    return name.startswith('runtime/') or (platform == 'windows-26300' and
        re.fullmatch(r'bin/[a-z0-9_.+-]+\.dll', name) is not None and name != 'bin/wsprrypi-recovery-native.dll')


def unpack_members(archive, root):
    records = {}
    folded = {}
    total = 0
    offset = 0
    for member in archive:
        name = member.name
        require(member.type == tarfile.REGTYPE and not member.pax_headers and safe_name(name)
                and name.casefold() not in folded and member.offset == offset and member.offset_data == offset + 512,
                'Unsafe or duplicate bundle member')
        for parent in PurePosixPath(name).parents:
            if parent == PurePosixPath('.'):
                break
            prefix = parent.as_posix()
            require(folded.setdefault(prefix.casefold(), prefix) == prefix and prefix not in records,
                    'Aliased bundle directory or file')
        folded[name.casefold()] = name
        offset += 512 + ((member.size + 511) // 512) * 512
        require(len(records) < 8192 and 0 < member.size <=
                (MAX_MANIFEST if name == MANIFEST else controller_binary.MAX_BINARY),
                'Bundle member exceeds supported bounds')
        total += member.size
        require(total <= MAX_ARCHIVE, 'Bundle payload exceeds supported bounds')
        destination = root / name
        destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        with archive.extractfile(member) as source:
            data = source.read(member.size + 1)
        require(len(data) == member.size, 'Truncated bundle member')
        with destination.open('xb') as stream:
            stream.write(data)
        records[name] = digest(data)
    return records


def validate(root, expected_architecture, records, platform=controller_binary.PLATFORM):
    wire = (root / MANIFEST).read_bytes()
    manifest = json.loads(wire, object_pairs_hook=unique)
    require(isinstance(manifest, dict) and set(manifest) ==
            {'schema', 'binary', 'files', 'ui_build', 'dependencies', 'source_snapshot', 'control', 'storage'},
            'Invalid controller bundle manifest')
    require(wire == encoded(manifest), 'Controller bundle manifest is not canonical')
    require(manifest['schema'] == SCHEMA and manifest['control'] == 'Wsprry-Control/1'
            and manifest['storage'] == 'application-state/1', 'Unsupported bundle, control, or storage format')
    files = {name: checksum for name, checksum in records.items() if name != MANIFEST}
    require(manifest['files'] == files, 'Bundle file inventory or digest differs')
    ubuntu = platform == controller_binary.PLATFORM
    if ubuntu:
        backend, age = 'bin/wsprrypi', 'bin/recovery-age'
        fixed = {backend, age, *LICENSE_FILES, *(f'recovery/{name}' for name in RECOVERY_FILES)}
    else:
        backend, age, fixed = native_payload(platform)
        fixed.update(native_dependencies(manifest['dependencies'], files, platform))
    ui = {name.removeprefix('ui/') for name in files if name.startswith('ui/')}
    require(REQUIRED_UI <= ui and all(ui_member(name) for name in ui)
            and set(files) == fixed | {f'ui/{name}' for name in ui}, 'Invalid controller payload inventory')

    if ubuntu:
        binary = controller_binary.verify(root / backend, expected_architecture, files[backend])
        controller_binary.elf_header(root / age, controller_binary.TARGETS[expected_architecture][0])
    else:
        binary = native_binary.inspect(root / backend, platform, expected_architecture, backend, files, main=True)
        binary['sha256'] = files[backend]
        native_binary.inspect(root / age, platform, expected_architecture, age, files, executable=True)
        for name in files:
            native_member = name.lower().endswith(('.dll', '.so', '.dylib', '.exe', '.pyd')) or name in (
                'runtime/bin/httpd', 'runtime/bin/python3')
            if platform.startswith('macos-') and name.startswith('runtime/') and not native_member:
                with (root / name).open('rb') as file:
                    native_member = file.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf')
            if name not in (backend, age) and native_member:
                native_binary.inspect(root / name, platform, expected_architecture, name, files,
                                      executable=name.endswith('.exe') or name in ('runtime/bin/httpd', 'runtime/bin/python3'))
    require(binary == manifest['binary'], 'Bundle executable identity differs')
    require((root / age).read_bytes() in (root / backend).read_bytes(),
            'Bundle recovery helper differs from the embedded executable image')
    dependencies = manifest['dependencies']
    require(not ubuntu or isinstance(dependencies, dict) and set(dependencies) == runtime_packages(binary)
            and all(isinstance(v, str) and re.fullmatch(r'[A-Za-z0-9.+:~_-]{1,128}', v)
                    for v in dependencies.values()), 'Invalid bundle dependency inventory')
    changes = manifest['source_snapshot']
    require(isinstance(changes, dict) and all(source_name(p) and
            (v is None or isinstance(v, str) and SHA256.fullmatch(v)) for p, v in changes.items())
            and (not binary['tracked_source_dirty'] or changes), 'Invalid source snapshot inventory')

    module = load_manifest_module(ROOT)
    ui_manifest = module.validate_manifest(json.loads((root / 'ui/ui-manifest.json').read_bytes(),
                                                        object_pairs_hook=unique))
    require(ui_manifest['source_commit'] == binary['source_commit']
            and ui_manifest['application_version'] == binary['application_version']
            and ui_manifest['packaged_ui_build_id'] == manifest['ui_build'], 'Executable and UI identity differ')
    ui_files = [{'path': name, 'sha256': files[f'ui/{name}']} for name in sorted(ui - {'ui-manifest.json'})]
    require(ui_manifest['files'] == ui_files, 'UI manifest inventory or digest differs')
    return manifest


def verify(bundle, sha256, architecture, root, platform=controller_binary.PLATFORM):
    require(architecture in TARGETS.get(platform, ()), 'Unsupported controller host profile')
    require(isinstance(sha256, str) and SHA256.fullmatch(sha256), 'Candidate SHA-256 is required')
    require(not any(root.iterdir()), 'Candidate staging directory must be empty')
    data = safe_source(bundle, MAX_ARCHIVE)
    require(digest(data) == sha256, 'Candidate archive checksum mismatch')
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:') as archive:
        records = unpack_members(archive, root)
        require(len(data) % 512 == 0 and len(data) >= archive.offset + 1024
                and not any(data[archive.offset:]), 'Unexpected data after the controller archive inventory')
    require(MANIFEST in records, 'Missing controller bundle manifest')
    return validate(root, architecture, records, platform)


def assemble(root, binary_path, binary_sha, age_path, architecture, bundle, dependencies,
             platform=controller_binary.PLATFORM, runtime=None, recovery_dll=None):
    root = root.resolve(strict=True)
    revision = git_bytes(root, 'rev-parse', 'HEAD').decode().strip()
    require(REVISION.fullmatch(revision), 'Invalid packaging checkout revision')
    before = snapshot(root)
    inputs = fingerprint(root)
    require(architecture in TARGETS.get(platform, ()) and isinstance(binary_sha, str)
            and SHA256.fullmatch(binary_sha), 'Unsupported architecture or missing executable SHA-256')
    ui_prefix = 'WsprryPi-UI/data/'
    ui = {name.removeprefix(ui_prefix): root / name for name in
          source_paths(root, 'ls-files', '-z', '--', ui_prefix) if ui_member(name.removeprefix(ui_prefix))}
    candidate = {**{f'ui/{name}': path for name, path in ui.items()},
                 'licenses/WsprryPi.txt': root / 'LICENSE.md',
                 'licenses/Botan.txt': binary_path.parent / 'BOTAN-LICENSE.txt',
                 'licenses/pairing-provider.json': binary_path.parent / 'PAIRING-PROVIDER.json',
                 'licenses/nlohmann-json.txt': root / 'docs/licenses/nlohmann-json/MIT.txt'}
    ubuntu = platform == controller_binary.PLATFORM
    if ubuntu:
        backend, age = 'bin/wsprrypi', 'bin/recovery-age'
        candidate.update({f'recovery/{name}': root / 'src/application_recovery' / name for name in RECOVERY_FILES})
    else:
        require(runtime is not None, 'Supply the sealed, platform-specific runtime directory')
        backend, age, fixed = native_payload(platform)
        candidate.update({name: root / 'src/application_recovery' / PurePosixPath(name).name
                          for name in fixed if name.startswith('lib/recovery/')})
        require(platform != 'windows-26300' or recovery_dll is not None, 'Supply the verified Windows recovery DLL')
        if recovery_dll is not None:
            candidate['bin/wsprrypi-recovery-native.dll'] = recovery_dll
        require(isinstance(dependencies, dict) and all(isinstance(item, dict) and isinstance(item.get('files'), dict)
                                                      for item in dependencies.values()), 'Invalid runtime inventory')
        for name in (name for item in dependencies.values() for name in item['files']):
            require(safe_name(name) and provider_member(name, platform), 'Unsafe dependency member name')
            path = runtime / (name.removeprefix('runtime/') if name.startswith('runtime/') else 'program-' + name)
            require(path.resolve().is_relative_to(runtime.resolve()), 'Runtime member escaped its input directory')
            candidate[name] = path
    candidate[backend], candidate[age] = binary_path, age_path
    with tempfile.TemporaryDirectory(prefix='controller-bundle-') as directory:
        stage = Path(directory)
        records = {}
        for name, source in candidate.items():
            data = safe_source(source, controller_binary.MAX_BINARY)
            destination = stage / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            records[name] = digest(data)
        require(snapshot(root) == before and fingerprint(root) == inputs
                and git_bytes(root, 'rev-parse', 'HEAD').decode().strip() == revision,
                'Packaging checkout changed during assembly')
        require(records[backend] == binary_sha, 'Executable changed during assembly')
        if ubuntu:
            binary = controller_binary.verify(stage / backend, architecture, binary_sha)
        else:
            binary = native_binary.inspect(stage / backend, platform, architecture, backend, records, main=True)
            binary['sha256'] = binary_sha
        require(binary['source_commit'] == revision and binary['tracked_source_dirty'] == before[0]
                and binary['controller_inputs_sha256'] == inputs,
                'Executable and packaging checkout differ')
        module = load_manifest_module(ROOT)
        ui_manifest = module.build_manifest(stage / 'ui', revision, binary['application_version'])
        data = module.serialize_manifest(ui_manifest)
        (stage / 'ui/ui-manifest.json').write_bytes(data)
        records['ui/ui-manifest.json'] = digest(data)
        payload = {'schema': SCHEMA, 'binary': binary, 'files': records, 'ui_build': ui_manifest['packaged_ui_build_id'],
                   'dependencies': dependencies, 'source_snapshot': before[1],
                   'control': 'Wsprry-Control/1', 'storage': 'application-state/1'}
        (stage / MANIFEST).write_bytes(encoded(payload))
        require((stage / MANIFEST).stat().st_size <= MAX_MANIFEST, 'Bundle manifest exceeds supported bounds')
        validate(stage, architecture, records, platform)

        # Publish an exact local archive. An existing path is never replaced.
        with tempfile.NamedTemporaryFile(prefix='.controller-bundle-', dir=bundle.parent) as output:
            with tarfile.open(fileobj=output, mode='w', format=tarfile.USTAR_FORMAT) as archive:
                for name in sorted([*records, MANIFEST]):
                    path = stage / name
                    info = tarfile.TarInfo(name)
                    info.size = path.stat().st_size
                    info.mode = 0o644
                    with path.open('rb') as data:
                        archive.addfile(info, data)
            require(output.tell() <= MAX_ARCHIVE, 'Bundle exceeds supported bounds')
            output.flush()
            os.fsync(output.fileno())
            os.link(output.name, bundle)
        if sys.platform != 'win32':
            directory = os.open(bundle.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    return digest(safe_source(bundle, MAX_ARCHIVE))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    pack = commands.add_parser('pack')
    pack.add_argument('--binary', type=Path, required=True)
    pack.add_argument('--binary-sha256', required=True)
    pack.add_argument('--recovery-age', type=Path, required=True)
    pack.add_argument('--runtime', type=Path)
    pack.add_argument('--dependency-manifest', type=Path)
    pack.add_argument('--recovery-dll', type=Path)
    check = commands.add_parser('verify')
    check.add_argument('--sha256', required=True)
    check.add_argument('--stage', type=Path, help='existing private, empty staging directory')
    for command in (pack, check):
        command.add_argument('--architecture', choices=('amd64', 'arm64'), required=True)
        command.add_argument('--platform', choices=TARGETS, default=controller_binary.PLATFORM)
        command.add_argument('bundle', type=Path)
    args = parser.parse_args()
    try:
        require(args.architecture in TARGETS[args.platform], 'Unsupported controller host profile')
        if args.command == 'pack':
            if args.platform == controller_binary.PLATFORM:
                require(not args.runtime and not args.dependency_manifest and not args.recovery_dll,
                        'Native runtime overrides are unavailable in the Ubuntu bundle format')
                binary = controller_binary.verify(args.binary, args.architecture, args.binary_sha256)
                dependencies = package_versions(runtime_packages(binary))
            else:
                require(args.dependency_manifest is not None, 'Supply the pinned dependency manifest')
                dependencies = json.loads(safe_source(args.dependency_manifest, MAX_MANIFEST), object_pairs_hook=unique)
            print(assemble(ROOT, args.binary, args.binary_sha256, args.recovery_age, args.architecture,
                           args.bundle, dependencies, args.platform, args.runtime, args.recovery_dll))
        else:
            if args.stage is None:
                with tempfile.TemporaryDirectory(prefix='controller-bundle-') as directory:
                    result = verify(args.bundle, args.sha256, args.architecture, Path(directory), args.platform)
            else:
                info = args.stage.lstat()
                require(stat.S_ISDIR(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400
                        and (sys.platform == 'win32' or info.st_uid == os.geteuid() and not info.st_mode & 0o077),
                        'The staging directory must be private and owned')
                result = verify(args.bundle, args.sha256, args.architecture, args.stage, args.platform)
            print(json.dumps({'binary': result['binary'], 'ui_build': result['ui_build']}, sort_keys=True))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f'Controller bundle rejected: {error}\n')


if __name__ == '__main__':
    main()

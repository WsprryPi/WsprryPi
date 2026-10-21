#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read bundled RP1 evidence independently of operation permission.

Evidence is installed with this application; no operation downloads metadata.
Provider inspection and evidence attribution do not change operating policy.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = {'rp1-qualification.json', 'rp1-qualification.schema.json', 'qualification_status.py', 'SHA256SUMS'}
BUNDLE_NAME = 'rp1-qualification'
HELPER_FILES = {'rp1_qualification.py', 'rp1-qualification-lock.json'}
INSTALLED_FILES = HELPER_FILES | {BUNDLE_NAME + '/' + name for name in ASSETS}
RUNTIME = Path('/usr/local/lib/wsprrypi')
OWNERSHIP = Path('/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json')
PROVIDER = Path('/usr/lib/rp1-gpclk-dkms/runtime_provider.py')
OWNER_UID = 0


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def strict_json(raw):
    def pairs(items):
        value = {}
        for key, item in items:
            require(key not in value, 'duplicate JSON field: ' + key)
            value[key] = item
        return value
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda _: require(False, 'nonfinite JSON number'))


def lock_path():
    source = ROOT / 'release_tools/rp1-qualification-lock.json'
    return source if source.is_file() else Path(__file__).with_name('rp1-qualification-lock.json')


def bundle_path():
    source = ROOT / 'release_tools' / BUNDLE_NAME
    return source if source.is_dir() else Path(__file__).with_name(BUNDLE_NAME)


def read(path, limit=4*1024*1024, trusted=False):
    for parent in path.parents:
        require(not parent.is_symlink(), 'symlink parent refused')
        if trusted and parent.exists():
            info = parent.stat()
            require(info.st_uid == OWNER_UID and not info.st_mode & 0o022, 'unsafe parent ownership')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_size <= limit and
                (not trusted or (info.st_uid == OWNER_UID and not info.st_mode & 0o022)),
                'unsafe or oversized metadata file')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(limit+1)
        require(len(data) <= limit, 'metadata limit exceeded')
        return data
    finally:
        os.close(fd)


def validate_assets(assets, lock):
    require(set(assets) == set(lock['assets']) == ASSETS, 'supplement asset set differs')
    for name, data in assets.items():
        require(sha(data) == lock['assets'][name], 'supplement digest differs: ' + name)
    sums = ''.join(lock['assets'][n] + '  ' + n + '\n' for n in sorted(ASSETS - {'SHA256SUMS'})).encode()
    require(assets['SHA256SUMS'] == sums, 'supplement checksum inventory differs')
    # Only the source-reviewed, digest-pinned supplemental interpreter is executed.
    scope = {'__name__': 'pinned_rp1_qualification'}
    exec(compile(assets['qualification_status.py'], 'pinned qualification_status.py', 'exec'), scope)
    metadata = scope['validate'](assets['rp1-qualification.json'], lock)
    return metadata, scope['interpret']


def evidence_assets(directory, lock, trusted=False):
    if not directory.exists() and not directory.is_symlink():
        raise FileNotFoundError('bundled evidence absent')
    require(directory.is_dir() and not directory.is_symlink(), 'unsafe evidence directory')
    require({p.name for p in directory.iterdir()} == ASSETS, 'bundled evidence asset set differs')
    assets = {name: read(directory / name, trusted=trusted) for name in ASSETS}
    validate_assets(assets, lock)
    return assets


def status(lock, route, report=None, ownership=None, directory=None):
    live_inspection = report is None
    base = {'status': 'missing', 'state': 'Evidence missing', 'permissionGate': False,
            'applicationEligibility': 'WsprryPi band/mode policy'}
    try:
        if report is None:
            provider = PROVIDER
            read(provider, trusted=True)
            with tempfile.TemporaryFile() as stream:
                # The released provider imports its root-owned sibling modules.
                # Ignore Python environment/user-site injection while retaining
                # that installed script directory in the import path.
                process = subprocess.run(['/usr/bin/python3', '-E', '-s', '-B', str(provider), 'inspect'],
                    stdout=stream, stderr=subprocess.DEVNULL, timeout=8, check=False)
                require(stream.tell() <= 4*1024*1024, 'runtime inspection response exceeded bound')
                stream.seek(0)
                report = strict_json(stream.read())
                require(process.returncode in (0, 10, 11, 12, 13, 14), 'runtime inspection failed')
        require(isinstance(report, dict), 'malformed runtime inspection')
        safety = report.get('safety', {})
        base['operationalReady'] = safety.get('operationalReady', 'unknown')
        base['currentOutput'] = safety
        snapshot = report.get('manager', {}).get('idle', {}).get('state', {}).get('outputLifecycle', {}).get('snapshot', {})
        base['structuralCompatibility'] = {3: 'Compatible-unqualified', 4: 'Unavailable',
                                          5: 'Rejected'}.get(snapshot.get('compatibility'), 'Unknown')
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError) as error:
        report = {}
        base['providerInspection'] = str(error)
    try:
        assets = evidence_assets(directory or bundle_path(), lock, trusted=live_inspection)
        metadata, interpret = validate_assets(assets, lock)
    except FileNotFoundError:
        return base
    except (OSError, ValueError, KeyError, TypeError) as error:
        return {**base, 'status': 'invalid', 'state': 'Evidence invalid', 'reason': str(error)}
    try:
        if ownership is None:
            ownership = strict_json(read(OWNERSHIP, trusted=True))
        return interpret(metadata, report, route or report.get('routes', {}).get('active'), ownership)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        return {**base, 'status': 'unattributed', 'state': 'Provider identity unverified', 'reason': str(error)}


def write_atomic(path, data, mode=0o644, trusted=True):
    for parent in path.parents:
        require(not parent.is_symlink(), 'symlink destination parent refused')
        if trusted:
            info = parent.stat()
            require(info.st_uid == OWNER_UID and not info.st_mode & 0o022, 'unsafe destination parent')
    require(not path.is_symlink(), 'symlink destination refused')
    fd, temporary = tempfile.mkstemp(prefix='.qualification-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            os.fchmod(stream.fileno(), mode)
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)


def install_runtime(lock):
    """Install the helper, reference and verified evidence with one receipt."""
    require(os.geteuid() == OWNER_UID, 'root required for application helper installation')
    receipt = RUNTIME / 'rp1-qualification-files.json'
    assets = evidence_assets(bundle_path(), lock)
    targets = {'rp1_qualification.py': read(Path(__file__)),
               'rp1-qualification-lock.json': read(lock_path()),
               **{BUNDLE_NAME + '/' + name: raw for name, raw in assets.items()}}
    RUNTIME.mkdir(parents=True, exist_ok=True)
    previous = strict_json(read(receipt, trusted=True)) if receipt.exists() else {}
    require(isinstance(previous, dict) and (not previous or
            set(previous) in (HELPER_FILES, INSTALLED_FILES)), 'invalid helper receipt')
    bundle = RUNTIME / BUNDLE_NAME
    require(not bundle.is_symlink() and (not bundle.exists() or
            (bundle.is_dir() and {p.name for p in bundle.iterdir()} <= ASSETS)),
            'foreign evidence directory retained')
    for name in targets:
        destination = RUNTIME / name
        if destination.exists() or destination.is_symlink():
            require(sha(read(destination, trusted=True)) in {previous.get(name), sha(targets[name])},
                    'foreign or modified qualification helper destination')
    # Old and desired digests above permit retries after an interrupted update.
    pending = {name: sha(raw) for name, raw in targets.items()}
    bundle.mkdir(exist_ok=True)
    # Install evidence before publishing the helper that reads it.
    targets = dict(sorted(targets.items(), key=lambda item: '/' not in item[0]))
    for name, raw in targets.items():
        write_atomic(RUNTIME / name, raw, 0o755 if name.endswith('.py') else 0o644)
    write_atomic(receipt, json.dumps(pending, sort_keys=True).encode())


def remove_runtime():
    require(os.geteuid() == OWNER_UID, 'root required for application helper removal')
    receipt = RUNTIME / 'rp1-qualification-files.json'
    if not receipt.exists():
        require(not any((RUNTIME / name).exists() or (RUNTIME / name).is_symlink()
                        for name in (*HELPER_FILES, BUNDLE_NAME)), 'unattributed helper files remain')
        return
    previous = strict_json(read(receipt, trusted=True))
    require(isinstance(previous, dict) and set(previous) in (HELPER_FILES, INSTALLED_FILES),
            'invalid helper receipt')
    bundle = RUNTIME / BUNDLE_NAME
    if bundle.exists() or bundle.is_symlink():
        require(set(previous) == INSTALLED_FILES and bundle.is_dir() and not bundle.is_symlink() and
                {p.name for p in bundle.iterdir()} <= ASSETS, 'foreign evidence directory retained')
    for name, digest in previous.items():
        path = RUNTIME / name
        if path.exists() or path.is_symlink():
            require(sha(read(path, trusted=True)) == digest, 'modified helper file retained: ' + name)
    for name in previous:
        (RUNTIME / name).unlink(missing_ok=True)
    if bundle.exists():
        bundle.rmdir()
    receipt.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['status', 'resolve', 'install-runtime', 'remove-runtime', 'refresh'])
    parser.add_argument('--lock', type=Path)
    parser.add_argument('--route', choices=['gpio4', 'gpio20'])
    parser.add_argument('--provider-report', type=Path)
    parser.add_argument('--ownership', type=Path)
    parser.add_argument('--directory', type=Path,
                        help='offline evidence input for status, or export destination for resolve/refresh')
    args = parser.parse_args()
    try:
        if args.operation == 'remove-runtime':
            remove_runtime()
            return
        lock = strict_json(read(args.lock or lock_path()))
        if args.operation == 'install-runtime':
            install_runtime(lock)
            return
        if args.operation in ('resolve', 'refresh'):
            # Retain legacy command names as offline verification/export aliases.
            assets = evidence_assets(bundle_path(), lock)
            destination = args.directory
            if destination is not None:
                require(not destination.is_symlink(), 'symlink evidence destination refused')
                destination.mkdir(parents=True, exist_ok=True)
                for name, raw in assets.items():
                    write_atomic(destination / name, raw, trusted=args.operation == 'refresh')
            print(json.dumps({'status': 'verified', 'source': 'bundled', 'metadataTag': lock['metadataTag'],
                              'assets': lock['assets']}))
            return
        observed = strict_json(read(args.provider_report)) if args.provider_report else None
        ownership = strict_json(read(args.ownership)) if args.ownership else None
        print(json.dumps(status(lock, args.route, observed, ownership, args.directory), sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError) as error:
        if args.operation == 'status':
            print(json.dumps({'status': 'invalid', 'state': 'Evidence invalid', 'reason': str(error),
                              'permissionGate': False}))
        else:
            raise SystemExit(str(error))


if __name__ == '__main__':
    main()

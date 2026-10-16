#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Resolve pinned RP1 evidence and report it independently of operation permission.

Status is offline. Refresh downloads only a separately pinned metadata release;
neither operation installs a provider, opens an endpoint or changes its policy.
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
BASE = 'https://github.com/WsprryPi/RP1-GPCLK-DKMS/releases/'
ASSETS = {'rp1-qualification.json', 'rp1-qualification.schema.json', 'qualification_status.py', 'SHA256SUMS'}
CACHE = Path('/var/cache/wsprrypi/rp1-qualification')
RUNTIME = Path('/usr/local/lib/wsprrypi')
OWNERSHIP = Path('/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json')
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


def resolve(client, lock):
    tag = lock['metadataTag']
    require(tag == 'qualification-v1.0.0-r' + str(lock['revision']), 'invalid supplemental tag')
    matches = [r for r in client.releases() if r.get('tag_name') == tag]
    require(len(matches) == 1, 'missing or ambiguous supplemental publication')
    release = matches[0]
    require(release.get('draft') is False and release.get('prerelease') is False and
            release.get('immutable') is True and release.get('published_at') and
            release.get('html_url') == BASE + 'tag/' + tag, 'unpublished or mutable supplemental release')
    require(client.tag_commit(tag) == lock['metadataSource']['commit'], 'supplement tag source differs')
    commit_object = client.json(installer_api() + '/git/commits/' + lock['metadataSource']['commit'])
    require(commit_object.get('tree', {}).get('sha') == lock['metadataSource']['tree'],
            'supplement source tree differs')
    require(client.tag_commit('v1.0.0') == lock['software']['sourceCommit'], 'original software tag moved')
    original_ref = client.json(installer_api() + '/git/ref/tags/v1.0.0')
    require(original_ref.get('object', {}).get('type') == 'tag' and
            original_ref['object']['sha'] == lock['software']['tagObject'], 'original annotated tag object moved')
    objects = release.get('assets', [])
    require(len(objects) == len(ASSETS) and {a.get('name') for a in objects} == ASSETS,
            'missing, duplicate or unexpected supplemental assets')
    result = {}
    for asset in objects:
        name = asset['name']
        url = BASE + 'download/' + tag + '/' + name
        require(asset.get('state') == 'uploaded' and asset.get('browser_download_url') == url and
                asset.get('digest') == 'sha256:' + lock['assets'][name], 'supplement asset provenance differs')
        result[name] = client.bytes(url, limit=4*1024*1024, allow_asset_redirect=True)
    validate_assets(result, lock)
    return result


def installer_api():
    return 'https://api.github.com/repos/WsprryPi/RP1-GPCLK-DKMS'


def cached(directory, lock, trusted=True):
    if not directory.exists() and not directory.is_symlink():
        raise FileNotFoundError('supplement cache absent')
    require(directory.is_dir() and not directory.is_symlink(), 'supplement cache absent')
    require({p.name for p in directory.iterdir()} == ASSETS, 'supplement cache asset set differs')
    return validate_assets({name: read(directory / name, trusted=trusted) for name in ASSETS}, lock)


def status(lock, route, report=None, ownership=None, directory=CACHE):
    live_inspection = report is None
    base = {'status': 'missing', 'state': 'Evidence missing', 'permissionGate': False,
            'applicationEligibility': 'WsprryPi band/mode policy'}
    try:
        if report is None:
            provider = Path('/usr/lib/rp1-gpclk-dkms/runtime_provider.py')
            read(provider, trusted=True)
            with tempfile.TemporaryFile() as stream:
                process = subprocess.run(['/usr/bin/python3', '-I', str(provider), 'inspect'],
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
        metadata, interpret = cached(directory, lock, trusted=live_inspection)
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
    """Install only application-owned helper/reference with a separate receipt."""
    require(os.geteuid() == OWNER_UID, 'root required for application helper installation')
    receipt = RUNTIME / 'rp1-qualification-files.json'
    targets = {'rp1_qualification.py': read(Path(__file__)),
               'rp1-qualification-lock.json': read(lock_path())}
    RUNTIME.mkdir(parents=True, exist_ok=True)
    previous = strict_json(read(receipt, trusted=True)) if receipt.exists() else {}
    for name in targets:
        destination = RUNTIME / name
        if destination.exists() or destination.is_symlink():
            require(sha(read(destination, trusted=True)) in {previous.get(name), sha(targets[name])},
                    'foreign or modified qualification helper destination')
    # Receipt covers old and new bytes during an interrupted update.
    pending = {name: sha(raw) for name, raw in targets.items()}
    for name, raw in targets.items():
        write_atomic(RUNTIME / name, raw, 0o755 if name.endswith('.py') else 0o644)
    write_atomic(receipt, json.dumps(pending, sort_keys=True).encode())


def remove_runtime():
    require(os.geteuid() == OWNER_UID, 'root required for application helper removal')
    receipt = RUNTIME / 'rp1-qualification-files.json'
    if not receipt.exists():
        require(not any((RUNTIME / name).exists() for name in
            ('rp1_qualification.py', 'rp1-qualification-lock.json')), 'unattributed helper files remain')
        return
    previous = strict_json(read(receipt, trusted=True))
    require(set(previous) == {'rp1_qualification.py', 'rp1-qualification-lock.json'}, 'invalid helper receipt')
    for name, digest in previous.items():
        path = RUNTIME / name
        if path.exists() or path.is_symlink():
            require(sha(read(path, trusted=True)) == digest, 'modified helper file retained: ' + name)
    for name in previous:
        (RUNTIME / name).unlink(missing_ok=True)
    receipt.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['status', 'resolve', 'install-runtime', 'remove-runtime', 'refresh'])
    parser.add_argument('--lock', type=Path)
    parser.add_argument('--route', choices=['gpio4', 'gpio20'])
    parser.add_argument('--provider-report', type=Path)
    parser.add_argument('--ownership', type=Path)
    parser.add_argument('--directory', type=Path, default=CACHE)
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
            import rp1_gpclk_dkms_install as installer
            assets = resolve(installer.GitHubClient(os.environ.get('GITHUB_TOKEN')), lock)
            destination = args.directory
            require(not destination.is_symlink(), 'symlink cache refused')
            destination.mkdir(parents=True, exist_ok=True)
            for name, raw in assets.items():
                write_atomic(destination / name, raw, trusted=args.operation == 'refresh')
            print(json.dumps({'status': 'verified', 'metadataTag': lock['metadataTag'],
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

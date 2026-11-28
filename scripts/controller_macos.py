#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install a verified local macOS controller bundle or render its launchd jobs."""

import argparse
import fcntl
import grp
import http.client
import json
import os
from pathlib import Path
import platform
import plistlib
import pwd
import re
import stat
import subprocess
import sys
import tarfile
import tempfile
import time

from application_state_install import listener_configuration
import controller_apache as apache
import controller_bundle as bundles
from controller_linux import atomic, directory, run, sync, symlink, trusted_file, trusted_link, trusted_parents

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from application_recovery.lifecycle import MAC_BACKEND, MAC_LABEL, MAC_RUNTIME, mac_backend_definition, mac_service_definition
from application_recovery import proxy


ROOT = '/Library/Application Support/WsprryPi'
PROGRAM = ROOT + '/Program'
INSTALLATION = Path(ROOT)
RELEASES = INSTALLATION / 'Releases'
RECEIPT = INSTALLATION / 'installation.json'
CONFIG = INSTALLATION / 'Configuration'
LAUNCHD = Path('/Library/LaunchDaemons')
LOG_ROOT = Path('/Library/Logs/WsprryPi')
ACCOUNT_NAMES = {'_wsprrypi': 'Controller', '_wsprrypiweb': 'Web', '_wsprrypihttp': 'Http'}
BACKEND_STATE = INSTALLATION / 'Controller/application-state'
MAINTENANCE = INSTALLATION / 'Maintenance'
START_ORDER = (MAC_LABEL + '.maintenance', MAC_LABEL, MAC_LABEL + '.web', MAC_LABEL + '.http')
FRESH_PORTS = (31415, 31416)


def definitions():
    return (
        mac_backend_definition(),
        mac_service_definition(MAC_LABEL + '.web', [MAC_BACKEND, '--web-service', '--runtime-directory', MAC_RUNTIME],
                               '_wsprrypiweb', '_wsprrypiweb', ROOT + '/Web', 'Web'),
        mac_service_definition(MAC_LABEL + '.maintenance', [MAC_BACKEND, '--maintenance-worker', '--role', 'controller'],
                               'root', '_wsprrypiweb', ROOT + '/Maintenance', 'Maintenance'),
        mac_service_definition(MAC_LABEL + '.http', [PROGRAM + '/runtime/bin/httpd', '-DFOREGROUND',
                                                   '-f', ROOT + '/Configuration/httpd.conf'],
                               '_wsprrypihttp', '_wsprrypihttp', ROOT + '/Http', 'Http'),
    )


def files(web, socket, profile):
    result = {LAUNCHD / (job['Label'] + '.plist'): plistlib.dumps(job) for job in definitions()}
    result[CONFIG / 'httpd.conf'] = apache.native_config(PROGRAM, CONFIG, LOG_ROOT / 'Http', profile,
                                                        runtime=INSTALLATION / 'Http').encode()
    result[CONFIG / 'php.ini'] = apache.PHP_CONFIG.encode()
    result[CONFIG / 'proxy.conf'] = apache.proxy_config(Path(PROGRAM) / 'ui', web, socket).encode()
    return result


def exists(path):
    return os.path.lexists(path)


def load_receipt():
    directory(INSTALLATION, create=False)
    observed = RECEIPT.lstat()
    raw = bundles.safe_source(RECEIPT, bundles.MAX_MANIFEST)
    bundles.require(observed.st_uid == 0 and stat.S_IMODE(observed.st_mode) == 0o600,
                    'Controller receipt must be root-private')
    value = json.loads(raw, object_pairs_hook=bundles.unique)
    bundles.require(isinstance(value, dict) and set(value) == {'schema', 'accounts', 'slots', 'active'}
                    and value['schema'] == 'macos-controller-installation/1'
                    and isinstance(value['accounts'], dict) and set(value['accounts']) == set(ACCOUNT_NAMES)
                    and all(type(uid) is int and 200 <= uid < 500 for uid in value['accounts'].values())
                    and len(set(value['accounts'].values())) == len(ACCOUNT_NAMES)
                    and isinstance(value['slots'], list) and len(value['slots']) == len(set(value['slots']))
                    and all(isinstance(sha, str) and bundles.SHA256.fullmatch(sha) for sha in value['slots'])
                    and (value['active'] is None or value['active'] in value['slots']), 'Malformed macOS installation receipt')
    return value


def account(name):
    try:
        return pwd.getpwnam(name)
    except KeyError:
        return None


def group(name):
    try:
        return grp.getgrnam(name)
    except KeyError:
        return None


def reserve_accounts():
    occupied = {entry.pw_uid for entry in pwd.getpwall()} | {entry.gr_gid for entry in grp.getgrall()}
    available = iter(uid for uid in range(499, 199, -1) if uid not in occupied)
    result = {name: next(available, None) for name in ACCOUNT_NAMES}
    bundles.require(all(result.values()), 'No available system service UID/GID')
    return result


def provision_accounts(receipt, create=False):
    owners = {}
    for name, uid in receipt['accounts'].items():
        home = str(INSTALLATION / ACCOUNT_NAMES[name])
        user_path, group_path = '/Users/' + name, '/Groups/' + name
        entry = account(name)
        if create:
            found = group(name)
            bundles.require(found is None or found.gr_gid == uid and not found.gr_mem, 'Controller group conflict')
            if found is None:
                run('/usr/bin/dscl', '.', '-create', group_path, 'PrimaryGroupID', uid)
            if entry is None:
                # Each create specifies the attribute it owns. An interrupted
                # provisioning attempt resumes using the receipt's reserved ID.
                run('/usr/bin/dscl', '.', '-create', user_path, 'UniqueID', uid)
            else:
                bundles.require(entry.pw_uid == uid, 'Controller UID conflict')
            for key, value in (('PrimaryGroupID', uid), ('NFSHomeDirectory', home),
                               ('UserShell', '/usr/bin/false'), ('Password', '*'), ('IsHidden', 1)):
                run('/usr/bin/dscl', '.', '-create', user_path, key, value)
            entry = account(name)
        found = group(name)
        bundles.require(entry and found and entry.pw_uid == entry.pw_gid == found.gr_gid == uid
                        and entry.pw_dir == home and entry.pw_shell == '/usr/bin/false' and not found.gr_mem
                        and not any(name in other.gr_mem for other in grp.getgrall()),
                        f'Missing or changed service account {name}')
        owners[name] = entry
    return owners


def launchctl(*args):
    return run('/bin/launchctl', *args)


def observed_job(label):
    try:
        return launchctl('print', 'system/' + label).decode()
    except subprocess.CalledProcessError as error:
        # launchctl reports an absent service as the fixed EX_NOPERM/ESRCH
        # diagnostic. Any other launchd error stops the transaction.
        message = (error.stderr or b'').decode(errors='replace')
        if 'Could not find service' in message:
            return None
        raise


def check_managed(receipt, profile):
    expected = files(*FRESH_PORTS, profile)
    if receipt is None:
        for path in (INSTALLATION, LOG_ROOT, *expected.keys()):
            bundles.require(not exists(path), f'Unowned controller path {path}')
        bundles.require(not any(account(name) or group(name) for name in ACCOUNT_NAMES),
                        'Unowned macOS controller service accounts')
    else:
        for path, data in expected.items():
            if path == CONFIG / 'proxy.conf' and exists(path):
                trusted_parents(path)
                observed = bundles.safe_source(path, bundles.MAX_MANIFEST)
                normalized, found = proxy.update_ports(observed, *FRESH_PORTS)
                bundles.require(found and normalized == data and path.lstat().st_uid == 0,
                                'Unrecognized controller proxy configuration')
            else:
                if exists(path):
                    trusted_file(path, data)
        trusted_link(Path(PROGRAM), [RELEASES / sha for sha in receipt['slots']])
    for label in START_ORDER:
        current = observed_job(label)
        if current is not None:
            target = LAUNCHD / (label + '.plist')
            reported = re.findall(r'^\s*path = ([^\r\n]+)$', current, re.MULTILINE)
            bundles.require(receipt is not None and target.is_file() and reported == [str(target)],
                            f'Unowned launchd service {label}')


def require_idle():
    if observed_job(MAC_LABEL + '.maintenance') is not None:
        value = json.loads(run(MAC_BACKEND, '--maintenance', 'jobs'))
        bundles.require(isinstance(value, dict) and value.get('schema') == 'recovery-job-list/1'
                        and 'active' in value and value['active'] is None, 'An active or unknown recovery job is retained')
    elif exists(MAINTENANCE / 'jobs.json'):
        path = MAINTENANCE / 'jobs.json'
        trusted_parents(path)
        observed = path.lstat()
        registry = json.loads(bundles.safe_source(path, bundles.MAX_MANIFEST), object_pairs_hook=bundles.unique)
        bundles.require(observed.st_uid == 0 and stat.S_IMODE(observed.st_mode) == 0o600
                        and isinstance(registry, dict) and registry.get('schema') == 'recovery-jobs/1'
                        and registry.get('role') == 'controller' and 'active' in registry and registry['active'] is None,
                        'A stopped maintenance worker still owns recovery work')


def bootout(label):
    current = observed_job(label)
    if current is None:
        return
    pids = re.findall(r'^\s*pid = (\d+)\s*$', current, re.MULTILINE)
    bundles.require(len(pids) < 2 and (not pids or int(pids[0]) > 1), f'Unexpected service PID for {label}')
    pid = int(pids[0]) if pids else None
    launchctl('bootout', 'system/' + label)
    deadline = time.monotonic() + 40
    while True:
        bundles.require(time.monotonic() < deadline, f'launchd retained service {label}')
        if observed_job(label) is None:
            if pid is None:
                return
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return
        time.sleep(.1)


def bootstrap(label):
    launchctl('bootstrap', 'system', str(LAUNCHD / (label + '.plist')))


def retire_launch_jobs():
    for label in START_ORDER:
        (LAUNCHD / (label + '.plist')).unlink(missing_ok=True)
    sync(LAUNCHD)


def quiesce():
    require_idle()
    web = observed_job(MAC_LABEL + '.web') is not None
    for label in (MAC_LABEL + '.http', MAC_LABEL + '.web'):
        bootout(label)
    try:
        require_idle()
    except Exception:
        if web:
            bootstrap(MAC_LABEL + '.web')
            bootstrap(MAC_LABEL + '.http')
        raise
    bootout(MAC_LABEL)
    bootout(MAC_LABEL + '.maintenance')


def stage_slot(source, manifest, target):
    directory(RELEASES)
    if not exists(target):
        with tempfile.TemporaryDirectory(prefix='.controller-slot-', dir=RELEASES) as name:
            slot = Path(name) / 'candidate'
            slot.mkdir(mode=0o755)
            for relative in sorted([*manifest['files'], bundles.MANIFEST]):
                path = slot / relative
                path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
                mode = 0o755 if relative.startswith(('bin/', 'runtime/bin/')) else 0o644
                atomic(path, bundles.safe_source(source / relative, bundles.MAX_ARCHIVE), mode)
            sync(slot)
            os.rename(slot, target)
            sync(RELEASES)
    directory(target, create=False)
    expected = {**manifest['files'], bundles.MANIFEST: bundles.digest(bundles.encoded(manifest))}
    observed = {path.relative_to(target).as_posix() for path in target.rglob('*') if path.is_symlink() or not path.is_dir()}
    bundles.require(observed == set(expected), 'Retained macOS slot inventory changed')
    for relative, checksum in expected.items():
        path = target / relative
        trusted_parents(path)
        mode = 0o755 if relative.startswith(('bin/', 'runtime/bin/')) else 0o644
        info = path.lstat()
        bundles.require(info.st_uid == 0 and stat.S_IMODE(info.st_mode) == mode
                        and bundles.digest(bundles.safe_source(path, bundles.MAX_ARCHIVE)) == checksum,
                        'Retained macOS slot content or permissions changed')


def provision_directories(owners):
    web_gid = owners['_wsprrypiweb'].pw_gid
    directory(CONFIG)
    for name, entry in owners.items():
        directory(Path(entry.pw_dir), entry.pw_uid, entry.pw_gid, 0o700)
    directory(MAINTENANCE, mode=0o700)
    directory(INSTALLATION / 'Run')
    directory(Path(MAC_RUNTIME), owners['_wsprrypi'].pw_uid, web_gid, 0o750)
    directory(INSTALLATION / 'Run/Maintenance', gid=web_gid, mode=0o750)
    directory(LOG_ROOT)
    logs = {ACCOUNT_NAMES[name]: (entry.pw_uid, entry.pw_gid) for name, entry in owners.items()}
    logs['Maintenance'] = (0, 0)
    for role, (uid, gid) in logs.items():
        parent = LOG_ROOT / role
        directory(parent)
        names = ('service.log', 'service-error.log', 'error.log') if role == 'Http' else ('service.log', 'service-error.log')
        for name in names:
            path = parent / name
            if not exists(path):
                fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
                try:
                    os.fchown(fd, uid, gid)
                    os.fsync(fd)
                finally:
                    os.close(fd)
                sync(parent)
            observed = path.lstat()
            bundles.require(stat.S_ISREG(observed.st_mode) and observed.st_nlink == 1
                            and (observed.st_uid, observed.st_gid) == (uid, gid)
                            and stat.S_IMODE(observed.st_mode) == 0o600, f'Unsafe service log {path}')


def wait_ready(owners, manifest):
    end = time.monotonic() + 60
    uids = {MAC_LABEL: owners['_wsprrypi'].pw_uid, MAC_LABEL + '.web': owners['_wsprrypiweb'].pw_uid,
            MAC_LABEL + '.http': owners['_wsprrypihttp'].pw_uid, MAC_LABEL + '.maintenance': 0}
    while time.monotonic() < end:
        connection = http.client.HTTPConnection('127.0.0.1', apache.HTTP_PORT, timeout=3)
        try:
            connection.request('GET', '/wsprrypi/version')
            response = connection.getresponse()
            if response.status == 200:
                document = json.loads(response.read(128 * 1024))
                if document.get('host_status', {}).get('controller_ready') is True:
                    bundles.require(document['host_status'].get('role') == 'controller-only'
                                    and document.get('wspr_commit') == manifest['binary']['source_commit']
                                    and document.get('wspr_exe_version') == manifest['binary']['application_version']
                                    and document.get('ancillary_gpio') is False and document.get('compiled_backends') == '',
                                    'Unexpected installed controller identity')
                    for label, uid in uids.items():
                        current = observed_job(label)
                        pid = re.findall(r'^\s*pid = (\d+)\s*$', current or '', re.MULTILINE)
                        bundles.require(len(pid) == 1 and int(pid[0]) > 1
                                        and run('/bin/ps', '-p', pid[0], '-o', 'uid=').strip() == str(uid).encode(),
                                        f'Unexpected effective service identity for {label}')
                    return
        except (OSError, http.client.HTTPException):
            pass
        finally:
            connection.close()
        time.sleep(.25)
    raise ValueError('Installed controller did not become ready through Apache')


def install(source, manifest, sha, profile, receipt):
    check_managed(receipt, profile)
    if receipt is None:
        receipt = {'schema': 'macos-controller-installation/1', 'accounts': reserve_accounts(), 'slots': [], 'active': None}
        with tempfile.TemporaryDirectory(prefix='.controller-installation-', dir=INSTALLATION.parent) as folder:
            candidate = Path(folder) / 'installation'
            directory(candidate)
            atomic(candidate / RECEIPT.name, bundles.encoded(receipt), 0o600)
            os.rename(candidate, INSTALLATION)
            sync(INSTALLATION.parent)
    owners = provision_accounts(receipt, create=not receipt['slots'])
    if sha not in receipt['slots']:
        receipt['slots'].append(sha)
        atomic(RECEIPT, bundles.encoded(receipt), 0o600)
    provision_directories(owners)
    backend = owners['_wsprrypi']
    slot = RELEASES / sha
    stage_slot(source, manifest, slot)
    image = slot / 'bin/wsprrypi'
    bundles.require(run(image, '--storage-format', owner=backend).strip() == b'application-state/1'
                    and bundles.digest(run(image, '--export-recovery-age-helper', owner=backend)) == manifest['files']['bin/recovery-age'],
                    'Selected executable cannot verify its storage and recovery runtime')
    quiesce()
    # Disarm boot before inspecting or initializing state for the selected image.
    retire_launch_jobs()
    if not exists(BACKEND_STATE):
        bundles.require(receipt['active'] is None, 'Established controller state is missing')
        run(image, '--controller-only', '--initialize-state', '--state-directory', BACKEND_STATE, owner=backend)
    listeners = listener_configuration(run(image, '--controller-only', '--check-state', '--state-directory',
                                           BACKEND_STATE, '--listener-bootstrap', owner=backend))
    bundles.require(listeners['role'] == 'controller', 'Saved role differs from the selected controller role')
    rendered = files(listeners['web_port'], listeners['socket_port'], profile)
    for path, data in rendered.items():
        if path.parent != LAUNCHD:
            atomic(path, data)
    symlink(Path(PROGRAM), slot)
    for owner in (owners['_wsprrypiweb'], owners['_wsprrypihttp']):
        run('/bin/test', '!', '-r', BACKEND_STATE, owner=owner)
        run('/bin/test', '-r', Path(PROGRAM) / 'ui/index.php', owner=owner)
    run(Path(PROGRAM) / 'runtime/bin/httpd', '-t', '-f', CONFIG / 'httpd.conf', owner=owners['_wsprrypihttp'])
    started = []
    try:
        for path, data in rendered.items():
            if path.parent == LAUNCHD:
                atomic(path, data)
        for label in START_ORDER:
            bootstrap(label)
            started.append(label)
        wait_ready(owners, manifest)
    except Exception:
        try:
            for label in reversed(started):
                bootout(label)
        finally:
            retire_launch_jobs()
        raise
    receipt['active'] = sha
    atomic(RECEIPT, bundles.encoded(receipt), 0o600)


def uninstall(receipt, profile):
    bundles.require(receipt is not None, 'No owned controller installation')
    check_managed(receipt, profile)
    provision_accounts(receipt)
    quiesce()
    for path in (Path(PROGRAM), *files(*FRESH_PORTS, profile)):
        if exists(path):
            path.unlink()
            sync(path.parent)
    print(f'Retained controller state, service accounts, and rollback slots under {ROOT}.')


def host():
    bundles.require(sys.platform == 'darwin', 'A native macOS installation requires Darwin')
    major = run('/usr/bin/sw_vers', '-productVersion').decode().split('.')[0]
    machine = {'arm64': 'arm64', 'x86_64': 'amd64'}.get(platform.machine())
    profile = 'macos-' + major
    bundles.require(machine in bundles.TARGETS.get(profile, ()), 'Unsupported macOS version or architecture')
    return profile, machine


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    render = actions.add_parser('render', help='write launchd definitions into an empty staging directory')
    render.add_argument('directory', type=Path)
    add = actions.add_parser('install')
    add.add_argument('--bundle', type=Path, required=True)
    add.add_argument('--sha256', required=True)
    add.add_argument('--dry-run', action='store_true')
    add.add_argument('--platform', choices=('macos-26', 'macos-27'))
    add.add_argument('--architecture', choices=('amd64', 'arm64'))
    actions.add_parser('uninstall')
    args = parser.parse_args()
    try:
        if args.action == 'render':
            bundles.require(args.directory.is_dir() and not args.directory.is_symlink() and not any(args.directory.iterdir()),
                            'Expected an empty render directory')
            for service in definitions():
                with (args.directory / (service['Label'] + '.plist')).open('xb') as stream:
                    plistlib.dump(service, stream)
            return
        if args.action == 'install' and args.dry_run:
            bundles.require(bool(args.platform) == bool(args.architecture), 'Supply both platform and architecture for cross-host verification')
            profile, machine = (args.platform, args.architecture) if args.platform and args.architecture else host()
            with tempfile.TemporaryDirectory(prefix='wsprrypi-controller-') as temporary:
                verified = bundles.verify(args.bundle, args.sha256, machine, Path(temporary), profile)
                report = {str(path): data.decode() for path, data in files(*FRESH_PORTS, profile).items()}
                print(json.dumps({'candidate': verified['binary'], 'files': report}, sort_keys=True))
            return
        bundles.require(os.geteuid() == 0, 'macOS controller installation requires root')
        profile, machine = host()
        os.umask(0o022)
        fd = os.open(INSTALLATION.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            receipt = load_receipt() if exists(RECEIPT) else None
            if args.action == 'uninstall':
                uninstall(receipt, profile)
            else:
                bundles.require(args.platform in (None, profile) and args.architecture in (None, machine),
                                'Candidate profile differs from the installed host')
                with tempfile.TemporaryDirectory(prefix='wsprrypi-controller-') as temporary:
                    source = Path(temporary)
                    verified = bundles.verify(args.bundle, args.sha256, machine, source, profile)
                    install(source, verified, args.sha256, profile, receipt)
        finally:
            os.close(fd)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f'macOS controller installation failed: {error}\n')


if __name__ == '__main__':
    main()

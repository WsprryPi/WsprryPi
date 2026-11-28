#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install a locally verified controller candidate on Ubuntu 26.04."""

import argparse
import fcntl
import grp
import http.client
import json
import os
from pathlib import Path
import pwd
import re
import stat
import subprocess
import sys
import tempfile
import time
import tarfile

from application_state_install import listener_configuration
import controller_bundle as bundles
from controller_apache import proxy_config


BACKEND = 'wsprrypi-controller'
WEB = 'wsprrypi-web'
HTTP = 'www-data'
HOME = {BACKEND: '/var/lib/wsprrypi/controller', WEB: '/var/lib/wsprrypi-web'}
STATE = Path('/var/lib/wsprrypi/application-state')
MAINTENANCE_STATE = Path('/var/lib/wsprrypi-maintenance')
RECORD_ROOT = Path('/var/lib/wsprrypi-controller-install')
RELEASES = Path('/usr/local/lib/wsprrypi-controller')
PROGRAM = Path('/usr/local/lib/wsprrypi')
BINARY = Path('/usr/local/bin/wsprrypi')
DOCUMENT_ROOT = Path('/var/www/wsprrypi')
APACHE = Path('/etc/apache2/sites-available/wsprrypi-controller.conf')
APACHE_LINK = Path('/etc/apache2/sites-enabled/zz-wsprrypi-controller.conf')
RULE = Path('/etc/udev/rules.d/95-wsprrypi-controller.rules')
UNIT_PATH = Path('/etc/systemd/system')
UNITS = ('wsprrypi-maintenance.service', 'wsprrypi.service', 'wsprrypi-web.service')
ENV = {'PATH': '/usr/bin:/usr/sbin:/bin', 'LANG': 'C', 'LC_ALL': 'C'}


def unit(description, start, service, relations=''):
    return f"""# Managed by the WsprryPi controller installer
[Unit]
Description={description}
After=network.target
{relations}
[Service]
Type=exec
ExecStart={BINARY} {start}
UMask=0077
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
Restart=on-failure
RestartSec=5s
TimeoutStopSec=30s
{service}
[Install]
WantedBy=multi-user.target
"""


def units():
    return dict(zip(UNITS, (
        unit('WsprryPi controller maintenance', '--maintenance-worker --role controller',
             """User=root
Group=wsprrypi-web
RuntimeDirectory=wsprrypi-maintenance
RuntimeDirectoryMode=0750
StateDirectory=wsprrypi-maintenance
StateDirectoryMode=0700
PrivateDevices=yes
CapabilityBoundingSet=CAP_CHOWN CAP_DAC_OVERRIDE CAP_FOWNER CAP_SETUID CAP_SETGID CAP_KILL
AmbientCapabilities=CAP_SETUID
ReadWritePaths=/var/lib/wsprrypi /etc/apache2/sites-available
""", 'Before=wsprrypi.service'),
        unit('WsprryPi controller backend',
             f'--controller-only --state-directory {STATE} --runtime-directory /run/wsprrypi --socket-loopback-only --journald',
             """User=wsprrypi-controller
Group=wsprrypi-web
SupplementaryGroups=wsprrypi-controller
RuntimeDirectory=wsprrypi
RuntimeDirectoryMode=0750
CapabilityBoundingSet=
ReadWritePaths=/var/lib/wsprrypi/controller /var/lib/wsprrypi/application-state
""", 'Wants=wsprrypi-maintenance.service\nAfter=wsprrypi-maintenance.service'),
        unit('WsprryPi controller browser', '--web-service --runtime-directory /run/wsprrypi',
             """User=wsprrypi-web
Group=wsprrypi-web
StateDirectory=wsprrypi-web
StateDirectoryMode=0700
CapabilityBoundingSet=
PrivateDevices=yes
InaccessiblePaths=/var/lib/wsprrypi /var/lib/wsprrypi-maintenance
IPAddressDeny=any
IPAddressAllow=localhost
""", 'Wants=wsprrypi.service\nAfter=wsprrypi.service'),
    )))


def apache_config(web, socket):
    return proxy_config(DOCUMENT_ROOT, web, socket)


USB_RULE = ('# WsprryPico management CDC only; no BOOTSEL/raw USB or other serial access.\n'
            'SUBSYSTEM=="tty", KERNEL=="ttyACM[0-9]*", ENV{ID_USB_INTERFACE_NUM}=="04", '
            'ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4012", ATTRS{product}=="WsprryPico", '
            'GROUP:="wsprrypi-controller", MODE:="0660"\n')


def run(*command, owner=None, timeout=40):
    env = ENV.copy()
    options = {}
    if owner:
        env['HOME'] = owner.pw_dir
        options.update(user=owner.pw_uid, group=owner.pw_gid, extra_groups=[])
    return subprocess.run([str(item) for item in command], env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=timeout, check=True, **options).stdout


def systemctl(*args):
    return run('/usr/bin/systemctl', *args)


def exists(path):
    return os.path.lexists(path)


def trusted_parents(path, state_owner=None):
    for parent in reversed(path.parents):
        info = parent.lstat()
        expected = state_owner if parent == STATE.parent and state_owner is not None else 0
        bundles.require(stat.S_ISDIR(info.st_mode) and info.st_uid == expected and not info.st_mode & 0o022,
                        f'Unsafe installation parent {parent}')


def directory(path, uid=0, gid=0, mode=0o755, state_owner=None, create=True):
    trusted_parents(path, state_owner)
    if create:
        try:
            path.mkdir(mode=mode)
        except FileExistsError:
            pass
        else:
            os.chown(path, uid, gid)
            path.chmod(mode)
    info = path.lstat()
    bundles.require(stat.S_ISDIR(info.st_mode) and (info.st_uid, info.st_gid) == (uid, gid)
                    and stat.S_IMODE(info.st_mode) == mode, f'Unsafe ownership or mode for {path}')


def sync(parent):
    fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic(path, data, mode=0o644):
    trusted_parents(path)
    fd, temporary = tempfile.mkstemp(prefix='.wsprrypi-controller-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        sync(path.parent)
    finally:
        Path(temporary).unlink(missing_ok=True)


def symlink(path, target):
    trusted_parents(path)
    with tempfile.TemporaryDirectory(prefix='.wsprrypi-controller-', dir=path.parent) as directory_name:
        temporary = Path(directory_name) / 'link'
        temporary.symlink_to(target)
        os.replace(temporary, path)
        sync(path.parent)


def trusted_file(path, expected):
    trusted_parents(path)
    if not exists(path):
        return
    info = path.lstat()
    bundles.require(info.st_uid == 0 and stat.S_ISREG(info.st_mode) and info.st_nlink == 1
                    and not info.st_mode & 0o022, f'Unsafe managed file {path}')
    data = bundles.safe_source(path, bundles.MAX_MANIFEST)
    if path == APACHE:
        ports = re.findall(rb'http://127\.0\.0\.1:([0-9]{4,5})/', data)
        sockets = re.findall(rb'ws://127\.0\.0\.1:([0-9]{4,5})/', data)
        bundles.require(len(set(ports)) == len(set(sockets)) == 1
                        and data == apache_config(int(ports[0]), int(sockets[0])).encode(),
                        f'Unrecognized controller Apache configuration {path}')
    else:
        bundles.require(data == expected, f'Modified managed file {path}')


def trusted_link(path, targets):
    trusted_parents(path)
    if exists(path):
        info = path.lstat()
        bundles.require(stat.S_ISLNK(info.st_mode) and info.st_uid == 0
                        and os.readlink(path) in {str(target) for target in targets},
                        f'Conflicting or modified installation path {path}')


def get_account(name):
    try:
        return pwd.getpwnam(name)
    except KeyError:
        return None


def get_group(name):
    try:
        return grp.getgrnam(name)
    except KeyError:
        return None


def reserve_accounts():
    occupied = {entry.pw_uid for entry in pwd.getpwall()} | {group.gr_gid for group in grp.getgrall()}
    numbers = (number for number in range(999, 199, -1) if number not in occupied)
    reserved = {name: next(numbers, None) for name in HOME}
    bundles.require(all(reserved.values()), 'No available system UID/GID pair for controller services')
    return {name: [number, number] for name, number in reserved.items()}


def accounts(record, create=False):
    installed = {}
    for name, home in HOME.items():
        entry = get_account(name)
        if entry is None and create:
            uid, gid = record[name]
            if get_group(name) is None:
                run('/usr/sbin/groupadd', '--system', '--gid', gid, name)
            bundles.require(get_group(name).gr_gid == gid, f'Controller group {name} conflicts with its reserved GID')
            run('/usr/sbin/useradd', '--system', '--uid', uid, '--gid', gid, '--no-create-home',
                '--home-dir', home, '--shell', '/usr/sbin/nologin', name)
            entry = get_account(name)
        bundles.require(entry and entry.pw_uid > 0 and entry.pw_gid > 0 and entry.pw_dir == home
                        and entry.pw_shell == '/usr/sbin/nologin'
                        and grp.getgrnam(name).gr_gid == entry.pw_gid, f'Invalid controller account {name}')
        bundles.require(not any(group.gr_gid != entry.pw_gid and name in group.gr_mem for group in grp.getgrall()),
                        f'Unexpected supplementary account groups for {name}')
        bundles.require(record[name] == [entry.pw_uid, entry.pw_gid], f'Controller identity has changed for {name}')
        installed[name] = entry
    http = get_account(HTTP)
    bundles.require(http and http.pw_uid > 0 and http.pw_gid > 0
                    and len({http.pw_uid, *(e.pw_uid for e in installed.values())}) == 3
                    and len({http.pw_gid, *(e.pw_gid for e in installed.values())}) == 3,
                    'Backend, browser, and Apache require distinct unprivileged accounts')
    bundles.require(not any(HTTP in grp.getgrnam(name).gr_mem for name in HOME),
                    'Apache must not be a member of a controller service group')
    return installed


def read_record(path):
    info = path.lstat()
    bundles.require(info.st_uid == 0 and stat.S_IMODE(info.st_mode) == 0o600, 'Unsafe controller receipt')
    value = json.loads(bundles.safe_source(path, bundles.MAX_MANIFEST), object_pairs_hook=bundles.unique)
    bundles.require(isinstance(value, dict) and set(value) == {'schema', 'accounts', 'managed_slots', 'active'}
                    and value['schema'] == 'controller-installation/1'
                    and isinstance(value['accounts'], dict) and set(value['accounts']) == set(HOME)
                    and all(isinstance(v, list) and len(v) == 2 and all(type(i) is int and i > 0 for i in v)
                            for v in value['accounts'].values())
                    and isinstance(value['managed_slots'], list)
                    and all(isinstance(s, str) and bundles.SHA256.fullmatch(s) for s in value['managed_slots'])
                    and (value['active'] is None or value['active'] in value['managed_slots']), 'Invalid controller receipt')
    return value


def check_managed(record):
    owned = {UNIT_PATH / name: value.encode() for name, value in units().items()}
    owned[RULE] = USB_RULE.encode()
    owned[APACHE] = None
    links = {APACHE_LINK: [APACHE], BINARY: [PROGRAM / 'bin/wsprrypi'], DOCUMENT_ROOT: [PROGRAM / 'ui'],
             PROGRAM: [RELEASES / sha for sha in record['managed_slots']] if record else []}
    if record is None:
        for path in (*owned, *links, RECORD_ROOT, RELEASES, STATE.parent, Path(HOME[WEB]),
                     MAINTENANCE_STATE, Path('/usr/local/etc/wsprrypi.ini'),
                     Path('/usr/local/etc/wsprrypi-listeners.json')):
            bundles.require(not exists(path), f'Conflicting or unowned installation at {path}')
        bundles.require(not any(get_account(name) or get_group(name) for name in HOME),
                        'Existing controller accounts lack an ownership receipt')
    else:
        for target, value in owned.items():
            trusted_file(target, value)
        for target, choices in links.items():
            trusted_link(target, choices)
    return owned, links


def state_of(name):
    return systemctl('show', '-p', 'ActiveState', '--value', name).decode().strip()


def loaded_units():
    loaded = []
    for name in UNITS:
        try:
            output = systemctl('show', '-p', 'LoadState', '-p', 'FragmentPath', '-p', 'DropInPaths', name)
        except subprocess.CalledProcessError as error:
            if b'LoadState=not-found' not in (error.stdout or b'').splitlines():
                raise
            output = error.stdout
        value = output.decode()
        record = dict(line.split('=', 1) for line in value.splitlines() if '=' in line)
        bundles.require(record.get('DropInPaths') == '' and record.get('LoadState') in ('loaded', 'not-found'),
                        f'Unrecognized systemd registration for {name}')
        if record['LoadState'] == 'loaded':
            bundles.require(record.get('FragmentPath') == str(UNIT_PATH / name),
                            f'Conflicting systemd service {name}')
            loaded.append(name)
    return loaded


def require_idle_maintenance(loaded):
    if UNITS[0] in loaded and state_of(UNITS[0]) == 'active':
        result = json.loads(run(BINARY, '--maintenance', 'jobs'))
        bundles.require(isinstance(result, dict) and result.get('schema') == 'recovery-job-list/1'
                        and 'active' in result and result['active'] is None,
                        'An active or unknown recovery job owns this installation')
    elif exists(MAINTENANCE_STATE):
        directory(MAINTENANCE_STATE, gid=grp.getgrnam(WEB).gr_gid, mode=0o700, create=False)
        path = MAINTENANCE_STATE / 'jobs.json'
        info = path.lstat()
        bundles.require(info.st_uid == 0 and stat.S_IMODE(info.st_mode) == 0o600,
                        'Unsafe stopped maintenance registry')
        registry = json.loads(bundles.safe_source(path, bundles.MAX_MANIFEST), object_pairs_hook=bundles.unique)
        bundles.require(isinstance(registry, dict) and set(registry) == {'schema', 'identity', 'role', 'active', 'jobs'}
                        and registry['schema'] == 'recovery-jobs/1' and registry['role'] == 'controller'
                        and isinstance(registry['identity'], str) and re.fullmatch(r'[0-9a-f]{32}', registry['identity'])
                        and isinstance(registry['jobs'], list) and len(registry['jobs']) <= 4
                        and all(isinstance(job, str) and re.fullmatch(r'[0-9a-f]{32}', job) for job in registry['jobs'])
                        and len(registry['jobs']) == len(set(registry['jobs']))
                        and registry['active'] is None, 'Stopped maintenance still owns an active or unknown recovery job')


def quiesce():
    loaded = loaded_units()
    web_was_active = UNITS[2] in loaded and state_of(UNITS[2]) == 'active'
    require_idle_maintenance(loaded)
    if web_was_active:
        systemctl('stop', UNITS[2])
    try:
        require_idle_maintenance(loaded)
    except Exception:
        if web_was_active:
            systemctl('start', UNITS[2])
        raise
    active = [name for name in reversed(loaded) if state_of(name) not in ('inactive', 'failed')]
    if active:
        systemctl('stop', *active)
    bundles.require(all(state_of(name) in ('inactive', 'failed') for name in loaded),
                    'Controller services failed to stop')
    if loaded:
        systemctl('disable', *loaded)


def stage_slot(source, manifest, slot):
    directory(RELEASES)
    if not exists(slot):
        with tempfile.TemporaryDirectory(prefix='.candidate-', dir=RELEASES) as name:
            staging = Path(name) / 'release'
            staging.mkdir(mode=0o755)
            for relative in [*manifest['files'], bundles.MANIFEST]:
                destination = staging / relative
                destination.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
                atomic(destination, (source / relative).read_bytes(), 0o755 if relative.startswith('bin/') else 0o644)
            sync(staging)
            os.rename(staging, slot)
            sync(RELEASES)
    directory(slot)
    expected = {**manifest['files'], bundles.MANIFEST: bundles.digest(bundles.encoded(manifest))}
    actual = {str(path.relative_to(slot)) for path in slot.rglob('*')
              if path.is_symlink() or not path.is_dir()}
    bundles.require(actual == set(expected), 'Retained candidate slot inventory differs')
    for relative, digest in expected.items():
        path = slot / relative
        for parent in path.relative_to(slot).parents:
            directory(slot / parent)
        info = path.lstat()
        mode = 0o755 if relative.startswith('bin/') else 0o644
        bundles.require((info.st_uid, info.st_gid) == (0, 0) and stat.S_IMODE(info.st_mode) == mode
                        and bundles.digest(bundles.safe_source(path, bundles.MAX_ARCHIVE)) == digest,
                        f'Retained candidate slot has changed at {relative}')


def wait_ready(users, binary):
    deadline = time.monotonic() + 45
    expected = dict(zip(UNITS, (0, users[BACKEND].pw_uid, users[WEB].pw_uid)))
    while time.monotonic() < deadline:
        if all(state_of(name) == 'active' for name in UNITS):
            for name, uid in expected.items():
                pid = int(systemctl('show', '-p', 'MainPID', '--value', name))
                bundles.require(pid > 1, 'Installed controller lacks a service process')
                info = Path(f'/proc/{pid}/status').read_text()
                ids = re.search(r'^Uid:\s+(\d+)\s+(\d+)', info, re.M)
                bundles.require(ids and int(ids[1]) == int(ids[2]) == uid, 'Installed service ran under the wrong identity')
                if uid != 0:
                    capabilities = re.findall(r'^Cap(?:Eff|Prm):\s+([0-9a-f]+)$', info, re.M)
                    bundles.require(len(capabilities) == 2 and not any(int(value, 16) for value in capabilities),
                                    'An unprivileged controller service has process capabilities')
            connection = http.client.HTTPConnection('127.0.0.1', timeout=3)
            try:
                connection.request('GET', '/wsprrypi/version')
                response = connection.getresponse()
                if response.status == 200:
                    version = json.loads(response.read(128 * 1024))
                    if version.get('host_status', {}).get('controller_ready') is True:
                        bundles.require(version['host_status'].get('role') == 'controller-only'
                                        and version.get('compiled_backends') == ''
                                        and version.get('ancillary_gpio') is False
                                        and version.get('wspr_commit') == binary['source_commit']
                                        and version.get('wspr_exe_version') == binary['application_version'],
                                        'Unexpected installed runtime identity')
                        return
            except (OSError, http.client.HTTPException):
                pass
            finally:
                connection.close()
        time.sleep(.25)
    raise ValueError('Installed controller did not become ready through Apache')


def install(source, manifest, sha, receipt, record):
    owned, links = check_managed(record)
    loaded = loaded_units()
    bundles.require(record is not None or not loaded, 'Unowned controller service registration')
    bundles.package_versions(bundles.runtime_packages(manifest['binary']))
    for module in ('proxy', 'proxy_http', 'proxy_wstunnel', 'headers', 'alias', 'dir', bundles.controller_binary.PHP_MODULE):
        bundles.require((Path('/etc/apache2/mods-enabled') / (module + '.load')).is_symlink(),
                        f'Enable Apache module {module} before controller installation')
    apache_runtime = run('/usr/sbin/apache2ctl', '-t', '-D', 'DUMP_RUN_CFG').decode()
    http_owner = get_account(HTTP)
    bundles.require(http_owner and f'User: name="{HTTP}" id={http_owner.pw_uid}' in apache_runtime
                    and f'Group: name="{HTTP}" id={http_owner.pw_gid}' in apache_runtime,
                    'Apache must serve WsprryPi under its dedicated www-data identity')
    if record is None:
        record = {'schema': 'controller-installation/1', 'active': None, 'managed_slots': [],
                  'accounts': reserve_accounts()}
        trusted_parents(RECORD_ROOT)
        with tempfile.TemporaryDirectory(prefix='.controller-receipt-', dir=RECORD_ROOT.parent) as temporary:
            pending = Path(temporary) / 'receipt'
            pending.mkdir(mode=0o700)
            atomic(pending / receipt.name, bundles.encoded(record), 0o600)
            os.rename(pending, RECORD_ROOT)
            sync(RECORD_ROOT.parent)
    users = accounts(record['accounts'], create=not record['managed_slots'])
    if sha not in record['managed_slots']:
        record['managed_slots'].append(sha)
    atomic(receipt, bundles.encoded(record), 0o600)
    backend = users[BACKEND]
    directory(STATE.parent, backend.pw_uid, backend.pw_gid, 0o700)
    directory(Path(backend.pw_dir), backend.pw_uid, backend.pw_gid, 0o700, state_owner=backend.pw_uid)
    web_user = users[WEB]
    directory(Path(web_user.pw_dir), web_user.pw_uid, web_user.pw_gid, 0o700)
    slot = RELEASES / sha
    stage_slot(source, manifest, slot)
    binary = slot / 'bin/wsprrypi'
    loader = bundles.controller_binary.TARGETS[manifest['binary']['architecture']][1]
    linked = run(loader, '--list', binary, owner=backend)
    bundles.require(b'not found' not in linked and b'libc.so.6' in linked, 'Controller runtime libraries are unavailable')
    bundles.require(run(binary, '--storage-format', owner=backend).strip() == b'application-state/1',
                    'Controller executable has an unsupported storage format')
    bundles.require(bundles.digest(run(binary, '--export-recovery-age-helper', owner=backend))
                    == manifest['files']['bin/recovery-age'], 'Recovery helper differs from the selected executable')
    if loaded:
        quiesce()
    if not exists(STATE) and record['active'] is not None:
        raise ValueError('Established controller state is missing and requires recovery')
    if not exists(STATE):
        run(binary, '--initialize-state', '--controller-only', '--state-directory', STATE, owner=backend)
    directory(STATE, backend.pw_uid, backend.pw_gid, 0o700, state_owner=backend.pw_uid, create=False)
    listeners = listener_configuration(run(binary, '--check-state', '--controller-only', '--state-directory',
                                           STATE, '--listener-bootstrap', owner=backend))
    bundles.require(listeners['role'] == 'controller', 'Controller state role differs')
    for destination, data in owned.items():
        atomic(destination, data if data is not None else apache_config(listeners['web_port'], listeners['socket_port']).encode())
    for destination in (BINARY, DOCUMENT_ROOT, APACHE_LINK):
        if not exists(destination):
            symlink(destination, links[destination][0])
    symlink(PROGRAM, slot)
    for owner in (web_user, http_owner):
        run('/usr/bin/test', '!', '-r', STATE, owner=owner)
        run('/usr/bin/test', '!', '-r', Path(backend.pw_dir), owner=owner)
        run('/usr/bin/test', '-r', DOCUMENT_ROOT / 'index.php', owner=owner)
    run('/usr/sbin/apache2ctl', 'configtest')
    run('/usr/bin/udevadm', 'control', '--reload')
    systemctl('daemon-reload')
    try:
        systemctl('start', *UNITS)
        systemctl('reload-or-restart', 'apache2.service')
        wait_ready(users, manifest['binary'])
        systemctl('enable', *UNITS)
    except Exception:
        # Preserve state and both artifact slots. Once a selected writer may
        # have opened state, never restart the previous writer automatically.
        try:
            systemctl('disable', *UNITS)
        finally:
            systemctl('stop', *reversed(UNITS))
        raise
    record['active'] = sha
    atomic(receipt, bundles.encoded(record), 0o600)


def uninstall(record):
    bundles.require(record is not None, 'No owned controller installation was found')
    owned, links = check_managed(record)
    accounts(record['accounts'])
    loaded = loaded_units()
    if loaded:
        quiesce()
    for path in (*links, *owned):
        if exists(path):
            path.unlink()
            sync(path.parent)
    systemctl('daemon-reload')
    run('/usr/bin/udevadm', 'control', '--reload')
    run('/usr/sbin/apache2ctl', 'configtest')
    if state_of('apache2.service') == 'active':
        systemctl('reload', 'apache2.service')
    # Keep the last installed identity, state witness, and rollback slots.
    print(f'Controller services and owned paths removed. State and rollback files retained under {STATE.parent} and {RELEASES}.')


def architecture():
    os_release = Path('/etc/os-release').read_text().splitlines()
    values = dict(line.split('=', 1) for line in os_release if '=' in line)
    release = bundles.controller_binary.PLATFORM.removeprefix('ubuntu-')
    bundles.require(sys.platform == 'linux' and values.get('ID') == 'ubuntu'
                    and values.get('VERSION_ID', '').strip('"') == release, f'Controller installation requires Ubuntu {release}')
    value = run('/usr/bin/dpkg', '--print-architecture').decode().strip()
    bundles.require(value in bundles.controller_binary.TARGETS, 'Unsupported Ubuntu controller architecture')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    add = actions.add_parser('install')
    add.add_argument('--bundle', required=True, type=Path)
    add.add_argument('--sha256', required=True)
    add.add_argument('--dry-run', action='store_true')
    add.add_argument('--architecture', choices=bundles.controller_binary.TARGETS)
    actions.add_parser('uninstall')
    args = parser.parse_args()
    try:
        if args.action == 'install' and args.dry_run:
            with tempfile.TemporaryDirectory(prefix='controller-install-') as directory_name:
                manifest = bundles.verify(args.bundle, args.sha256, args.architecture or architecture(), Path(directory_name))
                print(json.dumps({'candidate': manifest['binary'], 'units': units(),
                                  'dependencies': manifest['dependencies'],
                                  'apache': apache_config(31415, 31416), 'udev': USB_RULE}, sort_keys=True))
            return
        bundles.require(os.geteuid() == 0, 'Controller installation requires root')
        os.umask(0o022)
        selected = architecture()
        bundles.require(args.action == 'uninstall' or args.architecture in (None, selected),
                        'Candidate architecture differs from the installation host')
        # Lock an existing root-owned directory inode without writing to it.
        fd = os.open('/var/lib', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            receipt = RECORD_ROOT / 'installation.json'
            record = None
            if exists(receipt):
                directory(RECORD_ROOT, mode=0o700)
                record = read_record(receipt)
            if args.action == 'uninstall':
                uninstall(record)
            else:
                with tempfile.TemporaryDirectory(prefix='controller-install-') as directory_name:
                    source = Path(directory_name)
                    manifest = bundles.verify(args.bundle, args.sha256, selected, source)
                    install(source, manifest, args.sha256, receipt, record)
        finally:
            os.close(fd)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f'Controller installation failed: {error}\n')


if __name__ == '__main__':
    main()

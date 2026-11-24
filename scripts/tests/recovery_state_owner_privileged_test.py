#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicitly authorized, disposable Linux container UID-boundary qualification.

This checks the production owner relay and effective filesystem isolation. The
ordinary actual-engine recovery suites separately check archive/publication
semantics. This script never installs accounts, invokes services, or opens USB.
"""
import json
import os
from pathlib import Path
import pwd
import shutil
import stat
import subprocess
import sys
import tempfile


def require(value):
    if not value:
        raise RuntimeError('Private recovery owner qualification failed')


def as_owner(uid, gid, code, *arguments):
    result = subprocess.run([sys.executable, '-I', '-c', code, *map(str, arguments)],
        user=uid, group=gid, extra_groups=[], stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15,
        env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
    require(result.returncode == 0)
    return json.loads(result.stdout)


def main():
    require(sys.argv[1:] == ['--isolated-container'])
    require(sys.platform == 'linux' and os.geteuid() == 0 and Path('/.dockerenv').is_file())
    backend = pwd.getpwnam('wsprry-test')
    require(backend.pw_uid == 501 and backend.pw_gid == 20 and backend.pw_dir == '/home/wsprry-test')
    home = Path(backend.pw_dir)
    owner_info = home.lstat()
    require(stat.S_ISDIR(owner_info.st_mode) and owner_info.st_uid == backend.pw_uid and stat.S_IMODE(owner_info.st_mode) == 0o700)
    temporary = Path(tempfile.mkdtemp(prefix='recovery-owner-qualified-', dir='/tmp'))
    # Root-owned immutable program inputs remain readable by the backend child;
    # job, identity, and key material are never put in this directory.
    program = temporary / 'program'
    temporary.chmod(0o755)
    program.mkdir(mode=0o755)
    shutil.copytree('/source/application_recovery', program / 'application_recovery',
        ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    for path in (program / 'application_recovery').rglob('*'):
        require(not path.is_symlink())
        path.chmod(0o755 if path.is_dir() else 0o644)
    (program / 'application_recovery').chmod(0o755)
    sys.path.insert(0, str(program))
    from application_recovery.durable import publish_record
    from application_recovery.state_owner import StateOwner
    maintenance = temporary / 'maintenance'
    maintenance.mkdir(mode=0o700)
    job_id, identity = 'a' * 32, 'b' * 32
    job = maintenance / 'jobs' / job_id
    job.mkdir(parents=True, mode=0o700)
    (maintenance / 'jobs').chmod(0o700)
    publish_record(job / 'job.json', {'schema': 'recovery-job/1', 'job_id': job_id,
        'identity': identity, 'role': 'controller', 'state': 'capturing', 'confirmation_sha256': None}, None)
    application = home / 'application-state'
    prepared = as_owner(501, 20, '''
import json,os,stat,sys
from pathlib import Path
home=Path(sys.argv[1]);application=home/'application-state'
assert not list(home.iterdir())
application.mkdir(mode=0o700)
key=home/'backend-private-key';key.write_bytes(b'qualification-only');key.chmod(0o600)
lock=home/'.wsprrypi-controller-instance';lock.mkdir(mode=0o700)
file=lock/'instance.lock';file.touch(mode=0o600)
status=dict(line.split(':',1) for line in Path('/proc/self/status').read_text().splitlines() if ':' in line)
assert all(int(status[name].strip(),16)==0 for name in ('CapEff','CapPrm','CapAmb'))
assert os.getgroups()==[]
print(json.dumps({'uid':os.geteuid(),'gid':os.getegid()}))
''', home)
    require(prepared == {'uid': 501, 'gid': 20})
    config = {'root': str(maintenance), 'application_root': str(application),
        'backend_command': ['/usr/bin/false'], 'backend_runtime': '/tmp/unused-recovery-ipc',
        'identity': identity, 'role': 'controller', 'backend_owner': {
            'uid': 501, 'gid': 20, 'home': str(home), 'root': str(home / '.wsprrypi-recovery'),
            'worker': str(program / 'application_recovery/state_owner_worker.py')}}
    owner = StateOwner(config)
    owner.synchronize(job_id, 'capture')
    produced = as_owner(501, 20, '''
import json,os,stat,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from application_recovery.durable import publish_record
home=Path(sys.argv[2]);job=home/'.wsprrypi-recovery/jobs'/sys.argv[3]
metadata=(job/'job.json').stat();assert metadata.st_uid==os.geteuid() and stat.S_IMODE(metadata.st_mode)==0o600
snapshot=job/'snapshot';snapshot.mkdir(mode=0o700)
application=snapshot/'application';application.mkdir(mode=0o700)
file=application/'state.sqlite3';file.write_bytes(b'logical-snapshot-qualification');file.chmod(0o600)
publish_record(job/'capture-result.json',{'state':'ready','inventory':{'files':['application/state.sqlite3']}},None)
print(json.dumps({'backend_owned':True}))
''', program, home, job_id)
    require(produced == {'backend_owned': True})
    owner.collect(job_id)
    mirrored = job / 'snapshot/application/state.sqlite3'
    require(mirrored.read_bytes() == b'logical-snapshot-qualification')
    info = mirrored.stat()
    require(info.st_uid == 0 and stat.S_IMODE(info.st_mode) == 0o600)
    # Effective access checks run as distinct unprivileged identities, rather
    # than inferring isolation from stat bits or a rendered ACL alone.
    probe = '''
import json,sys
from pathlib import Path
for raw in sys.argv[1:]:
 for mode in ('rb','ab'):
  try:
   with Path(raw).open(mode):
    pass
  except PermissionError:
   continue
  raise RuntimeError('Unexpected private file access')
print(json.dumps({'private_access_refused':True}))
'''
    for uid, gid in ((65534, 65534), (65533, 65533)):
        require(as_owner(uid, gid, probe, home / 'backend-private-key', mirrored,
            home / '.wsprrypi-controller-instance/instance.lock') == {'private_access_refused': True})
    require(as_owner(501, 20, probe, mirrored, job / 'job.json') == {'private_access_refused': True})
    refused = as_owner(65534, 65534, '''
import json,os,socket,subprocess,sys
parent,child=socket.socketpair()
process=subprocess.Popen([sys.executable,'-I',sys.argv[1],'--serve',str(child.fileno())],
 pass_fds=(child.fileno(),),stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
child.close();parent.settimeout(5)
assert parent.recv(4096) and process.wait(timeout=5)==1
parent.close()
print(json.dumps({'nonroot_peer_refused':True}))
''', program / 'application_recovery/state_owner_worker.py')
    require(refused == {'nonroot_peer_refused': True})
    require(owner.root_available() is True)
    require(owner.call('prune-job', job_id) == {'state': 'pruned'})
    require(owner.call('prune-job', job_id) == {'state': 'pruned'})
    print(json.dumps({'result': 'pass', 'maintenance_uid': 0, 'backend_uid': 501,
        'frontend_probe_uids': [65534, 65533], 'backend_capabilities': 'zero',
        'scope': 'production relay and effective private-file ownership; no installation or full multi-UID recovery claim'}))


if __name__ == '__main__':
    main()

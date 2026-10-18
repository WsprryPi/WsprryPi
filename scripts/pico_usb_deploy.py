#!/usr/bin/env python3
"""Package-time deployment only; never invoked by a browser or USB transaction."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import urllib.request

FILES = {'scripts/usb_installer.py', 'scripts/ci/release-manifest-1.schema.json'}

def safe(path):
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError('Symlink in installer deployment destination')

def provision(action, repository, root=Path('/'), fetch=None):
    prefix = lambda path: root / path.lstrip('/')
    target = prefix('/usr/local/lib/wsprrypi/pico-usb')
    safe(target)
    descriptor = json.loads((repository / 'config/pico-usb-installer.json').read_text())
    if (descriptor['schema'] != 'wsprrypi-pico-usb-deployment/1' or
            not re.fullmatch('[0-9a-f]{40}', descriptor['revision']) or descriptor['revision'] == '0'*40 or
            set(descriptor['files']) != FILES or
            any(not re.fullmatch('[0-9a-f]{64}', value) for value in descriptor['files'].values())):
        raise ValueError('Unpinned shared installer deployment')
    if action == 'uninstall':
        # Backups, cache and maintenance markers deliberately survive removal.
        if not target.exists():
            return
        safe(target / 'deployment.json')
        prior = json.loads((target / 'deployment.json').read_text())
        if set(prior['files']) != FILES: raise ValueError('Unknown deployed installer inventory')
        expected = {**prior['files'], 'entry.py':prior['entry_sha256']}
        for name, digest in expected.items():
            path = target / name
            safe(path)
            if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError('Changed deployed installer retained for administrator review')
        for name in expected:
            (target / name).unlink(missing_ok=True)
        (target / 'deployment.json').unlink()
        for path in (target / 'scripts/ci', target / 'scripts', target):
            try: path.rmdir()
            except OSError: pass
        for name in ('/etc/udev/rules.d/70-wsprrypico-usb.rules','/usr/lib/tmpfiles.d/wsprrypico-usb.conf'):
            path = prefix(name); safe(path)
            if path.exists() and path.read_text() == owned_text(name): path.unlink()
        return
    payload = {}
    for name, digest in descriptor['files'].items():
        url = f"https://raw.githubusercontent.com/WsprryPi/WsprryPico/{descriptor['revision']}/{name}"
        if fetch:
            data = fetch(url)
        else:
            with urllib.request.urlopen(url, timeout=30) as response:
                if not response.url.startswith('https://'): raise ValueError('Insecure installer download')
                data = response.read(1024*1024+1)
        if len(data)>1024*1024 or hashlib.sha256(data).hexdigest()!=digest:
            raise ValueError('Pinned installer download failed integrity check')
        payload[name] = data
    entry = (repository / 'scripts/pico_usb_entry.py').read_bytes()
    descriptor['entry_sha256'] = hashlib.sha256(entry).hexdigest()
    payload['entry.py'] = entry
    payload['deployment.json'] = (json.dumps(descriptor, sort_keys=True, indent=2)+'\n').encode()
    # Validate all bytes before publishing any executable.
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        safe(target / 'deployment.json')
        prior = json.loads((target / 'deployment.json').read_text())
        if set(prior['files']) != FILES: raise ValueError('Unknown deployed installer inventory')
        for name,digest in {**prior['files'],'entry.py':prior['entry_sha256']}.items():
            path=target/name; safe(path)
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
                raise ValueError('Changed deployed installer retained; repair explicitly before upgrade')
    stage = Path(tempfile.mkdtemp(prefix='.pico-usb-',dir=target.parent))
    try:
        stage.chmod(0o755)
        for name,data in payload.items():
            path=stage/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data); path.chmod(0o644)
            path.parent.chmod(0o755)
        if target.exists():
            # Package-time replacement is refused while any member is held.
            holds=prefix('/var/lib/wsprrypi/pico-usb-control')
            if holds.exists() and any(holds.iterdir()): raise ValueError('Finish Pico USB maintenance before replacing its installer')
            for name in payload:
                path=target/name; path.parent.mkdir(parents=True,exist_ok=True); os.replace(stage/name,path)
        else: os.replace(stage,target)
    finally:
        if stage.exists(): shutil.rmtree(stage)
    for name,mode in (('/var/lib/wsprrypi/pico-usb',0o700),('/var/lib/wsprrypi/pico-usb-control',0o700),
                      ('/var/cache/wsprrypi/pico-usb',0o700),('/run/lock/wsprrypico',0o3770)):
        path=prefix(name); safe(path); path.mkdir(parents=True,exist_ok=True); path.chmod(mode)
        if root==Path('/'):
            import grp
            os.chown(path,0,grp.getgrnam('dialout').gr_gid if name.startswith('/run/') else 0)
    for name in ('/etc/udev/rules.d/70-wsprrypico-usb.rules','/usr/lib/tmpfiles.d/wsprrypico-usb.conf'):
        path=prefix(name); safe(path); path.parent.mkdir(parents=True,exist_ok=True)
        if path.exists() and path.read_text()!=owned_text(name): raise ValueError('Unrecognized USB permission file retained')
        path.write_text(owned_text(name)); path.chmod(0o644)

def owned_text(name):
    if name.endswith('.conf'):
        return '# WsprryPi Pico USB installer\nd /run/lock/wsprrypico 3770 root dialout -\n'
    return ('# WsprryPi Pico USB installer\n'
            'SUBSYSTEM=="tty", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4012", GROUP="dialout", MODE="0660"\n'
            'SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="000f", GROUP="dialout", MODE="0660"\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('action',choices=('install','uninstall')); p.add_argument('--repository',type=Path,required=True)
    args=p.parse_args()
    if os.geteuid()!=0: p.error('Package-time provisioning requires root; ordinary installer use does not')
    provision(args.action,args.repository)

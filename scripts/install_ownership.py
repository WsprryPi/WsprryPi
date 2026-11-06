#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Account for application-owned install files; preserve operator/shared data."""
import argparse
import errno
import hashlib
import json
import os
import re
import fcntl
from pathlib import Path
import stat
import tempfile

STATE='/var/lib/wsprrypi/application-files.json'
CODE='/usr/local/lib/wsprrypi/route_application.py'
FILES={CODE:'code',
       '/usr/local/etc/wsprrypi.ini.pre_migration.bak':'backup',
       '/usr/local/etc/wsprrypi.ini.wtp-devices.json':'fleet',
       '/etc/apache2/sites-available/wsprrypi.conf.wsprrypi.bak':'backup',
       '/usr/local/lib/wsprrypi/.pico-usb.lock':'data',
       '/run/lock/wsprrypi-rp1-reconcile.lock':'data'}
DIRECTORIES=['/etc/systemd/system/wsprrypi.service.d',
             '/usr/local/lib/wsprrypi/__pycache__','/usr/local/lib/wsprrypi/pico-usb',
             '/usr/local/lib/wsprrypi','/var/lib/wsprrypi/support-bundles',
             '/var/lib/wsprrypi/pico-usb','/var/lib/wsprrypi/pico-usb-control',
             '/var/cache/wsprrypi/pico-usb','/var/cache/wsprrypi',
             '/run/lock/wsprrypico','/run/wsprrypi-rp1','/var/lib/wsprrypi']


def physical(root,name):return root/name.lstrip('/')


def read(path):
    for parent in (path,*path.parents):
        if parent.is_symlink():raise ValueError('symlink installation path retained: '+str(parent))
    if not path.exists():return None
    info=path.stat()
    if (not stat.S_ISREG(info.st_mode) or info.st_uid!=os.geteuid() or info.st_mode&0o022
            or info.st_size>16*1024*1024):raise ValueError('unowned installation file retained: '+str(path))
    return path.read_bytes()


def digest(data):return None if data is None else hashlib.sha256(data).hexdigest()


def strict_json(raw):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError('duplicate application ownership field')
            result[key]=value
        return result
    return json.loads(raw,object_pairs_hook=unique)


def valid_digest(value):return value is None or (isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value) is not None)


def load(root):
    raw=read(physical(root,STATE))
    if raw is None:return None
    value=strict_json(raw)
    if (not isinstance(value,dict) or set(value)!={'version','phase','files','directories','protectBackups','iniSha256'}
            or type(value['version']) is not int or value['version']!=1
            or value['phase'] not in {'installing','complete'}
            or not isinstance(value['files'],dict) or not isinstance(value['directories'],dict)
            or type(value['protectBackups']) is not bool or not valid_digest(value['iniSha256'])):
        raise ValueError('invalid application file ownership receipt')
    for name,item in value['files'].items():
        cache=(name.startswith('/usr/local/lib/wsprrypi/__pycache__/') and
               Path(name).name.startswith(('route_application.','rp1_gpclk_dkms_install.','runtime_reconcile.'))
               and name.endswith('.pyc'))
        if (name not in FILES and not cache) or not isinstance(item,dict) or set(item)!={'preserve','sha256','kind'}:
            raise ValueError('application ownership path outside fixed scope')
        if (type(item['preserve']) is not bool or item['kind'] != FILES.get(name,'code')
                or not valid_digest(item['sha256'])):
            raise ValueError('invalid application ownership policy')
    if set(value['directories'])!=set(DIRECTORIES) or any(type(v) is not bool for v in value['directories'].values()):raise ValueError('application directory ownership differs')
    return value


def save(root,value):
    path=physical(root,STATE);path.parent.mkdir(parents=True,exist_ok=True)
    read(path)
    fd,name=tempfile.mkstemp(prefix='.application-files-',dir=path.parent)
    try:
        with os.fdopen(fd,'w') as out:
            os.fchmod(out.fileno(),0o600);json.dump(value,out,sort_keys=True);out.flush();os.fsync(out.fileno())
        os.replace(name,path)
    finally:Path(name).unlink(missing_ok=True)


def bound_companion(root):
    try:
        receipt=json.loads(read(physical(root,'/var/lib/rp1-gpclk-dkms/provider-installation.json')))
        raw=read(physical(root,'/etc/rp1-gpclk-dkms/runtime-controller.json'))
        binding=json.loads(raw)
        return (receipt['runtime']['bindingSha256']==digest(raw)
                and binding['externalFiles'][CODE]==digest(read(physical(root,CODE))))
    except (ValueError,KeyError,TypeError):return False


def begin(root,source):
    old=load(root)
    ini=digest(read(physical(root,'/usr/local/etc/wsprrypi.ini')))
    protect=(old['protectBackups'] if old else False) or (ini is not None and (old is None or ini!=old['iniSha256']))
    files=old['files'] if old else {}
    for name,kind in FILES.items():
        raw=read(physical(root,name))
        if name not in files:
            if name==CODE and raw is not None and not bound_companion(root):
                raise ValueError('unattributed route companion retained')
            files[name]={'preserve':raw is not None and name!=CODE,'sha256':digest(raw),'kind':kind}
        if kind=='code':
            allowed={files[name]['sha256'],digest((source/'scripts/route_application.py').read_bytes())}
            if digest(raw) not in allowed and raw is not None:raise ValueError('changed application companion retained')
    value={'version':1,'phase':'installing','files':files,
           'directories':old['directories'] if old else {n:physical(root,n).exists() for n in DIRECTORIES},
           'protectBackups':protect,'iniSha256':ini}
    save(root,value)


def complete(root):
    value=load(root)
    if value is None:raise ValueError('application installation has no ownership transaction')
    cache=physical(root,'/usr/local/lib/wsprrypi/__pycache__')
    if cache.exists():
        for path in cache.iterdir():
            if path.name.startswith(('route_application.','rp1_gpclk_dkms_install.','runtime_reconcile.')) and path.suffix=='.pyc':
                name='/'+str(path.relative_to(root))
                value['files'].setdefault(name,{'preserve':False,'sha256':None,'kind':'code'})
    for name,item in value['files'].items():item['sha256']=digest(read(physical(root,name)))
    value['phase']='complete';value['iniSha256']=digest(read(physical(root,'/usr/local/etc/wsprrypi.ini')))
    save(root,value)


def remove(root):
    value=load(root)
    if value is None:
        if physical(root,CODE).exists():raise ValueError('unattributed companion remains; owning installation required')
        return
    if value['phase']!='complete':raise ValueError('application installation incomplete; recover before file retirement')
    deletions=[]
    for name,item in value['files'].items():
        path=physical(root,name);raw=read(path)
        if raw is None:continue
        modified=digest(raw)!=item['sha256']
        if item['kind']=='code' and modified:raise ValueError('modified application code retained: '+name)
        preserve=item['preserve'] or modified or (item['kind']=='backup' and value['protectBackups'])
        if item['kind']=='fleet':
            try:preserve=preserve or json.loads(raw)!={'version':1,'assignments':[],'removals':[]}
            except ValueError:preserve=True
        if name=='/usr/local/lib/wsprrypi/.pico-usb.lock' and not preserve:
            # A held maintenance inode must remain stable for its other clients.
            with path.open('rb') as lock:
                try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                except BlockingIOError:preserve=True
            control=physical(root,'/var/lib/wsprrypi/pico-usb-control')
            if control.exists() and any(control.iterdir()):preserve=True
        if preserve:print('Retained operator/shared data: '+name)
        else:deletions.append(path)
    for path in deletions:path.unlink()
    pico=physical(root,'/usr/local/lib/wsprrypi/pico-usb')
    if not value['directories']['/usr/local/lib/wsprrypi/pico-usb'] and pico.is_dir():
        marker=pico/'deployment.json'
        if set(p.name for p in pico.iterdir())=={'deployment.json'}:
            report=json.loads(read(marker))
            if report=={'schema':'wsprrypi-pico-usb-deployment/2','adapter_files':{},'removed':True}:marker.unlink()
    physical(root,STATE).unlink()
    for name in DIRECTORIES:
        if value['directories'][name]:continue
        try:physical(root,name).rmdir()
        except OSError as error:
            if error.errno not in {errno.ENOENT,errno.ENOTEMPTY}:raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['begin','complete','remove'])
    parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
    args=parser.parse_args()
    try:
        if os.geteuid()!=0:raise ValueError('application ownership effects require root')
        if args.operation=='begin':begin(Path('/'),args.source)
        elif args.operation=='complete':complete(Path('/'))
        else:remove(Path('/'))
    except (OSError,ValueError,TypeError,KeyError) as error:raise SystemExit(str(error))

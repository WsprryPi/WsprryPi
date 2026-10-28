#!/usr/bin/env python3
"""Install/configure the dedicated UDP logging worker; no transmitter operations."""
import argparse
import array
import fcntl
import struct
import time
import grp
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile

SERVICE='wsprrypi-log.service'
SOCKET='wsprrypi-log.socket'
MANAGED='# Managed by WsprryPi UDP logging'


def execute(*args):
    subprocess.run(args,check=True,stdin=subprocess.DEVNULL,timeout=15,capture_output=True)

def atomic(path,data,mode=0o644):
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.is_symlink(): raise ValueError('Managed logging path is a symlink')
    fd,name=tempfile.mkstemp(prefix=path.name+'.new-',dir=path.parent)
    try:
        os.fchmod(fd,mode)
        with os.fdopen(fd,'wb') as stream:
            stream.write(data);stream.flush();os.fsync(stream.fileno())
        os.replace(name,path)
        directory=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
        try: os.fsync(directory)
        finally: os.close(directory)
    finally:
        if os.path.exists(name): os.unlink(name)

def quoted(value):
    if any(ord(c)<32 or ord(c)==127 for c in value): raise ValueError('Invalid service argument')
    return '"'+value.replace('\\','\\\\').replace('"','\\"').replace('%','%%').replace('$','$$')+'"'

def unique(pairs):
    value={}
    for key,item in pairs:
        if key in value: raise ValueError('Duplicate logging policy key')
        value[key]=item
    return value

def ipv4_interfaces():
    # Linux IPv4 ioctl inventory, including aliases; no DNS or shell commands.
    width=40 if struct.calcsize('P')==8 else 32
    storage=array.array('B',bytes(width*64))
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as control:
        count=struct.unpack('iP',fcntl.ioctl(control.fileno(),0x8912,struct.pack('iP',len(storage),storage.buffer_info()[0])))[0]
        if count>=len(storage):raise ValueError('Interface inventory exceeds the bounded limit')
        result=[]
        for offset in range(0,count,width):
            record=storage.tobytes()[offset:offset+width]
            if struct.unpack_from('H',record,16)[0]!=socket.AF_INET:continue
            name=record[:16].split(b'\0',1)[0].decode('ascii')
            address=ipaddress.IPv4Address(record[20:24])
            mask=fcntl.ioctl(control.fileno(),0x891b,struct.pack('256s',name.encode()))[20:24]
            result.append((name,int(address),int(ipaddress.IPv4Address(mask))))
        return result

def validate_network(value,managed_socket=None):
    s,c=value['sender'],value['collector']
    if not s['enabled'] and not c['enabled']:return
    interfaces=ipv4_interfaces()
    if s['enabled'] and s['destination_ipv4'] is not None:
        destination=int(ipaddress.IPv4Address(s['destination_ipv4']))
        if any(destination==ip or (mask and destination==(ip|(~mask&0xffffffff))) for _,ip,mask in interfaces):
            raise ValueError('Destination is a local address or directed broadcast')
        if destination&0xffff0000==0xa9fe0000 and not any(name==s['interface'] and destination&mask==ip&mask for name,ip,mask in interfaces):
            raise ValueError('Link-local destination is outside the selected link')
    if c['enabled']:
        addresses=[ip for name,ip,_ in interfaces if name==c['interface']]
        if len(set(addresses))!=1 or int(ipaddress.IPv4Address(c['bind_ipv4'])) not in addresses:
            raise ValueError('Listener address is not on the selected interface')
        owned=False
        try:
            status=Path('/run/wsprrypi-log/status.json');info=status.lstat()
            if stat.S_ISREG(info.st_mode) and info.st_uid==pwd.getpwnam('wsprrylog').pw_uid and info.st_size<=16384 and 0<=time.time()-info.st_mtime<=5:
                active=json.loads(status.read_text(),object_pairs_hook=unique)
                receiver=active['active']['collector']
                owned=active['schema']=='host-udp-logging-status/1' and active['collector_id']==value['collector_id'] and receiver['state']=='ready' and receiver['bind_ipv4']==c['bind_ipv4'] and type(receiver['port']) is int and receiver['port']==c['port']
        except (OSError,KeyError,ValueError,TypeError):pass
        if not owned and managed_socket is not None:
            try:
                text=managed_socket.read_text()
                owned=text.startswith(MANAGED+'\n') and ('\nListenDatagram='+c['bind_ipv4']+':'+str(c['port'])+'\n') in text
            except OSError:pass
        if not owned:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:probe.bind((c['bind_ipv4'],c['port']))

def policy(ini,owner=0,validate_live=True,verify_identity=True,managed_socket=None):
    path=Path(str(ini)+'.udp-logs.json')
    try: info=path.lstat()
    except FileNotFoundError:return None
    if not stat.S_ISREG(info.st_mode) or info.st_uid!=owner or info.st_mode&0o022 or info.st_size>16384:
        raise ValueError('Unsafe logging policy')
    value=json.loads(path.read_text(),object_pairs_hook=unique)
    if not isinstance(value,dict) or set(value)!={'schema','collector_id','revision','sender','collector'} or value['schema']!='host-udp-logging/1':
        raise ValueError('Unsupported logging policy')
    if not isinstance(value['collector_id'],str) or not re.fullmatch('[0-9a-f]{32}',value['collector_id']) or not isinstance(value['revision'],str) or not re.fullmatch(r'[\x20-\x7e]{1,128}',value['revision']):
        raise ValueError('Invalid logging identity/revision')
    def number(item,minimum,maximum):
        return type(item) is int and minimum<=item<=maximum
    def unicast(item):
        try: ip=ipaddress.IPv4Address(item)
        except (ValueError,TypeError,ipaddress.AddressValueError): return False
        return isinstance(item,str) and str(ip)==item and 0<(int(ip)>>24)<224 and not ip.is_loopback
    def interface(item,enabled):
        if item is None:
            if enabled: raise ValueError('Explicit interface required')
            return
        if not isinstance(item,str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,15}',item): raise ValueError('Invalid logging interface')
        if enabled: socket.if_nametoindex(item)
    s,c=value['sender'],value['collector']
    if not isinstance(s,dict) or set(s)!={'enabled','destination_ipv4','destination_port','maximum_severity','event_codes','rate','burst','interface','native_enabled','usb_sources'}:
        raise ValueError('Invalid sender shape')
    if type(s['enabled']) is not bool or type(s['native_enabled']) is not bool or not number(s['destination_port'],1,65535) or not number(s['maximum_severity'],0,7) or not number(s['rate'],1,64) or not number(s['burst'],1,128):
        raise ValueError('Invalid sender types/limits')
    if (s['destination_ipv4'] is not None and not unicast(s['destination_ipv4'])) or (s['enabled'] and s['destination_ipv4'] is None): raise ValueError('Invalid sender destination')
    interface(s['interface'],s['enabled'])
    codes=s['event_codes']
    if not isinstance(codes,list) or len(codes)>16 or any(not isinstance(code,str) or not re.fullmatch(r'[\x21\x23-\x5b\x5d-\x7e]{1,32}',code) for code in codes) or len(set(codes))!=len(codes): raise ValueError('Invalid event filter')
    sources=s['usb_sources']
    if not isinstance(sources,list) or len(sources)>8:raise ValueError('Invalid USB sources')
    devices,profiles=set(),set()
    for source in sources:
        if not isinstance(source,dict) or set(source)!={'profile_id','device_id'} or any(not isinstance(source[key],str) or not re.fullmatch('[0-9a-f]{32}',source[key]) for key in source): raise ValueError('Invalid USB binding')
        if source['device_id'] in devices or source['profile_id'] in profiles: raise ValueError('Duplicate USB binding')
        devices.add(source['device_id']);profiles.add(source['profile_id'])
    if not isinstance(c,dict) or set(c)!={'enabled','bind_ipv4','interface','port','peers'} or type(c['enabled']) is not bool or not number(c['port'],1,65535):raise ValueError('Invalid collector shape')
    if (c['bind_ipv4'] is not None and not unicast(c['bind_ipv4'])) or (c['enabled'] and c['bind_ipv4'] is None):raise ValueError('Invalid listener address')
    interface(c['interface'],c['enabled'])
    if not isinstance(c['peers'],list) or len(c['peers'])>32:raise ValueError('Invalid peers')
    slots,addresses=set(),set()
    for peer in c['peers']:
        if not isinstance(peer,dict) or set(peer)!={'slot','ipv4'} or not isinstance(peer['slot'],str) or not re.fullmatch('[A-Za-z0-9_-]{1,32}',peer['slot']) or not unicast(peer['ipv4']) or peer['slot'] in slots or peer['ipv4'] in addresses: raise ValueError('Invalid peer')
        slots.add(peer['slot']);addresses.add(peer['ipv4'])
    if verify_identity:
        machine=Path('/etc/machine-id').read_text().strip()
        if not re.fullmatch('[0-9a-f]{32}',machine) or value['collector_id']!=hashlib.sha256(('wsprrypi-wtp-device-v1:'+machine).encode()).hexdigest()[:32]:raise ValueError('Logging policy belongs to another host')
    if validate_live:validate_network(value,managed_socket)
    return value

def support_status(ini):
    # Only policy identity, enablement and bounded runtime metadata are exported.
    # Imported messages and journal bodies never enter this projection.
    result={'schema':'udp-logging-support/1','available':False}
    try:
        p=policy(ini,validate_live=False)
        if p is None:return {**result,'available':True,'configured':False}
        result.update(available=True,configured=True,collector_id=p['collector_id'],revision=p['revision'],
                      sender_enabled=p['sender']['enabled'],collector_enabled=p['collector']['enabled'],
                      bind_ipv4=p['collector']['bind_ipv4'],interface=p['collector']['interface'],port=p['collector']['port'],
                      peer_slots=[row['slot'] for row in p['collector']['peers']],active=None)
        status=Path('/run/wsprrypi-log/status.json');info=status.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=pwd.getpwnam('wsprrylog').pw_uid or info.st_size>16384 or not 0<=time.time()-info.st_mtime<=5:return result
        value=json.loads(status.read_text(),object_pairs_hook=unique)
        if set(value)!={'schema','collector_id','active'} or value['schema']!='host-udp-logging-status/1' or value['collector_id']!=p['collector_id']:return result
        active=value['active']
        if active['applied_revision']!=p['revision']:return result
        def counters(value,names):
            if not isinstance(value,dict) or set(value)!=set(names):raise ValueError('Invalid status counters')
            for item in value.values():
                if not isinstance(item,str) or not re.fullmatch(r'0|[1-9][0-9]{0,19}',item) or int(item)>2**64-1:raise ValueError('Invalid status counter')
            return value
        sender,collector=active['sender'],active['collector']
        if sender['state'] not in ('disabled','awaiting_network','invalid_destination','ready','submission_failed','unavailable') or collector['state'] not in ('disabled','awaiting_network','ready','bind_failed','unavailable','policy_invalid'):return result
        result['active']={'sender_state':sender['state'],'collector_state':collector['state'],
           'sender_counters':counters(sender['counters'],('generated','filtered','redacted_rejected','queued','overflow','expired','reconfigured_drops','truncated','attempted','locally_submitted','locally_failed')),
           'collector_counters':counters(collector['counters'],('received','oversize','disallowed','malformed','duplicate','conflict','limited','overflow','journal_failed','journal_submitted','discontinuity'))}
        return result
    except Exception:
        return result

def resource_controllers_available(root=Path('/')):
    try:
        path=root/'sys/fs/cgroup/cgroup.controllers'
        if path.exists():return {'memory','pids'}<=set(path.read_text().split())
        enabled=set()
        for line in (root/'proc/cgroups').read_text().splitlines():
            fields=line.split()
            if len(fields)==4 and fields[3]=='1':enabled.add(fields[0])
        return {'memory','pids'}<=enabled and all((root/'sys/fs/cgroup'/name).is_dir() for name in ('memory','pids'))
    except OSError:return False

def provision(action,ini,application,root=Path('/'),run=execute):
    physical=lambda path:root/path.lstrip('/')
    unit=physical('/etc/systemd/system/'+SERVICE)
    sock=physical('/etc/systemd/system/'+SOCKET)
    binary=physical('/usr/local/bin/wsprrypi-log')
    helper=physical('/usr/local/lib/wsprrypi/udp_log_control.py')
    generation=physical('/etc/systemd/system/'+SERVICE+'.d/50-udp-policy.conf')
    restart_installed=False
    production=root==Path('/')
    if production and os.getuid()!=0: raise ValueError('Logging installation requires root')
    ini=Path(ini)
    if not ini.is_absolute():raise ValueError('Absolute INI path required')
    for managed in (unit,sock,generation):
        if managed.is_symlink() or (managed.exists() and not managed.read_text().startswith(MANAGED+'\n')):
            raise ValueError('Refusing unmanaged logging unit: '+str(managed))
    if not unit.exists() and any(item.exists() or item.is_symlink() for item in (binary,helper)):
        raise ValueError('Refusing logging files without their managed service ownership marker')
    if action=='remove':
        names=[name for name in (SERVICE,SOCKET) if physical('/etc/systemd/system/'+name).exists()]
        if names:run('systemctl','disable','--now',*names)
        for item in (unit,sock,generation,binary,helper):
            if item.is_symlink():raise ValueError('Refusing unknown managed symlink')
            item.unlink(missing_ok=True)
        run('systemctl','daemon-reload');return
    value=policy(ini,owner=0 if production else os.getuid(),verify_identity=production,managed_socket=sock)
    if action=='install':
        data=Path(application).read_bytes()
        def unavailable():
            if value is not None and (value['sender']['enabled'] or value['collector']['enabled']):
                raise ValueError('Enabled UDP policy requires a compatible application')
            for name in (SOCKET,SERVICE):
                if physical('/etc/systemd/system/'+name).exists():run('systemctl','disable','--now',name)
            print('UDP logging unavailable in the selected executable; policies remain disabled.',file=sys.stderr)
        if b'--export-udp-logging-worker' not in data:
            unavailable();return
        exported=subprocess.run([str(application),'--export-udp-logging-worker'],stdin=subprocess.DEVNULL,capture_output=True,timeout=15,check=False)
        if exported.returncode==3 and not exported.stdout and not exported.stderr:
            unavailable();return
        if exported.returncode!=0:raise ValueError('Bundled logging worker extraction failed')
        image=exported.stdout
        if not image.startswith(b'\x7fELF') or len(image)>4*1024*1024 or image[4:6]!=data[4:6] or image[18:20]!=data[18:20]:
            raise ValueError('Bundled logging worker has the wrong executable identity')
        if production:
            try:grp.getgrnam('wsprrylog')
            except KeyError:run('groupadd','--system','wsprrylog')
            try:pwd.getpwnam('wsprrylog')
            except KeyError:run('useradd','--system','--gid','wsprrylog','--home-dir','/nonexistent','--shell','/usr/sbin/nologin','wsprrylog')
        restart_installed=not binary.exists() or binary.read_bytes()!=image
        atomic(binary,image,0o755)
        if hashlib.sha256(binary.read_bytes()).digest()!=hashlib.sha256(image).digest():raise ValueError('Logging worker readback mismatch')
        atomic(helper,Path(__file__).read_bytes(),0o755)
        content=MANAGED+'\n'+'''[Unit]
Description=Wsprry Pi diagnostic log sender and collector
After=network.target systemd-journald.service
ConditionControlGroupController=memory pids
[Service]
Type=simple
User=wsprrylog
Group=wsprrylog
SupplementaryGroups=systemd-journal
ExecStart=/usr/local/bin/wsprrypi-log --ini INI_ARGUMENT
Restart=on-failure
RestartSec=5
TimeoutStopSec=5
RuntimeDirectory=wsprrypi-log
RuntimeDirectoryMode=0755
UMask=0027
NoNewPrivileges=true
CapabilityBoundingSet=
AmbientCapabilities=
PrivateDevices=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=read-only
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictAddressFamilies=AF_INET AF_UNIX
RestrictSUIDSGID=true
LockPersonality=true
MemoryMax=32M
TasksMax=2
[Install]
WantedBy=multi-user.target
'''.replace('INI_ARGUMENT',quoted(str(ini)))
        restart_installed |= not unit.exists() or unit.read_bytes()!=content.encode()
        atomic(unit,content.encode())
        run('systemctl','daemon-reload')
    elif action!='configure':raise ValueError('Unsupported logging action')
    value=policy(ini,owner=0 if production else os.getuid(),verify_identity=production,managed_socket=sock)
    enabled=value is not None and (value['sender']['enabled'] or value['collector']['enabled'])
    if production and enabled and not resource_controllers_available():
        managed=[name for name in (SOCKET,SERVICE) if physical('/etc/systemd/system/'+name).exists()]
        if managed:run('systemctl','stop',*managed)
        raise ValueError('UDP logging requires enforceable memory and PID cgroup controllers; policy is preserved')
    privileged=enabled and value['collector']['enabled'] and value['collector']['port']==514
    # A passed 514 descriptor remains open in the worker after stopping its
    # socket unit. Stop both owners before rebinding or changing its generation.
    restart=privileged or sock.exists() or (enabled and restart_installed)
    if restart and unit.exists():run('systemctl','stop',SERVICE)
    if sock.exists():run('systemctl','disable','--now',SOCKET)
    if privileged:
        c=value['collector']
        content=MANAGED+'\n[Unit]\nDescription=Wsprry Pi explicit UDP log socket\n[Socket]\nListenDatagram='+c['bind_ipv4']+':514\nBindToDevice='+c['interface']+'\nService='+SERVICE+'\n[Install]\nWantedBy=sockets.target\n'
        atomic(sock,content.encode())
        atomic(generation,(MANAGED+'\n[Service]\nEnvironment='+quoted('WSPRRY_UDP_SOCKET_REVISION='+value['revision']).replace('$$','$')+'\n').encode())
        run('systemctl','daemon-reload');run('systemctl','enable','--now',SOCKET)
    else:
        changed=sock.exists() or generation.exists()
        sock.unlink(missing_ok=True);generation.unlink(missing_ok=True)
        if changed:run('systemctl','daemon-reload')
    if enabled:run('systemctl','enable','--now',SERVICE)
    elif unit.exists():run('systemctl','disable','--now',SERVICE)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('install','configure','remove','status'))
    parser.add_argument('--ini',required=True,type=Path)
    parser.add_argument('--application',default='/usr/local/bin/wsprrypi',type=Path)
    args=parser.parse_args()
    if args.action=='status':print(json.dumps(support_status(args.ini),sort_keys=True))
    else:provision(args.action,args.ini,args.application)

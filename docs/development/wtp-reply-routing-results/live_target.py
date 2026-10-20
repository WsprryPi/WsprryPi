#!/usr/bin/env python3
"""Network-only wspr5 acceptance. Never enables RF or sends WTP job commands."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.request

ROOT = Path('/home/pi/wtp-reply-routing-20261002/live-private')
ETH = '75a1216a-9d1a-30cd-8aca-ace5526ec021'
WIFI = '921301fe-cdfd-4965-8ac7-c96e9d908ea6'
ADDRESS = '192.168.1.54'
CLIENT = '192.168.1.120'
GUARD = 'wtp-routing-rollback'
CAPTURE = 'wtp-routing-capture'
BINARY = Path('/usr/local/bin/wsprrypi')
FILES = ['/usr/local/bin/wsprrypi', '/usr/local/etc/wsprrypi.ini',
         '/usr/local/etc/wsprrypi.ini.wtp-assignments.json',
         '/usr/local/etc/wsprrypi.ini.wtp-devices.json', '/etc/avahi/avahi-daemon.conf',
         '/etc/NetworkManager/system-connections/Bohica-IoT-wlan1.nmconnection',
         '/etc/NetworkManager/system-connections/Bohica-IoT-wlan0.nmconnection',
         '/run/NetworkManager/system-connections/netplan-eth0.nmconnection']


def command(*argv, check=True):
    result = subprocess.run(argv, text=True, capture_output=True, timeout=45)
    if check and result.returncode:
        raise RuntimeError({'argv': argv, 'stderr': result.stderr, 'stdout': result.stdout})
    return result.stdout.strip()


def get(resource):
    with urllib.request.urlopen('http://127.0.0.1:31415/api/v1/host/'+resource, timeout=4) as reply:
        return json.load(reply)


def safe():
    endpoint, fleet = get('wtp-endpoint'), get('fleet')
    assert endpoint['remote_state'] == 'empty'
    assert not any(endpoint.get(k) for k in ('local_requested', 'local_effective', 'local_work_active',
                                           'remote_owner', 'remote_output_active', 'output_unknown'))
    assert len(fleet['assignments']) == 5 and all(not a['enabled'] and not a['in_flight'] for a in fleet['assignments'])
    return endpoint, fleet


def snapshot():
    endpoint, fleet = safe()
    return {'endpoint': endpoint, 'fleet': fleet,
            'hashes': {p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in FILES},
            'routes': json.loads(command('ip', '-j', '-4', 'route')),
            'addresses': json.loads(command('ip', '-j', '-4', 'address')),
            'service': command('systemctl', 'show', 'wsprrypi', '-p', 'MainPID', '-p', 'ActiveState', '-p', 'SubState'),
            'chrony': command('systemctl', 'is-active', 'chrony'),
            'pps': command('readlink', '-f', '/dev/pps-gps')}


def save(name, data):
    (ROOT/name).write_text(json.dumps(data, indent=2)+'\n')


def wait(test, seconds=55):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        try:
            result = test()
            if result:
                return result
        except (OSError, ValueError):
            pass
        time.sleep(.5)
    raise RuntimeError('recovery deadline exceeded')


def ready():
    state = get('wtp-endpoint')
    return state if state['listener_running'] and state['dns_sd_published'] and state['address'] == ADDRESS else None


def prepare():
    assert not (ROOT/'baseline.json').exists(), 'preserve existing baseline'
    data = snapshot()
    assert data['routes'][0]['dev'] == 'eth0'
    save('baseline.json', data)
    command('tar', '-czf', str(ROOT/'private-original.tar.gz'), *FILES)
    os.chmod(ROOT/'private-original.tar.gz', 0o600)
    command('systemd-run', '--unit='+GUARD, '--on-active=900s', '--timer-property=AccuracySec=1s',
            '/usr/bin/python3', str(ROOT/'live_target.py'), 'rollback')
    assert command('systemctl', 'is-active', GUARD+'.timer') == 'active'
    return {'prepared': True, 'original_device_id': data['endpoint']['device_id']}


def reconnect():
    safe()
    assert command('systemctl', 'is-active', GUARD+'.timer') == 'active'
    command('nmcli', 'device', 'disconnect', 'eth0')
    withdrawn = wait(lambda: get('wtp-endpoint') if not get('wtp-endpoint')['listener_running'] else None)
    assert not withdrawn['dns_sd_published']
    command('nmcli', 'connection', 'up', 'uuid', ETH, 'ifname', 'eth0')
    state = wait(ready)
    route = json.loads(command('ip', '-j', '-4', 'route', 'get', CLIENT, 'from', ADDRESS))[0]
    assert route['dev'] == 'wlan1', route
    safe()
    return {'withdrawn': withdrawn, 'recovered': state, 'unrestricted_route': route}


def capture_start(label):
    assert label.isalnum(), 'finite capture label'
    command('systemctl', 'reset-failed', CAPTURE+'.service', check=False)
    command('systemd-run', '--unit='+CAPTURE, '--property=RuntimeMaxSec=90', '--property=TimeoutStopSec=3',
            '/usr/bin/tcpdump', '-U', '-n', '-i', 'any', '-w', str(ROOT/(label+'.pcap')),
            'tcp', 'port', '31417', 'and', 'host', CLIENT)
    wait(lambda: (ROOT/(label+'.pcap')).exists() and (ROOT/(label+'.pcap')).stat().st_size >= 24, 5)
    return {'capture': label}


def capture_stop(label):
    assert label.isalnum()
    command('systemctl', 'stop', CAPTURE+'.service')
    packet_text = command('tcpdump', '-n', '-r', str(ROOT/(label+'.pcap')))
    (ROOT/(label+'-packets.txt')).write_text(packet_text+'\n')
    return {'capture': label, 'packets': packet_text}


def deploy():
    safe()
    assert command('systemctl', 'is-active', GUARD+'.timer') == 'active'
    source = ROOT.parent/'source/src/build/bin/wsprrypi'
    candidate = Path('/usr/local/bin/wsprrypi.routing-candidate')
    shutil.copyfile(source, candidate)
    candidate.chmod(0o755)
    (ROOT/'candidate-installed').touch()
    os.replace(candidate, '/usr/local/bin/wsprrypi')
    command('systemctl', 'restart', 'wsprrypi')
    state = wait(ready)
    safe()
    return {'deployed': state, 'binary_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'sockets': command('ss', '-ltne', 'sport', '=', ':31417')}


def restore_network():
    command('nmcli', 'connection', 'up', 'uuid', ETH, 'ifname', 'eth0')
    command('nmcli', 'device', 'modify', 'wlan1', 'ipv4.route-metric', '101')
    command('nmcli', 'device', 'modify', 'wlan1', 'ipv4.route-metric', '100')
    wait(ready)


def rollback():
    command('systemctl', 'stop', CAPTURE+'.service', check=False)
    if (ROOT/'candidate-installed').exists():
        # Restore before waiting for a listener that the failed candidate cannot
        # serve. Write a fresh inode and rename it over the running executable;
        # overwriting its active inode can fail with ETXTBSY.
        temporary = BINARY.with_name(BINARY.name+'.routing-rollback')
        with tarfile.open(ROOT/'private-original.tar.gz', 'r:gz') as archive:
            member = archive.getmember('usr/local/bin/wsprrypi')
            with archive.extractfile(member) as original, temporary.open('wb') as target:
                shutil.copyfileobj(original, target)
                target.flush()
                os.fsync(target.fileno())
            temporary.chmod(member.mode & 0o777)
        os.replace(temporary, BINARY)
        command('systemctl', 'restart', 'wsprrypi')
        (ROOT/'candidate-installed').unlink()
    restore_network()
    return {'rolled_back': True}


def finalize():
    restore_network()
    data = snapshot()
    before = json.loads((ROOT/'baseline.json').read_text())
    for path in FILES[1:]:
        assert before['hashes'][path] == data['hashes'][path], path
    key = lambda x: json.dumps(x, sort_keys=True)
    assert sorted(data['routes'], key=key) == sorted(before['routes'], key=key)
    assert data['routes'] == before['routes'], 'original route order must be restored'
    assert json.loads(command('ip', '-j', '-4', 'route', 'get', CLIENT, 'from', ADDRESS))[0]['dev'] == 'eth0'
    assert data['endpoint']['device_id'] == before['endpoint']['device_id']
    assert data['fleet']['assignments'] == before['fleet']['assignments']
    # DHCP lifetimes decrease naturally; address identity/prefix remains exact.
    addresses = lambda rows: {(r['ifname'], a['local'], a['prefixlen']) for r in rows for a in r['addr_info']}
    assert addresses(data['addresses']) == addresses(before['addresses'])
    assert data['chrony'] == before['chrony'] == 'active' and data['pps'] == before['pps']
    save('restoration.json', data)
    command('systemctl', 'stop', GUARD+'.timer')
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'reconnect', 'capture-start', 'capture-stop', 'deploy', 'rollback', 'finalize', 'health'])
    parser.add_argument('label', nargs='?')
    args = parser.parse_args()
    os.umask(0o077)
    assert os.geteuid() == 0
    if args.action == 'capture-start': result = capture_start(args.label)
    elif args.action == 'capture-stop': result = capture_stop(args.label)
    elif args.action == 'health': result = snapshot()
    else: result = {'prepare': prepare, 'reconnect': reconnect, 'deploy': deploy,
                    'rollback': rollback, 'finalize': finalize}[args.action]()
    print(json.dumps(result, indent=2))

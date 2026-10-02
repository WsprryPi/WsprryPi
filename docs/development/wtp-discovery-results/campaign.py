#!/usr/bin/env python3
"""Bounded network-only discovery acceptance; never enables local/Fleet RF."""
import argparse, hashlib, http.client, json, os, socket, struct, subprocess, time, urllib.request, urllib.error
from pathlib import Path

ROOT=Path('/home/pi/wtp-discovery-20261002')
ETH='75a1216a-9d1a-30cd-8aca-ace5526ec021'
WIFI='921301fe-cdfd-4965-8ac7-c96e9d908ea6'
ID='e045017b11e54c2fb7029f0154e6deb9'
TARGET='wtp-'+ID+'.local'
TEST_CONNECTION='wtp-discovery-eth0-dhcp'
ORIGINAL_MAC='2c:cf:67:62:76:64'

class WifiConnection(http.client.HTTPConnection):
    def connect(self):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.setsockopt(socket.SOL_SOCKET,socket.SO_BINDTODEVICE,b'wlan1\0')
        self.sock.bind(('192.168.1.117',0));self.sock.connect((self.host,self.port))

class WifiHandler(urllib.request.HTTPHandler):
    def http_open(self,request):return self.do_open(WifiConnection,request)

def open_http(request,timeout=4):
    url=request if isinstance(request,str) else request.full_url
    # The independent observer's management traffic must use the unchanged
    # WiFi path during Ethernet readdressing, including its source MAC/IP.
    if url.startswith('http://192.168.1.123:'):
        return urllib.request.build_opener(WifiHandler()).open(request,timeout=timeout)
    return urllib.request.urlopen(request,timeout=timeout)

def command(*argv,check=True,timeout=40):
    result=subprocess.run(argv,text=True,capture_output=True,timeout=timeout)
    value={'argv':list(argv),'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr}
    log('command',value)
    if check and result.returncode:raise RuntimeError(value)
    return result

def log(kind,value):
    ROOT.mkdir(parents=True,exist_ok=True)
    with (ROOT/'actions.jsonl').open('a') as out:
        out.write(json.dumps({'utc_ns':str(time.time_ns()),'monotonic_ns':str(time.monotonic_ns()),'kind':kind,'value':value},separators=(',',':'))+'\n')

def get(host,resource):
    with open_http('http://'+host+':31415/api/v1/host/'+resource,timeout=4) as reply:return json.load(reply)

def safe():
    endpoint=get('127.0.0.1','wtp-endpoint');fleet=get('127.0.0.1','fleet')
    assert not endpoint['local_requested'] and not endpoint['local_effective'] and not endpoint['remote_output_active']
    assert not endpoint['remote_owner'] and not endpoint['output_unknown']
    assert len(fleet['assignments'])==5 and all(not a['enabled'] and not a['in_flight'] for a in fleet['assignments'])
    return endpoint,fleet

def baseline(label):
    endpoint,fleet=safe()
    hashes={}
    files=['/usr/local/bin/wsprrypi','/usr/local/etc/wsprrypi.ini','/usr/local/etc/wsprrypi.ini.wtp-assignments.json',
           '/usr/local/etc/wsprrypi.ini.wtp-devices.json','/etc/avahi/avahi-daemon.conf',
           '/etc/NetworkManager/system-connections/Bohica-IoT-wlan1.nmconnection',
           '/etc/NetworkManager/system-connections/Bohica-IoT-wlan0.nmconnection',
           '/run/NetworkManager/system-connections/netplan-eth0.nmconnection']
    for f in files:hashes[f]=hashlib.sha256(Path(f).read_bytes()).hexdigest()
    value={'hashes':hashes,'endpoint':endpoint,'fleet':fleet,'devices':get('127.0.0.1','devices'),
           'discovery':get('127.0.0.1','discovery')}
    for name,args in {'addresses':['ip','-j','address'],'routes':['ip','-j','-4','route'],
                      'nm_active':['nmcli','-t','-f','NAME,UUID,TYPE,DEVICE','connection','show','--active'],
                      'service':['systemctl','show','wsprrypi','-p','MainPID','-p','ActiveState'],
                      'pps':['readlink','-f','/dev/pps-gps']}.items():value[name]=command(*args).stdout
    (ROOT/(label+'.json')).write_text(json.dumps(value,indent=2)+'\n')
    return value

def restore(interface):
    uuid=ETH if interface=='eth0' else WIFI
    if interface=='eth0' and Path('/sys/class/net/eth0/address').read_text().strip()!=ORIGINAL_MAC:
        command('nmcli','device','disconnect','eth0',check=False)
        command('ip','link','set','dev','eth0','down')
        command('ip','link','set','dev','eth0','address',ORIGINAL_MAC)
    command('nmcli','connection','up','uuid',uuid,'ifname',interface)
    if interface=='eth0':
        # DHCP profiles both use metric 100. Reinsert the WiFi routes last to
        # restore the baseline Ethernet preference, including after rollback.
        metric=command('nmcli','-g','ipv4.route-metric','connection','show',WIFI).stdout.strip()
        command('nmcli','device','modify','wlan1','ipv4.route-metric','101')
        command('nmcli','device','modify','wlan1','ipv4.route-metric',metric)
        known=command('nmcli','-g','connection.id','connection','show',TEST_CONNECTION,check=False)
        if known.returncode==0 and known.stdout.strip()==TEST_CONNECTION:
            command('nmcli','connection','delete',TEST_CONNECTION)
    log('restored',interface)

def guard(interface):
    unit='wtp-discovery-rollback-'+interface
    command('systemctl','stop',unit+'.timer',check=False)
    command('systemctl','reset-failed',unit+'.service',check=False)
    command('systemd-run','--unit='+unit,'--on-active=120s','--timer-property=AccuracySec=1s',
            '/usr/bin/python3',str(ROOT/'campaign.py'),'restore',interface)
    state=command('systemctl','is-active',unit+'.timer').stdout.strip();assert state=='active'
    return unit

def cancel_guard(unit):command('systemctl','stop',unit+'.timer')

def wait(label,test,seconds=70):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:
            value=test()
            if value:log('accepted',{'label':label,'value':value});return value
        except Exception as e:log('waiting',{'label':label,'error':str(e)})
        time.sleep(.5)
    raise RuntimeError('Timeout: '+label)

def candidates(host,interface,online=True):
    records=get(host,'discovery')['candidates']
    return [x for x in records if x['interface']==interface and (x['state']=='online')==online]

def identify(host,record):
    body={'discovery_id':record['id'],'settings':{'Transport':'network_plain','Hostname':record['target'],'TCP Port':record['port'],
        'Device ID':ID,'Start Uncertainty ns':500000000,'Allow Frequency Adjustment':True,'Endpoint':'','USB Vendor ID':0,
        'USB Product ID':0,'USB Serial':'','TLS CA File':'','TLS Client Certificate':'','TLS Client Key':'','TLS Server Identity':''}}
    req=urllib.request.Request('http://'+host+':31415/api/v1/host/discovery/identify',data=json.dumps(body).encode(),
        headers={'Content-Type':'application/json','X-WsprryPico-Request':'1','Origin':'http://'+host+':31415'},method='POST')
    try:
        with open_http(req,timeout=15) as reply:return {'status':reply.status,'body':json.load(reply)}
    except urllib.error.HTTPError as error:return {'status':error.code,'body':json.loads(error.read())}

def link(interface):
    safe();index=socket.if_nametoindex(interface);survivor=4 if interface=='eth0' else 2
    before=candidates('127.0.0.1',index);assert len(before)>=4
    unit=guard(interface)
    try:
        log('phase-start',{'kind':'link','interface':interface,'index':index})
        command('nmcli','device','disconnect',interface)
        gone=wait(interface+' candidates removed',lambda: get('127.0.0.1','discovery') if not candidates('127.0.0.1',index) else None)
        remaining=candidates('127.0.0.1',survivor);assert len(remaining)>=4
        endpoint=get('127.0.0.1','wtp-endpoint')
        if interface=='eth0':
            assert not endpoint['listener_running'] and not endpoint['dns_sd_published']
        else:assert endpoint['listener_running'] and endpoint['address']=='192.168.1.54'
        log('link-down-evidence',{'interface':interface,'removed':gone,'remaining':remaining,'endpoint':endpoint})
        time.sleep(3)
    finally:restore(interface)
    wait(interface+' rediscovery',lambda:candidates('127.0.0.1',index) if len(candidates('127.0.0.1',index))>=4 else None)
    wait('listener publication recovered',lambda:get('127.0.0.1','wtp-endpoint') if get('127.0.0.1','wtp-endpoint').get('dns_sd_published') else None)
    cancel_guard(unit);safe();log('phase-complete',{'kind':'link','interface':interface})

def address():
    safe();unit=guard('eth0')
    try:
        result=command('arping','-D','-I','eth0','-c','3','-w','4','192.168.1.240');assert result.returncode==0
        log('phase-start',{'kind':'address','interface':'eth0','old':'192.168.1.54','new':'192.168.1.240'})
        command('nmcli','device','modify','eth0','ipv4.method','manual','ipv4.addresses','192.168.1.240/24',
                'ipv4.gateway','192.168.1.1','ipv4.dns','192.168.1.1','ipv4.route-metric','99')
        wait('listener rebound',lambda:get('127.0.0.1','wtp-endpoint') if get('127.0.0.1','wtp-endpoint').get('address')=='192.168.1.240' and get('127.0.0.1','wtp-endpoint').get('dns_sd_published') else None)
        command('avahi-resolve-host-name','-4',TARGET)
        records=wait('independent observer rediscovery',lambda:[x for x in get('192.168.1.123','discovery')['candidates'] if x['target']==TARGET and x['state']=='online'])
        result=identify('192.168.1.123',records[0]);log('address-identify',result);assert result['status']==200 and result['body']['device_id']==ID and not result['body']['output_active']
        command('ip','route','get','192.168.1.123','from','192.168.1.240')
        log('new-address-evidence',{'endpoint':get('127.0.0.1','wtp-endpoint'),'records':records})
        time.sleep(3)
    finally:restore('eth0')
    wait('original address publication recovered',lambda:get('127.0.0.1','wtp-endpoint') if get('127.0.0.1','wtp-endpoint').get('address')=='192.168.1.54' and get('127.0.0.1','wtp-endpoint').get('dns_sd_published') else None)
    records=wait('restored observer discovery',lambda:[x for x in get('192.168.1.123','discovery')['candidates'] if x['target']==TARGET and x['state']=='online'])
    result=identify('192.168.1.123',records[0]);log('restored-address-identify',result);assert result['status']==200 and result['body']['device_id']==ID
    cancel_guard(unit);safe();log('phase-complete',{'kind':'address'})

def address_dhcp():
    safe();unit=guard('eth0')
    try:
        assert command('nmcli','connection','show',TEST_CONNECTION,check=False).returncode!=0
        command('nmcli','connection','clone','--temporary','uuid',ETH,TEST_CONNECTION)
        command('nmcli','connection','modify','--temporary',TEST_CONNECTION,'connection.autoconnect','no',
                '802-3-ethernet.cloned-mac-address','02:cf:67:62:76:64','ipv4.route-metric','99')
        log('phase-start',{'kind':'address-dhcp','old':'192.168.1.54','test_mac':'02:cf:67:62:76:64'})
        command('nmcli','connection','up',TEST_CONNECTION,'ifname','eth0',timeout=55)
        addresses=json.loads(command('ip','-j','-4','address','show','dev','eth0').stdout)
        new=next(x['local'] for x in addresses[0]['addr_info'] if x['scope']=='global')
        assert new!='192.168.1.54'
        endpoint=wait('DHCP listener rebound',lambda:get('127.0.0.1','wtp-endpoint') if get('127.0.0.1','wtp-endpoint').get('address')==new and get('127.0.0.1','wtp-endpoint').get('dns_sd_published') else None,35)
        time.sleep(2)
        records=wait('DHCP observer rediscovery',lambda:[x for x in get('192.168.1.123','discovery')['candidates'] if x['target']==TARGET and x['state']=='online'],25)
        result=identify('192.168.1.123',records[0]);log('dhcp-address-identify',result)
        assert result['status']==200 and result['body']['device_id']==ID and not result['body']['output_active']
        command('ip','route','get','192.168.1.123','from',new)
        log('DHCP-address-evidence',{'new_address':new,'endpoint':endpoint,'records':records,'identify':result})
        time.sleep(3)
    finally:restore('eth0')
    wait('original DHCP listener restored',lambda:get('127.0.0.1','wtp-endpoint') if get('127.0.0.1','wtp-endpoint').get('address')=='192.168.1.54' and get('127.0.0.1','wtp-endpoint').get('dns_sd_published') else None)
    time.sleep(2)
    records=wait('restored independent rediscovery',lambda:[x for x in get('192.168.1.123','discovery')['candidates'] if x['target']==TARGET and x['state']=='online'])
    result=identify('192.168.1.123',records[0]);log('restored-address-identify',result)
    assert result['status']==200 and result['body']['device_id']==ID and not result['body']['output_active']
    assert Path('/sys/class/net/eth0/address').read_text().strip()==ORIGINAL_MAC
    cancel_guard(unit);safe();log('phase-complete',{'kind':'address-dhcp'})

def mdns_query():
    data=struct.pack('!6H',0,0,1,0,0,0)+b'\x04_wtp\x04_tcp\x05local\0'+struct.pack('!HH',12,1)
    for address in ('192.168.1.54','192.168.1.117'):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            sock.bind(('',5353));sock.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_IF,socket.inet_aton(address))
            sock.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_TTL,255)
            sock.sendto(data,('224.0.0.251',5353))
    log('multicast-query',{'service':'_wtp._tcp.local','known_answers':0})

def filters_remove():
    for interface in ('eth0','wlan1'):
        command('/usr/sbin/tc','qdisc','del','dev',interface,'clsact',check=False)
    log('mDNS-filters-removed',True)

def cache():
    safe();before=get('127.0.0.1','discovery')
    pico=[x for x in before['candidates'] if x['target']=='wsprrypico-0a60df.local' and x['state']=='online']
    assert len(pico)==2 and {x['interface'] for x in pico}=={2,4}
    profiles=get('127.0.0.1','devices')['profiles']
    settings=next(x['settings'] for x in profiles if x['settings']['Hostname']=='wsprrypico-0a60df.local')
    def probe(record):
        body={'discovery_id':record['id'],'settings':settings}
        req=urllib.request.Request('http://127.0.0.1:31415/api/v1/host/discovery/identify',data=json.dumps(body).encode(),
            headers={'Content-Type':'application/json','X-WsprryPico-Request':'1','Origin':'http://127.0.0.1:31415'},method='POST')
        try:
            with urllib.request.urlopen(req,timeout=15) as reply:return {'status':reply.status,'body':json.load(reply)}
        except urllib.error.HTTPError as error:return {'status':error.code,'body':json.loads(error.read())}
    initial=probe(pico[0]);log('cache-before-identify',initial)
    assert initial['status']==200 and initial['body']['device_id']==settings['Device ID'] and not initial['body']['output_active']
    for interface in ('eth0','wlan1'):
        value=json.loads(command('/usr/sbin/tc','-j','qdisc','show','dev',interface).stdout)
        assert not any(x['kind'] in ('clsact','ingress') for x in value)
    unit='wtp-discovery-rollback-mdns'
    command('systemctl','stop',unit+'.timer',check=False)
    command('systemctl','reset-failed',unit+'.service',check=False)
    command('systemd-run','--unit='+unit,'--on-active=300s','--timer-property=AccuracySec=1s',
            '/usr/bin/python3',str(ROOT/'campaign.py'),'filters-remove')
    assert command('systemctl','is-active',unit+'.timer').stdout.strip()=='active'
    try:
        mdns_query();time.sleep(2)
        for interface in ('eth0','wlan1'):
            command('/usr/sbin/tc','qdisc','add','dev',interface,'clsact')
            command('/usr/sbin/tc','filter','add','dev',interface,'ingress','protocol','ip','pref','49173','flower',
                    'ip_proto','udp','src_ip','192.168.1.47','src_port','5353','action','drop')
        log('phase-start',{'kind':'cache','target':'wsprrypico-0a60df.local','source_ip':'192.168.1.47','interfaces':['eth0','wlan1']})
        for record in pico:
            assert next(x for x in get('127.0.0.1','discovery')['candidates'] if x['id']==record['id'])['state']=='online'
        start=time.monotonic()
        def expired():
            current=get('127.0.0.1','discovery');records=[x for x in current['candidates'] if x['id'] in {y['id'] for y in pico}]
            return records if len(records)==2 and all(x['state']=='removed' for x in records) else None
        records=wait('natural mDNS TTL expiry on both interfaces',expired,155)
        duration=time.monotonic()-start
        rejected=[probe(record) for record in pico]
        log('cache-expired-identify',rejected)
        assert all(x['status']==409 and 'unavailable or stale' in json.dumps(x['body']) for x in rejected)
        other=[x for x in get('127.0.0.1','discovery')['candidates'] if x['state']=='online'];assert len(other)>=7
        independent=[x for x in get('192.168.1.123','discovery')['candidates'] if x['target']=='wsprrypico-0a60df.local' and x['state']=='online'];assert independent
        stats={interface:json.loads(command('/usr/sbin/tc','-s','-j','filter','show','dev',interface,'ingress').stdout) for interface in ('eth0','wlan1')}
        log('cache-expiry-evidence',{'elapsed_seconds':duration,'expired':records,'identify_rejected':rejected,'other_online':other,'independent_online':independent,'filters':stats})
    finally:filters_remove()
    mdns_query()
    def restored():
        records=[x for x in get('127.0.0.1','discovery')['candidates'] if x['target']=='wsprrypico-0a60df.local' and x['state']=='online']
        return records if len(records)==2 else None
    wait('Pico A rediscovery after multicast recovery',restored,35)
    result=probe(pico[0]);log('cache-after-identify',result);assert result['status']==200 and result['body']['device_id']==settings['Device ID']
    cancel_guard(unit);safe();log('phase-complete',{'kind':'cache'})

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['baseline','link','address','address-dhcp','restore','final','cache','filters-remove']);p.add_argument('interface',nargs='?')
    a=p.parse_args()
    if a.action=='restore':restore(a.interface)
    elif a.action=='baseline':
        os.umask(0o077);ROOT.mkdir(parents=True,exist_ok=True)
        command('tar','-czf',str(ROOT/'network-private-backup.tar.gz'),'/etc/NetworkManager/system-connections','/run/NetworkManager/system-connections','/etc/avahi/avahi-daemon.conf')
        baseline('before')
    elif a.action=='link':link(a.interface)
    elif a.action=='address':address()
    elif a.action=='address-dhcp':address_dhcp()
    elif a.action=='cache':cache()
    elif a.action=='filters-remove':filters_remove()
    elif a.action=='final':
        value=baseline('after');before=json.loads((ROOT/'before.json').read_text())
        assert before['hashes']==value['hashes'];assert before['pps']==value['pps']
        assert before['devices']==value['devices'] and before['fleet']['assignments']==value['fleet']['assignments']
        assert before['endpoint']['boot_id']==value['endpoint']['boot_id']
        log('restoration-accepted',{'hashes_equal':True,'profiles_equal':True,'assignments_equal':True,'boot_unchanged':True})

if __name__=='__main__':main()

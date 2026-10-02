#!/usr/bin/env python3
"""Assert captured native DNS-SD behavior and preserve bounded public evidence."""
import argparse, datetime, hashlib, json, shutil
from pathlib import Path

def read(path):return json.loads(path.read_text())
def lines(path):return [json.loads(row) for row in path.read_text().splitlines() if row]
def save(root,name,value):(root/name).write_text(json.dumps(value,indent=2)+'\n')
def stamp(ns):return datetime.datetime.fromtimestamp(int(ns)/1e9,datetime.timezone.utc).isoformat()

def main():
    p=argparse.ArgumentParser();p.add_argument('stage',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    stage=a.stage;out=a.output;out.mkdir(parents=True,exist_ok=True)
    before=read(stage/'wspr5/before.json');after=read(stage/'wspr5/after.json');cleanup=read(stage/'wspr5/cleanup.json')
    events=lines(stage/'wspr5/actions.jsonl')
    own_api=sorted(lines(stage/'wspr5/observer/api.jsonl')+lines(stage/'wspr5/observer-continued/api.jsonl'),key=lambda x:int(x['utc_ns']))
    own_wire=sorted(lines(stage/'wspr5/observer/dns.jsonl')+lines(stage/'wspr5/observer-continued/dns.jsonl'),key=lambda x:int(x['utc_ns']))
    remote_api=lines(stage/'wspr2/observer/api.jsonl');remote_wire=lines(stage/'wspr2/observer/dns.jsonl')
    original_api=lines(stage/'wspr4/observer/api.jsonl');original_wire=lines(stage/'wspr4/observer/dns.jsonl')
    completed=[x for x in events if x['kind']=='phase-complete']
    assert {x['value'].get('interface') for x in completed if x['value']['kind']=='link'}=={'eth0','wlan1'}
    down=[x for x in events if x['kind']=='link-down-evidence']
    assert len(down)==2
    for event in down:
        v=event['value'];index=2 if v['interface']=='eth0' else 4
        assert not any(c['state']=='online' and c['interface']==index for c in v['removed']['candidates'])
        assert len(v['remaining'])>=4 and all(c['state']=='online' and c['interface']!=index for c in v['remaining'])
        if index==2:assert not v['endpoint']['listener_running'] and not v['endpoint']['dns_sd_published']
        else:assert v['endpoint']['listener_running'] and v['endpoint']['address']=='192.168.1.54'
    address=next(x for x in events if x['kind']=='DHCP-address-evidence')
    v=address['value'];new=v['new_address'];assert new=='192.168.1.62'
    assert v['identify']['status']==200 and v['identify']['body']['device_id']==before['endpoint']['device_id']
    assert v['endpoint']['boot_id']==before['endpoint']['boot_id']
    target='wtp-'+before['endpoint']['device_id']+'.local.'
    address_records=[x for x in remote_wire if x['dns']['response'] and any(r['name']==target and r['type']==1 and r['value']==new and r['ttl']>0 for r in x['dns']['records'])]
    assert address_records
    restored=[x for x in events if x['kind']=='restored-address-identify'][-1]
    assert restored['value']['status']==200 and restored['value']['body']['device_id']==before['endpoint']['device_id']
    cache=next(x for x in events if x['kind']=='cache-expiry-evidence')
    start=[x for x in events if x['kind']=='phase-start' and x['value']['kind']=='cache' and int(x['utc_ns'])<int(cache['utc_ns'])][-1]
    end=next(x for x in completed if x['value']['kind']=='cache' and int(x['utc_ns'])>int(start['utc_ns']))
    cv=cache['value'];assert 110<cv['elapsed_seconds']<130
    assert len(cv['expired'])==2 and {c['interface'] for c in cv['expired']}=={2,4}
    assert all(c['state']=='removed' for c in cv['expired'])
    assert all(x['status']==409 and x['body']['error']['message']=='Discovery candidate is unavailable or stale' for x in cv['identify_rejected'])
    assert len(cv['other_online'])>=7 and cv['independent_online']
    assert not any(x['target']=='wsprrypico-0a60df.local' for x in cv['other_online'])
    elapsed_utc=(int(cache['utc_ns'])-int(start['utc_ns']))/1e9
    elapsed_monotonic=(int(cache['monotonic_ns'])-int(start['monotonic_ns']))/1e9
    assert abs(elapsed_utc-elapsed_monotonic)<.1
    def relevant(record):
        return record['name'].startswith('wsprrypico-0a60df.') or (record['type']==12 and str(record['value']).startswith('wsprrypico-0a60df.'))
    period=[x for x in own_wire if int(start['utc_ns'])<=int(x['utc_ns'])<=int(cache['utc_ns']) and x['source']=='192.168.1.47' and x['dns']['response']]
    assert period and not any(r['ttl']==0 for x in period for r in x['dns']['records'] if relevant(r))
    lifetime=[]
    for interface,index in [('eth0',2),('wlan1',4)]:
        prior=[x for x in own_wire if x['interface']==interface and x['source']=='192.168.1.47' and x['dns']['response'] and int(x['utc_ns'])<int(start['utc_ns']) and any(r['type']==12 and relevant(r) and r['ttl']==120 for r in x['dns']['records'])]
        assert prior
        latest=prior[-1];expired=next(x for x in cv['expired'] if x['interface']==index)
        seconds=(expired['state_changed_ms']*1000000-int(latest['utc_ns']))/1e9
        assert 115<=seconds<=125,(interface,seconds)
        actions=[action for filt in cv['filters'][interface] for action in filt.get('options',{}).get('actions',[])]
        drops=[action for action in actions if action.get('control_action',{}).get('type')=='drop']
        assert drops and all(x['stats']['drops']>0 for x in drops)
        lifetime.append({'interface':interface,'index':index,'advertised_ttl_seconds':120,'last_prior_response_utc_ns':latest['utc_ns'],
                         'expired_at_ms':expired['state_changed_ms'],'seconds_after_last_response':seconds,'drop_counters':[x['stats'] for x in drops]})
    cached=[x for x in own_api if int(start['utc_ns'])<=int(x['utc_ns'])<=int(end['utc_ns'])]
    assert cached and any(sum(c['state']=='online' for c in x['discovery']['candidates'] if c['target']=='wsprrypico-0a60df.local')==2 for x in cached)
    assert any(sum(c['state']=='removed' for c in x['discovery']['candidates'] if c['target']=='wsprrypico-0a60df.local')==2 for x in cached)
    recovery=[x for x in events if x['kind']=='cache-after-identify'][-1];assert recovery['value']['status']==200
    assert before['hashes']==after['hashes']
    assert before['devices']==after['devices'] and before['fleet']['assignments']==after['fleet']['assignments']
    assert before['service']==after['service'] and before['endpoint']['boot_id']==after['endpoint']['boot_id']
    assert before['pps']==after['pps'] and cleanup['pps']==before['pps'].strip()
    original_routes=json.loads(before['routes']);assert sorted(original_routes,key=lambda x:json.dumps(x,sort_keys=True))==sorted(cleanup['routes'],key=lambda x:json.dumps(x,sort_keys=True))
    assert next(x for x in cleanup['routes'] if x['dst']=='default')['dev']=='eth0'
    assert cleanup['ethernet_mac']=='2c:cf:67:62:76:64' and cleanup['test_units']=='' and cleanup['private_backup_mode']=='0o600'
    assert not any(q['kind'] in ('clsact','ingress') for qs in cleanup['qdiscs'].values() for q in qs)
    global_ip={x['ifname']:[r['local'] for r in x['addr_info'] if r['scope']=='global'] for x in cleanup['addresses']}
    assert global_ip['eth0']==['192.168.1.54'] and global_ip['wlan1']==['192.168.1.117']
    assert all(not x['enabled'] and not x['in_flight'] for x in cleanup['fleet']['assignments'])
    assert not cleanup['endpoint']['local_requested'] and not cleanup['endpoint']['local_effective'] and not cleanup['endpoint']['remote_output_active']
    for row in own_api:
        endpoint=row['wtp-endpoint']
        if 'error' not in endpoint:
            assert not endpoint['local_requested'] and not endpoint['local_effective'] and not endpoint['remote_output_active'] and not endpoint['remote_owner']
        if 'fleet' in row and 'error' not in row['fleet']:assert all(not x['enabled'] and not x['in_flight'] for x in row['fleet']['assignments'])
    final_wire=read(stage/'final-wire.json');assert len(final_wire)==5
    for x in final_wire:assert x['status']['state']=='empty' and not x['status']['output_active'] and not x['status']['owner_id'] and not x['status']['job_id']
    save(out,'case-verification.json',{'link':down,'address_change':address,'restored_identification':restored,
       'cache':{'start':start,'expiry':cache,'completed':end,'lifetime':lifetime,'recovery':recovery,'goodbye_count':0},
       'dns_sd_checks_passed':True,'unresolved_operational_findings':['Equal-metric route order can send WTP replies through the other interface','wspr4 public configuration snapshot can stall on Si5351 inventory reads']})
    save(out,'restoration.json',{'before_hashes':before['hashes'],'after_hashes':after['hashes'],'hashes_equal':True,
       'assignments':after['fleet']['assignments'],'profiles_unchanged':True,'service':cleanup['service'],
       'before_boot_id':before['endpoint']['boot_id'],'after_boot_id':after['endpoint']['boot_id'],'endpoint':cleanup['endpoint'],
       'addresses':global_ip,'routes':cleanup['routes'],'route_preference_restored':True,'ethernet_mac':cleanup['ethernet_mac'],
       'qdiscs':cleanup['qdiscs'],'test_units':cleanup['test_units'],'private_backup_mode':cleanup['private_backup_mode'],
       'avahi':cleanup['avahi'],'chrony':cleanup['chrony'],'pps':cleanup['pps']})
    save(out,'wire-evidence.json',{'address_change_responses':address_records,'cache_ttl':lifetime,
       'cache_filtered_responses':period,'native_api_transitions':[{'utc_ns':x['utc_ns'],'pico_candidates':[c for c in x['discovery']['candidates'] if c['target']=='wsprrypico-0a60df.local']} for x in cached]})
    save(out,'execution-events.json',events);save(out,'final-wire.json',final_wire)
    errors=[x for x in original_api if any(isinstance(v,dict) and 'error' in v for v in x.values())]
    save(out,'observation-summary.json',{'controller_api_samples':len(own_api),'controller_dns_packets':len(own_wire),
       'wspr2_api_samples':len(remote_api),'wspr2_dns_packets':len(remote_wire),'wspr4_api_samples':len(original_api),
       'wspr4_dns_packets':len(original_wire),'wspr4_error_samples':len(errors),
       'wspr4_first_error':errors[0] if errors else None,'start_utc':stamp(own_api[0]['utc_ns']),'end_utc':stamp(own_api[-1]['utc_ns'])})
    shutil.copyfile(stage/'wspr5/tcp-route-before.txt',out/'tcp-route-before.txt')
    shutil.copyfile(stage/'wspr4/blocked-api-backtrace.txt',out/'wspr4-blocked-api-backtrace.txt')
    manifest={str(path.relative_to(stage)):hashlib.sha256(path.read_bytes()).hexdigest() for path in stage.glob('*-evidence.tar.gz')}
    manifest.update({name:hashlib.sha256((stage/name).read_bytes()).hexdigest() for name in ('campaign.py','observe.py','analyze.py')})
    save(out,'source-manifest.json',{'base_commit':'960914333884158c87040c030d49ecaeb8795e65','input_and_harness_sha256':manifest,
       'installed_binary_sha256':after['hashes']['/usr/local/bin/wsprrypi'],'focused_linux_test_sha256':cleanup['focused_test_sha256'],
       'intermediate_harness_versions_hashed':False,'private_network_backup_transferred':False})
    print(json.dumps({'dns_sd_checks_passed':True,'cache_seconds':cv['elapsed_seconds'],'cache_lifetime':lifetime,'counts':read(out/'observation-summary.json')},indent=2))

if __name__=='__main__':main()
